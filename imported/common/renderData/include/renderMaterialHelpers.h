/**
* Copyright (c) 2011 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "renderStageContext.h"

RED_FORCE_INLINE static Uint64 CalculateMaterialHash( const Uint32 fileNameHash, const Uint32 contextId )
{
	Uint64 hash = fileNameHash;
	hash = hash << 32;
	return hash | contextId;
}

RED_FORCE_INLINE static Uint32 CalculateTechniqueHash( const SRenderStageContext& context, const Uint32 techniqueIndex, const CName& stagePassName, const Uint8 passOrderIndex )
{
	const Uint32 contextID	= context.CalcID();
	Uint32 hash				= red::CalculateHash32( &contextID, sizeof( contextID ) );
	hash					= red::CalculateHash32( &techniqueIndex, sizeof( techniqueIndex ), hash );
	hash					= red::CalculateHash32( stagePassName.AsChar(), hash );
	hash					= red::CalculateHash32( &passOrderIndex, sizeof( passOrderIndex ), hash );
	return hash;
}

struct CompiledTechniqueDesc
{
	RED_USE_MEMORY_POOL( rend::PoolMaterialTechniques );

	CName				stagePassName;
	Uint32				techniqueIndex;
	SRenderStageContext	renderStageContext;
	Uint8				passOrderIndex;

	Bool				isFallback;

	red::String ToString() const
	{
		return String::Printf( rend::PoolMaterialTechniques(), "CompiledTechnique [Index: %i, Pass '%hs', PassIndex: %u, Fallback: %d, %hs", (Int32)techniqueIndex, stagePassName.AsChar(), passOrderIndex, isFallback, renderStageContext.ToString().AsChar() );
	}

	red::String ToShortString() const
	{
		return String::Printf(rend::PoolMaterialTechniques(), "technique%i_pass%u_%hs_%hs", (Int32)techniqueIndex, passOrderIndex, stagePassName.AsChar(), renderStageContext.ToShortString().AsChar() );
	}

	RED_INLINE Uint32 GetHash() const
	{
		return CalculateTechniqueHash( renderStageContext, techniqueIndex, stagePassName, passOrderIndex );
	}
};

enum EShaderCompilerStatus
{
	SHADER_COMPILER_STATUS_SUCCESS							= 0,
	SHADER_COMPILER_STATUS_STATUS_COMPILATION_FAILED		= 1,
	SHADER_COMPILER_STATUS_STATUS_EMPTY_MATERIAL_LIST		= 2,
	SHADER_COMPILER_STATUS_COMMANDLINE_ERROR				= 3,
	SHADER_COMPILER_STATUS_DEPOT_INIT_FAILURE				= 4,
	SHADER_COMPILER_STATUS_STATUS_CODE_VALIDATION_FAILED	= 5,
};
