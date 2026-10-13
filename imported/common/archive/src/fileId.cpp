/*
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "fileId.h"

#ifndef RED_CONFIGURATION_FINAL
#include "../../../common/redReflection/include/resourcePath.h"
#include "../../../common/redContainers/include/string/string.h"
#endif

#include "../../../common/redSystem/include/hash.h"

namespace archive
{

FileID::FileID()
	: m_hash( 0ull )
{
}

FileID::FileID( FileIDCode id )
	: m_hash( id )
{
}

red::String FileID::ToDebugString() const
{
	return String::Printf( "0x%016llX", m_hash );
}

} // namespace archive
