/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "multilayerLayerTemplate.h"
#include "multilayerSetup.h"
#include "terrainSetup.h"


#include "../../redReflection/include/serializationSaver.h"
#include "../../resource/include/jsonSerializer.h"

class Multilayer_SetupResourceSaver final : public serialization::ISaver
{
public:
	virtual Bool SaveObjects( IFile& file, const serialization::SavingContext& context ) const override
	{
		RED_FATAL_ASSERT( !context.m_initialExports.Empty() );

		serialization::json::JsonSerializerContext jsonContext;
		serialization::json::JsonSerializer jsonSerializer( context.m_initialExports[0] );

		jsonContext.m_maxFloatDecimalPlaces = 2;
		jsonContext.m_indentChar = '\t';
		jsonContext.m_indentCharCount = 1;

		String resultString;
		if ( !jsonSerializer.Serialize( resultString, jsonContext ) )
		{
			return false;
		}

		file.Serialize( resultString.Data(), resultString.Length() );

		return true;
	}
};

class TerrainSetupResourceSaver final : public serialization::ISaver
{
public:
	virtual Bool SaveObjects( IFile& file, const serialization::SavingContext& context ) const override
	{
		RED_FATAL_ASSERT( !context.m_initialExports.Empty() );

		serialization::json::JsonSerializerContext jsonContext;
		serialization::json::JsonSerializer jsonSerializer( context.m_initialExports[0] );

		jsonContext.m_maxFloatDecimalPlaces = 7;                    // We need more precision for UV tiling params.
		jsonContext.m_indentChar = '\t';
		jsonContext.m_indentCharCount = 1;

		String resultString;
		if ( !jsonSerializer.Serialize( resultString, jsonContext ) )
		{
			return false;
		}

		file.Serialize( resultString.Data(), resultString.Length() );

		return true;
	}
};

void RegisterMultilayerResourceSavers()
{
	serialization::ISaver::RegisterCustomFactory< Multilayer_SetupResourceSaver >( Multilayer_Setup::GetFileExtension() );
	serialization::ISaver::RegisterCustomFactory< TerrainSetupResourceSaver >( CTerrainSetup::GetFileExtension() );
}
