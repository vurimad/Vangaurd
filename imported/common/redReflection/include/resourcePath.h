/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

#include "../../../common/redContainers/include/string/stringView.h"

namespace res
{
//------------------------------------------------------------------------------

// Path to a resource in the engine resource system
// All paths are normalised to be lower case, using \ as a path separator
// A hash is then computed from the normalised path and this is used to identify the resource
// The actual strings are stored in a shared cache. See ResourcePathCache
// In the final build there will be no string stored for the path
class ResourcePath
{
public:
	// We are setting a shorter max resource path here because it needs to be appended to
	// the depot paths and on windows MAX_PATH is 260 characters.
	static constexpr const Uint32 MAX_LENGTH = 200;

	// Build resource path from a string, will conform the string to the path standards
	RED_REFLECTION_API static ResourcePath Build( const red::StringView stringPath );

	// Build a resource path from a hash value
	static constexpr ResourcePath Build( Uint64 hash );

	constexpr ResourcePath();
	constexpr ResourcePath( const ResourcePath& other );
	constexpr ResourcePath& operator=( const ResourcePath& other );

	RED_REFLECTION_API String ToString() const;
	RED_REFLECTION_API red::StringView ToStringView() const;

	bool IsValid() const;
	bool IsEmpty() const;

	Uint64 GetHash() const;

	friend bool operator==( const ResourcePath& left, const ResourcePath& right );
	friend bool operator!=( const ResourcePath& left, const ResourcePath& right );
	friend bool operator<( const ResourcePath& left, const ResourcePath& right );

	// Somewhat deprecated, use the default constructor to create an empty ResourcePath
	// This is a helper for functions which return a const res::ResourcePath&
	RED_REFLECTION_API static const ResourcePath EMPTY;

	// Returns the path string in a way that is only safe to use in debugging actions
	// For example in asserts, log messages, etc
	RED_REFLECTION_API const char* ToDebugString() const;

private:
	explicit constexpr ResourcePath( Uint64 hash );

	// Path's hash, copied here for faster access
	Uint64 m_hash;
};

static_assert( sizeof( ResourcePath ) == 8, "ResourcePath size changed unexpectedly" );

//------------------------------------------------------------------------------

//
// Inline functions
//

constexpr ResourcePath ResourcePath::Build( Uint64 hash )
{
	return ResourcePath( hash );
}

constexpr ResourcePath::ResourcePath( Uint64 hash )
	: m_hash( hash )
{}

constexpr ResourcePath::ResourcePath()
	: ResourcePath( 0ull )
{}

constexpr ResourcePath::ResourcePath( const ResourcePath& other )
	: ResourcePath( other.m_hash )
{}

constexpr ResourcePath& ResourcePath::operator=( const ResourcePath& other )
{
	m_hash = other.m_hash;
	return *this;
}

RED_INLINE bool ResourcePath::IsEmpty() const
{
	return m_hash == 0ull;
}

RED_INLINE bool ResourcePath::IsValid() const
{
	return m_hash != 0ull;
}

RED_INLINE Uint64 ResourcePath::GetHash() const
{
	return m_hash;
}

RED_INLINE bool operator==( const ResourcePath& left, const ResourcePath& right )
{
	return left.m_hash == right.m_hash;
}

RED_INLINE bool operator!=( const ResourcePath& left, const ResourcePath& right )
{
	return left.m_hash != right.m_hash;
}

RED_INLINE bool operator<( const ResourcePath& left, const ResourcePath& right )
{
	return left.m_hash < right.m_hash;
}

//------------------------------------------------------------------------------

//
// Sorting Helper Functions
//

// Used to sort resource paths by the path hash, this is the default sorting used by operator<
// and is included here for completeness
RED_INLINE bool SortByPathHash( const ResourcePath& left, const ResourcePath& right )
{
	return left < right;
}

#ifdef RED_USE_RESOURCEPATH_STRINGS

// Used to sort resource paths by the directory in which they are in.
// This should be slightly more disk friendly than sorting by hash (the default) or by the whole path string
// since all files in a directory will be adjacent to each other
RED_REFLECTION_API bool SortByDirectory( const ResourcePath& left, const ResourcePath& right );

// Used to sort resource paths by the full path string for presentation purposes
RED_INLINE bool SortByPathString( const ResourcePath& left, const ResourcePath& right )
{
	return left.ToStringView() < right.ToStringView();
}

#endif // RED_USE_RESOURCEPATH_STRINGS

//------------------------------------------------------------------------------

#if !defined( RED_CONFIGURATION_FINAL ) && defined( RED_USE_RESOURCEPATH_STRINGS )

//
// Editor & Validation Helper functions
//

// Check if two sorted arrays contain at least one common resource path
RED_REFLECTION_API bool ContainsCommonPath( red::ArraySpan<const ResourcePath> left, red::ArraySpan<const ResourcePath> right );

RED_REFLECTION_API String SanitizeStringAsResourcePath( const red::StringView& path );

// Reasons a ResourcePath string would be invalid
// Based on https://confluence.cdprojektred.com/display/CPT/Resource+Paths
// Errors actually make the string invalid
// Warnings will be corrected during sanitisation
// All make the string an invalid ResourcePath
enum InvalidReason : Uint32
{
	Error_TooLong = RED_FLAG(0),						// Path string exceeds MAX_LENGTH characters
	Error_InvalidCharacter = RED_FLAG(1),				// Invalid character found
	Warning_UppercaseCharacter = RED_FLAG(8),			// Upper case character found
	Warning_BadPathSeparator = RED_FLAG(9),				// Character '/' used as a path separator
	Warning_QuoteCharacter = RED_FLAG(10),				// Contains '"' or '\'' character
	Warning_MultiplePathSeparators = RED_FLAG(11),		// Multiple \ or / characters in sequence
	Warning_InitialPathSeparator = RED_FLAG(12),		// Starts with \ or / characters
	Warning_BadDirectoryName = RED_FLAG(13),			// Directory contains . or invalid character
	Warning_InvalidQuotedPath = RED_FLAG(14),			// Path contains " or ' and contains strings after the ending quote
};

constexpr bool InvalidReasonIsError( InvalidReason reason )
{
	return reason == Error_TooLong || reason == Error_InvalidCharacter;
}
	
constexpr bool InvalidReasonIsWarning( InvalidReason reason )
{
	return reason == Warning_UppercaseCharacter || reason == Warning_BadDirectoryName ||
		reason == Warning_BadPathSeparator || reason == Warning_MultiplePathSeparators || reason == Warning_InitialPathSeparator ||
		reason == Warning_InvalidQuotedPath;
}

RED_REFLECTION_API const char* GetErrorText( InvalidReason reason );

RED_REFLECTION_API bool IsValidResourcePathString( const red::StringView& path, Uint32& reasons );

#endif // !defined( RED_CONFIGURATION_FINAL ) && defined( RED_USE_RESOURCEPATH_STRINGS )

} // namespace res

