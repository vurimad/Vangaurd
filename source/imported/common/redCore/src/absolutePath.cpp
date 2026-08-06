/**
 * Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
 */
#include "build.h"

#include "absolutePath.h"
#include "../../redContainers/include/ustring/utf8String.h"
#include "../../redContainers/include/ustring/utf16String.h"
#include "../../redContainers/include/string/stringUtils.h"

#if defined( RED_PLATFORM_LINUX )
#	include <limits.h>
#endif

namespace red
{

//------------------------------------------------------------------------------

namespace
{

	static constexpr Uint32 c_convertBufferSize = 1024;

	//
	// Common functions applicable to all platforms
	//

	void ResolveRelativePathComponents( String& str, char separator, Uint32 startOffset = 0 )
	{
		Uint32 prevIndex = startOffset;
		Uint32 index = 0;

		if ( !str.IndexOf( separator, index, startOffset ) )
		{
			return;
		}

		while ( index < str.Length() )
		{
			if ( index + 2 < str.Length() &&
				str[ index + 1 ] == '.' && str[ index + 2 ] == '.' )
			{
				// Resolve .. relative directory, /x/../ -> /
				str.Erase( prevIndex, index + 3 );
				index = prevIndex;

				if ( prevIndex > startOffset )
				{
					if ( !str.IndexOfLast( separator, prevIndex, prevIndex - 1 ) || prevIndex < startOffset )
					{
						prevIndex = index;
					}
				}
			}
			else if ( index + 1 < str.Length() &&
				str[ index + 1 ] == '.' )
			{
				// Resolve . current directory, /./ -> /
				str.Erase( index, index + 2 );
			}
			else if ( index + 1 < str.Length() &&
				str[ index + 1 ] == separator )
			{
				// Resolve // extra path separator, // -> /
				str.Erase( index, index + 1 );
			}
			else
			{
				prevIndex = index;
				if ( !str.IndexOf( separator, index, index + 1 ) )
				{
					break;
				}
			}
		}
	}

	String ConvertUtf16ToString( const Utf16String& str )
	{
		char buffer[ c_convertBufferSize ];
#if defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_DURANGO )
		RED_VERIFY( red::FileSystemStringToEngineString( str.AsChar(), buffer, c_convertBufferSize ) );
#else
		red::WideCharToStdChar( buffer, str.AsChar(), c_convertBufferSize );
#endif
		return String( buffer );
	}


	bool IsSeparator( const char code )
	{
		return code == '/' || code == '\\';
	}

	//
	// Windows functions needed for all platforms
	//

	bool HasWindowsDriveSpecifier( StringView str )
	{
		if ( str.Length() > 2 )
		{
			if ( str[1] == ':' )
			{
				const char c = str[0];
				// Valid drive specifiers are limited to A-Z/a-z
				if ( ( 'a' <= c && c <= 'z' ) || ( 'A' <= c && c <= 'Z' ) )
				{
					return IsSeparator( str[2] );
				}
			}
		}

		return false;
	}


	//
	// Platform specific functions
	//

#if defined( RED_PLATFORM_ORBIS )

	bool IsRootPath( const StringView str )
	{
		return str == "/";
	}

	bool IsHostPath( const StringView str )
	{
		return str.StartsWith( "/host/" );
	}

	bool IsHostAppPath( const StringView str )
	{
		return str.StartsWith( "/hostapp/" );
	}

	bool IsHostedWindowsPath( const StringView str )
	{
		return IsHostPath( str ) || IsHostAppPath( str );
	}

	bool IsAbsolute( const StringView str )
	{
		return HasWindowsDriveSpecifier( str ) || IsHostedWindowsPath( str ) || str.StartsWith( '/' );
	}

