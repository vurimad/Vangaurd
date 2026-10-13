/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "renderParticleCompiler.h"

#include "../../renderData/include/renderParticleBlob.h"
#include "../../redFileSystem/include/fileSys.h"

#include "../../shaderCache/include/shaderCache.h"
#include "../../shaderCache/include/shaderCacheCompilationUtils.h"

#ifdef RED_PLATFORM_WINPC
// requesting runtime shader recompilation
#include "../../redCore/include/processRunner.h"
#endif // RED_PLATFORM_WINPC
#include "../../redReflection/include/util.h"
#include "../../renderData/include/renderSettingsVars.h"

#ifndef RED_PLATFORM_CONSOLE

using red::HashMap;
using red::Utf16String;

namespace tools
{
	const auto PathBufferLen = 512;


	//////////////////////////////////////////////////////////////////////////////////////////////////////

	/*----------------------------------------------------------------------------------------------------------
		RenderParticleBlobCompiler
	----------------------------------------------------------------------------------------------------------*/

	RenderParticleBlobCompiler::RenderParticleBlobCompiler( Settings& settings )
		: m_settings( settings )
	{
	}

	RenderParticleBlobCompiler::~RenderParticleBlobCompiler()
	{
	}

	Bool RenderParticleBlobCompiler::CompileSimulationShaders( const IParticleCompilationSource& src, rend::RenderParticleBlob& outBlob )
	{
		Utf16String cookingPlatformStr( L"pc" );
		switch( m_settings.m_platform )
		{
		case PLATFORM_XboxOne:
			cookingPlatformStr = Utf16String( L"xboxone" );
			break;
		case PLATFORM_PS4:
			cookingPlatformStr = Utf16String( L"ps4" );
			break;
		default: break;
		}

		const red::AbsolutePath& basePath = GFileManager->GetEngineRoot();
		const red::AbsolutePath& tempPath = red::paths::GetTempDirectory();

		const red::AbsolutePath& rootPath = basePath.AddDirPath( ".." );

		red::AbsolutePath appPath;
		const Bool useLocalCompiler = Config::cvShaderCompilerUseLocalCompiler.Get();
		if ( !GetShaderCompilerToolPath( useLocalCompiler, rootPath, basePath, PARTICLE_SIMULATION_COMPILER_FILE_NAME, appPath ) )
		{
			RED_LOG_ERROR( "ParticleEmitterCompiler: Couldn't find compiler '%hs'.", appPath.AsChar() );
			return false;
		}
		
		Char arguments[ PathBufferLen ];
		red::SNPrintFUnsafe( &arguments[0], PathBufferLen, TXT("-verbose -platform=%ls -engineroot=\"%hs\" -out=\"%hs\" -initSetMask=%llu -modifSetMask=%llu -simMask=%lu -collMask=%u -noiseType=%u"),
			cookingPlatformStr.AsChar(),
			basePath.AsChar(),
			tempPath.AsChar(),
			src.GetInitializerSetMask(),
			src.GetModifierSetMask(),
			src.GetSimulationHash(),
			outBlob.m_updaterData.m_collisionEffectMask,
			outBlob.m_updaterData.m_noiseType );

		Uint32 timeout = 1000 * 60 * 4; // 4 min

		CProcessRunner runner;
		if ( !runner.Run( appPath, Utf16String( arguments ), red::utils::GetCurrentWorkingDirectory() ) || !runner.WaitForFinish( timeout ) || runner.GetExitCode() != 0 )
		{
			RED_LOG_ERROR( "ParticleEmitterCompiler: Shader compiler error: %hs", runner.GetFullCommandLine().AsChar() );

			String compilerOutput;
			runner.LogOutput( CProcessRunner::ELT_String, "", &compilerOutput );

			return false;
		}

		DataBuffer& emitCS = outBlob.m_gpuSimShaders.m_simCS[ESP_Emit];
		DataBuffer& updateCS = outBlob.m_gpuSimShaders.m_simCS[ESP_Update];

		// Set emit compute shader as a data buffer inside emitter resource
		const red::AbsolutePath emitFilePath = tempPath.AddFilePath( "emit.dat" );
		auto emitFile = GFileManager->CreateFileReader( emitFilePath, FOF_AbsolutePath );
		if( emitFile )
		{
			emitCS = DataBuffer( static_cast< Uint32 >( emitFile->GetSize() ) );
			emitFile->Serialize( emitCS.Data(), emitFile->GetSize() );
		}
		else
		{
			RED_LOG_ERROR( "ParticleEmitterCompiler: Couldn't find 'emit.dat' intermediate simulation compilation file." );
			return false;
		}

		// Set emit compute shader as a data buffer inside emitter resource
		const red::AbsolutePath updateFilePath = tempPath.AddFilePath( "update.dat" );
		auto updateFile = GFileManager->CreateFileReader( updateFilePath, FOF_AbsolutePath );
		if( updateFile )
		{
			updateCS = DataBuffer( static_cast< Uint32 >( updateFile->GetSize() ) );
			updateFile->Serialize( updateCS.Data(), updateFile->GetSize() );
		}
		else
		{
			// Clear emit shader buffer. Emit and update shaders should both be compiled.
			emitCS.Clear();

			RED_LOG_ERROR( "ParticleEmitterCompiler: Couldn't find 'update.dat' intermediate simulation compilation file." );
			return false;
		}

		return true;
	}

