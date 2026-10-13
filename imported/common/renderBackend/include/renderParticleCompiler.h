/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "renderParticleCompilationSource.h"
#include "renderBackendApi.h"

enum ECookingPlatform : Uint8;

namespace rend
{
	class RenderParticleBlob;
}

namespace tools
{
	class RENDERBACKEND_API RenderParticleBlobCompiler
	{
	public:
		struct Settings
		{
			ECookingPlatform	m_platform;
		};

		RenderParticleBlobCompiler( Settings& settings );
		~RenderParticleBlobCompiler();

		Settings m_settings;

		// Append ? Insert ? Update ? Remove ? blob segment data

		// Compile
		Bool Compile( const IParticleCompilationSource& textureSource, THandle<IRenderResourceBlob> & outBlob );

		Bool CompileSimulationShaders( const IParticleCompilationSource& src, rend::RenderParticleBlob& outBlob );
	};
}//tools