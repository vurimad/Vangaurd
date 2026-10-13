/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "renderMeshCompilationSource.h"
#include "renderBackendApi.h"

enum ECookingPlatform : Uint8;

//**************************************************************************************************************************//
//**************************************************************************************************************************//

namespace tools
{
	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 

	class IRenderMeshCompilerProcess
	{
	public:
		virtual void ProcessIndices( red::ArraySpan<Uint16> indices, Uint32 numVertices ) = 0;
	};

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 

	class RENDERBACKEND_API RenderMeshBlobCompiler
	{
	public:
		struct Settings
		{
			ECookingPlatform m_platform;
		};

		RenderMeshBlobCompiler( Settings& settings );
		~RenderMeshBlobCompiler();

		Settings m_settings;

		// Append ? Insert ? Update ? Remove ? blob segment data

		// Compile
		Bool Compile( const IMeshCompilationSource& meshSource, IRenderMeshCompilerProcess* process, THandle< IRenderResourceBlob > & outBlob );
	};

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 

}//tools

//**************************************************************************************************************************//
//**************************************************************************************************************************//
