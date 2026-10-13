/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "metricsCaptureDurango.h"

namespace red
{
namespace memory
{
	// Durango doesn't provide (documented) API for enumerating process modules, but toolhelpx.dll exports few undocumented functions which we can use.
	 // Other option might be to manually parse PEB ( Process Environment Block ) and get list of loaded modules, but it will be even more hacky.

	namespace
	{
		constexpr auto c_toolHelpXDLLName = L"toolhelpx.dll";
		constexpr auto c_EnumProcessModulesExFuncName = "K32EnumProcessModulesEx";
		constexpr auto c_GetModuleBaseNameFuncName = "K32GetModuleBaseNameW";

		typedef BOOL ( __stdcall *ENUMPROCESSMODULESEX )( HANDLE hProcess, HMODULE* pModule, DWORD cb, LPDWORD lpcbNeeded, DWORD dwFilterFlag );
		typedef DWORD ( __stdcall *GETMODULEBASENAME )( HANDLE hProcess, HMODULE hModule, LPWSTR lpFilename, DWORD nSize );

		enum ModuleFilterCriteria : DWORD
		{
			LIST_MODULES_DEFAULT = 0x0,
			LIST_MODULES_32BIT = 0x01,
			LIST_MODULES_64BIT = 0x02,
			LIST_MODULES_ALL = 0x03
		};

		void EnumerateLoadedModules64( HANDLE hProcess, BOOL ( __stdcall *enumerateLoadedModulesCallback )( PCSTR moduleName, DWORD64 moduleBase, ULONG moduleSize, PVOID userContext ), PVOID userContext )
		{
			HMODULE hToolHelpX = LoadLibraryW( c_toolHelpXDLLName );
			if ( !hToolHelpX )
			{
				RED_LOG_ERROR( "Failed to load %hs, error code=%d", c_toolHelpXDLLName, GetLastError() );
				return;
			}

			ENUMPROCESSMODULESEX enumProcessModules = reinterpret_cast< ENUMPROCESSMODULESEX >( GetProcAddress( hToolHelpX, c_EnumProcessModulesExFuncName ) );
			if ( !enumProcessModules )
			{
				RED_LOG_ERROR( "Failed to get function %hs, error code=%d", c_EnumProcessModulesExFuncName, GetLastError() );
				return;
			}

			GETMODULEBASENAME getModuleBaseName = reinterpret_cast< GETMODULEBASENAME >( GetProcAddress( hToolHelpX, c_GetModuleBaseNameFuncName ) );
			if ( !getModuleBaseName )
			{
				RED_LOG_ERROR( "Failed to get function %hs, error code=%d", c_GetModuleBaseNameFuncName, GetLastError() );
				return;
			}

			const int maxModulesCount = 256;
			HMODULE modules[ maxModulesCount ] = {};
			DWORD bytesNeeded = 0;

			if ( enumProcessModules( hProcess, modules, 256, &bytesNeeded, ModuleFilterCriteria::LIST_MODULES_DEFAULT ) == 0 )
			{
				RED_LOG_ERROR( "Failed to retrieve handle for each module, error code=%d", GetLastError() );
				return;
			}

			for ( int i = 0; i < maxModulesCount; ++i )
			{
				if ( modules[i] == 0 )
				{
					break;
				}

				WCHAR wideModuleName[ 1024 ] = {};
				if ( getModuleBaseName( hProcess, modules[i], wideModuleName, 1024 ) == 0 )
				{
					RED_LOG_ERROR( "Failed to get name for module = %ld", modules[ i ] );
					continue;
				}

				char moduleName[ 1024 ] = {};
				red::WideCharToStdChar_NoConv( moduleName, wideModuleName, 1024 );

				enumerateLoadedModulesCallback( moduleName, reinterpret_cast< Uint64 >( modules[ i ] ), 0, userContext );
			}
		}

		BOOL CALLBACK EnumerateLoadedModulesProc64( _In_ PCSTR moduleName, _In_ DWORD64 moduleBase, _In_ ULONG moduleSize, _In_opt_ PVOID userContext )
		{
			const char c_modulePrefix[] = "MOD_"; // Used to identify modules
			red::Uint32 moduleNameLength = static_cast< u32 >( Strlen( moduleName ) );
			red::Uint64 moduleSize64 = static_cast< u64 >( moduleSize );
			Serializer* serializer = static_cast< Serializer* >( userContext );

			serializer->Serialize( c_modulePrefix, sizeof( c_modulePrefix ) - 1 ); // Don't write the terminator
			serializer->Serialize( moduleSize64 );
			serializer->Serialize( moduleNameLength );
			serializer->Serialize( moduleName, moduleNameLength );
			serializer->Serialize( moduleBase );

			return TRUE;
		}
	}

	void MetricsCaptureDurango::WritePlatfromIdentifier( Serializer& serializer )
	{
		const char platformIdentifier[] = "DURANGO";
		const u16 platformIdentifierSize = static_cast< u16 >( sizeof( platformIdentifier ) - 1 );
		serializer.Serialize( platformIdentifierSize );
		serializer.Serialize( platformIdentifier, platformIdentifierSize );
	}

	void MetricsCaptureDurango::WriteLoadedModules( Serializer& serializer )
	{
		HANDLE thisProcess = GetCurrentProcess();
		EnumerateLoadedModules64( thisProcess, EnumerateLoadedModulesProc64, &serializer );
	}
}
}