/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

#include "types.h"
#include "rttiTypeName.h"

/// the fundamental types implementation is hidden, in here just expose the type names
RTTI_DECLARE_TYPE_NAME( Bool );
RTTI_DECLARE_TYPE_NAME( Uint8 );
RTTI_DECLARE_TYPE_NAME( Int8 );
RTTI_DECLARE_TYPE_NAME( Uint16 );
RTTI_DECLARE_TYPE_NAME( Int16 );
RTTI_DECLARE_TYPE_NAME( Uint32 );
RTTI_DECLARE_TYPE_NAME( Int32 );
RTTI_DECLARE_TYPE_NAME( Float );
RTTI_DECLARE_TYPE_NAME( Double );
RTTI_DECLARE_TYPE_NAME( Int64 );
RTTI_DECLARE_TYPE_NAME( Uint64 );

// TODO: we don't have a core project header to put those...
RTTI_DECLARE_TYPE_NAME( CGUID );
RTTI_DECLARE_TYPE_NAME( CRUID );
RTTI_DECLARE_TYPE_NAME( CRUIDRef );
RTTI_DECLARE_TYPE_NAME( CName );
RTTI_DECLARE_TYPE_NAME( TweakDBID );
