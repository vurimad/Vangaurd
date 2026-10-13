/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "cooker.h"
#include "dependencyCollector.h"
#include "resourceDependency.h"
#include "dataErrorHandler.h"
#include "dataPlatform.h"

// For cooker::prv namespace
#include "../../../common/redFileSystem/include/fileVersionList.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/redFileSystem/include/memoryFileReader.h"
#include "../../../common/redFileSystem/include/memoryFileWriter.h"
#include "../../../common/redJobs2/include/jobBuilder.h"
#include "../../../common/resource/include/backendResourceDepot.h"
#if !defined( VANGUARD_ASSET_HOST )
#include "../../../common/resource/include/jsonParser.h"
#include "../../../common/resource/include/jsonGenericLoader.h"
#include "../../../common/resource/include/jsonResource.h"
#endif
#include "../../../common/resource/include/2darray.h"
#include "../../../common/redReflection/include/resource.h"
#include "../../../common/redReflection/include/resourceLoader.h"
#include "../../../common/redReflection/include/serializationSaver.h"
#include "../../../common/redReflection/include/serializationLoader.h"
#include "../../../common/redReflection/include/serializationFileTables.h"
#include "../../../common/redReflection/include/serializationBinaryRuntimeTables.h"
#include "../../../common/redReflection/include/resourceClassLookup.h"
#include "../../backendEngine/include/resourceUtils.h"

//------------------------------------------------------------------------------

ICooker::~ICooker() = default;

CName ICooker::GetName() const
{
	return this->GetClass()->GetName();
}

//------------------------------------------------------------------------------

