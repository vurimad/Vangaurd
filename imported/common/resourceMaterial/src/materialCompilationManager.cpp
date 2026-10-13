/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "materialCompilationManager.h"

#include "materialParameter.h"

#include "../../redCore/include/processRunner.h"

#include "../../redFileSystem/include/fileSkipableBlock.h"
#include "../../redReflection/include/variant.h"

#include "../../renderData/include/renderEngineHandler.h"
#include "../../renderData/include/renderMaterialHelpers.h"
#include "../../renderData/include/renderer.h"

#include "../../shaderCache/include/shaderCacheCompilationUtils.h"
#include "../../shaderCache/include/shaderCacheManager.h"

#include "../../redJobs2/include/jobRunner.h"
#include "../../renderData/include/renderSettingsVars.h"

#ifdef NO_RUNTIME_MATERIAL_COMPILATION
RED_NO_EMPTY_FILE();
#endif // NO_RUNTIME_MATERIAL_COMPILATION

#ifndef NO_RUNTIME_MATERIAL_COMPILATION

using red::DynArray;

namespace
{
	static MaterialCompilationManager* s_materialCompilationManager = nullptr;
}

//////////////////////////////////////////////////////////////////////////

MaterialCompilationManager::MaterialCompilationManager()
	: m_isRunning( false )
	, m_forceUnroll( false )
	, m_currentBatchCounter( 0 )
{
}

MaterialCompilationManager::~MaterialCompilationManager()
{
}

void MaterialCompilationManager::Initialize()
{
	RED_ASSERT( !s_materialCompilationManager, "Multiple MaterialCompilationManager initialization" );
	s_materialCompilationManager = this;

	m_shaderCachePath = GShaderCache->GetPath();
}

red::AbsolutePath GenerateLowPriorityCachePath( const THandle< CMaterialTemplate >& material )
{
	return red::paths::GetTempDirectory().AddFilePath( String( material->GetName().AsChar() ) + "_lowpriority.cache" );
}

red::AbsolutePath GenerateHighPriorityCachePath(const THandle< CMaterialTemplate >& material)
{
	return red::paths::GetTempDirectory().AddFilePath( String( material->GetName().AsChar() ) + "_highpriority.cache" );
}

void MaterialCompilationManager::Register( const THandle< CMaterialTemplate >& material, Bool onlyHighPriority )
{
	const res::ResourcePath& resourcePath = material->GetPath();
	if ( m_asyncMaterialsCollection_HighPriority.KeyExist( resourcePath ) || m_asyncMaterialsCollection_LowPriority.KeyExist( resourcePath ) )
	{
		return;
	}

	red::DynArray< Uint32 > techniques { red::PoolFrame() };
	GRender->ReadUsedMaterialTechniques( material->GetRenderResource(), techniques );

	AsyncMaterialCompilationEntry highPriorityEntry;
	highPriorityEntry.m_material			= material;
	highPriorityEntry.m_cachePath			= GenerateHighPriorityCachePath( material );
	highPriorityEntry.m_state				= AsyncMaterialCompilationEntry::EAMCS_Pending;
	highPriorityEntry.m_techniques			= techniques;
	
	RED_VERIFY( m_asyncMaterialsCollection_HighPriority.Insert( resourcePath, highPriorityEntry ).IsSuccessful(), "Failed to register HIGH PRIORITY material '%hs' for async recompilation", resourcePath.ToDebugString() );
	
	if (!onlyHighPriority)
	{
		AsyncMaterialCompilationEntry lowPriorityEntry;
		lowPriorityEntry.m_material = material;
		lowPriorityEntry.m_cachePath = GenerateLowPriorityCachePath( material );
		lowPriorityEntry.m_state = AsyncMaterialCompilationEntry::EAMCS_Pending;
		lowPriorityEntry.m_techniques = techniques;

		RED_VERIFY( m_asyncMaterialsCollection_LowPriority.Insert( resourcePath, lowPriorityEntry ).IsSuccessful(), "Failed to register LOW PRIORITY material '%hs' for async recompilation", resourcePath.ToDebugString() );
	}
}

