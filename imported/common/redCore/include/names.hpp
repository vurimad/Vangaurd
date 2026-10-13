/**
 * Copyright (c) 2017-20 CD Projekt Red. All Rights Reserved.
 */

#pragma once

namespace red
{
namespace internal
{
	static constexpr const char c_nameNone[] = "None";
	static constexpr const Uint32 c_nameNoneLength = RED_ARRAY_COUNT( c_nameNone ) - 1;
}

	constexpr bool IsNameNone( const char* name, const Uint32 length )
	{
		return length == 0 || 
			( length == 4 && name[0] == 'N' && name[1] == 'o' && name[2] == 'n' && name[3] == 'e' );
	}

	constexpr bool IsNameNone( const red::StringView name )
	{
		return IsNameNone( name.Data(), name.Length() );
	}

	constexpr StringView GetNameNone()
	{
		return{ internal::c_nameNone, internal::c_nameNoneLength };
	}

}

constexpr CName::CName()
	: m_hash( red::c_invalidNameHashValue )
{}

constexpr CName::CName( std::nullptr_t )
	: m_hash( red::c_invalidNameHashValue )
{}

constexpr CName::CName( red::CNameHash nameHash )
	: m_hash( nameHash )
{}

constexpr red::CNameHash NameBuilder::CalculateHash( const AnsiChar* name )
{
	return CalculateHash( red::StringView{ name } );
}

constexpr red::CNameHash NameBuilder::CalculateHash( const AnsiChar* name, Uint32 length )
{
	return red::IsNameNone( name, length ) ? red::c_invalidNameHashValue : red::CalculateHash64WithLength( name, length );
}

constexpr red::CNameHash NameBuilder::CalculateHash( const red::StringView name )
{
	return red::IsNameNone( name.Data(), name.Length() ) ? red::c_invalidNameHashValue : red::CalculateHash64WithLength( name.Data(), name.Length() );
}

constexpr red::CNameHash NameBuilder::CalculateHash( const red::String& name )
{
	return red::IsNameNone( name.AsChar(), name.Length() ) ? red::c_invalidNameHashValue : red::CalculateHash64WithLength( name.AsChar(), name.Length() );
}

constexpr red::CNameHash NameBuilder::CalculateHash( const CName prefix, const AnsiChar* suffix )
{
	return red::IsNameNone( suffix ) 
		? prefix.GetHash() 
		: prefix.Empty() 
			? red::CalculateHash64( suffix ) 
			: red::CalculateHash64( suffix, prefix.GetHash() );
}

constexpr NameBuilder::NameBuilder( const CName name )
	: m_hash( name.GetHash() )
{}

constexpr NameBuilder::NameBuilder( const AnsiChar* name )
	: m_hash( CalculateHash( name ) )
{}

constexpr NameBuilder::NameBuilder( const AnsiChar* name, Uint32 length )
	: m_hash( CalculateHash( name, length ) )
{}

constexpr CName NameBuilder::Build( const CName name )
{
	// don't have to register debug string, because it is already registered
	return name;
}

constexpr bool operator==( const CName left, const CName right )
{
	return left.m_hash == right.m_hash;
}

constexpr bool operator!=( const CName left, const CName right )
{
	return left.m_hash != right.m_hash;
}

constexpr bool operator<( const CName left, const CName right )
{
	return left.m_hash < right.m_hash;
}

#if defined( RED_USE_CNAME_STRING_COMPARES )
constexpr bool operator==( const CName left, const char* right )
{
	return left
		? left.GetHash() == red::CalculateHash64( right )
		: red::IsNameNone( right );
}

constexpr bool operator==( const char* left, const CName right )
{
	return right == left;
}
#endif

constexpr bool operator==( const CName left, std::nullptr_t )
{
	return left.Empty();
}

constexpr bool operator==( std::nullptr_t, const CName right )
{
	return right.Empty();
}

#if defined( RED_USE_CNAME_STRING_COMPARES )
constexpr bool operator!=( const CName left, const char* right )
{
	return !( left == right );
}

constexpr bool operator!=( const char* left, const CName right )
{
	return !( left == right );
}
#endif

constexpr bool operator!=( const CName left, std::nullptr_t )
{
	return !left.Empty();
}

constexpr bool operator!=( std::nullptr_t, const CName right )
{
	return !right.Empty();
}
