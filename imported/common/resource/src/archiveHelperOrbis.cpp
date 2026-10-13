/*
 * Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "archiveHelperOrbis.h"

#ifdef RED_PLATFORM_ORBIS

#include "../../redFileSystem/include/fileSys.h"
#include "../../redCore/include/languageRegion.h"

#include <libsysmodule.h>

#include "archiveLanguageMap.h"
#include <system_param.h>
#include <system_service.h>

namespace playgo
{

//------------------------------------------------------------------------------

ModuleHandle::ModuleHandle()
	: m_loaded( false )
{}

ModuleHandle::~ModuleHandle()
{
	Unload();
}

bool ModuleHandle::Load()
{
	auto result = ::sceSysmoduleLoadModule( SCE_SYSMODULE_PLAYGO );
	if ( result != SCE_OK )
	{
		RED_LOG_ERROR( "PlayGo: Failed to load the PlayGo module, error: %08x", result );
		return false;
	}

	m_loaded = true;
	return true;
}

void ModuleHandle::Unload()
{
	if ( m_loaded )
	{
		auto result = ::sceSysmoduleUnloadModule( SCE_SYSMODULE_PLAYGO );
		RED_FATAL_ASSERT( result == SCE_OK, "PlayGo: Failed to unload the PlayGo module, error: %08x", result );
		m_loaded = false;
	}
}

bool ModuleHandle::IsLoaded()
{
	return m_loaded && ::sceSysmoduleIsLoaded( SCE_SYSMODULE_PLAYGO ) == SCE_OK;
}

//------------------------------------------------------------------------------

System::System()
	: m_initialised( false )
{}

System::~System()
{
	Shutdown();
}

bool System::Initialise()
{
	if ( m_initialised )
	{
		return true;
	}

	m_playGoHeap = red::CreateUniqueBuffer< red::PoolEngine >( SCE_PLAYGO_HEAP_SIZE, 16 );

	::ScePlayGoInitParams initParams;
	initParams.bufAddr = m_playGoHeap.Data();
	initParams.bufSize = m_playGoHeap.Size();
	initParams.reserved = 0;
	auto result = ::scePlayGoInitialize( &initParams );
	if ( result != SCE_OK && result != SCE_PLAYGO_ERROR_ALREADY_INITIALIZED )
	{
		RED_LOG_ERROR( "PlayGo: Failed to initialise PlayGo, error: %08x", result );
		return false;
	}

	m_initialised = true;
	return true;
}

void System::Shutdown()
{
	if ( m_initialised )
	{
		auto result = ::scePlayGoTerminate();
		RED_FATAL_ASSERT( result == SCE_OK, "PlayGo: Failed to terminate the PlayGo library, error: %08x", result );

		m_initialised = false;
		m_playGoHeap.Reset();
	}
}

bool System::IsInititalised() const
{
	return m_initialised;
}

Handle::Handle()
	: m_handle{}
	, m_opened( false )
{}

Handle::~Handle()
{
	Close();
}

bool Handle::Open()
{
	auto result = ::scePlayGoOpen( &m_handle, nullptr );
	if ( result != SCE_OK )
	{
		RED_LOG_ERROR( "PlayGoWrapper: Failed to open the PlayGo Handle, error: %08x", result );
		return false;
	}

	m_opened = true;
	return true;
}

void Handle::Close()
{
	if ( m_opened )
	{
		auto result = ::scePlayGoClose( m_handle );
		RED_FATAL_ASSERT( result == SCE_OK, "PlayGoWrapper: Failed to close the PlayGo Handle, error: %08x", result );

		m_opened = false;
		m_handle = 0;
	}
}

bool Handle::IsOpen() const
{
	return m_opened;
}

::ScePlayGoHandle Handle::Get() const
{
	RED_FATAL_ASSERT( m_opened );
	return m_handle;
}

::ScePlayGoLanguageMask GetLanguageMask( const Handle& handle )
{
	::ScePlayGoLanguageMask mask = 0ull;
	auto result = ::scePlayGoGetLanguageMask( handle.Get(), &mask );
	if ( result != SCE_OK )
	{
		RED_LOG_ERROR( "PlayGo: Failed to get the installed language mask, error: %08x", result );
		mask = 0ull;
	}
	return mask;
}

red::DynArray< Int8 > GetLocusForChunks( const Handle& handle, const red::ArraySpan< Uint16 >& chunkIds, Int32 &systemErrorCode )
{
	systemErrorCode = SCE_OK;

	red::DynArray< Int8 > loci{ chunkIds.Size(), red::PoolEngine() };
	if ( !chunkIds.Empty() )
	{
		systemErrorCode = ::scePlayGoGetLocus(
			handle.Get(),
			chunkIds.Data(),
			chunkIds.Size(),
			reinterpret_cast< ScePlayGoLocus* >( loci.TypedData() ) );
		if ( systemErrorCode != SCE_OK )
		{
			RED_LOG_ERROR( "PlayGo: Failed to get locus for chunks, error: %08x", systemErrorCode );
			loci.Clear();
		}
	}
	return loci;
}

} // namespace playgo

namespace res
{
namespace prv
{

//------------------------------------------------------------------------------

red::DynArray< red::String > GetInstalledLanguages( prv::ArchiveLanguageMap &languageMap )
{
	// TODO: Use ArchiveLanguageMap here to get this list

	red::DynArray< red::String > result{ red::PoolEngine() };

	playgo::ModuleHandle playgoModule;
	playgo::System playgoSystem;
	playgo::Handle playgoHandle;

	res::LanguageID defaultVO = languageMap.GetGlobalDefaultVoiceOver();
	res::LanguageID defaultFullVO = defaultVO;

	SceSystemParamLang systemLanguage{};
	const Int32 error = ::sceSystemServiceParamGetInt( SCE_SYSTEM_SERVICE_PARAM_ID_LANG, &systemLanguage );
	RED_FATAL_ASSERT( error == SCE_OK );


	red::StringView systemLanguageString = red::GetLanguageCodeNameFromSystemId( systemLanguage );
	res::LanguageID systemLanguageID = ToLanguageID( systemLanguageString );

	languageMap.SetSystemLanguageID( systemLanguageID );

	for ( const auto &entry : languageMap.GetTextEntries() )
	{
		if ( entry.language == systemLanguageID )
		{
			defaultVO = entry.defaultVO;
			defaultFullVO = entry.defaultFullVO;
			break;
		}
	}

	const Int32 c_defaultVoSystemID =
		static_cast< Int32 >( red::GetLanguageCodeNameToSystemId( RED_NAME( ToLanguageCode( defaultVO ) ) ) );

	const Int32 c_defaultFullVoSystemID =
		static_cast< Int32 >( red::GetLanguageCodeNameToSystemId( RED_NAME( ToLanguageCode( defaultFullVO ) ) ) );

	const Int32 c_defaultVO = c_defaultVoSystemID == -1 ? c_defaultFullVoSystemID : c_defaultVoSystemID;

	languageMap.SetDefaultVO( c_defaultVoSystemID == -1 ? defaultFullVO : defaultVO );

	red::DynArray< Int32 > installedLanguages{ red::PoolEngine() };
	installedLanguages.PushBack( c_defaultVO );

	for ( const auto &entry : languageMap.GetVoiceEntries() )
	{
		if ( entry.status == LanguagePackStatus::Installed )
		{
			const Int32 voID =
				static_cast< Int32 >(
					red::GetLanguageCodeNameToSystemId( RED_NAME( ToLanguageCode( entry.language ) ) ) );

			const Int32 fullVoID =
				static_cast< Int32 >(
					red::GetLanguageCodeNameToSystemId( RED_NAME( ToLanguageCode( entry.fullLanguage ) ) ) );

			const Int32 vo = voID == -1 ? fullVoID : voID;
			red::alg::PushBackUnique( installedLanguages, vo );
		}
	}

	if ( playgoModule.Load() && playgoSystem.Initialise() && playgoHandle.Open() )
	{
		::ScePlayGoLanguageMask systemLanguageMask = playgo::GetLanguageMask( playgoHandle );

		for ( auto const &voID : installedLanguages )
		{
			systemLanguageMask |= scePlayGoConvertLanguage( voID );
		}

		const Int32 langMaskError = ::scePlayGoSetLanguageMask( playgoHandle.Get(), systemLanguageMask );

		// Note: Technically to be correct this should be either 48 (the number of system languages that could exist) or 64 (maximum language count including custom)
		// But since the documentation only lists languages from 0 to 29 this should be enough
		for ( size_t langId = 0; langId < 30; ++langId )
		{
			const ::ScePlayGoLanguageMask langMask = ::scePlayGoConvertLanguage( langId );
			if ( (langMask & systemLanguageMask ) == 0 )
			{
				// Language is not installed at all, skip
				continue;
			}

			const red::String languageCodeName = red::GetLanguageCodeNameFromSystemId( langId );
			if ( languageCodeName.Empty() )
			{
				// Language is not supported by us, skip
				continue;
			}

			const auto langMapID = ToLanguageID( languageCodeName );

			auto chunks = languageMap.GetChunksForLanguage( langMapID );
			if ( chunks.Empty() )
			{
				// No chunks in this SKU, skip
				continue;
			}

			Int32 systemErrorCode;
			auto loci = playgo::GetLocusForChunks( playgoHandle, chunks, systemErrorCode );
			if ( systemErrorCode != SCE_OK )
			{
				continue;
			}

			auto IsLocusInstalled = []( ::ScePlayGoLocus locus )
			{
				return locus == SCE_PLAYGO_LOCUS_LOCAL_FAST || locus == SCE_PLAYGO_LOCUS_LOCAL_SLOW;
			};

			if ( red::alg::AllOf( loci, IsLocusInstalled ) )
			{
				auto smallestLanguage = red::ConvertToSmallestSupportedLanguageCode( languageCodeName );
				if ( languageCodeName == "pt-br" )
				{
					smallestLanguage = languageCodeName;
				}
				if ( !smallestLanguage.Empty() )
				{
					result.PushBack( smallestLanguage );
				}
			}
		}

		// If we got here and we've found no installed languages that we support then just create a list of just English
		// Doing this here because during development we will still want to load all archives that we find which will be blocked if we move it outside the if block
		if ( !languageMap.IsLoaded() && result.Empty() )
		{
			result.PushBack( "en" );
		}

		RED_FATAL_ASSERT( !result.Empty() );
	}

	return result;
}

Bool RefreshInstalledLanguages( prv::ArchiveLanguageMap &languageMap )
{
	playgo::ModuleHandle playgoModule;
	playgo::System playgoSystem;
	playgo::Handle playgoHandle;

	if ( playgoModule.Load() && playgoSystem.Initialise() && playgoHandle.Open() )
	{
		red::DynArray< Uint16 > chunks{ red::PoolEngine() };

		auto &textEntries = languageMap.GetTextEntries();
		auto &voiceEntries = languageMap.GetVoiceEntries();

		chunks.Reserve( textEntries.Size() + voiceEntries.Size() );

		for ( const auto &entry : textEntries )
		{
			chunks.PushBack( entry.chunkID );
		}

		for ( const auto &entry : voiceEntries )
		{
			chunks.PushBack( entry.chunkID );
		}

		red::alg::RemoveDuplicates( chunks );

		if ( chunks.Empty() )
		{
			return true;
		}

		Int32 systemErrorCode;

		const auto loci = playgo::GetLocusForChunks( playgoHandle, chunks, systemErrorCode );
		if ( systemErrorCode != SCE_OK )
		{
			return false;
		}

		const Uint32 size = chunks.Size();

		for ( Uint32 i = 0; i < size; ++i )
		{
			const Uint16 &chunkID = chunks[ i ];
			const ::ScePlayGoLocus locus = loci[ i ];
			const LanguagePackStatus status =
				locus == SCE_PLAYGO_LOCUS_LOCAL_FAST
				? LanguagePackStatus::Installed
				: LanguagePackStatus::NotInstalled;

			for ( auto &entry : textEntries )
			{
				if ( entry.chunkID == chunkID )
				{
					entry.status = status;
				}
			}

			for ( auto &entry : voiceEntries )
			{
				if ( entry.chunkID == chunkID )
				{
					entry.status = status;
				}
			}
		}

		return true;
	}

	return false;
}

} // prv
} // res

#else
RED_NO_EMPTY_FILE();
#endif // RED_PLATFORM_ORBIS