void MaterialCompilationManager::Run( const Bool includesMatch )
{
	if ( m_isRunning )
	{
		// wait for the current list to finish
		return;
	}

	if ( m_asyncMaterialsCollection_HighPriority.Empty() && m_asyncMaterialsCollection_LowPriority.Empty() )
	{
		RED_LOG( "Materials Compilation: Nothing to recompile, all shaders are up to date." );
		return;
	}

	RED_LOG( "Materials Compilation, Recompiling following materials:" );
	for ( const auto& asyncMaterial : m_asyncMaterialsCollection_HighPriority )
	{
		RED_LOG( "   [HIGH PRIORITY] * '%hs'", asyncMaterial.Key().ToDebugString() );
	}
	for ( const auto& asyncMaterial : m_asyncMaterialsCollection_LowPriority )
	{
		RED_LOG( "   [LOW PRIORITY] * '%hs'", asyncMaterial.Key().ToDebugString() );
	}

	m_forceUnroll = !includesMatch;
	m_isRunning = true;

	red::Clock::GetInstance().GetLocalTime( m_startTime );

	if ( PrepareNextBatch() )
	{
		RunPreparedBatch();
	}
}

Bool MaterialCompilationManager::PrepareNextBatch()
{
	Uint8 scheduled = 0;

	RED_ASSERT( m_currentBatchCounter.GetValue() == 0, "MaterialCompilationManager: Preparing next batch when previous did not finish (counter: %d)", m_currentBatchCounter.GetValue() );

	// TODO: use heuristic to schedule wide range of materials per batch
	// Schedule high priority first
	for ( auto asyncMaterial : m_asyncMaterialsCollection_HighPriority )
	{
		AsyncMaterialCompilationEntry& entry = asyncMaterial.Value();
		if ( entry.m_state == AsyncMaterialCompilationEntry::EAMCS_Pending )
		{
			entry.m_state = AsyncMaterialCompilationEntry::EAMCS_Scheduled;
			++scheduled;
			if ( scheduled == optimalBatchSize )
			{
				break;
			}
		}
	}

	// do not schedule low priority in the same batch as high priority - we want them to finish FAST
	if ( scheduled == 0 )
	{
		// TODO: use heuristic to schedule wide range of materials per batch
		for ( auto asyncMaterial : m_asyncMaterialsCollection_LowPriority )
		{
			AsyncMaterialCompilationEntry& entry = asyncMaterial.Value();
			if (entry.m_state == AsyncMaterialCompilationEntry::EAMCS_Pending)
			{
				entry.m_state = AsyncMaterialCompilationEntry::EAMCS_Scheduled;
				++scheduled;
				if ( scheduled == optimalBatchSize )
				{
					break;
				}
			}
		}
	}

	return scheduled > 0;
}

struct ResourceScheduledForCompilation
{
	res::ResourcePath	resourcePath;
	Bool				isHighPriority;
};

