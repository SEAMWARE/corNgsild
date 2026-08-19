#ifndef CORNGSILD_LD_IS_ENTITY_KEYWORD_H_
#define CORNGSILD_LD_IS_ENTITY_KEYWORD_H_

//
// FILE            ldIsEntityKeyword.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include <stdbool.h>
#include <string.h>

#include "corNgsild/LdVocab.h"



// -----------------------------------------------------------------------------
//
// ldIsEntityKeyword - check if a field name is an entity-level keyword
//
// These are fields that are NOT regular attributes and should not be
// wrapped, validated as attributes, or treated as sub-attributes.
//
static inline bool ldIsEntityKeyword(const char* name)
{
  if (strcmp(name, "id")                 == 0)  return true;
  if (strcmp(name, "@id")                == 0)  return true;
  if (strcmp(name, "type")               == 0)  return true;
  if (strcmp(name, "@type")              == 0)  return true;
  if (strcmp(name, "@context")           == 0)  return true;
  if (strcmp(name, LD_VOCAB_SCOPE)       == 0)  return true;
  if (strcmp(name, LD_VOCAB_CREATED_AT)  == 0)  return true;
  if (strcmp(name, LD_VOCAB_MODIFIED_AT) == 0)  return true;
  if (strcmp(name, LD_VOCAB_EXPIRES_AT)  == 0)  return true;

  return false;
}



// -----------------------------------------------------------------------------
//
// ldIsNotAttributeName - is this top-level member name anything other than an Attribute?
//
// The Entity members above, plus the two shapes that are never Attributes either: a nameless
// node, and any JSON-LD keyword. Every walk that separates Attributes from the rest asks THIS
// - so the separation cannot drift from file to file, which is exactly what it did until a
// member fell through to the attribute machinery and a plain PATCH crashed the broker.
//
static inline bool ldIsNotAttributeName(const char* name)
{
  if (name == NULL)    return true;
  if (name[0] == '@')  return true;

  return ldIsEntityKeyword(name);
}

#endif  // CORNGSILD_LD_IS_ENTITY_KEYWORD_H_
