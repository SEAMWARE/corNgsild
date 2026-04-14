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
// compactOrEncode - compact an IRI against the @context, URL-encode if uncompactable
//
static const char* compactOrEncode(const char* iri, SwldContext* contextP, KAlloc* allocP)
{
  // No context — internal storage mode: return the raw IRI unchanged.
  // URL-encoding is only needed for API responses where the consumer
  // must distinguish dots-in-IRIs from the q-filter sub-attribute separator.
  if (contextP == NULL)
    return iri;

  const char* compacted = swldCompact(contextP, iri);

  // If compaction succeeded (result differs from input and doesn't contain "://"),
  // use the compacted form
  if (compacted != NULL && compacted != iri && strstr(compacted, "://") == NULL)
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
  const char* attr = compactOrEncode(term->attr, contextP, allocP);
  const char* op   = opToString(term->op);
  int n = 0;

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