namespace cooker
{

namespace prv
{

static THandle< CResource > LoadBinaryResource( const res::ResourcePath& path, red::BlobView buffer )
{
	auto memPtr = red::CreateUniquePtr< CMemoryFileReaderExternalBuffer >( buffer.Data(), buffer.Size(), path.ToDebugString() );
	memPtr->SetCooker( true ); // Set cooker here as well so systems know

	Uint32 version = 0;
	serialization::FileTables fileTables;
	if ( !fileTables.Load( *memPtr, version ) )
	{
		return nullptr;
	}
	memPtr->m_version = version;

	serialization::LoadingContext context;
	context.m_initialResourcePath = context.m_resourcePath = path;
	context.m_skipImports = true;
	context.m_skipPostLoad = true;

	serialization::RuntimeTables runtimeTables{ serialization::RuntimeTables::eLoadExportsFromFile };
	runtimeTables.Resolve( context, fileTables );

	runtimeTables.CreateExports( context, fileTables );

	// Unused since no asyncSource
	job::Counter nullCounter;

	serialization::LoadingResult result;
	runtimeTables.LoadExports( 0, *memPtr, nullCounter, context, fileTables, result );

	if ( result.m_loadedRootObjects.Empty() )
	{
		return nullptr;
	}

	return Cast< CResource >( result.m_loadedRootObjects.Front() );
}

#if !defined( VANGUARD_ASSET_HOST )
static THandle< CResource > LoadJsonResource( const res::ResourcePath& path, red::BlobView buffer )
{
	const auto resourceClass = res::GetResourceClassForFile( path );
	if ( !resourceClass )
	{
		return nullptr;
	}

	auto resource = resourceClass->CreateHandle< CResource >();

	res::json::JsonParser jsonParser;
	jsonParser.SetDebugName( path.ToString() );

	if ( !resource->IsA< JsonResource >() )
	{
		// Load directly into resource
		jsonParser.SetRoot( resource );
	}

	red::String stringBuffer{ buffer.Pointer< const char >(), buffer.Size() };
	if ( !jsonParser.ParseJsonFile( std::move( stringBuffer ) ) )
	{
		return nullptr;
	}

	// Create table even if not loading imports. Otherwise will lose them when saving.
	if ( !jsonParser.CreateImportTable() )
	{
		return nullptr;
	}

	// Need to store, because the JSON parser will otherwise crash when trying to store a null token in a tresref, which calls token->GetPath()
	const auto& imports = jsonParser.GetDependencies();
	const Uint32 numImports = imports.Size();
	for ( Uint32 i = 0; i < numImports; ++i )
	{
		auto dummyLoadingToken = res::CreateFailedResourceToken( imports[ i ].GetPath(), res::ResourceTokenErrorType::None );
		jsonParser.StoreLoadingToken( i, dummyLoadingToken );
	}

	if ( !jsonParser.CreateExportTable() )
	{
		return nullptr;
	}

	const Bool skipPostLoad = true;
	if ( !jsonParser.LoadObjectStructure( PostLoadContext(), skipPostLoad ) )
	{
		return nullptr;
	}

	if ( auto jsonRes = Cast< JsonResource >( resource ) )
	{
		jsonRes->SetRoot( jsonParser.GetRoot() );
	}

	return resource;
}
#endif

static THandle< CResource > LoadCSVResource( const res::ResourcePath& path, red::BlobView buffer )
{
	auto resource = CreateHandle< C2dArray >();
	resource->ParseData( red::StringView( buffer.Pointer<const char>(), buffer.Size() ) );
	return resource;
}


} // prv

THandle< CResource > LoadResource( const res::ResourcePath path, data::ResourceDepot& depot )
{
	auto filePtr = depot.CreateResourceReader( path, false );
	if ( !filePtr )
	{
		return nullptr;
	}

	red::Blob buffer;
	if ( !red::LoadFileToBuffer( *filePtr, buffer ) )
	{
		return nullptr;
	}

	if ( buffer.Size() < sizeof( Uint32 ) )
	{
		return nullptr;
	}

	THandle< CResource > resource;

	if ( buffer.As< Uint32 >() == serialization::FileTables::FILE_MAGIC )
	{
		resource = prv::LoadBinaryResource( path, buffer );
	}
#if !defined( VANGUARD_ASSET_HOST )
	else if ( IsJsonResource( path ) )
	{
		resource = prv::LoadJsonResource( path, buffer );
	}
#endif
	else if ( IsCsvResource( path ) )
	{
		resource = prv::LoadCSVResource( path, buffer );
	}

	if ( resource )
	{
		resource->Internal_SetPath( path );

		if ( resource->GetCookingPlatform() != ECookingPlatform::PLATFORM_None )
		{
			RED_LOG_ERROR( "Attempting to cook an already cooked resource '%hs' Already cooked with platform %u", 
				resource->GetPath().ToDebugString(), static_cast<Uint32>( resource->GetCookingPlatform() ) );
			return nullptr;
		}
	}

	return resource;
}


bool SaveResource( const res::ResourcePath path, const THandle< CResource >& resource, data::ResourceDepot& depot, data::TargetPlatform platform )
{
	// Always use the default binary saver for cooking
	auto saver = serialization::ISaver::CreateSaver();
	RED_FATAL_ASSERT( saver, "No binary saver found!" );

	auto cookingPlatform = data::CookingPlatformFromTargetPlatform( platform );
	RED_FATAL_ASSERT( cookingPlatform != PLATFORM_None );
	resource->Cooker_SetPlatform( cookingPlatform );

	red::DynArray< Uint8 > data;
	data.Reserve( RED_KILO_BYTE( 64 ) );

	CMemoryFileWriter memoryWriter( data );
	memoryWriter.SetCooker( true );

	// Cook the resource
	serialization::SavingContext context( resource );
	context.m_cookingPlatform = cookingPlatform;
	context.m_isCooker = true;
	context.m_gpuSize = resource->GetGPUSize();

	if ( !saver->SaveObjects( memoryWriter, context ) )
	{
		return false;
	}

	auto filePtr = depot.CreateResourceWriter( path );
	if ( !filePtr )
	{
		return false;
	}

	filePtr->Serialize( data.Data(), data.Size() );
	if ( filePtr->HasErrors() )
	{
		filePtr->HandleIOError( "SaveResource: Failed to serialize objects into the file %hs", path.ToDebugString() );
		return false;
	}

	Uint64 fileSize = filePtr->GetSize();
	RED_LOG_INFO( "SaveResource: Cooked File '%hs' was saved, %llu bytes", path.ToDebugString(), fileSize );

	return true;
}

bool SaveResource( const res::ResourcePath path, const THandle< CResource >& resource, data::ResourceDepot& depot, data::TargetPlatform platform, const red::DynArray< THandle< CResource > >& inplaceResources )
{
	// Always use the default binary saver for cooking
	auto saver = serialization::ISaver::CreateSaver();
	RED_FATAL_ASSERT( saver, "No binary saver found!" );

	auto cookingPlatform = data::CookingPlatformFromTargetPlatform( platform );
	RED_FATAL_ASSERT( cookingPlatform != PLATFORM_None );
	resource->Cooker_SetPlatform( cookingPlatform );

	red::DynArray< Uint8 > data;
	data.Reserve( RED_KILO_BYTE( 64 ) );

	CMemoryFileWriter memoryWriter( data );
	memoryWriter.SetCooker( true );

	// Cook the resource
	serialization::SavingContext context( resource );
	context.m_cookingPlatform = cookingPlatform;
	context.m_isCooker = true;
	context.m_gpuSize = resource->GetGPUSize();
	
	for ( const THandle< CResource > & inplaceResource : inplaceResources )
	{
		context.m_initialExports.PushBack( inplaceResource );
	}

	if ( !saver->SaveObjects( memoryWriter, context ) )
	{
		return false;
	}

	auto filePtr = depot.CreateResourceWriter( path );
	if ( !filePtr )
	{
		return false;
	}

	filePtr->Serialize( data.Data(), data.Size() );
	if ( filePtr->HasErrors() )
	{
		filePtr->HandleIOError( "SaveResource: Failed to serialize objects into the file %hs", path.ToDebugString() );
		return false;
	}

	Uint64 fileSize = filePtr->GetSize();
	RED_LOG_INFO( "SaveResource: Cooked File '%hs' was saved, %llu bytes", path.ToDebugString(), fileSize );

	return true;
}

struct SaveContext
{
	serialization::SaverPtr m_saver;
	serialization::SavingContext m_context;
	red::DynArray< Uint8 > m_data;
	CMemoryFileWriter m_memoryWriter;
	data::ResourceDepot& m_depot;
	bool m_saveResult;

