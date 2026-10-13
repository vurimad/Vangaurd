/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

//////////////////////////////////////////////////////////////////////////
// headers
#include "build.h"
#include "serializationLoader.h"
#include "serializationBinaryLoader.h"
#include "serializationFileTables.h"
#include "serializationAsyncSource.h"
#include "resourceLoader.h"

#include "../../redJobs2/include/jobRunner.h"
#include "../../redCore/include/profiler.h"

using red::StaticArray;

namespace serialization
{	
	//----

	LoadingContext::LoadingContext()
		: m_parent( nullptr )
		, m_resourceLoader( GResourceLoader )
		, m_creationId( 0 )
		, m_priority( io::eAsyncPriority_Normal )
		, m_filterClassType( nullptr )
		, m_getAllLoadedObjects( false )
		, m_skipPostLoad( false )
		, m_skipImports( false )
		, m_skipBuffers( false )
		, m_verifyExportTypes( false )
		, m_replication( false )
		, m_skipBackendData( false )
		, m_useGameCache( false )
	{
	}

	//----

	LoadingResult::~LoadingResult() = default;

	//----

	StaticArray< ILoader::FormatPair, ILoader::MAX_FORMATS > ILoader::st_formats;

	ILoader::ILoader() = default;
	ILoader::~ILoader() = default;

	void ILoader::RegisterCustomFactory( const AnsiChar* constImmutableExt, const TLoaderFactory& factoryFunc )
	{
		RED_FATAL_ASSERT( !st_formats.Full(), "To many custom serialization formats registered" );

		FormatPair pair;
		pair.m_constImmutableExt = constImmutableExt;
		pair.m_loaderFactory = factoryFunc;
		st_formats.PushBack( pair );
	}

	LoaderPtr ILoader::CreateLoader( const AnsiChar* formatExtension /*= nullptr*/ )
	{
		// try custom format
		if ( formatExtension && formatExtension[0] )
		{
			for ( const auto& it : st_formats )
			{
				if ( 0 == red::StrcmpNC( formatExtension, it.m_constImmutableExt ) )
				{
					return LoaderPtr( it.m_loaderFactory() );
				}
			}
		}

		// use binary serialization as default
		return LoaderPtr( RED_NEW( BinaryLoader ) );
	}

	const Bool LoadFromMemory( const void* memoryData, const Uint32 memorySize, const LoadingContext& context, LoadingResult& outResult, Bool doActiveWaiting )
	{
		PC_SCOPE( LoadFromMemory );

		// no data - wonder why we even called this method
		if ( !memoryData || memorySize < 32 ) // 32 - minimal size of the binary header
			return false;

		// quickly check the magic value - saves us from building job chain just to fail :)
		const auto fileMagic = *(const Uint32*) memoryData;
		if ( fileMagic != FileTables::FILE_MAGIC )
			return false;

		// load inplace from binary buffer
		BinaryLoader loader;
		return loader.LoadFromMemory( memoryData, memorySize, context, outResult, doActiveWaiting );
	}

} // serialization
