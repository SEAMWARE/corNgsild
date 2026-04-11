#ifndef SWNGSILD_LDVOCAB_H_
#define SWNGSILD_LDVOCAB_H_

//
// FILE            LdVocab.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//
// Expanded IRI names from the NGSI-LD Core Context v1.9.
// After JSON-LD expansion, attribute names in the tree use these full IRIs.
// "id" and "type" are NOT here — they expand to @id/@type which the expander skips.
//

// Value keys (attribute value fields)
#define LD_VOCAB_HAS_VALUE         "https://uri.etsi.org/ngsi-ld/hasValue"
#define LD_VOCAB_HAS_OBJECT        "https://uri.etsi.org/ngsi-ld/hasObject"
#define LD_VOCAB_HAS_LANGUAGE_MAP  "https://uri.etsi.org/ngsi-ld/hasLanguageMap"
#define LD_VOCAB_HAS_VOCAB         "https://uri.etsi.org/ngsi-ld/hasVocab"
#define LD_VOCAB_HAS_VALUE_LIST    "https://uri.etsi.org/ngsi-ld/hasValueList"
#define LD_VOCAB_HAS_OBJECT_LIST   "https://uri.etsi.org/ngsi-ld/hasObjectList"
#define LD_VOCAB_HAS_JSON          "https://uri.etsi.org/ngsi-ld/hasJSON"

// Language
#define LD_VOCAB_LANG              "https://uri.etsi.org/ngsi-ld/lang"

// Attribute metadata
#define LD_VOCAB_OBSERVED_AT       "https://uri.etsi.org/ngsi-ld/observedAt"
#define LD_VOCAB_UNIT_CODE         "https://uri.etsi.org/ngsi-ld/unitCode"
#define LD_VOCAB_DATASET_ID        "https://uri.etsi.org/ngsi-ld/datasetId"

// Timestamps
#define LD_VOCAB_CREATED_AT        "https://uri.etsi.org/ngsi-ld/createdAt"
#define LD_VOCAB_MODIFIED_AT       "https://uri.etsi.org/ngsi-ld/modifiedAt"

// Entity-level
#define LD_VOCAB_SCOPE             "https://uri.etsi.org/ngsi-ld/scope"

// NGSI-LD null marker — used in Merge Entity (PATCH) to request member deletion.
// See ETSI GS CIM 009 v1.9.1 clauses 5.5.12 and 5.6.17.
#define LD_VOCAB_NGSILD_NULL       "urn:ngsi-ld:null"

// GeoJSON
#define LD_VOCAB_GEOJSON_PREFIX    "https://purl.org/geojson/vocab#"
#define LD_VOCAB_GEOJSON_PREFIX_LEN 31
#define LD_VOCAB_COORDINATES       "https://purl.org/geojson/vocab#coordinates"
#define LD_VOCAB_GEO_POINT         "https://purl.org/geojson/vocab#Point"
#define LD_VOCAB_GEO_LINE_STRING   "https://purl.org/geojson/vocab#LineString"
#define LD_VOCAB_GEO_POLYGON       "https://purl.org/geojson/vocab#Polygon"
#define LD_VOCAB_GEO_MULTI_POINT   "https://purl.org/geojson/vocab#MultiPoint"
#define LD_VOCAB_GEO_MULTI_LINE    "https://purl.org/geojson/vocab#MultiLineString"
#define LD_VOCAB_GEO_MULTI_POLYGON "https://purl.org/geojson/vocab#MultiPolygon"

// Subscription fields
#define LD_VOCAB_ENTITIES          "https://uri.etsi.org/ngsi-ld/entities"
#define LD_VOCAB_WATCHED_ATTRS     "https://uri.etsi.org/ngsi-ld/watchedAttributes"
#define LD_VOCAB_NOTIFICATION      "https://uri.etsi.org/ngsi-ld/notification"
#define LD_VOCAB_THROTTLING        "https://uri.etsi.org/ngsi-ld/throttling"
#define LD_VOCAB_EXPIRES_AT        "https://uri.etsi.org/ngsi-ld/expiresAt"
#define LD_VOCAB_ENDPOINT          "https://uri.etsi.org/ngsi-ld/endpoint"
#define LD_VOCAB_FORMAT            "https://uri.etsi.org/ngsi-ld/format"
#define LD_VOCAB_ATTRIBUTES        "https://uri.etsi.org/ngsi-ld/attributes"
#define LD_VOCAB_URI               "https://uri.etsi.org/ngsi-ld/uri"
#define LD_VOCAB_ID_PATTERN        "https://uri.etsi.org/ngsi-ld/idPattern"

// Registration fields
#define LD_VOCAB_INFORMATION       "https://uri.etsi.org/ngsi-ld/information"
#define LD_VOCAB_MODE              "https://uri.etsi.org/ngsi-ld/mode"

#endif  // SWNGSILD_LDVOCAB_H_