	SaveContext( const THandle< CResource >& resource, data::ResourceDepot& depot )
		: m_saver( serialization::ISaver::CreateSaver() )
		, m_context( resource )
		, m_memoryWriter( m_data )
		, m_depot( depot )
		, m_saveResult( false )
	{
		RED_FATAL_ASSERT( m_saver, "No binary saver found!" );

		m_data.Reserve( RED_KILO_BYTE( 64 ) );
		m_memoryWriter.SetCooker( true );
	}
};

data::Result SaveResource( const res::ResourcePath path, const THandle< CResource >& resource, data::ResourceDepot& depot, data::TargetPlatform platform, const red::DynArray< THandle< CResource > > & inplaceResources, job::Builder& builder )
{
	data::Result result;

	auto cookingPlatform = data::CookingPlatformFromTargetPlatform( platform );
	RED_FATAL_ASSERT( cookingPlatform != PLATFORM_None );
	resource->Cooker_SetPlatform( cookingPlatform );

	auto saveContext = red::CreateSharedPtr<SaveContext, red::PoolBackend>( resource, depot );

	// Cook the resource
	serialization::SavingContext& context = saveContext->m_context;
	context.m_cookingPlatform = cookingPlatform;
	context.m_isCooker = true;
	context.m_gpuSize = resource->GetGPUSize();
	
	for ( const THandle< CResource > & inplaceResource : inplaceResources )
	{
		context.m_initialExports.PushBack( inplaceResource );
	}

	if ( saveContext->m_saver->SaveObjects( saveContext->m_memoryWriter, context, builder, &saveContext->m_saveResult ) )
	{
		builder.DispatchJob( "cooker_SaveResource_write", [saveContext, path, result]( const job::RunContext& )
		{
			if ( !saveContext->m_saveResult )
			{
				result.SetValue( false );
				return;
			}

			auto& depot = saveContext->m_depot;
			auto& data = saveContext->m_data;

			auto filePtr = depot.CreateResourceWriter( path );
			if ( !filePtr )
			{
				result.SetValue( false );
				return;
			}

			filePtr->Serialize( data.Data(), data.Size() );
			if ( filePtr->HasErrors() )
			{
				filePtr->HandleIOError( "SaveResource: Failed to serialize objects into the file %hs", path.ToDebugString() );
				result.SetValue( false );
				return;
			}

			Uint64 fileSize = filePtr->GetSize();
			RED_LOG_INFO( "SaveResource: Cooked File '%hs' was saved with jobs, %llu bytes", path.ToDebugString(), fileSize );
			result.SetValue( true );
		} );
	}

	return result;
}

//------------------------------------------------------------------------------

bool BasicCooker::CollectDependencies( const res::ResourcePath& path, job::Builder& builder, dependency::Collector& collector, data::Context& system ) const
{
	collector.Add( MakeSourceDependency( path, dependency::Requirement::Required ) );
	return true;
}

bool BasicCooker::Cook( const res::ResourcePath& path, job::Builder& builder, data::Context& system ) const
{
	THandle< CResource > resource = LoadResource( path, system.SourceDepot() );
	if ( !resource )
	{
		system.ErrorHandler().ReportError( "Cooking: Failed to load '%hs' for cooking", path.ToDebugString() );
		return false;
	}

	bool cookerResult = CookLoadedResource( resource, system );
	if ( !cookerResult )
	{
		system.ErrorHandler().ReportError( "Cooking: Failed to cook resource '%hs' with cooker '%hs'", path.ToDebugString(), GetName().AsChar()  );
		return false;
	}

	bool saveResult = SaveResource( path, resource, system.CookedDepot(), system.TargetPlatform() );
	if ( !saveResult )
	{
		system.ErrorHandler().ReportError( "Cooking: Failed to save resource '%hs'", path.ToDebugString() );
		return false;
	}

	return true;
}

} // cooker

