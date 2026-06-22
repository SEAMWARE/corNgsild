//
// FILE            ldQParse.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//
// Recursive descent parser for the NGSI-LD ?q= URL parameter.
//
// Grammar (precedence: OR < AND < atom):
//   expr     = andExpr *( '|' andExpr )
//   andExpr  = atom *( ';' atom )
//   atom     = '(' expr ')' | term
//   term     = attrName [ operator value ]
//
// Operators: ==  !=  >  <  >=  <=  ~=  !~=
// Values:    number, "string", true/false, date-time, range (lo..hi), list (v1,v2,...)
//
#define _GNU_SOURCE
#include <ctype.h>                                       // isdigit, isalpha
#include <stdlib.h>                                      // strtod
#include <string.h>                                      // strlen, strncmp, strcmp, strchr, memcpy

#include "kalloc/KAlloc.h"                             // kaAlloc
#include "kalloc/kaAlloc.h"                            // kaAlloc
#include "kalloc/kaStrdup.h"                            // kaStrdup
#include "swJsonld/swldExpand.h"                           // swldExpand

#include "swNgsild/LdProblem.h"                          // LD_ERROR_BAD_REQUEST_DATA
#include "swNgsild/SwNgsild.h"                           // swNgsild
#include "swNgsild/ldError.h"                            // ldError
#include "swNgsild/ldCheckDateTime.h"                    // ldIsoToNanoseconds
#include "swNgsild/ldQParse.h"                           // Own interface



// -----------------------------------------------------------------------------
//
// Forward declarations
//
static LdQNode* parseOr(const char** pp, KAlloc* kaP);



// -----------------------------------------------------------------------------
//
// skipWs - skip whitespace
//
static void skipWs(const char** pp)
{
  while (**pp == ' ' || **pp == '\t')
    (*pp)++;
}



// -----------------------------------------------------------------------------
//
// expandAttr - expand an attribute name via the request's @context
//
static char* expandAttr(const char* name, int len, KAlloc* kaP)
{
  // Make a NUL-terminated copy
  char* buf = (char*) kaAlloc(kaP, len + 1);
  if (buf == NULL)
    return NULL;
  memcpy(buf, name, len);
  buf[len] = 0;

  // Already a full IRI — don't expand again
  if (swldAlreadyExpanded(buf))
    return buf;

  char* expanded = swldExpand(swNgsild.contextP, buf, kaP, NULL, NULL);

  // swldExpand may return a pointer into the context structure (itemP->id),
  // which lives in the request allocator.  The q-expr tree persists in the
  // subscription cache, so copy the result into kaP (the cache allocator).
  if (expanded != NULL && expanded != buf)
    return kaStrdup(kaP, expanded);

  return buf;
}



// -----------------------------------------------------------------------------
//
// urlDecodeSegment - %XX-decode one attrPath segment
//
// IRI dots travel %2E-encoded inside q (the dot is the attrPath
// separator), and round-tripped q strings carry fully %-encoded IRIs
// (ldQRender's compactOrEncode) — decode AFTER splitting on raw dots.
//
static char* urlDecodeSegment(const char* s, int len, KAlloc* kaP)
{
  char* out = (char*) kaAlloc(kaP, len + 1);
  int   o   = 0;

  for (int i = 0; i < len; i++)
  {
    if (s[i] == '%' && i + 2 < len && isxdigit((unsigned char) s[i + 1]) && isxdigit((unsigned char) s[i + 2]))
    {
      int hi = (s[i + 1] <= '9') ? s[i + 1] - '0' : (tolower(s[i + 1]) - 'a' + 10);
      int lo = (s[i + 2] <= '9') ? s[i + 2] - '0' : (tolower(s[i + 2]) - 'a' + 10);
      out[o++] = (char) (hi * 16 + lo);
      i += 2;
    }
    else
      out[o++] = s[i];
  }

  out[o] = 0;
  return out;
}



