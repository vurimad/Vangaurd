/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "renderMaterialSetupParam.h"


Uint64 CalculateMaterialSetupParametersHash( const SMaterialSetupParameters& params, Uint64 baseHash )
{
	Uint64 hash = baseHash;

	// These will build up a hash from the parameter values and names.
	//
	// Names should be fine, since CName contains exactly the info that uniquely identifies it (at runtime).
	//
	// For any RenderResource parameters, we just use the pointer as a hash value. In some cases we might be able to do
	// a little better by hashing the data itself (maybe two different resources have the same data), but in that case
	// we could end up with a duplicated RenderResource in memory (create some surfaces with a resource; unload that
	// resource from engine side, but it's still alive in renderer for a short time longer while the object waits to be
	// removed from a scene; reload the resource, create a new surface with it, but we'd pick up a surface using the old
	// resource).
	hash = red::CalculateHash64( params.m_scalarParameters.Data(), params.m_scalarParameters.DataSize(), hash );
	hash = red::CalculateHash64( params.m_vectorParameters.Data(), params.m_vectorParameters.DataSize(), hash );
	hash = red::CalculateHash64( params.m_colorParameters.Data(), params.m_colorParameters.DataSize(), hash );
	hash = red::CalculateHash64( params.m_nameU64Parameters.Data(), params.m_nameU64Parameters.DataSize(), hash );
	hash = red::CalculateHash64( params.m_textureParameters.Data(), params.m_textureParameters.DataSize(), hash );
	hash = red::CalculateHash64( params.m_specialParameters.Data(), params.m_specialParameters.DataSize(), hash );
	return hash;
}


SMaterialSetupEntry::SMaterialSetupEntry()
	: m_materialResource( nullptr )
	, m_isMasked( false )
	, m_isMultilayered( false )
{
}

SMaterialSetupEntry::~SMaterialSetupEntry()
{
}

Uint64 SMaterialSetupEntry::CalculateHash() const
{
	Uint64 hash = m_cachedHash;
	if ( hash == 0 )
	{
		hash = RED_FNV_OFFSET_BASIS64;
		hash = red::CalculateHash64( &m_isMasked, sizeof( m_isMasked ), hash );
		for ( Uint32 shader_i = MSH_First; shader_i < MSH_MAX; ++shader_i )
		{
			const SMaterialSetupParameters& params = m_parameters[shader_i];
			hash = CalculateMaterialSetupParametersHash( params, hash );
		}

		IRenderResource* materialPtr = m_materialResource.Get();
		hash = red::CalculateHash64( &materialPtr, sizeof( materialPtr ), hash );

		m_cachedHash = hash;
	}
	return hash;
}
