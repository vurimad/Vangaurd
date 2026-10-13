/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "errorHandlerImplAttachments.h"
#include "dbgUtils.h"

#ifdef RED_PLATFORM_DURANGO
# if _XDK_VER < 0x3AD70402
#  error Your XDK version is too old
# endif
# include <werapi.h>
#endif

namespace red { namespace err
{
	namespace helper
	{

#ifdef RED_PLATFORM_DURANGO
		static const char* GetWerHResultName( HRESULT hr )
		{
			if ( hr == S_OK )
			{
				return "S_OK: Success.";
			}
			if ( hr == WER_E_INVALID_STATE )
			{
				return "WER_E_INVALID_STATE: The process state is not valid.";
			}
			if ( hr == WER_E_INSUFFICIENT_BUFFER )
			{
				return "WER_E_INSUFFICIENT_BUFFER: The number of registered files exceeds the limit.";
			}
			if ( hr == WER_E_NOT_FOUND )
			{
				return "WER_E_NOT_FOUND: The list of registered files does not contain the specified file.";
			}
			return "<Unknown>";
		}
#endif
	}

	namespace hacks
	{
		// Shouldn't have to do this, but the Xbox keeps WER files around multiple process runs
		// #tbd: also delete old files
		// #tbd: Unicode macro trap in other code using non-explicit WIN32_FIND_DATA(W)?
#ifdef RED_PLATFORM_DURANGO
		using FindFileCallback = void( const wchar_t* absolutePath );
		static void DurangoFindFilesNonRecursive( const wchar_t* registeredFilesDir, FindFileCallback* callback )
		{
			if ( !callback )
			{
				RED_DBG_TRACE( "DurangoFindFilesNonRecursive: no callback" );
				return;
			}

			wchar_t searchDir[ MAX_PATH ] = L"";
			red::Strcpy( searchDir, registeredFilesDir, RED_ARRAY_COUNT_U32( searchDir ) );
			const Uint32 searchDirLen = (Uint32)red::Strlen( searchDir );
			if ( searchDirLen > 0 && searchDir[ searchDirLen - 1 ] != L'\\' )
			{
				red::Strcat( searchDir, L"\\", RED_ARRAY_COUNT_U32( searchDir ) );
			}

			wchar_t searchPattern[ MAX_PATH ] = L"";
			red::Strcpy( searchPattern, searchDir, RED_ARRAY_COUNT_U32( searchPattern ) );
			red::Strcat( searchPattern, L"*.*", RED_ARRAY_COUNT_U32( searchPattern ) );

			WIN32_FIND_DATAW ffd;
			const HANDLE hFind = ::FindFirstFileExW( searchPattern, FindExInfoStandard, &ffd, FindExSearchNameMatch, nullptr, 0 );
			if ( hFind == INVALID_HANDLE_VALUE )
			{
				RED_DBG_TRACE( "Failed to search searchDir '%ls', GetLastError=%u", searchDir, ::GetLastError() );
				return;
			}

			do
			{
				if ( ( ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) == 0 )
				{
					if ( ffd.cFileName[ 0 ] == '.' ) // ignore . and .., and also any potential dot-file directories
						continue;

					wchar_t tmpAbsPath[ MAX_PATH ] = L"";
					red::Strcpy( tmpAbsPath, searchDir, RED_ARRAY_COUNT_U32( tmpAbsPath ) );
					red::Strcat( tmpAbsPath, ffd.cFileName, RED_ARRAY_COUNT_U32( tmpAbsPath ) );
					callback( tmpAbsPath );
				}
			} while ( ::FindNextFileW( hFind, &ffd ) != 0 );

			if ( ::GetLastError() != ERROR_NO_MORE_FILES )
			{
				RED_DBG_TRACE( "Error enumerating searchDir. GetLastError=%u", ::GetLastError() );
			}
			::FindClose( hFind );
		}

		static void UnregisterFileCallback( const wchar_t* absolutePath )
		{
			const HRESULT hr = ::WerUnregisterFile( absolutePath );

			// Log files successfully unregistered or unexpected error
			if ( hr != WER_E_NOT_FOUND )
			{
				RED_DBG_TRACE( "WerUnregisterFile '%ls' HRESULT=0x%08X: %hs", absolutePath, hr, helper::GetWerHResultName( hr ) );
			}
		}

		void DurangoUnregisterFilesNonRecursive( const wchar_t* registeredFilesDir )
		{
			DurangoFindFilesNonRecursive( registeredFilesDir, &UnregisterFileCallback );
		}
#endif
	} // hacks

