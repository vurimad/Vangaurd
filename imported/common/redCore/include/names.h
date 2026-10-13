/**
* Copyright (c) 2013-20 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../redCore/include/redCoreApi.h"
#include "../../redSystem/include/typetraits.h"
#include "../../redSystem/include/hash.h"
#include "../../redContainers/include/string/stringView.h"
#include "../../redContainers/include/redContainersPublic.h"

// Use precomputed hashes to compare CNames 
//#define RED_USE_CNAME_STRING_COMPARES

class NameBuilder;

namespace red
{
	using CNameHash = Uint64;
	const constexpr Uint32 c_redCNameMaxLength = 255;
	const constexpr CNameHash c_invalidNameHashValue = 0;

	constexpr bool IsNameNone( red::StringView name );
	constexpr StringView GetNameNone();
}

// Indexed name
class CName
{
public:
	constexpr CName();

	constexpr explicit CName( std::nullptr_t );

	constexpr explicit CName( red::CNameHash nameHash );

	constexpr explicit operator bool() const { return !Empty(); }

	constexpr red::CNameHash GetHash() const { return m_hash; }

	constexpr bool Empty() const { return m_hash == red::c_invalidNameHashValue; }

	static constexpr CName NONE() { return {}; }

	REDCORE_API const AnsiChar* AsChar() const;
	REDCORE_API red::StringView AsStringView() const;

	// For use with assert and log messages
	REDCORE_API const AnsiChar* ToDebugString() const;

	REDCORE_API red::StringView GetRootName() const;

	REDCORE_API Int32 GetTrailingNumber() const;

	REDCORE_API void Set( CName name );
	REDCORE_API void Set( red::CNameHash hash );

	friend constexpr bool operator==( const CName left, const CName right );
	friend constexpr bool operator!=( const CName left, const CName right );
	friend constexpr bool operator<( const CName left, const CName right );

private:
	red::CNameHash m_hash;
};

#if defined( RED_USE_CNAME_STRING_COMPARES )
constexpr bool operator==( const CName left, const char* right );
constexpr bool operator==( const char* left, const CName right );
#endif
constexpr bool operator==( const CName left, std::nullptr_t );
constexpr bool operator==( std::nullptr_t, const CName right );

#if defined( RED_USE_CNAME_STRING_COMPARES )
constexpr bool operator!=( const CName left, const char* right );
constexpr bool operator!=( const char* left, const CName right );
#endif
constexpr bool operator!=( const CName left, std::nullptr_t );
constexpr bool operator!=( std::nullptr_t, const CName right );


class NameBuilder
{
public:
	constexpr NameBuilder( const CName name );
	constexpr NameBuilder( const AnsiChar* name );
	constexpr NameBuilder( const AnsiChar* name, Uint32 length );

	static constexpr red::CNameHash CalculateHash( const AnsiChar* name );
	static constexpr red::CNameHash CalculateHash( const AnsiChar* name, Uint32 length );
	static constexpr red::CNameHash CalculateHash( red::StringView name );
	static constexpr red::CNameHash CalculateHash( const red::String& name );
	static constexpr red::CNameHash CalculateHash( const CName prefix, const AnsiChar* suffix );

	REDCORE_API static void Register( red::CNameHash hash, const AnsiChar* name );
	REDCORE_API static void Register( red::CNameHash hash, const AnsiChar* name, Uint32 length );

	static constexpr CName Build( const CName name );
	REDCORE_API static CName Build( const AnsiChar* name );
	REDCORE_API static CName Build( const AnsiChar* name, Uint32 length );
	REDCORE_API static CName Build( red::StringView name );
	REDCORE_API static CName Build( const red::String& name );
	REDCORE_API static CName Build( const CName prefix, const AnsiChar* suffix );

	red::CNameHash m_hash;
};

template< red::CNameHash Hash >
struct ConstNameBuilder
{
	static CName Build( const AnsiChar* name )
	{
		if( !s_registered )
		{
			NameBuilder::Register( Hash, name );
			s_registered = true;
		};
		return CName{ Hash };
	}
	static Bool s_registered;
};

template< red::CNameHash Hash >
Bool ConstNameBuilder< Hash >::s_registered = false;


#if defined( _MSC_VER )
RED_DISABLE_WARNING_MSC( 4307 ) /* unsigned int overflow */
#endif

