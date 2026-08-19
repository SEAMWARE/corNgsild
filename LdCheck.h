#ifndef CORNGSILD_LDCHECK_H_
#define CORNGSILD_LDCHECK_H_

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
//
#include "corNgsild/LdProblem.h"                          // LD_ERROR_*
#include "corNgsild/ldError.h"                            // ldError
#include "corNgsild/ldCheckUri.h"                         // ldCheckUri
#include "corNgsild/ldCheckDateTime.h"                    // ldCheckDateTime



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
// OBJECT_CHECK_IR - same as OBJECT_CHECK but emits InvalidRequest.
//
// § 4.9: a wrong-shape body (e.g. `[]` where an object is required) is
// InvalidRequest, not BadRequestData (which is reserved for semantic errors
// inside a syntactically-valid payload).
//
#define OBJECT_CHECK_IR(nodeP, title, detail)                                                                       \
do                                                                                                                 \
{                                                                                                                  \
  if ((nodeP) == NULL || (nodeP)->type != KjObject)                                                                \
  {                                                                                                                \
    ldError(400, LD_ERROR_INVALID_REQUEST, title, "%s", detail);                                                   \
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
  if (!ldCheckDateTime(str, NULL))                                                                                    \
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

#endif  // CORNGSILD_LDCHECK_H_