// -----------------------------------------------------------------------------
//
// expandAttrPath - split a § 4.9 attrPath on raw dots, decode + expand each segment
//
// First segment → term.attr, the rest → term.subPathV. All segments are
// context aliases (sub-attribute names expand exactly like attribute
// names do).
//
static void expandAttrPath(LdQTerm* termP, const char* start, int len, KAlloc* kaP)
{
  int segN = 1;
  for (int i = 0; i < len; i++)
    if (start[i] == '.') segN++;

  termP->subPathV = NULL;
  termP->subPathN = 0;
  if (segN > 1)
    termP->subPathV = (char**) kaAlloc(kaP, (segN - 1) * sizeof(char*));

  int segStart = 0;
  int segIx    = 0;
  for (int i = 0; i <= len; i++)
  {
    if (i == len || start[i] == '.')
    {
      char* decoded  = urlDecodeSegment(start + segStart, i - segStart, kaP);
      char* expanded = expandAttr(decoded, strlen(decoded), kaP);

      if (segIx == 0)
        termP->attr = expanded;
      else
        termP->subPathV[termP->subPathN++] = expanded;

      segIx++;
      segStart = i + 1;
    }
  }
}



// -----------------------------------------------------------------------------
//
// isDateTimeChar - check if character can appear in an ISO 8601 date-time
//
static bool isDateTimeChar(char c)
{
  return isdigit(c) || c == '-' || c == 'T' || c == ':' || c == '.' || c == 'Z' || c == '+';
}



// -----------------------------------------------------------------------------
//
// looksLikeDateTime - check if string starts with YYYY-MM-DD pattern
//
static bool looksLikeDateTime(const char* s)
{
  // Minimum: "YYYY-MM-DDT..."
  if (strlen(s) < 10)
    return false;

  return isdigit(s[0]) && isdigit(s[1]) && isdigit(s[2]) && isdigit(s[3]) && s[4] == '-' && isdigit(s[5]) && isdigit(s[6]) && s[7] == '-' && isdigit(s[8]) && isdigit(s[9]);
}



// -----------------------------------------------------------------------------
//
// scanValue - scan to end of a value token (stops at ; | ) or NUL)
//
static int scanValueLen(const char* p)
{
  const char* start = p;

  // Stop at the AND/OR group separators, the paren close, and — for
  // § 4.9 LinkedEntityRelation — the closing '}' of a sub-q. The
  // surrounding parser handles each terminator separately.
  while (*p != 0 && *p != ';' && *p != '|' && *p != ')' && *p != '}')
    p++;

  return (int)(p - start);
}



// -----------------------------------------------------------------------------
//
// parseValueList - parse a comma-separated value list: v1,v2,...
//
// Items can be quoted strings, numbers, or booleans (all same type expected).
//
static bool parseValueList(const char* raw, int rawLen, LdQTerm* term, KAlloc* kaP)
{
  // Count commas to determine size
  int count = 1;

  for (int i = 0; i < rawLen; i++)
  {
    if (raw[i] == ',' && (i == 0 || raw[i - 1] != '\\'))
      count++;
  }

  char** values  = (char**) kaAlloc(kaP, count * sizeof(char*));
  int    ix      = 0;
  const char* p  = raw;
  const char* end = raw + rawLen;

  LdQValueType itemType = LdQNoValue;

  while (p < end && ix < count)
  {
    // Skip leading whitespace
    while (p < end && *p == ' ')
      p++;

    const char* itemStart;
    int         itemLen;

    if (*p == '"')
    {
      // Quoted string
      p++;  // skip opening quote
      itemStart = p;

      while (p < end && *p != '"')
        p++;

      itemLen = (int)(p - itemStart);

      if (p < end)
        p++;  // skip closing quote

      if (itemType == LdQNoValue)
        itemType = LdQString;
    }
    else
    {
      // Unquoted value
      itemStart = p;

      while (p < end && *p != ',')
        p++;

      itemLen = (int)(p - itemStart);

      // Trim trailing whitespace
      while (itemLen > 0 && itemStart[itemLen - 1] == ' ')
        itemLen--;

      if (itemType == LdQNoValue)
      {
        if (itemLen == 4 && strncmp(itemStart, "true", 4) == 0)
          itemType = LdQBool;
        else if (itemLen == 5 && strncmp(itemStart, "false", 5) == 0)
          itemType = LdQBool;
        else
          itemType = LdQNumber;
      }
    }

    char* item = (char*) kaAlloc(kaP, itemLen + 1);
    memcpy(item, itemStart, itemLen);
    item[itemLen] = 0;
    values[ix++] = item;

    // Skip comma
    if (p < end && *p == ',')
      p++;
  }

  term->valueType              = LdQValueList;
  term->value.list.values      = values;
  term->value.list.count       = ix;
  term->value.list.itemType    = itemType;

  return true;
}