// NOTE
// Macro Selection:
// - Is string representation not required to be available at runtime? Use NOREG
// - Is string representation only required in non-final at runtime? Use DEBUGREG
// - Is string representation available to be hashed at compile time? Use CONSTEXPR

#define RED_NAME_HASH( x ) std::integral_constant< red::CNameHash, NameBuilder::CalculateHash( x ) >::value

#define RED_NAME_CONSTEXPR( x ) ConstNameBuilder< NameBuilder::CalculateHash( x ) >::Build( x )
#define RED_NAME_CONSTEXPR_NOREG( x ) CName( RED_NAME_HASH( x ) )

#define RED_NAME( x ) NameBuilder::Build( x )
#define RED_NAME_WITH_LENGTH( name, length ) NameBuilder::Build( name, length )
#define RED_NAME_NOREG( x ) CName( NameBuilder::CalculateHash( x ) )

#if defined( RED_CONFIGURATION_FINAL ) && !defined( USE_PROFILER )

#define RED_NAME_DEBUGREG( x ) CName( NameBuilder::CalculateHash( x ) )
#define RED_NAME_CONSTEXPR_DEBUGREG( x ) CName( RED_NAME_HASH( x ) )

#else

#define RED_NAME_DEBUGREG( x ) NameBuilder::Build( x )
#define RED_NAME_CONSTEXPR_DEBUGREG( x ) ConstNameBuilder< NameBuilder::CalculateHash( x ) >::Build( x )

#endif

#define RED_APPEND_NAME( prefix, suffix ) NameBuilder::Build( prefix, suffix )

//////////////////////////////////////////////////////////////////////////
// type formatter+
namespace red
{
	REDCORE_API constexpr const char* GetFormatString( const CName val ) { RED_UNUSED( val ); return "%hs"; }
	extern REDCORE_API Bool CheckFormatString( const CName val, const char* formatToCheck );
	extern REDCORE_API Bool ToBuffer( char* buffer, const Uint32 bufferLen, const CName val, Int32& written, const char* formatString = nullptr );

	// Implement this function so we don't need a CalcHash() method on the CName class.
	template<>
	RED_INLINE red::THash32 GetHash< CName >( const CName& name )
	{
		return static_cast< red::THash32 >( name.GetHash() );
	}

	namespace err
	{
		// Used in a function expression and never stored; m_buf not owned and must remain valid
		struct NameViewConvertType
		{
			NameViewConvertType(const char* buf)
				: m_buf{ buf }
			{}

			NameViewConvertType(const CName& name )
				: m_buf{ name.AsStringView() }
			{}

			NameViewConvertType(const String& name)
				: m_buf{ name }
			{}

			const red::StringView m_buf;
		};
	}

	template<Uint32 Length>
	struct err::CrashDataTypeAdapter<CName, Length>
	{
		static_assert( Length <= c_redCNameMaxLength, "Length too long");
		using StorageType = char[DefaultCrashDataCharArrayLength<Length>() + 1];
		using SetType = NameViewConvertType;
	
		// Extract the name now, since it the names pool could get destructed at exit
		// or some other corruption occurs
		static CrashDataCopyResult Copy(StorageType& storage, const SetType& value)
		{
			// Note: our strcpy wrapper doesn't have the same behavior on Orbis, so manually truncate
			constexpr Uint32 destSize = RED_ARRAY_COUNT_U32(storage);
			if ( !value.m_buf.Data() || !red::Strcpy( storage, value.m_buf.Data(), destSize, destSize - 1 ) )
			{
				return CrashDataCopyResult::Error;
			}
			
			const Uint32 srcLen = value.m_buf.Length();
			return srcLen + 1 <= destSize ? CrashDataCopyResult::Success : CrashDataCopyResult::Truncated;
		}

		static Bool Print(char* buffer, Uint32 bufferLen, const StorageType& val)
		{
			return red::Strcpy(buffer, val, bufferLen);
		}
	};

}

RED_ALLOW_TYPE_AS_POD( CName );

#include "names.hpp"
