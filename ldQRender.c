//
// FILE            ldQRender.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
// Render a parsed LdQNode tree back to a q-filter string.
//
#include <stdio.h>                                     // snprintf
#include <string.h>                                    // strlen, strcpy, strcat, strcmp
#include <stdlib.h>                                    // malloc

#include "corAlloc/CorAlloc.h"                         // CorAlloc
#include "corAlloc/corAlloc.h"                         // corAlloc
#include "corJsonld/CorLdContext.h"                      // CorLdContext
#include "corJsonld/corLdCompact.h"                      // corLdCompact
#include "corJsonld/corLdExpand.h"                       // corLdAlreadyExpanded

#include "corNgsild/LdQ.h"                              // LdQNode, LdQTerm
#include "corNgsild/ldSysTimestamp.h"                   // ldSysTimestampToIso
#include "corNgsild/ldQRender.h"                        // Own interface



// -----------------------------------------------------------------------------
//
// isQGrammarChar - true for characters that are structural in the q grammar
//
// These would be mis-parsed if they appeared raw inside an attribute name or a
// value-path member: the term/expression delimiters, the sub-attribute path '.',
// the value-path '[' ']', the operators and the string delimiter.
//
static bool isQGrammarChar(unsigned char c)
{
  return (c == '.' || c == '[' || c == ']' || c == '(' || c == ')' ||
          c == '{' || c == '}' || c == ';' || c == '|' || c == ',' ||
          c == '=' || c == '!' || c == '<' || c == '>' || c == '~' ||
          c == '"' || c == ' ');
}