// -----------------------------------------------------------------------------
//
// parseRange - parse a range value: lo..hi
//
static bool parseRange(const char* raw, int rawLen, const char* dotdot, LdQTerm* term, KAlloc* kaP)
{
  int loLen = (int)(dotdot - raw);
  int hiLen = rawLen - loLen - 2;  // skip ".."

  char* lo = (char*) kaAlloc(kaP, loLen + 1);
  memcpy(lo, raw, loLen);
  lo[loLen] = 0;

  char* hi = (char*) kaAlloc(kaP, hiLen + 1);
  memcpy(hi, dotdot + 2, hiLen);
  hi[hiLen] = 0;

  if (looksLikeDateTime(lo))
  {
    term->valueType          = LdQDateRange;
    term->value.dateRange.lo = lo;
    term->value.dateRange.hi = hi;
  }
  else
  {
    term->valueType         = LdQRange;
    term->value.numRange.lo = strtod(lo, NULL);
    term->value.numRange.hi = strtod(hi, NULL);
  }

  return true;
}



// isoToNanoseconds is provided by ldCheckDateTime (ldIsoToNanoseconds) — the
// single ISO 8601 → epoch-nanoseconds converter (handles fractional seconds and
// the timezone offset). See ldCheckDateTime.h.



// -----------------------------------------------------------------------------
//
// parseTerm - parse: attrName [operator value]
//
static LdQNode* parseTerm(const char** pp, KAlloc* kaP)
{
  const char* p = *pp;

  skipWs(&p);

  //
  // Leading '!' — unary not-exists prefix (q=!attr). Distinct from the infix
  // '!=' / '!~=' operators, which follow an attribute name. A '!' here, before
  // any attribute name, negates an existence check.
  //
  bool notExists = false;
  if (*p == '!')
  {
    notExists = true;
    p++;
    skipWs(&p);
  }

  //
  // Scan attribute name — ends at operator char or delimiter
  // Attribute names can contain: letters, digits, '_', ':', '/', '.', '-', '#', '~' (for URIs)
  //
  const char* attrStart = p;

  while (*p != 0 && *p != '=' && *p != '!' && *p != '>' && *p != '<' && *p != '~' && *p != ';' && *p != '|' && *p != ')' && *p != '{' && *p != '}' && *p != '[' && *p != ' ')
    p++;

  if (p == attrStart)
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid q parameter", "empty attribute name in q expression");
    return NULL;
  }

  int attrLen = (int)(p - attrStart);

  //
  // § 4.9 LinkedEntityRelation: attrName "{" sub-q "}" — sub-query is
  // evaluated against the target of the named Relationship attribute.
  // Detected before the normal operator scan so the term doesn't first
  // become an existence check that the surrounding expression has to
  // re-interpret.
  //
  skipWs(&p);
  if (*p == '{')
  {
    p++;  // consume '{'

    LdQNode* subQ = parseOr(&p, kaP);
    if (subQ == NULL)
      return NULL;

    skipWs(&p);
    if (*p != '}')
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid q parameter",
              "expected '}' after linked-entity sub-query");
      return NULL;
    }
    p++;  // consume '}'

    LdQNode* linkedP        = (LdQNode*) kaAlloc(kaP, sizeof(LdQNode));
    linkedP->type           = LdQLinkedNode;
    linkedP->linked.relName = expandAttr(attrStart, attrLen, kaP);
    linkedP->linked.subQ    = subQ;

    *pp = p;
    return linkedP;
  }

  //
  // Allocate the node
  //
  LdQNode* nodeP = (LdQNode*) kaAlloc(kaP, sizeof(LdQNode));
  nodeP->type = LdQTermNode;
  expandAttrPath(&nodeP->term, attrStart, attrLen, kaP);

  //
  // § 4.9 "[...]" — path INTO the value. Opaque JSON member names: NO
  // context expansion, dots separate members, %XX decoded per segment
  // (a literal dot inside a member name travels %2E-encoded).
  //
  nodeP->term.valuePathV = NULL;
  nodeP->term.valuePathN = 0;
  if (*p == '[')
  {
    p++;  // consume '['
    const char* vpStart = p;
    while (*p != 0 && *p != ']')
      p++;
    if (*p != ']')
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid q parameter", "expected ']' after value path");
      return NULL;
    }
    int vpLen = (int) (p - vpStart);
    p++;  // consume ']'

    int segN = 1;
    for (int i = 0; i < vpLen; i++)
      if (vpStart[i] == '.') segN++;
    nodeP->term.valuePathV = (char**) kaAlloc(kaP, segN * sizeof(char*));

    int segStart = 0;
    for (int i = 0; i <= vpLen; i++)
    {
      if (i == vpLen || vpStart[i] == '.')
      {
        nodeP->term.valuePathV[nodeP->term.valuePathN++] = urlDecodeSegment(vpStart + segStart, i - segStart, kaP);
        segStart = i + 1;
      }
    }
  }

  //
  // Detect operator
  //
  if (*p == 0 || *p == ';' || *p == '|' || *p == ')' || *p == '}')
  {
    // No operator — existence check (also matches end-of-sub-q '}'). A leading
    // '!' negates it.
    nodeP->term.op        = notExists ? LdQNotExists : LdQExists;
    nodeP->term.valueType = LdQNoValue;
    *pp = p;
    return nodeP;
  }

  if (notExists)
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid q parameter", "the '!' not-exists operator cannot be combined with a comparison");
    return NULL;
  }

  if (p[0] == '=' && p[1] == '=')
  {
    nodeP->term.op = LdQEqual;
    p += 2;
  }
  else if (p[0] == '!' && p[1] == '=')
  {
    nodeP->term.op = LdQUnequal;
    p += 2;
  }
  else if (p[0] == '>' && p[1] == '=')
  {
    nodeP->term.op = LdQGreaterEq;
    p += 2;
  }
  else if (p[0] == '<' && p[1] == '=')
  {
    nodeP->term.op = LdQLessEq;
    p += 2;
  }
  else if (p[0] == '>')
  {
    nodeP->term.op = LdQGreater;
    p += 1;
  }
  else if (p[0] == '<')
  {
    nodeP->term.op = LdQLess;
    p += 1;
  }
  else if (p[0] == '~' && p[1] == '=')
  {
    nodeP->term.op = LdQPattern;
    p += 2;
  }
  else if (p[0] == '!' && p[1] == '~' && p[2] == '=')
  {
    nodeP->term.op = LdQNotPattern;
    p += 3;
  }
  else
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid q parameter", "unrecognized operator in q expression");
    return NULL;
  }

  //
  // Parse value
  //
  skipWs(&p);

  // An operator (==, !=, >, ...) with no value — "A==" — is an unfinished
  // statement, not "attr equals the number 0". A deliberately empty value must
  // be a quoted empty string (A=="").
  if (*p == 0 || *p == ';' || *p == '|' || *p == ')' || *p == '}')
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid q parameter", "unfinished statement (operator with no value) in q expression");
    return NULL;
  }

  if (*p == '"')
  {
    // Quoted string
    p++;  // skip opening quote
    const char* sStart = p;

    while (*p != 0 && *p != '"')
      p++;

    int sLen = (int)(p - sStart);

    if (*p == '"')
      p++;

    char* s = (char*) kaAlloc(kaP, sLen + 1);
    memcpy(s, sStart, sLen);
    s[sLen] = 0;

    //
    // Check for comma-separated list of quoted strings: "red","blue"
    //
    if (*p == ',' && (nodeP->term.op == LdQEqual || nodeP->term.op == LdQUnequal))
    {
      // Reparse from original position as a value list
      const char* listStart = sStart - 1;  // back to opening quote
      int listLen = scanValueLen(listStart);
      parseValueList(listStart, listLen, &nodeP->term, kaP);
      *pp = listStart + listLen;
      return nodeP;
    }

    // § 4.9 — q is URL-encoded; spaces and other reserved chars come
    // through as %xx. Decode in-place so the match value is the raw
    // string (046_05_01: `name=="Eiffel%20Tower"` vs entity name
    // "Eiffel Tower").
    {
      char* r = s;
      char* w = s;
      while (*r != 0)
      {
        if (r[0] == '%' && r[1] != 0 && r[2] != 0)
        {
          int hi = (r[1] >= '0' && r[1] <= '9') ? r[1] - '0' :
                   (r[1] >= 'A' && r[1] <= 'F') ? r[1] - 'A' + 10 :
                   (r[1] >= 'a' && r[1] <= 'f') ? r[1] - 'a' + 10 : -1;
          int lo = (r[2] >= '0' && r[2] <= '9') ? r[2] - '0' :
                   (r[2] >= 'A' && r[2] <= 'F') ? r[2] - 'A' + 10 :
                   (r[2] >= 'a' && r[2] <= 'f') ? r[2] - 'a' + 10 : -1;
          if (hi >= 0 && lo >= 0)
          {
            *w++ = (char) ((hi << 4) | lo);
            r += 3;
            continue;
          }
        }
        *w++ = *r++;
      }
      *w = 0;
    }

    nodeP->term.valueType = LdQString;
    nodeP->term.value.s   = s;
  }
  else if (strncmp(p, "true", 4) == 0 && (p[4] == 0 || p[4] == ';' || p[4] == '|' || p[4] == ')' || p[4] == '}'))
  {
    nodeP->term.valueType = LdQBool;
    nodeP->term.value.b   = true;
    p += 4;
  }
  else if (strncmp(p, "false", 5) == 0 && (p[5] == 0 || p[5] == ';' || p[5] == '|' || p[5] == ')' || p[5] == '}'))
  {
    nodeP->term.valueType = LdQBool;
    nodeP->term.value.b   = false;
    p += 5;
  }
  else if (nodeP->term.op == LdQPattern || nodeP->term.op == LdQNotPattern)
  {
    // Regex pattern: take everything until delimiter
    const char* rStart = p;
    int rLen = scanValueLen(p);
    p += rLen;

    char* s = (char*) kaAlloc(kaP, rLen + 1);
    memcpy(s, rStart, rLen);
    s[rLen] = 0;

    nodeP->term.valueType = LdQString;
    nodeP->term.value.s   = s;
  }
  else
  {
    // Number, range, date-time, or value list
    const char* vStart = p;
    int vLen = scanValueLen(p);
    p += vLen;

    // Check for range: look for ".." (not preceded by digit+'.'+digit — i.e. not a decimal number)
    const char* dotdot = NULL;

    for (int i = 0; i < vLen - 1; i++)
    {
      if (vStart[i] == '.' && vStart[i + 1] == '.')
      {
        // Make sure it's not inside a decimal number (e.g. "3.14")
        // A ".." range has two consecutive dots
        dotdot = vStart + i;
        break;
      }
    }

    if (dotdot != NULL)
    {
      parseRange(vStart, vLen, dotdot, &nodeP->term, kaP);
    }
    else if (strchr(vStart, ',') != NULL && (int)(strchr(vStart, ',') - vStart) < vLen && (nodeP->term.op == LdQEqual || nodeP->term.op == LdQUnequal))
    {
      // Value list (comma-separated)
      parseValueList(vStart, vLen, &nodeP->term, kaP);
    }
    else if (looksLikeDateTime(vStart))
    {
      char* s = (char*) kaAlloc(kaP, vLen + 1);
      memcpy(s, vStart, vLen);
      s[vLen] = 0;

      nodeP->term.valueType = LdQDateTime;
      nodeP->term.value.ns  = (long long) ldIsoToNanoseconds(s);
    }
    else
    {
      // Try numeric first; if strtod doesn't consume the whole token,
      // treat the value as an unquoted string. § 4.9 grammatically
      // requires quotes around strings, but URIs, plain words and
      // similar tokens routinely appear unquoted in real-world q
      // expressions (and the ETSI suite tests for it — 019_09_08:
      // `locatedIn==urn:ngsi-ld:City:Pisa`). Without this fallback,
      // strtod returns 0.0 and the match silently never fires.
      char* endP = NULL;
      double n = strtod(vStart, &endP);
      if (endP != NULL && (endP == vStart + vLen))
      {
        nodeP->term.valueType = LdQNumber;
        nodeP->term.value.n   = n;
      }
      else
      {
        char* s = (char*) kaAlloc(kaP, vLen + 1);
        memcpy(s, vStart, vLen);
        s[vLen] = 0;

        nodeP->term.valueType = LdQString;
        nodeP->term.value.s   = s;
      }
    }
  }


  // expandValues handling moved to ldExpandParams: at parse time, URL param
  // order may not have set expandValuesV yet, and term.attr is already in
  // expanded form — both sides need to be expanded for the match to work.
  *pp = p;
  return nodeP;
}