void MaterialCompilationManager::RunPreparedBatch()
{
	RED_ASSERT( m_currentBatchCounter.GetValue() == 0, "MaterialCompilationManager: Running next batch when previous did not finish (counter: %d)", m_currentBatchCounter.GetValue() );

	red::StaticArray< ResourceScheduledForCompilation, optimalBatchSize > resourcesToRun;
	for ( auto asyncMaterial : m_asyncMaterialsCollection_HighPriority )
	{
		if ( asyncMaterial.Value().m_state == AsyncMaterialCompilationEntry::EAMCS_Scheduled )
		{
			resourcesToRun.PushBack( { asyncMaterial.Key(), true } );
		}
	}
	for ( auto asyncMaterial : m_asyncMaterialsCollection_LowPriority )
	{
		if ( asyncMaterial.Value().m_state == AsyncMaterialCompilationEntry::EAMCS_Scheduled )
		{
			resourcesToRun.PushBack( { asyncMaterial.Key(), false } );
		}
	}

	m_currentBatchCounter.SetValue( resourcesToRun.Size() );

	job::Counter noWait{ job::Priority::Latent };
	job::Counter noAccum{ job::Priority::Latent };
	job::DispatchParallelForJob< rend::PoolShaderCompilation >(
		"Materials/Async/MaterialCompilation",
		noWait, noAccum,
		{ resourcesToRun.Size() },
		[ this, resourcesToRun ]( Uint32 index, const job::RunContext& )
	{
		const ResourceScheduledForCompilation& resourceForCompilation = resourcesToRun[ index ];
		const res::ResourcePath& resourcePath = resourceForCompilation.resourcePath;

		AsyncMaterialCompilationEntry& entry = resourceForCompilation.isHighPriority ? m_asyncMaterialsCollection_HighPriority[ resourcePath ] : m_asyncMaterialsCollection_LowPriority[ resourcePath ];

		entry.m_state = AsyncMaterialCompilationEntry::EAMCS_Compiling;

		if ( GFileManager->FileExist( entry.m_cachePath ) )
		{
			GFileManager->DeleteFile( entry.m_cachePath );
		}

		red::AbsolutePath rootPath = GFileManager->GetEngineRoot().AddDirPath( ".." );
		const red::AbsolutePath& engineRootPath = GFileManager->GetEngineRoot();
		const red::AbsolutePath& gameRootPath = GFileManager->GetGameRoot();
		
		const Bool useLocalCompiler = Config::cvShaderCompilerUseLocalCompiler.Get();

		red::AbsolutePath appPath;
		if ( !GetShaderCompilerToolPath( useLocalCompiler, rootPath, engineRootPath, SHADER_COMPILER_FILE_NAME, appPath ) )
		{
			entry.m_isValid = false;
			entry.m_state = AsyncMaterialCompilationEntry::EAMCS_Compiled;
			m_currentBatchCounter.Decrement();
			RED_ASSERT( m_currentBatchCounter.GetValue() >= 0 );
			return;
		}

		String forceShaderCodeUnrollString;
		RED_VERIFY( ToString( forceShaderCodeUnrollString, m_forceUnroll ) );

		red::StringBuilder< String > techniques;
		if ( resourceForCompilation.isHighPriority )
		{
			for ( const Uint32 technique : entry.m_techniques )
			{
				techniques.Appendf( "-techniqueToCompile=%u ", technique );
			}
		}
		else
		{
			for ( const Uint32 technique : entry.m_techniques )
			{
				techniques.Appendf( "-techniqueToExclude=%u ", technique );
			}
		}

		const String arguments = String::Printf( "-platform=pc -breakOnError -root=\"%hs\" -engineRoot=\"%hs\" -gameRoot=\"%hs\" -forceShaderCodeUnroll=%hs -dumpAssembly=%hs -optimize=%hs %hs -material=\"%hs\" -outputCachePath=\"%hs\" -verifyOnFlush=false %hs",
									rootPath.AsChar(),
									engineRootPath.AsChar(),
									gameRootPath.AsChar(),
									forceShaderCodeUnrollString.AsChar(),
									Config::cvShaderCompilerDumpAssembly.Get() ? "true" : "false",
									Config::cvShaderCompilerOptimize.Get() ? "true" : "false",
									Config::cvShaderCompilerForceRecompile.Get() ? "-force" : "",
									resourcePath.ToString().AsChar(),
									entry.m_cachePath.AsChar(),
									techniques.AsChar() );

		// #TODO: Drop this back down, once multilayer shaders are sane again? Terrain can cause a timeout at 5min, with
		// its three different ML versions in a single shader.
		const Uint32 timeout = 1000 * 60 * 10; // 5 min
		
		String compilerOutput;

		CProcessRunner runner;
		runner.Run( appPath, Utf16String( arguments.AsChar() ), red::utils::GetCurrentWorkingDirectory() );
		runner.WaitForFinish( timeout );
		runner.LogOutput( CProcessRunner::ELT_String, String::EMPTY(), &compilerOutput );
		red::DynArray< String > compilerOutputLines{ rend::PoolShaderCompilation() };
		compilerOutput.Slice( compilerOutputLines, "\r\n" );
		for ( const auto& line : compilerOutputLines )
		{
			if ( line.BeginsWith( "error:" ) )
			{
				RED_LOG_ERROR( line.AsChar() );
			}
			else
			{
				RED_LOG( line.AsChar() );
			}
		}
		runner.Terminate();

		entry.m_isValid = runner.GetExitCode() == EShaderCompilerStatus::SHADER_COMPILER_STATUS_SUCCESS;
		entry.m_state = AsyncMaterialCompilationEntry::EAMCS_Compiled;
		
		m_currentBatchCounter.Decrement();
		RED_ASSERT( m_currentBatchCounter.GetValue() >= 0, "MaterialCompilationManager: Too many material recompilation processes (counter: %d)", m_currentBatchCounter.GetValue() );
	});
}

