/**
 * Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
 */
#pragma once

#include "../../../common/redContainers/include/string/string.h"
#include "../../../common/redContainers/include/string/stringView.h"
#include "../../../common/redContainers/include/dynArray.h"

namespace red
{
class Utf8String;
class Utf16String;

// AbsolutePath represents an absolute path in the system, it is never relative
// Paths can be appended in the following ways:
// * AP + str -> AP
// * AP + RP -> AP
// * AP + AP = Invalid!
// A path to a directory always ends with a slash, either forward or backward
// A path that does not end with a slash is treated as a file, and cannot be appended to
// Main rule is that you cannot append two absolute paths together (in AbsolutePath classes or otherwise)
class REDCORE_API AbsolutePath
{

public:
	// Warning: CreateDirPath/CreateFilePath asserts if you pass invalid absolute path! If your path is coming from untrusted
	// source such as user input. Before creating a path you should call IsValidPath() before and handle errors
	// the best for your use case.

	static AbsolutePath CreateDirPath( const StringView pathname );
	static AbsolutePath CreateDirPath( const Utf16String& pathname );

	static AbsolutePath CreateFilePath( const StringView pathname );
	static AbsolutePath CreateFilePath( const Utf16String& pathname );

	static AbsolutePath ParseDirPath( const StringView str );
	static AbsolutePath ParseFilePath( const StringView str );

	static Bool IsValidPath( const StringView pathname );
	static Bool IsValidPath( const Utf16String& pathname ); //Warning: does internal convertion to red::String

	AbsolutePath();
	AbsolutePath( const AbsolutePath& other );
	AbsolutePath( AbsolutePath&& other );
	~AbsolutePath();

	AbsolutePath& operator=( const AbsolutePath& other );
	AbsolutePath& operator=( AbsolutePath&& other );

	RED_INLINE void Clear();
	RED_INLINE bool Empty() const;
	RED_INLINE Uint32 Length() const;

	bool IsDirectoryPath() const;
	bool IsFilePath() const;
	bool IsOnlyRootPath() const;

	friend REDCORE_API bool operator==( const AbsolutePath& left, const AbsolutePath& right );
	friend REDCORE_API bool operator!=( const AbsolutePath& left, const AbsolutePath& right );
	friend REDCORE_API bool operator<( const AbsolutePath& left, const AbsolutePath& right );

	RED_INLINE const AnsiChar* AsChar() const;
	RED_INLINE const String& AsString() const;
	RED_INLINE StringView AsStringView() const;

	Utf8String ToUtf8String() const;
	Utf16String ToUtf16String() const;

	//
	// Append works like +=
	//
	AbsolutePath& AppendDirPath( const StringView str );	// Ensures the AbsolutePath ends with a /
	AbsolutePath& AppendFilePath( const StringView str );	// Ensures the AbsolutePath does not end with a /

	// For interacting with the system
	AbsolutePath& AppendDirPath( const Utf16String& str );
	AbsolutePath& AppendFilePath( const Utf16String& str );

	//
	// Add works like +
	//
	AbsolutePath AddDirPath( const StringView str ) const;	// Ensures the AbsolutePath ends with a /
	AbsolutePath AddFilePath( const StringView str ) const;	// Ensures the AbsolutePath does not end with a /

	// For interacting with the system
	AbsolutePath AddDirPath( const Utf16String& str ) const;
	AbsolutePath AddFilePath( const Utf16String& str ) const;

	RED_INLINE Uint32 CalcHash() const;

	// For use in asserts and log messages only
	RED_INLINE const char* ToDebugString() const;

protected:
	String m_path;

private:
	struct ConformedToken {};
	AbsolutePath( String&& conformedPath, ConformedToken );
};

// Sort function to sort paths by directory first then file name, this groups all files in
// the same directory together and sorts them by name, it puts files in the parent directory
// before child directories.
REDCORE_API bool SortByDirectory( const AbsolutePath& left, const AbsolutePath& right );

namespace paths
{
	REDCORE_API AbsolutePath GetCurrentWorkingDirectory();