	namespace helper
	{	
		static Bool RegisterAttachmentEntry( const wchar_t* pathToRegister, dbgutils::RegisteredAttachmentEntry& entry )
		{
			const Uint32 maxPath = RED_ARRAY_COUNT_U32( entry.m_absolutePath );

#if defined( RED_PLATFORM_WINPC )
			const Uint32 len = ::GetFullPathNameW( pathToRegister, maxPath, entry.m_absolutePath, nullptr );
			if ( len == 0 )
			{
				RED_DBG_TRACE( "ConvertAttachmentToAbsolutePath: GetFullPathNameW failed for '%ls': 0x%08X", pathToRegister, ::GetLastError() );
				return false;
			}
			if ( len >= maxPath )
			{
				RED_DBG_TRACE( "ConvertAttachmentToAbsolutePath: failed to register file '%ls'. Path length exceeded", pathToRegister );
				return false;
			}
			red::ReplaceChar( entry.m_absolutePath, maxPath, L'/', L'\\' );
			return true;

#elif defined( RED_PLATFORM_DURANGO )
			// Should still RECORD in the table which files are registered...
			HRESULT hr = ::WerRegisterFile( pathToRegister, WER_REGISTER_FILE_TYPE::WerRegFileTypeOther, 0 );
			if ( FAILED( hr ) )
			{
				RED_DBG_TRACE( "Failed to register '%ls'. WerRegisterFile HRESULT=0x%08X: %hs", pathToRegister, hr, GetWerHResultName( hr ) );
				return false;
			}

			return true;
#else
			RED_DBG_TRACE( "Unicode RegisterAttachmentEntry called on the wrong platform" );
			return false;
#endif
		}

		static Bool RegisterAttachmentEntry( const char* pathToRegister, dbgutils::RegisteredAttachmentEntry& entry )
		{
			const Uint32 maxPath = RED_ARRAY_COUNT_U32( entry.m_absolutePath );

#if defined( RED_PLATFORM_ORBIS )
			red::Strcpy( entry.m_absolutePath, pathToRegister, maxPath );
			red::ReplaceChar( entry.m_absolutePath, maxPath, '\\', '/' );
			return true;

#elif defined( RED_PLATFORM_LINUX )
			red::Strcpy( entry.m_absolutePath, pathToRegister, maxPath );
			red::ReplaceChar( entry.m_absolutePath, maxPath, '\\', '/' );
			return true;

#else
			RED_DBG_TRACE( "ANSI RegisterAttachmentEntry called on the wrong platform" );
			return false;
#endif
		}

		static Bool GetAttachmentIndex( dbgutils::RegisteredAttachmentTable& table, Uint32* outIndex )
		{
			const Uint32 index = atomic::Increment32( atomic::alias_cast32( &table.m_numRegisteredAttachments ) ) - 1;
			if ( index >= dbgutils::RegisteredAttachmentTable::MAX_FILE_ATTACHMENTS )
			{
				RED_DBG_TRACE( "RegisterAttachmentForErrorReport: Exceeded maximum registered files" );
				return false;
			}

			*outIndex = index;
			return true;
		}

		template< typename TChar >
		Bool VerifyAttachmentPath( const TChar* pathToRegister )
		{
			if ( !pathToRegister || !pathToRegister[ 0 ] )
			{
				RED_DBG_TRACE( "Trying to register invalid path" );
				return false;
			}

			const Uint32 len = static_cast<Uint32>( red::Strlen( pathToRegister ) );
			if ( len >= dbgutils::RegisteredAttachmentEntry::MaxPath )
			{
				RED_DBG_TRACE( "RegisterAttachmentForErrorReport: failed to register file '%ls'. Path length exceeded", pathToRegister );
				return false;
			}
			return true;
		}

	} // helper

// #tbd: shouldn't register a remote file path, especially since could cause network hang
template< typename TChar >
void RegisterAttachment( dbgutils::RegisteredAttachmentTable& table, const TChar* pathToRegister )
{
	if ( !helper::VerifyAttachmentPath( pathToRegister ) )
	{
		atomic::Increment32( atomic::alias_cast32( &table.m_numFailedToRegister ) );
		return;
	}

	Uint32 index = 0;
	if ( !helper::GetAttachmentIndex( table, &index ) )
	{
		atomic::Increment32( atomic::alias_cast32( &table.m_numFailedToRegister ) );
		return;
	}

	dbgutils::RegisteredAttachmentEntry& entry = table.m_registeredAttachments[ index ];
	helper::RegisterAttachmentEntry( pathToRegister, entry );
}

template void RegisterAttachment<char>( dbgutils::RegisteredAttachmentTable& table, const char* pathToRegister );
template void RegisterAttachment<wchar_t>( dbgutils::RegisteredAttachmentTable& table, const wchar_t* pathToRegister );

} } // red/err
