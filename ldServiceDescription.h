#ifndef CORNGSILD_LDSERVICEDESCRIPTION_H_
#define CORNGSILD_LDSERVICEDESCRIPTION_H_

//
// FILE            ldServiceDescription.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
// A Service Description stored in an entity (coraine's Service Execution, ETSI GR CIM-055 § 6.3.3): an
// attribute of type "ServiceDescription" - not a Property, no value - whose members describe a service
// of the entity and where it is executed:
//
//   { "type": "ServiceDescription", "title"?, "description"?, "mode"?, "inputSchema"?, "outputSchema"?,
//     "endpoint", "serviceDescriptionInEntity"? }
//
// Opaque to the attribute machinery (normalisation, sub-attributes): checked here, stored as it comes.
// Off unless the application turns it on - without Service Execution it is no attribute type.
//
#include <stdbool.h>                                  // bool

#include "corTree/CorNode.h"                          // CorNode



// -----------------------------------------------------------------------------
//
// ldServiceDescriptionAccepted - does this broker take ServiceDescription attributes? (false)
//
extern bool ldServiceDescriptionAccepted;



// -----------------------------------------------------------------------------
//
// ldServiceDescriptionIs - is this attribute (an object) of type ServiceDescription?
//
extern bool ldServiceDescriptionIs(CorNode* attrP);



// -----------------------------------------------------------------------------
//
// ldServiceDescriptionCheck - is it a valid one? false: a 400 has been raised (also when not accepted)
//
extern bool ldServiceDescriptionCheck(CorNode* attrP);

#endif  // CORNGSILD_LDSERVICEDESCRIPTION_H_