//------------------------------------------------------------------------------

namespace red
{

// Implement this function so we don't need a CalcHash() method on the ResourcePath class. 
// There was confusion and CalcHash was used when it should not have been.
template <>
RED_INLINE red::THash32 GetHash< res::ResourcePath >( const res::ResourcePath& path )
{
	return static_cast< red::THash32 >( path.GetHash() );
}

} // red

//------------------------------------------------------------------------------

namespace res
{
namespace prv
{

// Constexpr function to compute the ResourcePath hash given an already sanitised string
// Note: Used mainly in testing and in internal functions which know the input
constexpr Uint64 ComputeResourcePathHash_Sanitised( const red::StringView& sanitisedPath )
{
	return red::CalculateHash64( sanitisedPath.Data(), sanitisedPath.Length(), RED_FNV_OFFSET_BASIS64 );
}

// Constexpr function to compute the ResourcePath hash given almost any string
// Note: This is designed to be used with the macro for creating a constexpr ResourcePath
template < size_t length >
constexpr Uint64 ComputeResourcePathHash_Const( const char (&stringPath)[length] )
{
	Uint64 hash = RED_FNV_OFFSET_BASIS64;
	const char* path = stringPath;

	// Note: Length includes null terminating character which we do not want to include
	while ( *path )
	{
		char chr = *path;
		if ( chr == '/' )
		{
			chr = '\\';
		}
		else if ( chr <= 'Z' && chr >= 'A' )
		{
			chr += 'a' - 'A';
		}

		hash ^= chr;
		hash *= RED_FNV_PRIME64;

		++path;
		if ( chr == '\\' )
		{
			while ( *path == '/' || *path == '\\' )
			{
				++path;
			}
		}
	}

	return hash;
}

} // prv
} // res

#ifdef RED_USE_RESOURCEPATH_STRINGS

#define RED_CONST_RESOURCEPATH( str ) res::ResourcePath::Build( str )

#else

#define RED_CONST_RESOURCEPATH( str ) res::ResourcePath::Build( res::prv::ComputeResourcePathHash_Const( str ) )

#endif

//------------------------------------------------------------------------------

namespace res
{
namespace prv
{
// Helper function to copy a string based represnetation of a resource path to a buffer
// When resource path strings are supported it will copy the string based path
// When no strings are present it will copy the hash as a hex string
RED_REFLECTION_API bool Debug_CopyResourcePathToString( const res::ResourcePath path, char* buffer, size_t size );

// Helper function to get a string useful for debugging for a given resource path
RED_REFLECTION_API red::String Debug_GetResourcePathDebugText( const res::ResourcePath path );
} // prv
} // res


namespace red
{
namespace err
{
struct ResourcePathStorage
{
#if defined( RED_USE_RESOURCEPATH_STRINGS ) || defined( RED_ENABLE_RESOURCEPATH_DEBUGGING )
	static constexpr size_t c_pathSize = res::ResourcePath::MAX_LENGTH;
#else
	static constexpr size_t c_pathSize = sizeof(Uint64) * 2;
#endif
	char m_path[ c_pathSize ];
	Uint64 m_hash;
};
} // err

template< Uint32 Length >
struct err::CrashDataTypeAdapter<res::ResourcePath, Length>
{
	//#tbd: longest allowed path
	static_assert(Length == 0, "Length should already be set for longest resource path");
	using StorageType = ResourcePathStorage;
	using SetType = res::ResourcePath;

	// Extract the name now, since could lose its refcount, get destructed atexit
	// or some other corruption occurs
	static CrashDataCopyResult Copy(StorageType& mem, const SetType& value)
	{
		mem.m_hash = value.GetHash();
		if ( !res::prv::Debug_CopyResourcePathToString( value, mem.m_path, ResourcePathStorage::c_pathSize ) )
		{
			return CrashDataCopyResult::Error;
		}

		return CrashDataCopyResult::Success;
	}

	static Bool Print(char* buffer, const Uint32 bufferLen, const StorageType& val)
	{
		static_assert(sizeof(val.m_hash) == sizeof(Uint64), "");
		const Int32 ret = red::SNPrintFUnsafe(buffer, bufferLen, "%s<0x%016llX>", val.m_path, val.m_hash);
		return ret > -1;
	}
};

} // red

//------------------------------------------------------------------------------