	void ConformToAbsolutePath( String& str )
	{
		// Make this work transparently. Even the PS4 platform tools do this.
		// E.g., specify path=Z:\somepath and it's the same as /host/Z:\somepath
		if ( HasWindowsDriveSpecifier( str ) )
		{
			str = String( "/host/" ) + str;
		}

		str.ReplaceAll( '\\', '/' );

		RED_ASSERT( str.Front() == '/', "First character in absolute Orbis path needs to be /" );

		if ( IsHostAppPath( str ) )
		{
			const Uint32 windowsPathOffset = static_cast<Uint32>( std::strlen( "/hostapp/" ) );
			if ( str.Length() == windowsPathOffset )
			{
				return;
			}

			ResolveRelativePathComponents( str, '/', windowsPathOffset ); // only resolve after '/hostapp/'
			// Make path after '/hostapp/' be a Windows path
			str.Replace( '/', '\\', windowsPathOffset );

			RED_ASSERT( !HasWindowsDriveSpecifier( StringView( str ).SubView( windowsPathOffset ) ),
				"The path specified with '/hostapp/' cannot be an absolute windows path" );
		}
		else if ( IsHostPath( str ) )
		{
			const Uint32 windowsPathOffset = static_cast<Uint32>( std::strlen( "/host/" ) );
			if ( str.Length() == windowsPathOffset )
			{
				return;
			}

			ResolveRelativePathComponents( str, '/', windowsPathOffset ); // only resolve after '/host/'
			str.Replace( '/', '\\', windowsPathOffset );

			const auto windowsPath = StringView( str ).SubView( windowsPathOffset );
			// The path can start with something like %appdata%
			if ( !windowsPath.StartsWith( "%" ) )
			{
				// Check path specified after '/host/' is an absolute Windows path
				RED_ASSERT( HasWindowsDriveSpecifier( windowsPath ),
					"The path specified with the '/host/' needs to be an absolute windows path" );
			}
		}
		else
		{
			ResolveRelativePathComponents( str, '/' );

			RED_FATAL_ASSERT( str.CountChars( '/' ) < 9, "Orbis only supports a maximum of 8 directories" );
		}
	}

	void ConformToDirectoryPath( String& str )
	{
		if ( str.Back() != '/' && str.Back() != '\\' )
		{
			if ( str.ContainsFromRight( '\\' ) )
			{
				str.Append( '\\' );
			}
			else
			{
				str.Append( '/' );
			}
		}
	}

	void ConformToFilePath( String& str )
	{
		RED_ASSERT( !IsSeparator( str.Back() ), "File path cannot end with path separator" );
	}

	void AppendPath( String& absoluteBasePath, StringView relativePath )
	{
		if( relativePath.Empty() )
			return;

		const Uint32 replaceStartIndex = absoluteBasePath.Length();
		absoluteBasePath.Append( relativePath.Data(), relativePath.Length() );

		const bool hasWindowsPath = IsHostedWindowsPath( absoluteBasePath );
		if ( hasWindowsPath )
		{
			absoluteBasePath.Replace( '/', '\\', replaceStartIndex );
		}
		else
		{
			absoluteBasePath.Replace( '\\', '/', replaceStartIndex );
		}

		if ( hasWindowsPath )
		{
			Uint32 windowsPathOffset;
			if ( !absoluteBasePath.IndexOf( '/', windowsPathOffset, 1 ) )
			{
				RED_FATAL( "Malformed path detected, not '/' found in Orbis absolute host path" );
			}

			ResolveRelativePathComponents( absoluteBasePath, '\\', windowsPathOffset + 1 );
		}
		else
		{
			ResolveRelativePathComponents( absoluteBasePath, '/' );
		}
	}

#elif defined ( RED_PLATFORM_LINUX )

	bool IsRootPath( const StringView str )
	{
		return str == "/";
	}

	bool IsAbsolute( const StringView str )
	{
		return str.StartsWith( '/' );
	}

	void ConformToAbsolutePath( String& str )
	{
		RED_ASSERT( str.Front() == '/', "First character in absolute Linux path needs to be /" );
		str.ReplaceAll( '\\', '/' );
		ResolveRelativePathComponents( str, '/' );
	}

	void ConformToDirectoryPath( String& str )
	{
		str.ReplaceAll( '\\', '/' );
		if ( str.Back() != '/' )
		{
			str.Append( '/' );
		}
	}

	void ConformToFilePath( String& str )
	{
		RED_ASSERT( !IsSeparator( str.Back() ), "File path cannot end with path separator" );
	}


