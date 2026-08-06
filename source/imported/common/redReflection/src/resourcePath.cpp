/**
* Copyright (c) 2007-2017 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "resourcePath.h"
#include "resourcePathCache.h"

namespace res
{
//------------------------------------------------------------------------------

namespace prv
{

// NOTE: This function is needed even when RED_USE_RESOURCEPATH_STRINGS is not defined
// as we still need to process string based paths but we just don't store or use them internally
Uint32 SanitizeResourcePath( const red::StringView& path, char* const outputBuffer )
{
	char* outputChar = outputBuffer;

	const char* inputChar = path.begin();

	// Skip initial quote
	if ( *inputChar == '"' || *inputChar == '\'' )
	{
		++inputChar;
	}

	// Skip leading directory separators
	while ( *inputChar == '/' || *inputChar == '\\' )
	{
		++inputChar;
	}

	while ( inputChar != path.end() )
	{
		if ( *inputChar == '/' || *inputChar == '\\' )
		{
			*outputChar = '\\';
			++outputChar;

			// Skip consecutive directory separators
			do
			{
				++inputChar;
			}
			while ( *inputChar == '/' || *inputChar == '\\' );
		}
		else if ( *inputChar == '"' || *inputChar == '\'' )
		{
			break;
		}
		else
		{
			*outputChar = static_cast< char >( std::tolower( *inputChar ) );
			++outputChar;
			++inputChar;
		}
	}

	*outputChar = '\0';

	return static_cast< Uint32 >( outputChar - outputBuffer );
}

} // prv

//------------------------------------------------------------------------------

// This is really only here to support returning an empty ResourcePath
// when the return value needs to be a const res::ResourcePath&.
const ResourcePath ResourcePath::EMPTY;

#ifdef RED_USE_RESOURCEPATH_STRINGS

ResourcePath ResourcePath::Build( const red::StringView path )
{
	RED_FATAL_ASSERT( path.Length() < MAX_LENGTH, "Resource path '%hs' (length %u) too long!", path.Data(), path.Length() );
	if ( path.Empty() )
	{
		return ResourcePath();
	}

	char buffer[ ResourcePath::MAX_LENGTH ];
	Uint32 length = prv::SanitizeResourcePath( path, buffer );

	red::StringView sanitizedPath{ buffer, length };
	prv::ResourcePathCacheItem* item = prv::GetResourcePathCache().FindOrCreateItem( sanitizedPath );
	RED_FATAL_ASSERT( item != nullptr, "Should not get null item from the cache" );
	return ResourcePath( item->GetHash() );
}

String ResourcePath::ToString() const
{
	prv::ResourcePathCacheItem* item = prv::GetResourcePathCache().FindItem( m_hash );
	return item ? item->ToString() : String();
}

red::StringView ResourcePath::ToStringView() const
{
	prv::ResourcePathCacheItem* item = prv::GetResourcePathCache().FindItem( m_hash );
	return item ? item->ToStringView() : red::StringView();
}

const char* ResourcePath::ToDebugString() const
{
	prv::ResourcePathCacheItem* item = prv::GetResourcePathCache().FindItem( m_hash );
	return item ? item->ToDebugString() : "<no path>";
}

#else // RED_USE_RESOURCEPATH_STRINGS

ResourcePath ResourcePath::Build( const red::StringView path )
{
	RED_FATAL_ASSERT( path.Length() < MAX_LENGTH, "Resource path '%hs' (length %u) too long!", path.Data(), path.Length() );
	if ( path.Empty() )
	{
		return ResourcePath();
	}

	char buffer[ ResourcePath::MAX_LENGTH ];
	Uint32 length = prv::SanitizeResourcePath( path, buffer );
	red::StringView sanitisedPath{ buffer, length };

#ifdef RED_ENABLE_RESOURCEPATH_DEBUGGING
	prv::ResourcePathCacheItem* item = prv::GetResourcePathCache().FindOrCreateItem( sanitisedPath );
	RED_FATAL_ASSERT( item != nullptr, "Should not get null item from the cache" );
	return ResourcePath( item->GetHash() );
#else
	Uint64 hash = prv::ComputeResourcePathHash_Sanitised( sanitisedPath );
	return ResourcePath( hash );
#endif
}

String ResourcePath::ToString() const
{
	return String();
}

red::StringView ResourcePath::ToStringView() const
{
	return red::StringView();
}

const char* ResourcePath::ToDebugString() const
{
#ifdef RED_ENABLE_RESOURCEPATH_DEBUGGING
	const auto* pathItem = prv::GetResourcePathCache().FindItem( m_hash );
	if ( pathItem )
	{
		return pathItem->ToDebugString();
	}
#endif
	return "";
}

#endif // RED_USE_RESOURCEPATH_STRINGS

//------------------------------------------------------------------------------

#ifdef RED_USE_RESOURCEPATH_STRINGS

bool SortByDirectory( const ResourcePath& left, const ResourcePath& right )
{
	const auto leftParent = red::paths::GetParentPath( left.ToStringView() );
	const auto rightParent = red::paths::GetParentPath( right.ToStringView() );
	if ( leftParent == rightParent )
	{
		const auto leftName = red::paths::GetFileName( left.ToStringView() );
		const auto rightName = red::paths::GetFileName( right.ToStringView() );
		return leftName < rightName;
	}
	return leftParent < rightParent;
}

#endif // RED_USE_RESOURCEPATH_STRINGS

//------------------------------------------------------------------------------

#if !defined( RED_CONFIGURATION_FINAL ) && defined( RED_USE_RESOURCEPATH_STRINGS )

bool ContainsCommonPath( red::ArraySpan<const res::ResourcePath> left, red::ArraySpan<const res::ResourcePath> right )
{
	// Computes a set intersection between the two array spans
	// but since we don't need the result we can just exit if any values are equal
	auto leftIter = left.begin(), leftEnd = left.end(), rightIter = right.begin(), rightEnd = right.end();
	while ( leftIter != leftEnd && rightIter != rightEnd )
	{
		if ( *leftIter < *rightIter )
		{
			++leftIter;
		}
		else if ( *rightIter < *leftIter )
		{
			++rightIter;
		}
		else
		{
			return true;
		}
	}
	return false;
}

String SanitizeStringAsResourcePath( const red::StringView& path )
{
	String result{ path.Length() };
	if ( !path.Empty() )
	{
		Uint32 newLength = prv::SanitizeResourcePath( path, result.AsChar() );
		result.Resize( newLength );
	}
	return result;
}

const char* GetErrorText( InvalidReason reason )
{
	switch ( reason )
	{
	case Error_TooLong:
		return "santitised string too long";
	case Error_InvalidCharacter:
		return "invalid character found";
	case Warning_UppercaseCharacter:
		return "upper case character found (will be lower cased)";
	case Warning_QuoteCharacter:
		return "invalid quote character found";
	case Warning_BadPathSeparator:
		return "bad path separator found (will be converted)";
	case Warning_MultiplePathSeparators:
		return "consecutive path separators found (will be merged)";
	case Warning_InitialPathSeparator:
		return "path separator(s) at start of string (will be ignored)";
	case Warning_BadDirectoryName:
		return "bad directory name";
	case Warning_InvalidQuotedPath:
		return "path contains characters after closing quote charater (will be ignored)";

	default:
		RED_FATAL( "Invalid ResourcePath String Invalid Reason" );
		return "unknown or invalid reason";
	};
}

static void CheckPathSeparators( const char*& inputChar, Uint32& reasons )
{
	Uint32 count = 0;
	do
	{
		if ( *inputChar == '/' )
		{
			reasons |= InvalidReason::Warning_BadPathSeparator;
		}
		++inputChar;
		++count;
	}
	while ( *inputChar == '/' || *inputChar == '\\' );

	if ( count > 1 )
	{
		reasons |= InvalidReason::Warning_MultiplePathSeparators;
	}
}

bool IsValidResourcePathString( const red::StringView& path, Uint32& reasons )
{
	reasons = 0;

	// NOTE: Right now this check is here which uses the input length
	// TODO: Move this to the end where we can use sanitisedLength to check
	if ( path.Length() >= ResourcePath::MAX_LENGTH )
	{
		reasons |= InvalidReason::Error_TooLong;
	}

	bool hasStartingQuote = false;
	bool hasEndingQuote = false;

	const char* inputChar = path.begin();
	if ( *inputChar == '"' || *inputChar == '\'' )
	{
		reasons |= InvalidReason::Warning_QuoteCharacter;
		hasStartingQuote = true;
		++inputChar;
	}

	if ( *inputChar == '/' || *inputChar == '\\' )
	{
		reasons |= InvalidReason::Warning_InitialPathSeparator;
		
		CheckPathSeparators( inputChar, reasons );
	}

	Uint32 sanitisedLength = 0;
	bool hasDot = false;
	while ( inputChar != path.end() )
	{
		if ( hasStartingQuote && hasEndingQuote )
		{
			reasons |= InvalidReason::Warning_InvalidQuotedPath;
			break;
		}

		++sanitisedLength;
		if ( *inputChar == '/' || *inputChar == '\\' )
		{
			if ( hasDot )
			{
				reasons |= InvalidReason::Warning_BadDirectoryName;
			}
			CheckPathSeparators( inputChar, reasons );
		}
		else
		{
			if ( ('a' <= *inputChar && *inputChar <= 'z') || ('0' <= *inputChar && *inputChar <= '9') || *inputChar == '_' || *inputChar == '-' )
			{
				// Valid characters
			}
			else if ( *inputChar == '.' )
			{
				hasDot = true;
			}
			else if ( *inputChar == '"' || *inputChar == '\'' )
			{
				reasons |= InvalidReason::Warning_QuoteCharacter;
				hasEndingQuote = true;
			}
			else if ( 'A' <= *inputChar && *inputChar <= 'Z' )
			{
				reasons |= InvalidReason::Warning_UppercaseCharacter;
			}
			else
			{
				reasons |= InvalidReason::Error_InvalidCharacter;
			}
			++inputChar;
		}
	}

	return reasons == 0;
}

#endif // !defined( RED_CONFIGURATION_FINAL ) && defined( RED_USE_RESOURCEPATH_STRINGS )

//------------------------------------------------------------------------------

namespace prv
{

bool Debug_CopyResourcePathToString( const res::ResourcePath path, char* buffer, size_t size )
{
#if defined( RED_USE_RESOURCEPATH_STRINGS )
	auto pathView = path.ToStringView();
	if ( pathView.Empty() )
	{
		pathView = "<No path string found>";
	}
	return red::Strcpy( buffer, pathView.Data(), size, pathView.Length() );
#elif defined( RED_ENABLE_RESOURCEPATH_DEBUGGING )
	red::StringView pathView = "<No path string found>";
	const auto* pathItem = prv::GetResourcePathCache().FindItem( path.GetHash() );
	if ( pathItem != nullptr && !pathItem->ToStringView().Empty() )
	{
		pathView = pathItem->ToStringView();
	}
	return red::Strcpy( buffer, pathView.Data(), size, pathView.Length() );
#else
	// Do nothing here since we already print the hex code of the path
	buffer[0] = 0;
	return true;
#endif
}

red::String Debug_GetResourcePathDebugText( const res::ResourcePath path )
{
#if defined( RED_USE_RESOURCEPATH_STRINGS )
	return path.ToString();
#elif defined( RED_ENABLE_RESOURCEPATH_DEBUGGING )
	const auto* pathItem = prv::GetResourcePathCache().FindItem( path.GetHash() );
	if ( pathItem != nullptr )
	{
		return pathItem->ToString();
	}
	return String::Printf( "0x%016llX", path.GetHash() );
#else
	return String::Printf( "0x%016llX", path.GetHash() );
#endif
}

} // prv


} // res

