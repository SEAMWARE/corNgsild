//
// FILE            CorTerm.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#ifndef CORNGSILD_CORTERM_H_
#define CORNGSILD_CORTERM_H_



// -----------------------------------------------------------------------------
//
// CorTerm - the id of an NGSI-LD core term
//
// One entry per term of the core context (v1.9), in the core context's own order,
// then the terms of coraine's Bridge/Channel payloads. The id is what a CorNode
// carries in 'termId' and a CorLdItem in its own 'termId' (ldCoreTermIdsInit sets
// them at startup), so which core term a member is costs no string compare.
//
// ⚠ APPEND-ONLY. The ids are stored and sent on the binary wire - never reorder,
// never reuse, never remove; a term a later core context adds goes at the end.
// Every entry has its name in ldCoreTermNameV (ldCoreTermIds.c), at the same index.
//
// Naming: CorTerm + the term, first letter upper-cased, '-' dropped. Where a type
// and a member differ only in case (EntityMap / entityMap), the type gets 'Type'.
//
// 0 is "not a core term" - a user term, or a node no one classified.
//
typedef enum CorTerm
{
  CorTermNone,                             // 0 - not a core term

  // NGSI-LD core context v1.9
  CorTermNgsiLd,                           // ngsi-ld
  CorTermGeojson,                          // geojson
  CorTermId,                               // id
  CorTermType,                             // type
  CorTermAttribute,                        // Attribute
  CorTermAttributeListType,                // AttributeList
  CorTermContextSourceIdentity,            // ContextSourceIdentity
  CorTermContextSourceNotification,        // ContextSourceNotification
  CorTermContextSourceRegistration,        // ContextSourceRegistration
  CorTermDate,                             // Date
  CorTermDateTime,                         // DateTime
  CorTermEntityMapType,                    // EntityMap
  CorTermEntityType,                       // EntityType
  CorTermEntityTypeInfo,                   // EntityTypeInfo
  CorTermEntityTypeList,                   // EntityTypeList
  CorTermExecutionResultDetails,           // ExecutionResultDetails
  CorTermFeature,                          // Feature
  CorTermFeatureCollection,                // FeatureCollection
  CorTermGeoProperty,                  // GeoProperty
  CorTermGeometryCollection,               // GeometryCollection
  CorTermJsonProperty,                     // JsonProperty
  CorTermLanguageProperty,                 // LanguageProperty
  CorTermLineString,                       // LineString
  CorTermListProperty,                     // ListProperty
  CorTermListRelationship,                 // ListRelationship
  CorTermMultiLineString,                  // MultiLineString
  CorTermMultiPoint,                       // MultiPoint
  CorTermMultiPolygon,                     // MultiPolygon
  CorTermNotificationType,                 // Notification
  CorTermPoint,                            // Point
  CorTermPolygon,                          // Polygon
  CorTermProperty,                         // Property
  CorTermRelationship,                     // Relationship
  CorTermSnapshot,                         // Snapshot
  CorTermSnapshotNotification,             // SnapshotNotification
  CorTermSubscription,                     // Subscription
  CorTermTemporalProperty,                 // TemporalProperty
  CorTermTime,                             // Time
  CorTermVocabProperty,                    // VocabProperty
  CorTermAccept,                           // accept
  CorTermAggrParams,                       // aggrParams
  CorTermAggrMethods,                      // aggrMethods
  CorTermAggrPeriodDuration,               // aggrPeriodDuration
  CorTermAttributeCount,                   // attributeCount
  CorTermAttributeDetails,                 // attributeDetails
  CorTermAttributeList,                    // attributeList
  CorTermAttributeName,                    // attributeName
  CorTermAttributeNames,                   // attributeNames
  CorTermAttributeTypes,                   // attributeTypes
  CorTermAttributes,                       // attributes
  CorTermAttrs,                            // attrs
  CorTermAvg,                              // avg
  CorTermBbox,                             // bbox
  CorTermCacheDuration,                    // cacheDuration
  CorTermCollation,                        // collation
  CorTermContainedBy,                      // containedBy
  CorTermContextSourceAlias,               // contextSourceAlias
  CorTermContextSourceExtras,              // contextSourceExtras
  CorTermContextSourceInfo,                // contextSourceInfo
  CorTermContextSourceTimeAt,              // contextSourceTimeAt
  CorTermContextSourceUptime,              // contextSourceUptime
  CorTermCooldown,                         // cooldown
  CorTermCoordinates,                      // coordinates
  CorTermCreatedAt,                        // createdAt
  CorTermCsf,                              // csf
  CorTermData,                             // data
  CorTermDataset,                          // dataset
  CorTermDatasetId,                        // datasetId
  CorTermDeletedAt,                        // deletedAt
  CorTermDescription,                      // description
  CorTermDetail,                           // detail
  CorTermDistinctCount,                    // distinctCount
  CorTermEndAt,                            // endAt
  CorTermEndTimeAt,                        // endTimeAt
  CorTermEndpoint,                         // endpoint
  CorTermEntities,                         // entities
  CorTermEntity,                           // entity
  CorTermEntityCount,                      // entityCount
  CorTermEntityId,                         // entityId
  CorTermEntityList,                       // entityList
  CorTermEntityMap,                        // entityMap
  CorTermEntityMapLifetime,                // entityMapLifetime
  CorTermError,                            // error
  CorTermErrors,                           // errors
  CorTermExpandValues,                     // expandValues
  CorTermExpiresAt,                        // expiresAt
  CorTermFeatures,                         // features
  CorTermFormat,                           // format
  CorTermGeoQ,                             // geoQ
  CorTermGeometry,                         // geometry
  CorTermGeoproperty,                      // geoproperty
  CorTermGeorel,                           // georel
  CorTermIdPattern,                        // idPattern
  CorTermInformation,                      // information
  CorTermInstanceId,                       // instanceId
  CorTermIsActive,                         // isActive
  CorTermJoin,                             // join
  CorTermJoinLevel,                        // joinLevel
  CorTermJson,                             // json
  CorTermJsonKeys,                         // jsonKeys
  CorTermJsonldContext,                    // jsonldContext
  CorTermJsons,                            // jsons
  CorTermKey,                              // key
  CorTermLang,                             // lang
  CorTermLanguageMap,                      // languageMap
  CorTermLanguageMaps,                     // languageMaps
  CorTermLangString,                       // langString
  CorTermLastFailure,                      // lastFailure
  CorTermLastN,                            // lastN
  CorTermLastNotification,                 // lastNotification
  CorTermLastSuccess,                      // lastSuccess
  CorTermLastUsedAt,                       // lastUsedAt
  CorTermLinkedMaps,                       // linkedMaps
  CorTermLocalOnly,                        // localOnly
  CorTermLocation,                         // location
  CorTermManagement,                       // management
  CorTermManagementInterval,               // managementInterval
  CorTermMax,                              // max
  CorTermMin,                              // min
  CorTermMode,                             // mode
  CorTermModifiedAt,                       // modifiedAt
  CorTermNgsildproof,                      // ngsildproof
  CorTermNgsildConformance,                // ngsildConformance
  CorTermNotification,                     // notification
  CorTermNotificationTrigger,              // notificationTrigger
  CorTermNotifiedAt,                       // notifiedAt
  CorTermNotifierInfo,                     // notifierInfo
  CorTermNotUpdated,                       // notUpdated
  CorTermObject,                           // object
  CorTermObjectList,                       // objectList
  CorTermObjectLists,                      // objectLists
  CorTermObjects,                          // objects
  CorTermObjectType,                       // objectType
  CorTermObservationInterval,              // observationInterval
  CorTermObservationSpace,                 // observationSpace
  CorTermObservedAt,                       // observedAt
  CorTermOmit,                             // omit
  CorTermOperations,                       // operations
  CorTermOperationSpace,                   // operationSpace
  CorTermOrderBy,                          // orderBy
  CorTermOrdering,                         // ordering
  CorTermPick,                             // pick
  CorTermPreviousJson,                     // previousJson
  CorTermPreviousLanguageMap,              // previousLanguageMap
  CorTermPreviousObject,                   // previousObject
  CorTermPreviousObjectList,               // previousObjectList
  CorTermPreviousValue,                    // previousValue
  CorTermPreviousValueList,                // previousValueList
  CorTermPreviousVocab,                    // previousVocab
  CorTermProblemDetails,                   // problemDetails
  CorTermProperties,                       // properties
  CorTermPropertyNames,                    // propertyNames
  CorTermQ,                                // q
  CorTermReason,                           // reason
  CorTermReceiverInfo,                     // receiverInfo
  CorTermRefreshRate,                      // refreshRate
  CorTermRegistrationId,                   // registrationId
  CorTermRegistrationName,                 // registrationName
  CorTermRelationshipNames,                // relationshipNames
  CorTermResultStatus,                     // resultStatus
  CorTermScope,                            // scope
  CorTermScopeQ,                           // scopeQ
  CorTermShowChanges,                      // showChanges
  CorTermSnapshotId,                       // snapshotId
  CorTermSnapshotLifetime,                 // snapshotLifetime
  CorTermSnapshotPriority,                 // snapshotPriority
  CorTermSnapshotQueries,                  // snapshotQueries
  CorTermSnapshotQueriesDetails,           // snapshotQueriesDetails
  CorTermSnapshotStatus,                   // snapshotStatus
  CorTermSnapshotTemporalQueries,          // snapshotTemporalQueries
  CorTermSnapshotTemporalQueriesDetails,   // snapshotTemporalQueriesDetails
  CorTermSplitEntities,                    // splitEntities
  CorTermStartAt,                          // startAt
  CorTermStatus,                           // status
  CorTermStddev,                           // stddev
  CorTermSubscriptionId,                   // subscriptionId
  CorTermSubscriptionName,                 // subscriptionName
  CorTermSuccess,                          // success
  CorTermSum,                              // sum
  CorTermSumsq,                            // sumsq
  CorTermSysAttrs,                         // sysAttrs
  CorTermTemporalQ,                        // temporalQ
  CorTermTenant,                           // tenant
  CorTermThrottling,                       // throttling
  CorTermTimeAt,                           // timeAt
  CorTermTimeInterval,                     // timeInterval
  CorTermTimeout,                          // timeout
  CorTermTimeproperty,                     // timeproperty
  CorTermTimerel,                          // timerel
  CorTermTimesFailed,                      // timesFailed
  CorTermTimesSent,                        // timesSent
  CorTermTitle,                            // title
  CorTermTotalCount,                       // totalCount
  CorTermTriggerReason,                    // triggerReason
  CorTermTypeList,                         // typeList
  CorTermTypeName,                         // typeName
  CorTermTypeNames,                        // typeNames
  CorTermUnchanged,                        // unchanged
  CorTermUnitCode,                         // unitCode
  CorTermUpdated,                          // updated
  CorTermUri,                              // uri
  CorTermValue,                            // value
  CorTermValueList,                        // valueList
  CorTermValueLists,                       // valueLists
  CorTermValues,                           // values
  CorTermValueType,                        // valueType
  CorTermVocab,                            // vocab
  CorTermVocabs,                           // vocabs
  CorTermWatchedAttributes,                // watchedAttributes

  // coraine Bridge/Channel - PROVISIONAL: a proposal to ETSI, not (yet) in the core
  // context. The special case: corLdCoreTermsAdd makes them core terms at startup.
  CorTermContextBridge,                    // ContextBridge
  CorTermChannel,                          // Channel
  CorTermGoal,                             // Goal
  CorTermBridgeId,                         // bridgeId
  CorTermBridgeOptions,                    // bridgeOptions
  CorTermPlugin,                           // plugin
  CorTermChannelTarget,                    // channelTarget
  CorTermChannelKind,                      // channelKind
  CorTermChannelDirection,                 // channelDirection
  CorTermRetention,                        // retention
  CorTermCodec,                            // codec
  CorTermEntityAttribute,                  // entityAttribute
  CorTermStatusReason,                     // statusReason
  CorTermGoals,                            // goals
  CorTermGoalId,                           // goalId
  CorTermGoalRequest,                      // goalRequest
  CorTermGoalFeedback,                     // goalFeedback
  CorTermGoalResult,                       // goalResult

  CorTermLast                              // the number of ids - not a term
} CorTerm;

//
// Ids fit in 15 bits: the top bit of CorNode.termId stays free.
//
_Static_assert(CorTermLast <= 0x8000, "CorTerm ids must fit in 15 bits");

#endif  // CORNGSILD_CORTERM_H_
