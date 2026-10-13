/**
* Copyright (c) 2007-16 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "fileFormat.h"

using red::String;

CFileFormat::CFileFormat()
{
}

CFileFormat::CFileFormat( const String& ext, const String& desc )
	: m_extension( ext )
	, m_description( desc )
{
}

Bool CFileFormat::operator==( const CFileFormat& fileFormat ) const
{
	return ( m_extension == fileFormat.m_extension ) && ( m_description == fileFormat.m_description );
}
