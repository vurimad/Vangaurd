/* 
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */
#include "build.h"
#include "resourceSaver.h"
#include "backendResourceDepot.h"
#include "resourceEditorDepotImpl.h"
#include "../../redReflection/include/resource.h"
#include "../../redReflection/include/serializationSaver.h"
#include "../../redReflection/include/deferredBufferMapper.h"
#include "../../redFileSystem/include/file.h"
#include "../../redFileSystem/include/memoryFileWriter.h"
#include "../../redCore/include/absolutePath.h"
#include "../../redSystem/include/stopWatch.h"

namespace res
{

	ResourceSavingOptions::ResourceSavingOptions()
		: m_depot( GetEditorDepot() )
		, m_loadDeferredDataBuffers( true )
		, m_isCloner( false )
		, m_cookedOutput( false )
		, m_isGenerator( false )
		, m_magicFlagForTerrain( false )
	{
	}

	Bool SerializeResource( const THandle< CResource >& resource, const ResourcePath& targetPath,
		const ResourceSavingOptions& options, red::DynArray< Uint8 >& outData )
	{
		RED_FATAL_ASSERT( resource, "Invalid Resource to serialize" );
		if ( !targetPath.IsValid() )
		{
			RED_LOG_ERROR( "SerializeResource: No destination path" );
			return false;
		}

		if ( options.m_cookedOutput )
		{
			RED_FATAL_ASSERT( resource->GetCookingPlatform() != PLATFORM_None, "Must set cooking platform before saving cooked data" );
		}

		if ( options.m_loadDeferredDataBuffers )
		{
			serialization::DeferredDataBufferLoader bufferLoader;
			bufferLoader.Run( resource );
		}

		const red::String fileExtension = red::paths::ExtractExtension( targetPath.ToStringView() );
		const serialization::SaverPtr saver = serialization::ISaver::CreateSaver( fileExtension.AsChar() );
		if ( !saver )
		{
			RED_LOG_ERROR( "SerializeResource: Cannot create saver, extension '%hs' is not recognized.", fileExtension.AsChar() );
			return false;
		}

		outData.Clear();
		outData.Reserve( RED_KILO_BYTE( 64 ) );

		CMemoryFileWriter memoryWriter( outData );
		memoryWriter.SetCooker( options.m_cookedOutput );
		memoryWriter.SetFileNameForDebug( targetPath.ToString() );

		serialization::SavingContext context( resource );
		context.m_isCooker = options.m_cookedOutput;
		context.m_isCloner = options.m_isCloner;
		context.m_gpuSize = resource->GetGPUSize();
		context.m_debugVerifyBufferCompression = true;
		context.m_isGenerator = options.m_isGenerator;
		context.m_magicFlagForTerrain = options.m_magicFlagForTerrain;

		if ( fileExtension == "terraintile" )
		{
			context.m_magicFlagForTerrain = true;
		}

		if ( !saver->SaveObjects( memoryWriter, context ) )
		{
			RED_LOG_ERROR( "SerializeResource: Failed to serialize data in memory for '%hs'", targetPath.ToDebugString() );
			return false;
		}

		return true;
	}

	Bool SaveResource( const THandle< CResource >& resource, const ResourceSavingOptions& options )
	{
		red::StopWatch timer;

		RED_FATAL_ASSERT( resource, "Invalid Resource to save" );
		RED_FATAL_ASSERT( options.m_depot, "Invalid Resource Depot to save to" );

		// determine target path
		const ResourcePath& targetPath = options.m_targetPath.IsValid() ? options.m_targetPath : resource->GetPath();
		if ( !targetPath.IsValid() )
		{
			RED_LOG_ERROR( "SaveResource: No destination path" );
			return false;
		}

		red::SharedPtr<IFile> filePtr = options.m_depot->CreateResourceWriter( targetPath );
		if ( !filePtr )
		{
			RED_LOG_ERROR( "SaveResource: Cannot create file writer for '%hs'", targetPath.ToDebugString() );
			return false;
		}

		red::DynArray<Uint8> data{ red::PoolBackend() };
		if ( !SerializeResource( resource, targetPath, options, data ) )
		{
			return false;
		}

		filePtr->Serialize( data.Data(), data.Size() );
		if ( filePtr->HasErrors() )
		{
			RED_LOG_ERROR( "SaveResource: Failed to serialize objects into the file %hs", targetPath.ToDebugString() );
			return false;
		}

		resource->UnmarkModified();

		// get the size of the stored data
		Uint64 fileSize = filePtr->GetSize();
		RED_FATAL_ASSERT( data.Size() == fileSize, "Serialized size does not match written file size (%u != %llu)", data.Size(), fileSize );

		// resource was saved, log it
		RED_LOG_INFO( "SaveResource: File '%hs' was saved, %llu bytes, took %0.3lf ms", targetPath.ToDebugString(), fileSize, timer.GetDeltaMS() );
		return true;
	}
} // res