// -----------------------------------------------------------------------------
//
// parseAtom - parse: '(' expr ')' | term
//
static LdQNode* parseAtom(const char** pp, KAlloc* kaP)
{
  skipWs(pp);

  if (**pp == '(')
  {
    (*pp)++;  // skip '('
    LdQNode* inner = parseOr(pp, kaP);

    if (inner == NULL)
      return NULL;

    skipWs(pp);

    if (**pp != ')')
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid q parameter", "missing ')' in q expression");
      return NULL;
    }
    (*pp)++;

    return inner;
  }

  return parseTerm(pp, kaP);
}



// -----------------------------------------------------------------------------
//
// groupAdd - add a child to an AND/OR group, growing the array if needed
//
static void groupAdd(LdQNode* groupP, LdQNode* childP, KAlloc* kaP)
{
  if (groupP->group.count >= groupP->group.allocated)
  {
    int newAlloc = (groupP->group.allocated == 0) ? 4 : groupP->group.allocated * 2;
    LdQNode** newV = (LdQNode**) kaAlloc(kaP, newAlloc * sizeof(LdQNode*));

    if (groupP->group.childV != NULL)
      memcpy(newV, groupP->group.childV, groupP->group.count * sizeof(LdQNode*));

    groupP->group.childV    = newV;
    groupP->group.allocated = newAlloc;
  }

  groupP->group.childV[groupP->group.count++] = childP;
}



