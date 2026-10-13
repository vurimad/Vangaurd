/*
* Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "backendResourceDepot.h"

namespace res
{

bool IBackendResourceDepot::IsLanguageVoicePackAvailable( const CName languageCodeName ) const
{
	return true;
}

bool IBackendResourceDepot::IsLanguageVoicePackInstalled( const CName languageCodeName ) const
{
	return true;
}

red::AbsolutePath IBackendResourceDepot::ResolveWritableResourcePath( const res::ResourcePath resourcePath ) const
{
	return TranslateResourcePath( resourcePath );
}


#if defined(RED_PLATFORM_ORBIS) || defined(RED_PLATFORM_DURANGO)

Uint16 IBackendResourceDepot::GetChunkForVoicePack( res::LanguageID languageID )
{
	return 0;
}

LanguagePackStatus IBackendResourceDepot::GetVoicePackStatus( res::LanguageID languageID )
{
	return LanguagePackStatus::Invalid;
}

void IBackendResourceDepot::ChangeChunksStatus( const red::DynArray< Uint16 >& chunks, LanguagePackStatus status )
{
}

bool IBackendResourceDepot::HaveArchiveMap() const
{
	return false;
}

bool IBackendResourceDepot::IsLanguageChunkID( Uint16 chunkID ) const
{
	return false;
}

res::prv::ArchiveLanguageMap* IBackendResourceDepot::GetLanguageMap()
{
	return nullptr;
}

LanguageID IBackendResourceDepot::GetSystemLanguageID() const
{
	return LanguageID::EN;
}

#endif

} // res
