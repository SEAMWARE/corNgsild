//
// FILE            ldQRender.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// Render a parsed LdQNode tree back to a q-filter string.
//
#include <stdio.h>                                     // snprintf
#include <string.h>                                    // strlen, strcpy, strcat, strcmp
#include <stdlib.h>                                    // malloc

#include "kalloc/KAlloc.h"                             // KAlloc
#include "kalloc/kaAlloc.h"                            // kaAlloc
#include "swJsonld/SwldContext.h"                      // SwldContext
#include "swJsonld/swldCompact.h"                      // swldCompact
#include "swJsonld/swldExpand.h"                       // swldAlreadyExpanded

#include "swNgsild/LdQ.h"                              // LdQNode, LdQTerm
#include "swNgsild/ldQRender.h"                        // Own interface



// -----------------------------------------------------------------------------
//
// urlEncode - URL-encode a string (allocates with kaAlloc or malloc)
//
// Encodes all characters except unreserved: A-Z a-z 0-9 - _ . ~
//
static char* urlEncode(const char* s, KAlloc* allocP)
{
  // Worst case: every char becomes %XX (3x expansion)
  int   len    = strlen(s);
  int   outLen = len * 3 + 1;
  char* out    = (char*) kaAlloc(allocP, outLen);
  char* p      = out;

  for (int i = 0; i < len; i++)
  {
    unsigned char c = (unsigned char) s[i];

    // In q-filter context, dots and brackets have special meaning (sub-attribute
    // access), so they must be encoded too — not just the RFC 3986 reserved set.
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
        || c == '-' || c == '_' || c == '~')
    {
      *p++ = c;
    }
    else
    {
      p += sprintf(p, "%%%02X", c);
    }
  }

  *p = 0;
  return out;
}



// -----------------------------------------------------------------------------
//
// ldCompactOrEncode - compact an IRI against the @context, URL-encode if uncompactable
//
const char* ldCompactOrEncode(const char* iri, SwldContext* contextP, KAlloc* allocP)
{
  // No context — internal storage mode: return the raw IRI unchanged.
  // URL-encoding is only needed for API responses where the consumer
  // must distinguish dots-in-IRIs from the q-filter sub-attribute separator.
  if (contextP == NULL)
    return iri;

  const char* compacted = swldCompact(contextP, iri);

  // swldCompact's contract: returns a different pointer for an actual short
  // term, returns iri unchanged otherwise. Belt-and-suspenders, also reject
  // any result that is itself still an IRI (urn:, http://, https://) — a
  // future swldCompact change that re-emits an IRI form mustn't slip past.
  if (compacted != NULL && compacted != iri && swldAlreadyExpanded(compacted) == false)
    return compacted;

  // Can't compact — URL-encode the full IRI
  return urlEncode(iri, allocP);
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
  }
  return "";
}



// Forward declaration
static int renderNode(LdQNode* nodeP, SwldContext* contextP, KAlloc* allocP, char* buf, int bufSize);



// -----------------------------------------------------------------------------
//
// renderTerm - render a single term
//
static int renderTerm(LdQTerm* term, SwldContext* contextP, KAlloc* allocP, char* buf, int bufSize)
{
  const char* attr = ldCompactOrEncode(term->attr, contextP, allocP);
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
    const char** segV = (const char**) kaAlloc(allocP, term->subPathN * sizeof(char*));
    for (int i = 0; i < term->subPathN; i++)
    {
      segV[i] = ldCompactOrEncode(term->subPathV[i], contextP, allocP);
      total  += strlen(segV[i]) + 1;
    }

    char* joined = (char*) kaAlloc(allocP, total);
    strcpy(joined, attr);
    for (int i = 0; i < term->subPathN; i++)
    {
      strcat(joined, ".");
      strcat(joined, segV[i]);
    }
    attr = joined;
  }

  //
  // § 4.9 "[...]" — value-path members are opaque (no compaction):
  // %-encode only, dots inside a member name become %2E, the joining
  // dots stay raw.
  //
  if (term->valuePathN > 0)
  {
    int total = strlen(attr) + 3;
    const char** vsegV = (const char**) kaAlloc(allocP, term->valuePathN * sizeof(char*));
    for (int i = 0; i < term->valuePathN; i++)
    {
      vsegV[i] = urlEncode(term->valuePathV[i], allocP);
      total   += strlen(vsegV[i]) + 1;
    }

    char* joined = (char*) kaAlloc(allocP, total);
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

  switch (term->valueType)
  {
  case LdQNumber:
    n = snprintf(buf, bufSize, "%s%s%g", attr, op, term->value.n);
    break;

  case LdQString:
    n = snprintf(buf, bufSize, "%s%s\"%s\"", attr, op, term->value.s);
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
      if (i > 0) n += snprintf(buf + n, bufSize - n, ",");
      if (term->value.list.itemType == LdQString)
        n += snprintf(buf + n, bufSize - n, "\"%s\"", term->value.list.values[i]);
      else
        n += snprintf(buf + n, bufSize - n, "%s", term->value.list.values[i]);
    }
    break;

  case LdQDateTime:
    // Render as ISO 8601 — for now, just render the nanosecond value as a number
    // (a proper implementation would convert back to ISO string)
    n = snprintf(buf, bufSize, "%s%s%lld", attr, op, term->value.ns);
    break;

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
static int renderNode(LdQNode* nodeP, SwldContext* contextP, KAlloc* allocP, char* buf, int bufSize)
{
  if (nodeP == NULL)
    return 0;

  if (nodeP->type == LdQTermNode)
    return renderTerm(&nodeP->term, contextP, allocP, buf, bufSize);

  if (nodeP->type == LdQLinkedNode)
  {
    // § 4.9 LinkedEntityRelation: attrName "{" sub-q "}"
    const char* attr = ldCompactOrEncode(nodeP->linked.relName, contextP, allocP);
    int n = 0;

    int alen = strlen(attr);
    if (n + alen >= bufSize) return n;
    memcpy(buf + n, attr, alen); n += alen;

    if (n >= bufSize) return n;
    buf[n++] = '{';

    n += renderNode(nodeP->linked.subQ, contextP, allocP, buf + n, bufSize - n);

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

    n += renderNode(nodeP->group.childV[i], contextP, allocP, buf + n, bufSize - n);

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
char* ldQRender(LdQNode* nodeP, SwldContext* contextP, KAlloc* allocP)
{
  if (nodeP == NULL)
    return NULL;

  // Allocate a generous buffer
  int   bufSize = 4096;
  char* buf     = (char*) kaAlloc(allocP, bufSize);

  int n = renderNode(nodeP, contextP, allocP, buf, bufSize);
  buf[n] = 0;

  return buf;
}