void MaterialCompilationManager::FlushFinishedBatch()
{
	red::UniquePtr< IShaderCache > inputCache = IShaderCache::CreateReadWrite( m_shaderCachePath );
	for ( auto asyncMaterial : m_asyncMaterialsCollection_HighPriority )
	{
		if ( asyncMaterial.Value().m_state == AsyncMaterialCompilationEntry::EAMCS_Compiled )
		{
			MergeFinishedMaterial( asyncMaterial.Value(), inputCache.Get() );
		}
	}
	for ( auto asyncMaterial : m_asyncMaterialsCollection_LowPriority )
	{
		if ( asyncMaterial.Value().m_state == AsyncMaterialCompilationEntry::EAMCS_Compiled )
		{
			MergeFinishedMaterial(asyncMaterial.Value(), inputCache.Get());
		}
	}

	const Bool shouldReload = inputCache->IsDirty();
	inputCache->Flush();
	if ( shouldReload )
	{
		GShaderCache->Reload( m_shaderCachePath );
		while ( !GShaderCache->VerifyLoaded() )
		{
			RED_LOG( "MaterialCompilationManager: shader cache '%hs' reloading after recompilation..", GShaderCache->GetPath().AsChar() );
			red::SleepOnCurrentThread( 500 );
		}
	}

	for ( auto asyncMaterial : m_asyncMaterialsCollection_HighPriority )
	{
		if ( asyncMaterial.Value().m_state == AsyncMaterialCompilationEntry::EAMCS_Merged )
		{
			CleanupFinishedMaterial( asyncMaterial.Value() );
		}
	}
	for ( auto asyncMaterial : m_asyncMaterialsCollection_LowPriority )
	{
		if (asyncMaterial.Value().m_state == AsyncMaterialCompilationEntry::EAMCS_Merged)
		{
			CleanupFinishedMaterial(asyncMaterial.Value());
		}
	}

	GRender->GetEngineHandler()->ReloadMultilayerMaterial();
}

void MaterialCompilationManager::Tick( const Float engineTimeDelta, const tick::Info& tickInfo )
{
#if 0
	static Bool CYB_185377 = false;
	if ( !CYB_185377 )
	{
		job::Counter noWait{ job::Priority::Latent };
		job::Counter noAccum{ job::Priority::Latent };
		job::DispatchParallelForJob< rend::PoolShaderCompilation >(
			"Materials/Async/MaterialCompilation",
			noWait, noAccum, { 100 },
			[this]( Uint32 index, const job::RunContext& )
			{
				red::AbsolutePath scPath = GShaderCache->GetPath();
				ced::UniquePtr< IShaderCache > cache = IShaderCache::CreateReadWrite( scPath );
				cache->ForceDirty();
				cache->Flush();
			}
		);
		CYB_185377 = true;
	}
#endif // 0

	if ( !m_isRunning )
	{
		return;
	}

	static Float s_accumulatedTimeDelta = 0.f;
	s_accumulatedTimeDelta += engineTimeDelta;
	if ( s_accumulatedTimeDelta < 1.0f )
	{
		return;
	}
	s_accumulatedTimeDelta = 0.f;
	
	RED_ASSERT( m_currentBatchCounter.GetValue() >= 0, "MaterialCompilationManager: Invalid batch count: %d", m_currentBatchCounter.GetValue() );
	if ( m_currentBatchCounter.GetValue() > 0 )
	{
		return;
	}
	else
	{
		RED_ASSERT( m_currentBatchCounter.GetValue() == 0, "MaterialCompilationManager: Previous batch must finish before next starts (counter: %d)", m_currentBatchCounter.GetValue() );
		FlushFinishedBatch();
		if ( PrepareNextBatch() )
		{
			RunPreparedBatch();
			return;
		}
	}
	
	red::Clock::GetInstance().GetLocalTime( m_endTime );
	String sStartTime;
	ToString( sStartTime, m_startTime );
	String sEndTime;
	ToString( sEndTime, m_endTime );
	RED_LOG( "AsyncCompilationFinished, start: [%hs] end: [%hs]", sStartTime.AsChar(), sEndTime.AsChar() );

	m_asyncMaterialsCollection_HighPriority.Clear();
	m_asyncMaterialsCollection_LowPriority.Clear();

	m_isRunning = false;
}

