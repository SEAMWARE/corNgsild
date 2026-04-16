#ifndef SWNGSILD_LDVOCAB_H_
#define SWNGSILD_LDVOCAB_H_

//
// FILE            LdVocab.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// Canonical attribute / system-field key strings.
//
// Per the core-context shortcut (orion-ld style, see project memory
// "Core-context shortcut"), terms defined in the NGSI-LD core context
// stay as their *short* form everywhere — in the parsed tree after
// expand, in the in-memory cache, in the persisted DB row, and on the
// wire. The expand step in swJsonld already enforces this; the macros
// below are the single source of truth callers compare against.
//
// "id" and "type" are NOT here — they map to JSON-LD @id/@type which
// the expander short-circuits independently.
//
// Anything that is *not* a core-context term (e.g. user attribute
// names like "speed") is still expanded to its full IRI on the way
// in. Those use the user @context (or @vocab fallback) and have no
// canonical short form, so they don't appear here.
//

// Value keys (attribute payload field — Property "value", Relationship
// "object", LanguageProperty "languageMap", VocabProperty "vocab",
// ListProperty "valueList", ListRelationship "objectList",
// JsonProperty "json"). Core-context binds the long-IRI variants
// "hasValue" / "hasObject" / ... to these short forms.
#define LD_VOCAB_HAS_VALUE         "value"
#define LD_VOCAB_HAS_OBJECT        "object"
#define LD_VOCAB_HAS_LANGUAGE_MAP  "languageMap"
#define LD_VOCAB_HAS_VOCAB         "vocab"
#define LD_VOCAB_HAS_VALUE_LIST    "valueList"
#define LD_VOCAB_HAS_OBJECT_LIST   "objectList"
#define LD_VOCAB_HAS_JSON          "json"

// Language
#define LD_VOCAB_LANG              "lang"

// Attribute metadata
#define LD_VOCAB_OBSERVED_AT       "observedAt"
#define LD_VOCAB_UNIT_CODE         "unitCode"
#define LD_VOCAB_DATASET_ID        "datasetId"

// Timestamps
#define LD_VOCAB_CREATED_AT        "createdAt"
#define LD_VOCAB_MODIFIED_AT       "modifiedAt"

// Entity-level
#define LD_VOCAB_SCOPE             "scope"

// NGSI-LD null marker — used in Merge Entity (PATCH) to request member
// deletion. See ETSI GS CIM 009 v1.9.1 clauses 5.5.12 and 5.6.17.
#define LD_VOCAB_NGSILD_NULL       "urn:ngsi-ld:null"

// GeoJSON — geometry-type *values* (Point, LineString, ...) are still
// expanded to their full IRI per the "type values always expand" rule.
// The "coordinates" KEY is in core context however, so the shortcut
// keeps it short along with all other core keys.
#define LD_VOCAB_GEOJSON_PREFIX    "https://purl.org/geojson/vocab#"
#define LD_VOCAB_GEOJSON_PREFIX_LEN 31
#define LD_VOCAB_COORDINATES       "coordinates"
#define LD_VOCAB_GEO_POINT         "https://purl.org/geojson/vocab#Point"
#define LD_VOCAB_GEO_LINE_STRING   "https://purl.org/geojson/vocab#LineString"
#define LD_VOCAB_GEO_POLYGON       "https://purl.org/geojson/vocab#Polygon"
#define LD_VOCAB_GEO_MULTI_POINT   "https://purl.org/geojson/vocab#MultiPoint"
#define LD_VOCAB_GEO_MULTI_LINE    "https://purl.org/geojson/vocab#MultiLineString"
#define LD_VOCAB_GEO_MULTI_POLYGON "https://purl.org/geojson/vocab#MultiPolygon"

// Subscription fields (all in core context, all short).
#define LD_VOCAB_ENTITIES          "entities"
#define LD_VOCAB_WATCHED_ATTRS     "watchedAttributes"
#define LD_VOCAB_NOTIFICATION      "notification"
#define LD_VOCAB_THROTTLING        "throttling"
#define LD_VOCAB_EXPIRES_AT        "expiresAt"
#define LD_VOCAB_ENDPOINT          "endpoint"
#define LD_VOCAB_FORMAT            "format"
#define LD_VOCAB_ATTRIBUTES        "attributes"
#define LD_VOCAB_URI               "uri"
#define LD_VOCAB_ID_PATTERN        "idPattern"
#define LD_VOCAB_IS_ACTIVE         "isActive"
#define LD_VOCAB_STATUS            "status"

// Registration fields
#define LD_VOCAB_INFORMATION       "information"
#define LD_VOCAB_MODE              "mode"

#endif  // SWNGSILD_LDVOCAB_H_