	void AppendPath( String& absoluteBasePath, const StringView relativePath )
	{
		if ( relativePath.Empty() )
			return;

		if ( IsSeparator( relativePath.Front() ) && !( relativePath.Length() == 1 ) )
		{
			// If you see this warning then you are trying to append something that looks
			// like an absolute path (i.e. starts with a slash, / or \ and is not just a slash)
			// to an AbsolutePath. This may not do exactly what you want.
			RED_LOG_WARNING( "Appending what appears to be an absolute path to AbsolutePath. Appending '%hs' to '%hs'.", relativePath.ToString().AsChar(), absoluteBasePath.AsChar() );
		}



		///////////////////////////

		if ( relativePath.Empty() )
			return;

		if ( relativePath.Front() == '/' && !( relativePath.Length() == 1 ) )
		{
			// If you see this warning then you are trying to append something that looks
			// like an absolute path (i.e. starts with a slash, / and is not just a slash)
			// to an AbsolutePath. This may not do exactly what you want.
			RED_LOG_WARNING( "Appending what appears to be an absolute path to AbsolutePath. Appending '%hs' to '%hs'.", relativePath.ToString().AsChar(), absoluteBasePath.AsChar() );
		}

		const Uint32 replaceStartIndex = absoluteBasePath.Length();
		absoluteBasePath.Append( relativePath.Data(), relativePath.Length() );
		absoluteBasePath.Replace( '\\', '/', replaceStartIndex );

		ResolveRelativePathComponents( absoluteBasePath, '/' );
	}

#elif defined( RED_PLATFORM_DURANGO ) || defined( RED_PLATFORM_WINPC )

	static constexpr const Uint32 UncSpecifierLength = 2; // Length of "\\\\"
	static constexpr const Uint32 DriveSpecifierLength = 3; // Length of "A:\\"

	bool HasUncSpecifier( const StringView str )
	{
		return str.StartsWith( "\\\\" );
	}

	bool IsRootPath( const StringView str )
	{
		if ( HasWindowsDriveSpecifier( str ) )
		{
			return str.Length() == DriveSpecifierLength;
		}
		if ( HasUncSpecifier( str ) )
		{
			return str.Length() == UncSpecifierLength;
		}
		return false;
	}

	bool IsAbsolute( const StringView str )
	{
		return str.Length() >= 3 && ( HasWindowsDriveSpecifier( str ) || HasUncSpecifier( str ) );
	}

	void ConformToAbsolutePath( String& str )
	{
		str.ReplaceAll( '/', '\\' );
		if ( HasWindowsDriveSpecifier( str ) )
		{
			ResolveRelativePathComponents( str, '\\', DriveSpecifierLength - 1 );
			red::StrToUpper( &str[0], 1 );
		}
		else if ( HasUncSpecifier( str ) )
		{
			ResolveRelativePathComponents( str, '\\', UncSpecifierLength - 1 );
		}
		else
		{
			RED_FATAL( "Absolute path needs a drive or UNC specifier. '%hs' has neither.", str.AsChar() );
		}
	}

	void ConformToDirectoryPath( String& str )
	{
		if ( str.Back() != '\\' && str.Back() != '/' )
		{
			str.Append( '\\' );
		}
	}

	void ConformToFilePath( String& str )
	{
		RED_ASSERT( str.Back() != '\\', "File path cannot end with path separator" );
	}

