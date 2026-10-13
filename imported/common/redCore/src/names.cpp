/**
* Copyright (c) 2015-20 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "names.h"
#include "nameRegistry.h"
#include "../../redContainers/include/string/stringUtils.h"

namespace
{
	const Int32 c_invalidTrailingDigit = -1;

	Int32 ExtractDigit( Uint32 digitOffset, const red::StringView& view )
	{
		if ( digitOffset != view.Length() )
		{
			Int32 value = 0;
			const bool result = red::StringToInt( value, view.SubView( digitOffset ).Data(), nullptr, red::BaseTen );
			return result ? value : c_invalidTrailingDigit;
		}

		return c_invalidTrailingDigit;
	}
}

const AnsiChar* CName::AsChar() const
{
	return ::red::Debug_GetNameString( m_hash ).Data();
}

red::StringView CName::AsStringView() const
{
	return ::red::Debug_GetNameString( m_hash );
}

const AnsiChar* CName::ToDebugString() const
{
#if defined( RED_CONFIGURATION_FINAL )
	return "";
#else
	return ::red::Debug_GetNameString( m_hash ).Data();
#endif
}

red::StringView CName::GetRootName() const
{
	red::StringView view = AsStringView();
	const Uint32 digitOffset = red::FindDigitOffset( view );
	return digitOffset != 0 ? view.SubView( 0, digitOffset ) : view;
}

Int32 CName::GetTrailingNumber() const
{
	red::StringView view = AsStringView();
	const Uint32 digitOffset = red::FindDigitOffset( view );
	return ExtractDigit( digitOffset, view );
}

void CName::Set( const CName name )
{
	m_hash = name.GetHash();
}

void CName::Set( red::CNameHash hash )
{
	m_hash = hash;
}

void NameBuilder::Register( const red::CNameHash hash, const AnsiChar* name )
{
	// todo: remove debug string registration from final build
	red::Debug_RegisterNameString( hash, { name } );
}

void NameBuilder::Register( const red::CNameHash hash, const AnsiChar* name, const Uint32 length )
{
	// todo: remove debug string registration from final build
	red::Debug_RegisterNameString( hash, { name, length } );
}

CName NameBuilder::Build( const AnsiChar* name )
{
	const NameBuilder builder{ name };
	Register( builder.m_hash, name );
	return CName{ builder.m_hash };
}

CName NameBuilder::Build( const AnsiChar* name, const Uint32 length )
{
	const NameBuilder builder{ name, length };
	Register( builder.m_hash, name, length );
	return CName{ builder.m_hash };
}

CName NameBuilder::Build( const red::StringView name )
{
	const NameBuilder builder{ name.Data(), name.Length() };
	Register( builder.m_hash, name.Data(), name.Length() );
	return CName{ builder.m_hash };
}

CName NameBuilder::Build( const red::String& name )
{
	const NameBuilder builder{ name.AsChar(), name.Length() };
	Register( builder.m_hash, name.AsChar(), name.Length() );
	return CName{ builder.m_hash };
}

CName NameBuilder::Build( const CName prefix, const AnsiChar* suffix )
{
	// If the prefix is CName::NONE, return calculation for just the suffix.
	if( prefix.Empty() )
	{
		return NameBuilder::Build( suffix );
	}

	red::StringView suffixString{ suffix };

	// No appending, registration, or hash calculation required with CName::NONE suffix.
	if( red::IsNameNone( suffixString ) )
	{
		return prefix;
	}

	// Whether the prefix has been registered or not, the returned
	// hash must be the result of appending.
	auto result = CName{ red::CalculateHash64( suffixString.Data(), suffixString.Length(), prefix.GetHash() ) };

	// Only register the result of appending if the prefix has been registered
	// (if it hasn't been registered there will be no string to append to and
	// the registry could become poisoned).
	const auto prefixString = prefix.AsStringView();
	if( !prefixString.Empty() )
	{ 
		red::String nameString = String_CreateExternal_OnStack( prefixString.Length() + suffixString.Length() + 1 );
		nameString += prefixString;
		nameString += suffixString;

		Register( result.GetHash(), nameString.AsChar(), nameString.Length() );
	}

	return result;
}

//////////////////////////////////////////////////////////////////////////
// type formatters
Bool red::CheckFormatString( const CName val, const char* formatToCheck )
{
	if ( formatToCheck )
	{
		// old c-style format
		const char* checkFormatName = std::strchr( formatToCheck, '%' );
		if ( checkFormatName )
		{
			return true;
		}
	}
	return false;
}

Bool red::ToBuffer( char* buffer, const Uint32 bufferLen, const CName val, Int32& written, const char* formatString )
{
	if ( buffer && bufferLen )
	{
		// using provided format string
		if ( formatString && (CheckFormatString( val, formatString ) || prv::CheckNewFormatString( val, formatString )) )
		{
			char outFormatString[ 128 ];
			prv::PrepareFormatString( formatString, red::GetFormatString( val ), outFormatString, sizeof( outFormatString ) );
			written += SNPrintFUnsafe( buffer, bufferLen, outFormatString, val.AsChar() );
			return true;
		}
	}
	written = 0;
	return false;
}
