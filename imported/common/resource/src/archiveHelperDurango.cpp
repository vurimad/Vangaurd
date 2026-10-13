/*
 * Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "archiveHelperDurango.h"

#ifdef RED_PLATFORM_DURANGO

#include "../../redCore/include/languageRegion.h"

#include <collection.h>
#include "archiveLanguageMap.h"

namespace res
{
namespace prv
{

static red::String ConvertXboxLanguage( Platform::String^ language )
{
	char codeName[LOCALE_NAME_MAX_LENGTH];
	red::WideCharToStdChar( codeName, language->Data(), LOCALE_NAME_MAX_LENGTH );
	return red::SanitiseLocale( codeName );
}

red::DynArray< red::String > GetInstalledLanguages( prv::ArchiveLanguageMap &languageMap )
{
	Windows::Xbox::Management::Deployment::PackageTransferManager^ packageManager = Windows::Xbox::Management::Deployment::PackageTransferManager::Current;
	Windows::Foundation::Collections::IVector< Platform::String^ >^ languages = packageManager->AvailableChunkSpecifiers->Languages;

	red::DynArray< red::String > result{ red::PoolEngine() };
	result.Reserve( languages->Size );

	for ( Windows::Foundation::Collections::IIterator< Platform::String^ >^ iter = languages->First(); iter->HasCurrent; iter->MoveNext() )
	{
		Platform::String^ language = iter->Current;

		Windows::Xbox::Management::Deployment::ChunkSpecifiers^ specifier = ref new Windows::Xbox::Management::Deployment::ChunkSpecifiers;
		specifier->Languages->Append( language );

		if ( packageManager->GetInstallationState( specifier ) == Windows::Xbox::Management::Deployment::InstallationState::Installed )
		{
			auto langStr = ConvertXboxLanguage( language );
			auto smallestLanguage = red::ConvertToSmallestSupportedLanguageCode( langStr );

			if (langStr == "pt-br")
			{
				smallestLanguage = langStr;
			}

			if ( !smallestLanguage.Empty() )
			{
				result.PushBack( smallestLanguage );
			}
		}
	}

	return result;
}

Bool RefreshInstalledLanguages( prv::ArchiveLanguageMap &languageMap )
{
	Windows::Xbox::Management::Deployment::PackageTransferManager^ packageManager = Windows::Xbox::Management::Deployment::PackageTransferManager::Current;

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

	const Uint32 size = chunks.Size();

	for ( Uint32 i = 0; i < size; ++i )
	{
		const Uint16 &chunkID = chunks[ i ];

		Platform::Collections::Vector< UINT32 >^ v = ref new Platform::Collections::Vector< UINT32 >();
		v->Append( chunkID );

		const LanguagePackStatus status =
			packageManager->GetInstallationState( v ) == Windows::Xbox::Management::Deployment::InstallationState::Installed
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

} // prv
} // res

#else

RED_NO_EMPTY_FILE();

#endif // RED_PLATFORM_DURANGO
