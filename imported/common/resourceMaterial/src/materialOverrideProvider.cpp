/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "materialOverrideProvider.h"

using red::DynArray;

///////////////////////////////////////////////////////////////////////////////
// SMaterialOverrideContext

SMaterialOverrideContext::SMaterialOverrideContext ()
	: m_originalMaterial( NULL )
	, m_chunkIndex( 0 )
{}

///////////////////////////////////////////////////////////////////////////////