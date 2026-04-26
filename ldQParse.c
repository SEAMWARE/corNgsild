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
#include <time.h>                                        // strptime, timegm

#include "kalloc/KAlloc.h"                             // kaAlloc
#include "kalloc/kaAlloc.h"                            // kaAlloc
#include "kalloc/kaStrdup.h"                            // kaStrdup
#include "swJsonld/swldExpand.h"                           // swldExpand

#include "swNgsild/LdProblem.h"                          // LD_ERROR_BAD_REQUEST_DATA
#include "swNgsild/SwNgsild.h"                           // swNgsild
#include "swNgsild/ldError.h"                            // ldError
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



// -----------------------------------------------------------------------------
//
// isoToNanoseconds - convert ISO 8601 date-time string to nanoseconds since epoch
//
static long long isoToNanoseconds(const char* iso)
{
  struct tm tm;
  memset(&tm, 0, sizeof(tm));
  const char* rest = strptime(iso, "%Y-%m-%dT%H:%M:%S", &tm);
  long long ns = (long long) timegm(&tm) * 1000000000LL;
  if (rest != NULL && *rest == '.')
  {
    rest++;
    long long frac = 0;
    int digits = 0;
    while (*rest >= '0' && *rest <= '9' && digits < 9)
    {
      frac = frac * 10 + (*rest - '0');
      rest++;
      digits++;
    }
    // Pad to 9 digits
    while (digits < 9) { frac *= 10; digits++; }
    ns += frac;
  }
  return ns;
}



// -----------------------------------------------------------------------------
//
// parseTerm - parse: attrName [operator value]
//
static LdQNode* parseTerm(const char** pp, KAlloc* kaP)
{
  const char* p = *pp;

  skipWs(&p);

  //
  // Scan attribute name — ends at operator char or delimiter
  // Attribute names can contain: letters, digits, '_', ':', '/', '.', '-', '#', '~' (for URIs)
  //
  const char* attrStart = p;

  while (*p != 0 && *p != '=' && *p != '!' && *p != '>' && *p != '<' && *p != '~' && *p != ';' && *p != '|' && *p != ')' && *p != '{' && *p != '}' && *p != ' ')
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
  nodeP->term.attr = expandAttr(attrStart, attrLen, kaP);

  //
  // Detect operator
  //
  if (*p == 0 || *p == ';' || *p == '|' || *p == ')' || *p == '}')
  {
    // No operator — existence check (also matches end-of-sub-q '}')
    nodeP->term.op        = LdQExists;
    nodeP->term.valueType = LdQNoValue;
    *pp = p;
    return nodeP;
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
      nodeP->term.value.ns  = isoToNanoseconds(s);
    }
    else
    {
      // Numeric
      nodeP->term.valueType = LdQNumber;
      nodeP->term.value.n   = strtod(vStart, NULL);
    }
  }


  //
  // expandValues: if the attribute is in expandValuesV, expand string values via @context
  //
  if (swNgsild.expandValuesV != NULL && nodeP->term.valueType == LdQString &&
      nodeP->term.op != LdQPattern && nodeP->term.op != LdQNotPattern)
  {
    for (int ix = 0; swNgsild.expandValuesV[ix] != NULL; ix++)
    {
      if (strcmp(nodeP->term.attr, swNgsild.expandValuesV[ix]) == 0)
      {
        char* expanded = swldExpand(swNgsild.contextP, nodeP->term.value.s, kaP, NULL, NULL);
        if (expanded != NULL)
          nodeP->term.value.s = expanded;
        break;
      }
    }
  }
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

    if (**pp == ')')
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
