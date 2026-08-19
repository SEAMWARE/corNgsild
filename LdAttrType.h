//
// FILE            LdAttrType.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
// 
//
#ifndef LD_ATTR_TYPE_H
#define LD_ATTR_TYPE_H



// -----------------------------------------------------------------------------
//
// LdAttrType -
//
typedef enum LdAttrType
{
  LdAttrNone = 0,
  LdAttrProperty,
  LdAttrRelationship,
  LdAttrGeoProperty,
  LdAttrLanguageProperty,
  LdAttrVocabProperty,
  LdAttrListProperty,
  LdAttrListRelationship,
  LdAttrJsonProperty
} LdAttrType;

#endif
