#ifndef SWNGSILD_LDCHECK_H_
#define SWNGSILD_LDCHECK_H_

//
// FILE            LdCheck.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//
// Validation macros for NGSI-LD payload checking.
// Each macro calls ldError() and returns false on failure.
// Modeled after Orion-LD's CHECK.h.
//
#include "swNgsild/LdProblem.h"                          // LD_ERROR_*
#include "swNgsild/ldError.h"                            // ldError
#include "swNgsild/ldCheckUri.h"                         // ldCheckUri
#include "swNgsild/ldCheckDateTime.h"                    // ldCheckDateTime



// -----------------------------------------------------------------------------
//
// OBJECT_CHECK -
//
#define OBJECT_CHECK(nodeP, title, detail)                                                                          \
do                                                                                                                 \
{                                                                                                                  \
  if ((nodeP) == NULL || (nodeP)->type != KjObject)                                                                \
  {                                                                                                                \
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, title, "%s", detail);                                                  \
    return false;                                                                                                  \
  }                                                                                                                \
} while (0)



// -----------------------------------------------------------------------------
//
// ARRAY_CHECK -
//
#define ARRAY_CHECK(nodeP, title, detail)                                                                           \
do                                                                                                                 \
{                                                                                                                  \
  if ((nodeP)->type != KjArray)                                                                                    \
  {                                                                                                                \
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, title, "%s", detail);                                                  \
    return false;                                                                                                  \
  }                                                                                                                \
} while (0)



// -----------------------------------------------------------------------------
//
// STRING_CHECK -
//
#define STRING_CHECK(nodeP, title, detail)                                                                          \
do                                                                                                                 \
{                                                                                                                  \
  if ((nodeP)->type != KjString)                                                                                   \
  {                                                                                                                \
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, title, "%s", detail);                                                  \
    return false;                                                                                                  \
  }                                                                                                                \
} while (0)



// -----------------------------------------------------------------------------
//
// NUMBER_CHECK -
//
#define NUMBER_CHECK(nodeP, title, detail)                                                                          \
do                                                                                                                 \
{                                                                                                                  \
  if ((nodeP)->type != KjInt && (nodeP)->type != KjFloat)                                                          \
  {                                                                                                                \
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, title, "%s", detail);                                                  \
    return false;                                                                                                  \
  }                                                                                                                \
} while (0)



// -----------------------------------------------------------------------------
//
// EMPTY_ARRAY_CHECK -
//
#define EMPTY_ARRAY_CHECK(nodeP, detail)                                                                           \
do                                                                                                                 \
{                                                                                                                  \
  if ((nodeP)->value.firstChildP == NULL)                                                                            \
  {                                                                                                                \
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Empty Array", "%s", detail);                                          \
    return false;                                                                                                  \
  }                                                                                                                \
} while (0)



// -----------------------------------------------------------------------------
//
// EMPTY_OBJECT_CHECK -
//
#define EMPTY_OBJECT_CHECK(nodeP, detail)                                                                          \
do                                                                                                                 \
{                                                                                                                  \
  if ((nodeP)->value.firstChildP == NULL)                                                                            \
  {                                                                                                                \
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Empty Object", "%s", detail);                                         \
    return false;                                                                                                  \
  }                                                                                                                \
} while (0)



// -----------------------------------------------------------------------------
//
// DUPLICATE_CHECK - if pointer is already set, it's a duplicate; otherwise assign
//
#define DUPLICATE_CHECK(pointer, fieldName, value)                                                                  \
do                                                                                                                 \
{                                                                                                                  \
  if ((pointer) != NULL)                                                                                           \
  {                                                                                                                \
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Duplicated field", "%s", fieldName);                                   \
    return false;                                                                                                  \
  }                                                                                                                \
  (pointer) = (value);                                                                                             \
} while (0)



// -----------------------------------------------------------------------------
//
// DUPLICATE_FLAG_CHECK - like DUPLICATE_CHECK but with boolean flags (for id/@id, type/@type)
//
#define DUPLICATE_FLAG_CHECK(flag1, flag2, title, detail)                                                           \
do                                                                                                                 \
{                                                                                                                  \
  if ((flag1) == true || (flag2) == true)                                                                          \
  {                                                                                                                \
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, title, "%s", detail);                                                  \
    return false;                                                                                                  \
  }                                                                                                                \
} while (0)



// -----------------------------------------------------------------------------
//
// URI_CHECK -
//
#define URI_CHECK(uri)                                                                                             \
do                                                                                                                 \
{                                                                                                                  \
  if (ldCheckUri(uri) == false)                                                                                    \
    return false;                                                                                                  \
} while (0)



// -----------------------------------------------------------------------------
//
// DATETIME_CHECK -
//
#define DATETIME_CHECK(str, detail)                                                                                \
do                                                                                                                 \
{                                                                                                                  \
  if (ldCheckDateTime(str) < 0)                                                                                    \
  {                                                                                                                \
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid DateTime", "%s", detail);                                     \
    return false;                                                                                                  \
  }                                                                                                                \
} while (0)



// -----------------------------------------------------------------------------
//
// MANDATORY_CHECK - field pointer must not be NULL
//
#define MANDATORY_CHECK(pointer, title, detail)                                                                     \
do                                                                                                                 \
{                                                                                                                  \
  if ((pointer) == NULL)                                                                                           \
  {                                                                                                                \
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, title, "%s", detail);                                                  \
    return false;                                                                                                  \
  }                                                                                                                \
} while (0)



// -----------------------------------------------------------------------------
//
// POSITIVE_NUMBER_CHECK -
//
#define POSITIVE_NUMBER_CHECK(nodeP, detail)                                                                        \
do                                                                                                                 \
{                                                                                                                  \
  if (((nodeP)->type == KjInt && (nodeP)->value.i < 0) || ((nodeP)->type == KjFloat && (nodeP)->value.f < 0.0))    \
  {                                                                                                                \
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Negative Number", "%s", detail);                                      \
    return false;                                                                                                  \
  }                                                                                                                \
} while (0)

#endif  // SWNGSILD_LDCHECK_H_