void MaterialCompilationManager::Shutdown()
{
	m_asyncMaterialsCollection_HighPriority.Clear();
	m_asyncMaterialsCollection_LowPriority.Clear();

	m_isRunning = false;
}

Bool MaterialCompilationManager::MergeFinishedMaterial( AsyncMaterialCompilationEntry& entry, IShaderCache* cache )
{
	RED_ASSERT( entry.m_state == AsyncMaterialCompilationEntry::EAMCS_Compiled, "MaterialCompilationManager: Merge - Invalid entry state '%d' for '%hs'", entry.m_state, entry.m_cachePath.AsChar() );
	if ( entry.m_isValid )
	{
		if ( GFileManager->FileExist( entry.m_cachePath ) )
		{
			red::UniquePtr< IShaderCache > outputCache = IShaderCache::CreateReadOnly( entry.m_cachePath );
			// we can turn on integrity verification on adding each material entry, but it's a costly process, so leave that as a debug option
			const Bool verifyOnAdd = false;
			entry.m_isValid = cache->Merge( outputCache.Get(), nullptr, verifyOnAdd );
			RED_LOG( "MaterialCompilationManager: merging partial cache '%hs', result: %hs", entry.m_cachePath.AsChar(), entry.m_isValid ? "SUCCESS" : "FAILURE" );
		}
	}
	entry.m_state = AsyncMaterialCompilationEntry::EAMCS_Merged;
	return entry.m_isValid;
}

void MaterialCompilationManager::CleanupFinishedMaterial( AsyncMaterialCompilationEntry& entry )
{
	RED_ASSERT( entry.m_state == AsyncMaterialCompilationEntry::EAMCS_Merged, "MaterialCompilationManager: Cleanup - Invalid entry state '%d' for '%hs'", entry.m_state, entry.m_cachePath.AsChar() );
	entry.m_material->SetIsValid( entry.m_isValid );

	GFileManager->DeleteFileW( entry.m_cachePath );

#ifndef NO_RUNTIME_MATERIAL_COMPILATION
	entry.m_material->ForceRecompilation();
#endif // NO_RUNTIME_MATERIAL_COMPILATION

	entry.m_state = AsyncMaterialCompilationEntry::EAMCS_CleanedUp;
}

void MaterialCompilationManager::GetMaterialsBeingRecompiled( red::DynArray< MaterialRecompilationInfo >& materialsPaths ) const
{
	materialsPaths.Reserve( m_asyncMaterialsCollection_HighPriority.Size() + m_asyncMaterialsCollection_LowPriority.Size() );
	for ( const auto& asyncMaterial : m_asyncMaterialsCollection_HighPriority )
	{
		materialsPaths.PushBack( { asyncMaterial.Key(), asyncMaterial.Value().m_state, asyncMaterial.Value().m_isValid, true } );
	}
	for ( const auto& asyncMaterial : m_asyncMaterialsCollection_LowPriority )
	{
		materialsPaths.PushBack( { asyncMaterial.Key(), asyncMaterial.Value().m_state, asyncMaterial.Value().m_isValid, false } );
	}
}

//////////////////////////////////////////////////////////////////////////

MaterialCompilationManager& GetMaterialCompilationManager()
{
	RED_ASSERT( s_materialCompilationManager, "MaterialCompilationManager: MaterialCompilationManager is not initialized!" );
	return *s_materialCompilationManager;
}

#endif // NO_RUNTIME_MATERIAL_COMPILATION