/**
* Copyright (c) 2014-16 CD Projekt Red. All Rights Reserved.
*/

#pragma once


//////////////////////////////////////////////////////////////////////////
// headers
#include "rttiClassDeclarationMacros.h"
#include "rttiTypeName.h"
#include "../../redSystem/include/clock.h"


// This class should never contain any virtual functions
// It serves as a kind of wrapper class at the Core project level
// to allow for it to be integrated into the RTTI system
class RED_REFLECTION_API CDateTime : public red::DateTime
{
public:
	RED_INLINE CDateTime() {}
	RED_INLINE explicit CDateTime( const CDateTime& other )
	:	red::DateTime( other )
	{
	}
	
	RED_INLINE explicit CDateTime( const red::DateTime& other )
	:	red::DateTime( other )
	{
	}

	RED_INLINE void operator=( const CDateTime& other ) { red::DateTime::operator=( other ); }
	RED_INLINE void operator=( const red::DateTime& other ) { red::DateTime::operator=( other ); }

	void ImportFromOldFileTimeFormat( Uint64 winStyleTimestamp );

	// Serialize to binary file
	void Serialize( IFile& file );

	// Serialization operator
	friend void operator<<( IFile& file, CDateTime& dt )
	{
		dt.Serialize( file );
	}

	static CDateTime INVALID;
};

RTTI_DECLARE_TYPE_NAME( CDateTime );

//////////////////////////////////////////////////////////////////////////
// Type formatters
namespace red
{
	RED_REFLECTION_API constexpr const char* GetFormatString(const CDateTime& val) { return "%02d/%02d/%02d %02d:%02d:%02d"; }
	extern RED_REFLECTION_API Bool CheckFormatString(const CDateTime& val, const char* formatToCheck);
	extern RED_REFLECTION_API Bool ToBuffer(char* buffer, const red::Uint32 bufferLen, const CDateTime& val, red::Int32& written, const char* formatString = nullptr);
}

//////////////////////////////////////////////////////////////////////////
// Conversion from/to string
extern RED_REFLECTION_API const Bool ToString(red::String& outTxt, const CDateTime& val, const char* customFormat = nullptr);
extern RED_REFLECTION_API const Bool FromString(const red::String& txt, CDateTime& outVal);
