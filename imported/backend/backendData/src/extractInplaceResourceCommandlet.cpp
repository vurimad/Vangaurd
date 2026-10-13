/*
 * Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"

#include "../../../common/core/include/commandlet.h"
#include "../../../common/redCore/include/commandline.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/redFileSystem/include/memoryFileReader.h"
#include "../../../common/redFileSystem/include/memoryFileWriter.h"
#include "../../../common/redReflection/include/resource.h"
#include "../../../common/redReflection/include/serializationLoader.h"
#include "../../../common/redReflection/include/serializationSaver.h"
//#include "../../../common/redReflection/include/serializationFileTables.h"
//#include "../../../common/redReflection/include/serializationBinaryRuntimeTables.h"

#include "../../backendEngine/include/commandletUtils.h"

class ExtractInplaceResourceCommandlet : public red::ICommandlet
{
	RTTI_DECLARE_TYPE( ExtractInplaceResourceCommandlet );

public:
	ExtractInplaceResourceCommandlet();
	~ExtractInplaceResourceCommandlet();

	virtual bool Execute( const red::CommandLine& options ) override;
	virtual const char* GetOneLiner() const override;
	virtual void PrintHelp() const override;

private:
	struct Settings;
	bool ParseSettings( const red::CommandLine& commandline, Settings& settings );
};

RTTI_BEGIN_TYPE( ExtractInplaceResourceCommandlet )
RTTI_PARENT_TYPE( red::ICommandlet )
RTTI_END_TYPE();

//------------------------------------------------------------------------------

ExtractInplaceResourceCommandlet::ExtractInplaceResourceCommandlet()
	: ICommandlet( RED_NAME_CONSTEXPR( "extract-inplace-resource" ) )
{
}

ExtractInplaceResourceCommandlet::~ExtractInplaceResourceCommandlet()
{
}

const char* ExtractInplaceResourceCommandlet::GetOneLiner() const
{
	return "Extracts inplace resources from another resource";
}

void ExtractInplaceResourceCommandlet::PrintHelp() const
{
}

//------------------------------------------------------------------------------
struct ExtractInplaceResourceCommandlet::Settings
{
	red::AbsolutePath outputPath;
	res::ResourcePath inputPath;
};

bool ExtractInplaceResourceCommandlet::ParseSettings( const red::CommandLine& commandline, Settings& settings )
{
	red::String param;
	if ( commandline.GetFirstOption( "file", param ) )
	{
		settings.inputPath = res::ResourcePath::Build( param );
	}

	if ( commandline.GetFirstOption( "output", param ) )
	{
		settings.outputPath = red::AbsolutePath::ParseDirPath( param );
	}

	return true;
}

//------------------------------------------------------------------------------

bool LoadResourceWithInplaceResources( res::IBackendResourceDepot& depot, const res::ResourcePath path, red::DynArray< THandle< CResource > >& result )
{
	auto filePtr = depot.CreateResourceReader( path, false );
	if ( !filePtr )
	{
		return false;
	}

	red::Blob buffer;
	if ( !red::LoadFileToBuffer( *filePtr, buffer ) )
	{
		return false;
	}

	if ( buffer.Size() < sizeof( Uint32 ) )
	{
		return false;
	}

	auto memPtr = red::CreateUniquePtr< CMemoryFileReaderExternalBuffer >( buffer.Data(), buffer.Size(), path.ToDebugString() );

	Uint32 version = 0;
	serialization::FileTables fileTables;
	if ( !fileTables.Load( *memPtr, version ) )
	{
		return false;
	}
	memPtr->m_version = version;

	serialization::LoadingContext context;
	context.m_initialResourcePath = context.m_resourcePath = path;
	context.m_getAllLoadedObjects = true;
	context.m_skipImports = true;
	context.m_skipPostLoad = true;

	serialization::RuntimeTables runtimeTables{ serialization::RuntimeTables::eLoadExportsFromFile };
	runtimeTables.Resolve( context, fileTables );

	runtimeTables.CreateExports( context, fileTables );

	job::Counter nullCounter;
	serialization::LoadingResult loadingResult;
	runtimeTables.LoadExports( 0, *memPtr, nullCounter, context, fileTables, loadingResult );

	job::FlushCounter( nullCounter );

	result.Reserve( result.Size() + loadingResult.m_loadedRootObjects.Size() );
	for ( const auto& inplace : fileTables.m_inplace )
	{
		const auto pathIndex = fileTables.m_imports[ inplace.m_importIndex - 1 ].m_path;
		const char* pathStr = &fileTables.m_strings[ pathIndex ];
		const auto path = res::ResourcePath::Build( pathStr );
		auto resourceHandle = Cast< CResource >( runtimeTables.m_mappedExports[ inplace.m_exportIndex ].m_object );
		if ( resourceHandle )
		{
			resourceHandle->Internal_SetPath( path );
			result.PushBack( resourceHandle );
		}
	}

	return true;
}

bool SaveResourceWithCustomName( const red::AbsolutePath& path, const THandle< CResource >& resource )
{
	// Always use the default binary saver for cooking
	auto saver = serialization::ISaver::CreateSaver();
	RED_FATAL_ASSERT( saver, "No binary saver found!" );

	red::DynArray<Uint8> data;
	data.Reserve( RED_KILO_BYTE( 64 ) );

	CMemoryFileWriter memoryWriter( data );
	memoryWriter.SetCooker( true );

	// Cook the resource
	serialization::SavingContext context( resource );
	context.m_gpuSize = resource->GetGPUSize();
	context.m_isCooker = true;
	if ( !saver->SaveObjects( memoryWriter, context ) )
	{
		return false;
	}

	auto filePtr = GFileManager->CreateFileWriter( path, FOF_AbsolutePath );
	if ( !filePtr )
	{
		return false;
	}

	filePtr->Serialize( data.Data(), data.Size() );
	if ( filePtr->HasErrors() )
	{
		return false;
	}

	return true;
}


bool ExtractInplaceResourceCommandlet::Execute( const red::CommandLine& options )
{
	Settings settings;
	if ( !ParseSettings( options, settings ) )
	{
		return false;
	}

	commandlet::BackendDepotHolder depotHolder{ options };

	red::DynArray< THandle< CResource > > resources;

	if ( ! LoadResourceWithInplaceResources( *depotHolder.GetDepot(), settings.inputPath, resources ) )
	{
		return false;
	}

	GFileManager->CreatePath( settings.outputPath );

	const auto basename = red::paths::ExtractFileName( settings.inputPath.ToStringView() );
	const auto basedir = settings.outputPath.AddDirPath( basename );
	for ( const auto& resource : resources )
	{
		auto outputPath = basedir.AddFilePath( resource->GetPath().ToStringView() );
		if ( ! SaveResourceWithCustomName( outputPath, resource ) )
		{
			RED_LOG_ERROR( "Failed to save export %hs", outputPath.AsChar() );
		}
	}

	return true;
}