// -----------------------------------------------------------------------------
//
// parseAnd - parse: atom *(';' atom)
//
static LdQNode* parseAnd(const char** pp, KAlloc* kaP)
{
  LdQNode* left = parseAtom(pp, kaP);

  if (left == NULL)
    return NULL;

  skipWs(pp);

  if (**pp != ';')
    return left;  // single atom, no AND

  // Build AND group
  LdQNode* andP = (LdQNode*) kaAlloc(kaP, sizeof(LdQNode));
  andP->type              = LdQAndNode;
  andP->group.childV      = NULL;
  andP->group.count       = 0;
  andP->group.allocated   = 0;

  groupAdd(andP, left, kaP);

  while (**pp == ';')
  {
    (*pp)++;  // skip ';'
    LdQNode* right = parseAtom(pp, kaP);

    if (right == NULL)
      return NULL;

    groupAdd(andP, right, kaP);
    skipWs(pp);
  }

  return andP;
}



// -----------------------------------------------------------------------------
//
// parseOr - parse: andExpr *('|' andExpr)
//
static LdQNode* parseOr(const char** pp, KAlloc* kaP)
{
  LdQNode* left = parseAnd(pp, kaP);

  if (left == NULL)
    return NULL;

  skipWs(pp);

  if (**pp != '|')
    return left;  // single andExpr, no OR

  // Build OR group
  LdQNode* orP = (LdQNode*) kaAlloc(kaP, sizeof(LdQNode));
  orP->type              = LdQOrNode;
  orP->group.childV      = NULL;
  orP->group.count       = 0;
  orP->group.allocated   = 0;

  groupAdd(orP, left, kaP);

  while (**pp == '|')
  {
    (*pp)++;  // skip '|'
    LdQNode* right = parseAnd(pp, kaP);

    if (right == NULL)
      return NULL;

    groupAdd(orP, right, kaP);
    skipWs(pp);
  }

  return orP;
}



// -----------------------------------------------------------------------------
//
// ldQParse - parse a ?q= expression into an expression tree
//
LdQNode* ldQParse(const char* q, KAlloc* kaP)
{
  if (q == NULL || q[0] == 0)
    return NULL;

  const char* p = q;

  LdQNode* root = parseOr(&p, kaP);

  if (root == NULL)
    return NULL;

  skipWs(&p);

  if (*p != 0)
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid q parameter", "unexpected characters at end of q expression");
    return NULL;
  }

  return root;
}