	Bool RenderParticleBlobCompiler::Compile( const IParticleCompilationSource& src, THandle<IRenderResourceBlob> & outBlob ) // in/out blob
	{
		rend::RenderParticleBlob blob;

		Uint32 lodCount = src.GetLODCount();
		if( lodCount == 0 )
		{
			RED_LOG_WARNING( "ParticleEmitterCompiler: Particle emitter has 0 LODs." );
			return false;
		}

		EParticleVertexDrawerType drawerType = src.GetVertexDrawerType();
		if( drawerType == PVDT_Invalid )
		{
			RED_LOG_WARNING( "ParticleEmitterCompiler: Invalid drawer type." );
			return false;
		}

		// ------ Set header ------
		rend::RenderParticleBlob::Header& header = blob.m_header;
		// version
		header.m_version = rend::ParticleFileVersion::CURRENT;
		header.m_emitterInfo.m_simulationType = src.IsGPUEmitter() ? PEST_GPU : PEST_CPU;
		header.m_emitterInfo.m_eventSetMask = src.GetEventSetMask();
		header.m_emitterInfo.m_numModifiers = src.GetNumModifiers();
		header.m_emitterInfo.m_numInitializers = src.GetNumInitializers();
		header.m_emitterInfo.m_modifierSetMask = src.GetModifierSetMask();
		header.m_emitterInfo.m_initializerSetMask = src.GetInitializerSetMask();
		header.m_emitterInfo.m_simulationHash = src.GetSimulationHash();
		header.m_emitterInfo.m_seeds = src.GetSeeds();
		header.m_emitterInfo.m_emitterLoops = src.GetEmitterLoops();
		header.m_emitterInfo.m_maxParticles = src.GetMaxParticles();
		header.m_emitterInfo.m_windInfluence = src.GetWindInfluence();
		header.m_emitterInfo.m_internalPriority = src.GetInternalPriority();
		header.m_emitterInfo.m_envColorGroup = src.GetEnvColorGroup();
		header.m_emitterInfo.m_emitterGroup = src.GetEmitterGroup();
		header.m_emitterInfo.m_renderObjectType = src.GetRenderObjectType();
		header.m_emitterInfo.m_keepSimulationLocal = src.GetKeepSimulationLocal();
		header.m_emitterInfo.m_killOnCollision = src.GetKillOnCollision();
		header.m_emitterInfo.m_initialParticleCount = src.GetInitialParticleCount();
		header.m_emitterInfo.m_diffuseWrapFactor = src.GetDiffuseWrapFactor();
		header.m_emitterInfo.m_maskInsideCar = src.GetMaskInsideCar();
		header.m_emitterInfo.m_maskInsideInterior = src.GetMaskInsideInterior();
		header.m_emitterInfo.m_maskAboveWater = src.GetMaskAboveWater();
		header.m_emitterInfo.m_maskUnderWater = src.GetMaskUnderWater();
		header.m_emitterInfo.m_backLightingFactor = src.GetBackLightingFactor();
		header.m_emitterInfo.m_lightingMipBias = src.GetLightingMipBias();
		header.m_emitterInfo.m_useSubFrameEmission = src.GetUseSubFrameEmission();
		header.m_emitterInfo.m_particleType = src.GetParticleType();
		header.m_emitterInfo.m_volumetricParticleEnabled = src.GetVolumetricParticleEnabled();
		header.m_emitterInfo.m_volumetricParticleRelative = src.GetVolumetricParticleRelative();
		header.m_emitterInfo.m_volumetricParticleUseFogColor = src.GetVolumetricParticleUseFogColor();
		header.m_emitterInfo.m_volumetricParticleColor = src.GetVolumetricParticleColor();
		header.m_emitterInfo.m_volumetricParticleSize = src.GetVolumetricParticleSize();
		header.m_emitterInfo.m_volumetricParticleDensity = src.GetVolumetricParticleDensity();
		header.m_emitterInfo.m_volumetricParticleFalloff = src.GetVolumetricParticleFalloff();
		header.m_emitterInfo.m_volumetricParticleNoiseScale = src.GetVolumetricParticleNoiseScale();
		header.m_emitterInfo.m_volumetricParticleNoiseThreshold= src.GetVolumetricParticleNoiseThreshold();
		header.m_emitterInfo.m_volumetricParticleNoiseVelocity = src.GetVolumetricParticleNoiseVelocity();
		header.m_emitterInfo.m_collisionAngularVelocityDamp = src.GetCollisionAngularVelocityDamp();
		header.m_emitterInfo.m_vertexDrawerType = drawerType;

		if( src.GetNumInitializers() > 0 || src.GetNumModifiers() > 0 )
		{
			// Extract updater data
			src.ExtractUpdaterData( blob.m_updaterData );
		}

		// Extract lods and drawers
		src.ExtractLODs( header.m_emitterInfo.m_lods );

		// Compile simulation shader into blob if necessary
		if( src.IsGPUEmitter() )
		{
			if( !CompileSimulationShaders( src, blob ) )
			{
				RED_LOG_WARNING( "ParticleEmitterCompiler: Couldn't compile gpu simulation compute shaders." );
				return false;
			}
		}

		{
			outBlob.Reset( RED_NEW( rend::RenderParticleBlob )() );
			rend::RenderParticleBlob* particleBlob = static_cast< rend::RenderParticleBlob* >( outBlob.Get() );
			particleBlob->m_header = header;
			particleBlob->m_updaterData = std::move( blob.m_updaterData );
			particleBlob->m_gpuSimShaders = std::move( blob.m_gpuSimShaders ); // TODO: check?
		}

		return true;
	}

}//tools

#endif //! RED_PLATFORM_CONSOLE