	void AppendPath( String& absoluteBasePath, const StringView relativePath )
	{
		if( relativePath.Empty() )
			return;

		if( IsSeparator( relativePath.Front() ) && !( relativePath.Length() == 1 ) )
		{
			// If you see this warning then you are trying to append something that looks
			// like an absolute path (i.e. starts with a slash, / or \ and is not just a slash)
			// to an AbsolutePath. This may not do exactly what you want.
			RED_LOG_WARNING( "Appending what appears to be an absolute path to AbsolutePath. Appending '%hs' to '%hs'.", relativePath.ToString().AsChar(), absoluteBasePath.AsChar() );
		}
		const Uint32 replaceStartIndex = absoluteBasePath.Length();
		absoluteBasePath.Append( relativePath.Data(), relativePath.Length() );

		absoluteBasePath.Replace( '/', '\\', replaceStartIndex );

		if( HasWindowsDriveSpecifier( absoluteBasePath ) )
		{
			ResolveRelativePathComponents( absoluteBasePath, '\\', DriveSpecifierLength - 1 );
		}
		else if( HasUncSpecifier( absoluteBasePath ) )
		{
			ResolveRelativePathComponents( absoluteBasePath, '\\', UncSpecifierLength - 1 );
		}
	}

#else
#error Unsupported platform!
#endif // PLATFORMS
}

//------------------------------------------------------------------------------

namespace
{
static constexpr Uint32 DEFAULT_STRING_SIZE = 256;
}

AbsolutePath AbsolutePath::CreateDirPath( const StringView pathname )
{
	String path{ DEFAULT_STRING_SIZE };
	path.Append( pathname.Data(), pathname.Length() );
	ConformToDirectoryPath( path );
	ConformToAbsolutePath( path );
	return AbsolutePath( std::move( path ), ConformedToken() );
}

AbsolutePath AbsolutePath::CreateDirPath( const Utf16String& pathname )
{
	String path( ConvertUtf16ToString( pathname ) );
	ConformToDirectoryPath( path );
	ConformToAbsolutePath( path );
	return AbsolutePath( std::move( path ), ConformedToken() );
}

AbsolutePath AbsolutePath::CreateFilePath( const StringView pathname )
{
	String path{ DEFAULT_STRING_SIZE };
	path.Append( pathname.Data(), pathname.Length() );
	ConformToAbsolutePath( path );
	ConformToFilePath( path );
	return AbsolutePath( std::move( path ), ConformedToken() );
}

AbsolutePath AbsolutePath::CreateFilePath( const Utf16String& pathname )
{
	String path( ConvertUtf16ToString( pathname ) );
	ConformToAbsolutePath( path );
	ConformToFilePath( path );
	return AbsolutePath( std::move( path ), ConformedToken() );
}

AbsolutePath AbsolutePath::ParseDirPath( const StringView str )
{
	RED_FATAL_ASSERT( !str.StartsWith( "\"" ) && !str.StartsWith( "'" ), "Path %hs is quoted, will incorrectly parse as a relative path", str.Data() );

	if ( !IsAbsolute( str ) )
	{
		return paths::GetCurrentWorkingDirectory().AppendDirPath( str );
	}
	return AbsolutePath::CreateDirPath( str );
}

AbsolutePath AbsolutePath::ParseFilePath( const StringView str )
{
	RED_FATAL_ASSERT( !str.StartsWith( "\"" ) && !str.StartsWith( "'" ), "Path %hs is quoted, will incorrectly parse as a relative path", str.Data() );

	if ( !IsAbsolute( str ) )
	{
		return paths::GetCurrentWorkingDirectory().AppendFilePath( str );
	}
	return AbsolutePath::CreateFilePath( str );
}

Bool AbsolutePath::IsValidPath( const StringView pathname )
{
	return IsAbsolute( pathname );
}


Bool AbsolutePath::IsValidPath( const Utf16String& pathname )
{
	return IsAbsolute( ConvertUtf16ToString( pathname ) );
}

AbsolutePath::AbsolutePath()
{
}

AbsolutePath::AbsolutePath( String&& conformedPath, AbsolutePath::ConformedToken )
	: m_path( std::move( conformedPath ) )
{
}

AbsolutePath::AbsolutePath( const AbsolutePath& other )
	: m_path( other.m_path )
{
}

AbsolutePath::AbsolutePath( AbsolutePath&& other )
	: m_path( std::move( other.m_path ) )
{
}

AbsolutePath::~AbsolutePath()
{
}

AbsolutePath& AbsolutePath::operator=( const AbsolutePath& other )
{
	if ( this != &other )
	{
		m_path = other.m_path;
	}

	return *this;
}

AbsolutePath& AbsolutePath::operator=( AbsolutePath&& other )
{
	if ( this != &other )
	{
		m_path = std::move( other.m_path );
	}

	return *this;
}

bool AbsolutePath::IsDirectoryPath() const
{
	return IsSeparator( m_path.Back() );
}

bool AbsolutePath::IsFilePath() const
{
	return !Empty() && !IsSeparator( m_path.Back() );
}

bool AbsolutePath::IsOnlyRootPath() const
{
	return IsRootPath( m_path );
}

bool operator==( const AbsolutePath& left, const AbsolutePath& right )
{
	return left.m_path.EqualsNC( right.m_path );
}

bool operator!=( const AbsolutePath& left, const AbsolutePath& right )
{
	return ! left.m_path.EqualsNC( right.m_path );
}

bool operator<( const AbsolutePath& left, const AbsolutePath& right )
{
	return left.m_path < right.m_path;
}

Utf8String AbsolutePath::ToUtf8String() const
{
	return Utf8String( m_path.AsChar(), m_path.Length() );
}

Utf16String AbsolutePath::ToUtf16String() const
{
#if defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_DURANGO )
	UniChar buffer[ c_convertBufferSize ];
	RED_VERIFY( red::EngineStringToFileSystemString( m_path.AsChar(), buffer, c_convertBufferSize ) );
	return Utf16String( buffer );
#else
	return Utf16String( m_path.AsChar(), m_path.Length() );
#endif
}

AbsolutePath& AbsolutePath::AppendDirPath( const StringView str )
{
	RED_ASSERT( !IsFilePath(), "Cannot append a directory path onto a file path" );

	AppendPath( m_path, str );
	ConformToDirectoryPath( m_path );
	return *this;
}

AbsolutePath& AbsolutePath::AppendFilePath( const StringView str )
{
	AppendPath( m_path, str );
	ConformToFilePath( m_path );
	return *this;
}

AbsolutePath AbsolutePath::AddDirPath( const StringView str ) const
{
	AbsolutePath result( *this );
	result.AppendDirPath( str );
	return result;
}

AbsolutePath AbsolutePath::AddFilePath( const StringView str ) const
{
	AbsolutePath result;
	result.m_path.Reserve( m_path.Length() + str.Length() );
	result = *this;
	result.AppendFilePath( str );
	return result;
}

AbsolutePath& AbsolutePath::AppendDirPath( const Utf16String& str )
{
	RED_ASSERT( !IsFilePath(), "Cannot append a directory path onto a file path" );
	String relPath( ConvertUtf16ToString( str ) );
	AppendPath( m_path, relPath );
	ConformToDirectoryPath( m_path );
	return *this;
}

AbsolutePath& AbsolutePath::AppendFilePath( const Utf16String& str )
{
	String relPath( ConvertUtf16ToString( str ) );
	AppendPath( m_path, relPath );
	ConformToFilePath( m_path );
	return *this;
}

AbsolutePath AbsolutePath::AddDirPath( const Utf16String& str ) const
{
	AbsolutePath result( *this );
	result.AppendDirPath( str );
	return result;
}

AbsolutePath AbsolutePath::AddFilePath( const Utf16String& str ) const
{
	AbsolutePath result( *this );
	result.AppendFilePath( str );
	return result;
}

bool SortByDirectory( const AbsolutePath& left, const AbsolutePath& right )
{
	const auto leftParent = red::paths::GetParentPath( left.AsStringView() );
	const auto rightParent = red::paths::GetParentPath( right.AsStringView() );
	if ( leftParent == rightParent )
	{
		const auto leftName = red::paths::GetFileName( left.AsStringView() );
		const auto rightName = red::paths::GetFileName( right.AsStringView() );
		return red::CompareAlphaNum( leftName, rightName ) < 0;
	}
	return red::CompareAlphaNum( leftParent, rightParent ) < 0;
}

//------------------------------------------------------------------------------

namespace paths
{

namespace
{
	Uint32 GetLastSeperatorIndex( const StringView str, Uint32 offset = StringView::npos )
	{
		if  ( offset == StringView::npos )
		{
			offset = str.Length();
		}

		// For generic string paths we support both forward and backward slashes
		return str.FindAnyOfReverse( "/\\", offset );
	}

