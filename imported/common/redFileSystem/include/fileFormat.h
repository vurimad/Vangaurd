/**
* Copyright (c) 2007-16 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../redContainers/include/string/string.h"

/// Description of file format
class RED_FILESYSTEM_API CFileFormat
{
protected:
	red::String		m_extension;
	red::String		m_description;

public:
	CFileFormat();
	CFileFormat( const red::String& ext, const red::String& desc );

	Bool operator==( const CFileFormat& fileFormat ) const;

	RED_INLINE const red::String& GetExtension() const { return m_extension; }
	RED_INLINE const red::String& GetDescription() const { return m_description; }
};