//------------------------------------------------------------------------------

namespace data
{
namespace prv
{

red::DynArray< ICooker* > EnumerateCookers()
{
	red::DynArray< const rtti::ClassType* > cookerClasses;
	GetRttiSystem().EnumClasses( ClassID<ICooker>(), cookerClasses );

	red::DynArray< ICooker* > cookers;
	cookers.Reserve( cookerClasses.Size() );

	for ( const rtti::ClassType* cookerClass : cookerClasses )
	{
		ICooker* cooker = cookerClass->GetDefaultObject< ICooker >();
		if ( cooker )
		{
			cookers.PushBack( cooker );
		}
	}

	return cookers;
}

} // prv
} // data

red::UniquePtr< ICooker > ICooker::Create( const res::ResourcePath& path )
{
	static red::DynArray< ICooker* > cookers{ data::prv::EnumerateCookers() };

	for ( ICooker* cooker : cookers )
	{
		if ( cooker->CanCook( path ) )
		{
			// Create a new instance of the object here.
			return red::UniquePtr< ICooker >( cooker->GetClass()->CreateObject< ICooker >() );
		}
	}

	return nullptr;
}

RTTI_BEGIN_ABSTRACT_TYPE( ICooker );
RTTI_END_TYPE();

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( BasicCooker, cooker );
RTTI_PARENT_TYPE( ICooker );
RTTI_END_TYPE();