	Uint32 GetFileNameIndex( const red::StringView str )
	{
		const Uint32 lastSeparator = GetLastSeperatorIndex( str );

		if ( lastSeparator == StringView::npos )
		{
			return StringView::npos;
		}

		return lastSeparator + 1;
	}

	Uint32 GetExtensionIndex( const red::StringView str )
	{
		Uint32 filePosition = GetFileNameIndex( str );
		if ( filePosition == StringView::npos )
		{
			// If no directory separators exist, assume the start of the string is the filename
			filePosition = 0;
		}

		const Uint32 dotPosition = str.FindReverse( '.' );
		if ( dotPosition < filePosition )
		{
			return StringView::npos;
		}

		return dotPosition;
	}

}

AbsolutePath GetCurrentWorkingDirectory()
{
#if defined( RED_PLATFORM_ORBIS )

	return AbsolutePath::CreateDirPath( "/" );

#elif defined( RED_PLATFORM_DURANGO ) || defined( RED_PLATFORM_WINPC )

	UniChar buffer[ MAX_PATH ];
	::GetCurrentDirectoryW( MAX_PATH, buffer );
	return AbsolutePath::CreateDirPath( Utf16String( buffer ) );

#elif defined( RED_PLATFORM_LINUX )
	AnsiChar buffer[ PATH_MAX ];
	getcwd( buffer, PATH_MAX );
	return AbsolutePath::CreateDirPath( StringView( buffer ) );
#else
#	error Unsupported platform!
#endif
}

AbsolutePath ParentAbsolutePath( const red::AbsolutePath& path )
{
	AbsolutePath result;
	const StringView parentPath = GetParentPath( path );
	if ( !parentPath.Empty() )
	{
		result = AbsolutePath::CreateDirPath( parentPath );
	}
	return result;
}

AbsolutePath AncestorAbsolutePath( const AbsolutePath& path, Int32 count )
{
	const auto& str = path.AsString();
	if ( str.Empty() )
	{
		return AbsolutePath();
	}

	Uint32 find_offset = str.Length() - 1;
	if ( path.IsDirectoryPath() )
	{
		if ( path.IsOnlyRootPath() )
		{
			return AbsolutePath();
		}

		// Skip the last directory separator at the end of the string
		find_offset -= 1;
	}

	Uint32 last_separator = StringView::npos;
	while ( count > 0 )
	{
		last_separator = GetLastSeperatorIndex( str, find_offset );
		if ( last_separator == StringView::npos )
		{
			RED_FATAL( "Trying to go up too many directories" );
			// Return empty path if no parent path
			return AbsolutePath();
		}

		find_offset = last_separator - 1;
		--count;
	}

	return AbsolutePath::CreateDirPath( StringView( str ).SubView( 0, last_separator + 1 ) );
}

//------------------------------------------------------------------------------

DynArray< StringView > SplitPath( const AbsolutePath& path )
{
	return SplitPath( path.AsStringView() );
}

DynArray< StringView > SplitPath( const StringView path )
{
	DynArray< StringView > result{ red::PoolEngine() };

	const Uint32 length = path.Length();
	Uint32 start = 0;
	Uint32 index = 0;

	while ( index < length )
	{
		while ( index < length && ( path[index] == '/' || path[index] == '\\' ) )
		{
			++index;
		}

		if ( index == length )
		{
			break;
		}
		start = index;

		while ( index < length && ( path[index] != '/' && path[index] != '\\' ) )
		{
			++index;
		}

		result.PushBack( path.Slice( start, index ) );
	}

	return result;
}

//------------------------------------------------------------------------------

String ExtractFileName( const AbsolutePath& path )
{
	return GetFileName( path ).ToString();
}

String ExtractFileName( const String& path )
{
	return GetFileName( path ).ToString();
}

String ExtractFileName( const StringView path )
{
	return GetFileName( path ).ToString();
}

StringView GetFileName( const AbsolutePath& path )
{
	return GetFileName( path.AsStringView() );
}

StringView GetFileName( const StringView path )
{
	const Uint32 index = GetFileNameIndex( path );
	if ( index == path.Length() )
	{
		return StringView();
	}
	if ( index == StringView::npos )
	{
		return path;
	}

	return path.SubView( index );
}

//------------------------------------------------------------------------------

String ExtractFileStem( const AbsolutePath& path )
{
	return GetFileStem( path ).ToString();
}

String ExtractFileStem( const String& path )
{
	return GetFileStem( path ).ToString();
}

String ExtractFileStem( const StringView path )
{
	return GetFileStem( path ).ToString();
}

StringView GetFileStem( const AbsolutePath& path )
{
	return GetFileStem( path.AsStringView() );
}

StringView GetFileStem( const StringView path )
{
	Uint32 filenameIndex = GetFileNameIndex( path );
	if ( filenameIndex == path.Length() )
	{
		return StringView();
	}
	if ( filenameIndex == StringView::npos )
	{
		filenameIndex = 0;
	}

	const Uint32 extensionIndex = path.FindReverse( '.' );
	if ( extensionIndex < filenameIndex || extensionIndex == StringView::npos )
	{
		return path.SubView( filenameIndex );
	}

	return path.Slice( filenameIndex, extensionIndex );
}

//------------------------------------------------------------------------------

String ExtractExtension( const AbsolutePath& path )
{
	return GetExtension( path ).ToString();
}

String ExtractExtension( const String& path )
{
	return GetExtension( path ).ToString();
}

String ExtractExtension( const StringView path )
{
	return GetExtension( path ).ToString();
}

StringView GetExtension( const AbsolutePath& path )
{
	return GetExtension( path.AsStringView() );
}

StringView GetExtension( const StringView path )
{
	Uint32 index = GetExtensionIndex( path );
	if ( index == StringView::npos || index >= path.Length() - 1 )
	{
		return StringView();
	}

	return path.SubView( index + 1 );
}

StringView RemoveExtension( const StringView path )
{
	Uint32 index = GetExtensionIndex( path );
	if ( index == StringView::npos || index >= path.Length() )
	{
		return path;
	}

	return path.SubView( 0, index );
}

//------------------------------------------------------------------------------

String ExtractParentPath( const AbsolutePath& path )
{
	return GetParentPath( path ).ToString();
}

String ExtractParentPath( const String& path )
{
	return GetParentPath( path ).ToString();
}

String ExtractParentPath( const StringView path )
{
	return GetParentPath( path ).ToString();
}

StringView GetParentPath( const AbsolutePath& path )
{
	return GetParentPath( path.AsStringView() );
}

StringView GetParentPath( const StringView path )
{
	if ( path.Empty() )
	{
		return StringView();
	}

	Uint32 find_offset = path.Length() - 1;
	if ( IsSeparator( path.Back() ) )
	{
		if ( IsRootPath( path ) )
		{
			return StringView();
		}

		// Skip the last directory separator at the end of the string
		find_offset -= 1;
	}

	Uint32 last_separator = GetLastSeperatorIndex( path, find_offset );
	if ( last_separator == StringView::npos )
	{
		// Return empty path if no parent path
		return StringView();
	}

	return path.SubView( 0, last_separator + 1 );
}

//------------------------------------------------------------------------------

String ExtractParentPathName( const AbsolutePath& path )
{
	return GetParentPathName( path ).ToString();
}

String ExtractParentPathName( const String& path )
{
	return GetParentPathName( path ).ToString();
}

String ExtractParentPathName( const StringView path )
{
	return GetParentPathName( path ).ToString();
}

StringView GetParentPathName( const AbsolutePath& path )
{
	return GetParentPathName( path.AsStringView() );
}

StringView GetParentPathName( const StringView path )
{
	if ( path.Empty() )
	{
		return StringView();
	}

	Uint32 find_offset = path.Length() - 1;
	if ( IsSeparator( path.Back() ) )
	{
		if ( IsRootPath( path ) )
		{
			return StringView();
		}

		// Skip the last directory separator at the end of the string
		find_offset -= 1;
	}

	Uint32 last_separator = GetLastSeperatorIndex( path, find_offset );
	if ( last_separator == StringView::npos )
	{
		// Return empty path if no parent path
		return StringView();
	}

	Uint32 prev_separator = GetLastSeperatorIndex( path, last_separator - 1 ) + 1;
	// Note: This will wrap StringView::npos around to 0 which is what we want if there's no other path separator in the string

	return path.Slice( prev_separator, last_separator );
}

//------------------------------------------------------------------------------

bool HasFileName( const AbsolutePath& path )
{
	return path.IsFilePath();
}

bool HasFileName( const String& path )
{
	return !path.Empty() && !IsSeparator( path.Back() );
}

bool HasFileName( const StringView path )
{
	return !path.Empty() && !IsSeparator( path.Back() );
}

bool HasExtension( const AbsolutePath& path )
{
	return GetExtensionIndex( path.AsStringView() ) != StringView::npos;
}

bool HasExtension( const String& path )
{
	return GetExtensionIndex( path ) != StringView::npos;
}

bool HasExtension( const StringView path )
{
	return GetExtensionIndex( path ) != StringView::npos;
}

red::AbsolutePath ReplaceExtension( const AbsolutePath& path, const StringView extension )
{
	RED_ASSERT( extension.Data() != nullptr, "Cannot append null extension" );

	if ( !path.IsFilePath() )
	{
		return path;
	}

	String str = path.AsString();
	ReplaceExtension( str, extension );

	return AbsolutePath::CreateFilePath( str );
}

AbsolutePath SetExtension( const AbsolutePath& path, const StringView extension )
{
	return ReplaceExtension( path, extension );
}

Bool IsSubpath( const AbsolutePath& container, const AbsolutePath& contained )
{
	if( !container.IsDirectoryPath() )
		return false;

	return contained.AsStringView().StartsWith( container.AsStringView() );
}

void ReplaceExtension( String& str, const StringView extension )
{
	Uint32 extensionIndex = GetExtensionIndex( str );
	if ( extensionIndex == StringView::npos )
	{
		if ( !extension.Empty() )
		{
			// Append extension to the filename and dot
			str.Append( '.' );
			str.Append( extension.Data(), extension.Length() );
		}
	}
	else
	{
		extensionIndex += 1;
		str.Erase( extensionIndex, str.Length() );
		str.Append( extension.Data(), extension.Length() );
	}

	if ( str.Back() == '.' )
	{
		// Trim off the trailing dot
		str.Resize( str.Length() - 1 );
	}
}

void SetExtension( String& str, const StringView extension )
{
	ReplaceExtension( str, extension );
}

bool IsAbsolutePath( const StringView pathString )
{
	return IsAbsolute( pathString );
}

bool IsDirectoryPath( const StringView pathString )
{
	return IsSeparator( pathString.Back() );
}

#if defined( RED_PLATFORM_WINPC )
String GetOnDiskName( const AbsolutePath& path )
{
	auto GetOnDiskName = []( red::StringView path )
	{
		WIN32_FIND_DATAA fd{};
		HANDLE handle = FindFirstFileA( path.Data(), &fd );
		if ( handle != INVALID_HANDLE_VALUE )
		{
			const String realFilename = fd.cFileName;
			FindClose( handle );
			return realFilename;
		}

		return String::EMPTY();
	};

	const red::StringView view = path.AsStringView();
	const Bool hasDrive = HasWindowsDriveSpecifier( view );
	const red::DynArray< StringView > parts = SplitPath( view );
	const Uint32 partsSize = parts.Size();

	red::DynArray< String > onDiskNames{ red::PoolEngine() };
	onDiskNames.Resize( partsSize );

	String testPath;
	Uint32 startIndex{};

	if ( hasDrive )
	{
		onDiskNames[ 0 ] = parts[ 0 ].ToString();
		testPath = onDiskNames[ 0 ];
		testPath += '\\';
		startIndex = 1;
	}

	for ( Uint32 i = startIndex; i < partsSize; ++i )
	{
		testPath += parts[ i ].ToString();

		onDiskNames[ i ] = GetOnDiskName( testPath );

		if ( i < partsSize - 1 )
		{
			testPath += '\\';
		}
	}

	return String::Join( onDiskNames, "\\" );
}
#endif

//--------

namespace
{
#if defined( RED_PLATFORM_ORBIS ) || defined( RED_PLATFORM_LINUX )

	constexpr const AnsiChar DEFAULT_PATH_SEPARATOR = '/';

#elif defined( RED_PLATFORM_DURANGO ) || defined( RED_PLATFORM_WINPC )

	constexpr const AnsiChar DEFAULT_PATH_SEPARATOR = '\\';

#endif
}

char LastSeparatorUsed( const AbsolutePath& path )
{
	const auto& str = path.AsString();
	const Uint32 lastSeparator = GetLastSeperatorIndex( str );
	if ( lastSeparator != StringView::npos )
	{
		return str[ lastSeparator ];
	}
	return DEFAULT_PATH_SEPARATOR;
}


String ConcatRelativePath( const String& basePath, const String& appendPath, const char separator )
{
	String path;
	path.Reserve( basePath.Length() + appendPath.Length() + 1 ); // 1 for separator
	if ( !basePath.Empty() )
	{
		path = basePath;
		if ( path.Back() != separator )
		{
			path.Append( separator );
		}
	}
	path += appendPath;
	return path;
}

} // namespace paths

} // namespace red
