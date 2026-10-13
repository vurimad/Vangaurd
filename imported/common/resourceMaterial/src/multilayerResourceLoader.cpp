/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "multilayerLayerTemplate.h"
#include "multilayerSetup.h"
#include "terrainSetup.h"
#include "../../redReflection/include/serializationLoader.h"
#include "../../resource/include/jsonGenericLoader.h"
#include "../../resource/include/jsonFormatResourceLoader.h"

class Multilayer_SetupResourceLoader final : public json::JsonFormatResourceLoader< Multilayer_Setup >
{
};

class Multilayer_LayerTemplateResourceLoader final : public json::JsonFormatResourceLoader< Multilayer_LayerTemplate >
{
};

class TerrainSetup_SetupResourceLoader final : public json::JsonFormatResourceLoader< CTerrainSetup >
{
};


void RegisterMultilayerResourceLoaders()
{
	serialization::ILoader::RegisterCustomFactory< Multilayer_SetupResourceLoader >( Multilayer_Setup::GetFileExtension() );
	if ( red::Strcmp(Multilayer_Setup::GetFileExtension(), Multilayer_Setup::GetDeprecatedFileExtension()) != 0 )
	{
		serialization::ILoader::RegisterCustomFactory< Multilayer_SetupResourceLoader >( Multilayer_Setup::GetDeprecatedFileExtension() );
	}

	serialization::ILoader::RegisterCustomFactory< Multilayer_LayerTemplateResourceLoader >( Multilayer_LayerTemplate::GetFileExtension() );
	if( red::Strcmp( Multilayer_LayerTemplate::GetFileExtension(), Multilayer_LayerTemplate::GetDeprecatedFileExtension() ) != 0 )
	{
		serialization::ILoader::RegisterCustomFactory< Multilayer_LayerTemplateResourceLoader >( Multilayer_LayerTemplate::GetDeprecatedFileExtension() );
	}

	serialization::ILoader::RegisterCustomFactory< TerrainSetup_SetupResourceLoader >( CTerrainSetup::GetFileExtension() );
}