	REDCORE_API AbsolutePath ParentAbsolutePath( const AbsolutePath& path );
	REDCORE_API AbsolutePath AncestorAbsolutePath( const AbsolutePath& path, Int32 count );

	//
	// AbsolutePath based helpers
	//
	REDCORE_API StringView GetFileName( const AbsolutePath& path );
	REDCORE_API StringView GetFileStem( const AbsolutePath& path );
	REDCORE_API StringView GetExtension( const AbsolutePath& path );
	REDCORE_API StringView GetParentPath( const AbsolutePath& path );
	REDCORE_API StringView GetParentPathName( const AbsolutePath& path );
	REDCORE_API DynArray< StringView > SplitPath( const AbsolutePath& path );

	REDCORE_API String ExtractFileName( const AbsolutePath& path );
	REDCORE_API String ExtractFileStem( const AbsolutePath& path );
	REDCORE_API String ExtractExtension( const AbsolutePath& path );
	REDCORE_API String ExtractParentPath( const AbsolutePath& path );
	REDCORE_API String ExtractParentPathName( const AbsolutePath& path );

	REDCORE_API bool HasFileName( const AbsolutePath& path );
	REDCORE_API bool HasExtension( const AbsolutePath& path );

	REDCORE_API AbsolutePath ReplaceExtension( const AbsolutePath& path, const StringView extension );
	REDCORE_API AbsolutePath SetExtension( const AbsolutePath& path, const StringView extension );

	REDCORE_API Bool IsSubpath( const AbsolutePath& container, const AbsolutePath& contained );

	//
	// String based path helpers
	//
	REDCORE_API StringView GetFileName( const StringView path );
	REDCORE_API StringView GetFileStem( const StringView path );
	REDCORE_API StringView GetExtension( const StringView path );
	REDCORE_API StringView RemoveExtension( const StringView path );
	REDCORE_API StringView GetParentPath( const StringView path );
	REDCORE_API StringView GetParentPathName( const StringView path );
	REDCORE_API DynArray< StringView > SplitPath( const StringView path );

	REDCORE_API String ExtractFileName( const String& path );
	REDCORE_API String ExtractFileName( const StringView path );
	REDCORE_API String ExtractFileStem( const String& path );
	REDCORE_API String ExtractFileStem( const StringView path );
	REDCORE_API String ExtractExtension( const String& path );
	REDCORE_API String ExtractExtension( const StringView path );
	REDCORE_API String ExtractParentPath( const String& path );
	REDCORE_API String ExtractParentPath( const StringView path );
	REDCORE_API String ExtractParentPathName( const String& path );
	REDCORE_API String ExtractParentPathName( const StringView path );

	REDCORE_API bool HasFileName( const String& path );
	REDCORE_API bool HasFileName( const StringView path );
	REDCORE_API bool HasExtension( const String& path );
	REDCORE_API bool HasExtension( const StringView path );

	REDCORE_API void ReplaceExtension( String& str, const StringView extension );
	REDCORE_API void SetExtension( String& str, const StringView extension );

	REDCORE_API bool IsAbsolutePath( const StringView pathString );
	REDCORE_API bool IsDirectoryPath( const StringView pathString );

#if defined( RED_PLATFORM_WINPC )
	REDCORE_API String GetOnDiskName( const AbsolutePath& path );
#endif

	// These are temporary functions here to assist in the implementation of
	// FileFilesRelative and other similar functions at a low level in the code
	REDCORE_API char LastSeparatorUsed( const AbsolutePath& path );
	REDCORE_API String ConcatRelativePath( const String& basePath, const String& appendPath, const char separator );

} // namespace paths

// Compatability fix
namespace utils
{
using namespace paths;
}

#if defined( RED_PLATFORM_ORBIS )
#define MAX_PATH 260
#elif defined( RED_PLATFORM_LINUX )
#include <limits.h>
#define MAX_PATH PATH_MAX
#endif

} // namespace red

#include "absolutePath.hpp"
