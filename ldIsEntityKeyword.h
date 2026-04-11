#ifndef SWNGSILD_LD_IS_ENTITY_KEYWORD_H_
#define SWNGSILD_LD_IS_ENTITY_KEYWORD_H_

//
// FILE            ldIsEntityKeyword.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include <stdbool.h>
#include <string.h>

#include "swNgsild/LdVocab.h"



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

#endif  // SWNGSILD_LD_IS_ENTITY_KEYWORD_H_
