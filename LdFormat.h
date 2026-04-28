//
// FILE            LdFormat.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//
#ifndef LD_FORMAT_H
#define LD_FORMAT_H



// -----------------------------------------------------------------------------
//
// LdFormat -
//
typedef enum LdFormat
{
  LdFormatNone = 0,
  LdFormatNormalized,
  LdFormatConcise,
  LdFormatSimplified,
  LdFormatTemporalValues,
  LdFormatAggregatedValues
} LdFormat;

#endif