// -----------------------------------------------------------------------------
//
// urlEncode - percent-encode a string (allocates via corAlloc)
//
// qGrammarOnly = false: full encoding — everything except the RFC 3986 unreserved
//   set (A-Z a-z 0-9 - _ ~). Used when the q is rendered into a URL (forwarding).
// qGrammarOnly = true: encode ONLY the q-grammar-significant characters, leaving
//   URL-reserved chars (':' '/' '#' …) raw. Used when the q is rendered into a
//   response BODY, where there is no URL-transport reason to encode but the
//   q-grammar ambiguity (a dot meaning sub-attribute path) still must be removed.
//
static char* urlEncode(const char* s, CorAlloc* allocP, bool qGrammarOnly)
{
  // Worst case: every char becomes %XX (3x expansion)
  int   len    = strlen(s);
  int   outLen = len * 3 + 1;
  char* out    = (char*) corAlloc(allocP, outLen);
  char* p      = out;

  for (int i = 0; i < len; i++)
  {
    unsigned char c = (unsigned char) s[i];

    bool encode;
    if (qGrammarOnly)
      encode = isQGrammarChar(c);
    else
      encode = !((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
                 || c == '-' || c == '_' || c == '~');

    if (encode)
      p += sprintf(p, "%%%02X", c);
    else
      *p++ = c;
  }

  *p = 0;
  return out;
}



// -----------------------------------------------------------------------------
//
// storageEncode - an expanded attribute name as a STORED q carries it
//
// A subscription's q is stored with its attribute names EXPANDED: the short name is only an
// alias, valid in the @context it came with, and the stored q outlives that request - the
// caches parse it again at every restart, with no context at all. But an IRI's own dots are
// the q-grammar's sub-attribute separator, so raw, "https://uri.etsi.org/.../speed" parses as
// an attribute "https://uri" with a path "etsi", "org/...". (This mode used to return the IRI
// raw - and nothing could read the result back.)
//
// So every dot of the IRI is written as '^', which a valid IRI never contains (RFC 3986 has no
// raw '^') and the q grammar does not use; ldQParse turns it back into a dot. Same length as the
// IRI - the common case, a dotted host name and nothing else. Any OTHER q-grammar character in
// the IRI ('=', ';', ',', ...: legal in an IRI, rare in an attribute name) is %-encoded, which
// ldQParse already decodes.
//
static CorLdContext storageMode;   // a marker, never a context: "render the stored form"

static const char* storageEncode(const char* iri, CorAlloc* allocP)
{
  bool other = false;

  for (const char* p = iri; *p != 0; ++p)
  {
    if ((*p != '.') && isQGrammarChar((unsigned char) *p))
    {
      other = true;
      break;
    }
  }

  int   len = strlen(iri);
  char* out = (char*) corAlloc(allocP, (other == true) ? 3 * len + 1 : len + 1);
  char* o   = out;

  if (out == NULL)
    return iri;

  for (const char* p = iri; *p != 0; ++p)
  {
    unsigned char c = (unsigned char) *p;

    if (c == '.')
      *o++ = '^';
    else if (isQGrammarChar(c))
      o += sprintf(o, "%%%02X", c);
    else
      *o++ = c;
  }

  *o = 0;
  return out;
}



// -----------------------------------------------------------------------------
//
// ldCompactOrEncode - compact an IRI against the @context, URL-encode if uncompactable
//
const char* ldCompactOrEncode(const char* iri, CorLdContext* contextP, CorAlloc* allocP, bool qGrammarOnly)
{
  // The STORED form of a q (ldQRenderStored, see storageEncode)
  if (contextP == &storageMode)
    return storageEncode(iri, allocP);

  // No context: the raw IRI unchanged
  if (contextP == NULL)
    return iri;

  const char* compacted = corLdCompact(contextP, iri);

  // corLdCompact's contract: returns a different pointer for an actual short
  // term, returns iri unchanged otherwise. Belt-and-suspenders, also reject
  // any result that is itself still an IRI (urn:, http://, https://) — a
  // future corLdCompact change that re-emits an IRI form mustn't slip past.
  if (compacted != NULL && compacted != iri && corLdAlreadyExpanded(compacted) == false)
    return compacted;

  // Can't compact — percent-encode the IRI. In a response body only the
  // q-grammar chars are encoded; for a forward URL the whole IRI is encoded.
  return urlEncode(iri, allocP, qGrammarOnly);
}



// -----------------------------------------------------------------------------
//
// opToString - render an operator
//
static const char* opToString(LdQOperator op)
{
  switch (op)
  {
  case LdQEqual:      return "==";
  case LdQUnequal:    return "!=";
  case LdQGreater:    return ">";
  case LdQLess:       return "<";
  case LdQGreaterEq:  return ">=";
  case LdQLessEq:     return "<=";
  case LdQPattern:    return "~=";
  case LdQNotPattern: return "!~=";
  case LdQExists:     return "";
  case LdQNotExists:  return "";  // rendered as a '!' prefix on the attribute, not an infix op
  }
  return "";
}



// Forward declaration
static int renderNode(LdQNode* nodeP, CorLdContext* contextP, CorAlloc* allocP, char* buf, int bufSize, bool qGrammarOnly, char** cvV);



// -----------------------------------------------------------------------------
//
// renderTerm - render a single term
//
static int renderTerm(LdQTerm* term, CorLdContext* contextP, CorAlloc* allocP, char* buf, int bufSize, bool qGrammarOnly, char** cvV)
{
  const char* attr = ldCompactOrEncode(term->attr, contextP, allocP, qGrammarOnly);
  const char* op   = opToString(term->op);
  int n = 0;

  //
  // § 4.9 attrPath — append the sub-attribute segments, each compacted
  // (or %-encoded — IRI dots become %2E, keeping the raw '.' unambiguous
  // as the path separator) and joined with '.'.
  //
  if (term->subPathN > 0)
  {
    int total = strlen(attr) + 1;
    const char** segV = (const char**) corAlloc(allocP, term->subPathN * sizeof(char*));
    for (int i = 0; i < term->subPathN; i++)
    {
      segV[i] = ldCompactOrEncode(term->subPathV[i], contextP, allocP, qGrammarOnly);
      total  += strlen(segV[i]) + 1;
    }

    char* joined = (char*) corAlloc(allocP, total);
    strcpy(joined, attr);
    for (int i = 0; i < term->subPathN; i++)
    {
      strcat(joined, ".");
      strcat(joined, segV[i]);
    }
    attr = joined;
  }

  //
  // § 4.9 "[...]" — value-path members are terms (expanded by ldQParse), so they
  // are compacted back like the sub-attribute segments above: IRI dots become
  // %2E when a segment cannot be compacted, the joining dots stay raw. "*" is
  // the wildcard, rendered as it is.
  //
  if (term->valuePathN > 0)
  {
    int total = strlen(attr) + 3;
    const char** vsegV = (const char**) corAlloc(allocP, term->valuePathN * sizeof(char*));
    for (int i = 0; i < term->valuePathN; i++)
    {
      vsegV[i] = (strcmp(term->valuePathV[i], "*") == 0) ? "*" : ldCompactOrEncode(term->valuePathV[i], contextP, allocP, qGrammarOnly);
      total   += strlen(vsegV[i]) + 1;
    }

    char* joined = (char*) corAlloc(allocP, total);
    strcpy(joined, attr);
    strcat(joined, "[");
    for (int i = 0; i < term->valuePathN; i++)
    {
      if (i > 0) strcat(joined, ".");
      strcat(joined, vsegV[i]);
    }
    strcat(joined, "]");
    attr = joined;
  }

  if (term->op == LdQExists)
  {
    n = snprintf(buf, bufSize, "%s", attr);
    return n;
  }

  if (term->op == LdQNotExists)
  {
    n = snprintf(buf, bufSize, "!%s", attr);
    return n;
  }

  //
  // cvV: the (expanded) attributes whose string values were EXPANDED because expandValues named them
  // - a stored Subscription's q. The client sent them compact and gets them back compact.
  //
  bool compactValues = false;
  for (int ix = 0; (cvV != NULL) && (cvV[ix] != NULL); ix++)
  {
    if (strcmp(cvV[ix], term->attr) == 0)
    {
      compactValues = true;
      break;
    }
  }

  switch (term->valueType)
  {
  case LdQNumber:
    n = snprintf(buf, bufSize, "%s%s%g", attr, op, term->value.n);
    break;

  case LdQString:
    n = snprintf(buf, bufSize, "%s%s\"%s\"", attr, op, (compactValues == true) ? corLdCompact(contextP, term->value.s) : term->value.s);
    break;

  case LdQBool:
    n = snprintf(buf, bufSize, "%s%s%s", attr, op, term->value.b ? "true" : "false");
    break;

  case LdQRange:
    n = snprintf(buf, bufSize, "%s%s%g..%g", attr, op, term->value.numRange.lo, term->value.numRange.hi);
    break;

  case LdQValueList:
    n = snprintf(buf, bufSize, "%s%s", attr, op);
    for (int i = 0; i < term->value.list.count && n < bufSize; i++)
    {
      LdQValueType itemType = term->value.list.itemTypeV[i];

      if (i > 0) n += snprintf(buf + n, bufSize - n, ",");
      if (itemType == LdQString)
        n += snprintf(buf + n, bufSize - n, "\"%s\"", (compactValues == true) ? corLdCompact(contextP, term->value.list.values[i]) : term->value.list.values[i]);
      else
        n += snprintf(buf + n, bufSize - n, "%s", term->value.list.values[i]);
    }
    break;

  case LdQDateTime:
  {
    // Render back to the ISO 8601 string it was parsed from — emitting the raw
    // nanosecond integer would re-parse as a Number (semantics silently changed).
    char isoBuf[40];
    ldSysTimestampToIso(term->value.ns, isoBuf, sizeof(isoBuf));
    n = snprintf(buf, bufSize, "%s%s%s", attr, op, isoBuf);
    break;
  }

  default:
    n = snprintf(buf, bufSize, "%s%s?", attr, op);
    break;
  }

  return n;
}



// -----------------------------------------------------------------------------
//
// renderNode - recursively render a node
//
static int renderNode(LdQNode* nodeP, CorLdContext* contextP, CorAlloc* allocP, char* buf, int bufSize, bool qGrammarOnly, char** cvV)
{
  if (nodeP == NULL)
    return 0;

  if (nodeP->type == LdQTermNode)
    return renderTerm(&nodeP->term, contextP, allocP, buf, bufSize, qGrammarOnly, cvV);

  if (nodeP->type == LdQLinkedNode)
  {
    // § 4.9 LinkedEntityRelation: attrName "{" sub-q "}"
    const char* attr = ldCompactOrEncode(nodeP->linked.relName, contextP, allocP, qGrammarOnly);
    int n = 0;

    int alen = strlen(attr);
    if (n + alen >= bufSize) return n;
    memcpy(buf + n, attr, alen); n += alen;

    if (n >= bufSize) return n;
    buf[n++] = '{';

    n += renderNode(nodeP->linked.subQ, contextP, allocP, buf + n, bufSize - n, qGrammarOnly, cvV);

    if (n >= bufSize) return n;
    buf[n++] = '}';

    if (n < bufSize) buf[n] = 0;
    return n;
  }

  char sep = (nodeP->type == LdQAndNode) ? ';' : '|';
  int  n   = 0;

  for (int i = 0; i < nodeP->group.count && n < bufSize; i++)
  {
    if (i > 0)
    {
      buf[n++] = sep;
      if (n >= bufSize) break;
    }

    bool needParens = (nodeP->type == LdQOrNode && nodeP->group.childV[i]->type == LdQAndNode);

    if (needParens && n < bufSize)
      buf[n++] = '(';

    n += renderNode(nodeP->group.childV[i], contextP, allocP, buf + n, bufSize - n, qGrammarOnly, cvV);

    if (needParens && n < bufSize)
      buf[n++] = ')';
  }

  if (n < bufSize)
    buf[n] = 0;

  return n;
}



// -----------------------------------------------------------------------------
//
// ldQRender -
//
char* ldQRender(LdQNode* nodeP, CorLdContext* contextP, CorAlloc* allocP, bool qGrammarOnly)
{
  if (nodeP == NULL)
    return NULL;

  // Allocate a generous buffer
  int   bufSize = 4096;
  char* buf     = (char*) corAlloc(allocP, bufSize);

  int n = renderNode(nodeP, contextP, allocP, buf, bufSize, qGrammarOnly, NULL);
  buf[n] = 0;

  return buf;
}



// -----------------------------------------------------------------------------
//
// ldQRenderCompactValues - ldQRender, plus the string values of the attributes cvV names compacted
//
char* ldQRenderCompactValues(LdQNode* nodeP, CorLdContext* contextP, CorAlloc* allocP, bool qGrammarOnly, char** cvV)
{
  if (nodeP == NULL)
    return NULL;

  int   bufSize = 4096;
  char* buf     = (char*) corAlloc(allocP, bufSize);

  int n = renderNode(nodeP, contextP, allocP, buf, bufSize, qGrammarOnly, cvV);
  buf[n] = 0;

  return buf;
}



// -----------------------------------------------------------------------------
//
// ldQRenderStored - a q as it is STORED: attribute names expanded, their dots as '^'
//
// See storageEncode. What ldQParse reads back with no @context at all.
//
char* ldQRenderStored(LdQNode* nodeP, CorAlloc* allocP)
{
  return ldQRender(nodeP, &storageMode, allocP, true);
}
