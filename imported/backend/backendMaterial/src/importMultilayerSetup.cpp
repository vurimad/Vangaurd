/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "../../backendEngine/include/importer.h"
#include "../../backendEngine/include/factory.h"
#include "../../../common/redReflection/include/cloningUtils.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/resource/include/jsonParser.h"
#include "../../../common/resourceMaterial/include/multilayerSetup.h"
#include "../../../common/redJobs2/include/jobDeferral.h"

//////////////////////////////////////////////////////////////////////////////////////////////////
class MultilayerSetupFactory : public IFactory
{
	RTTI_DECLARE_TYPE( MultilayerSetupFactory );

public:
	MultilayerSetupFactory()
	{
		m_resourceClass = ClassID< Multilayer_Setup >();
	}

	virtual Bool DoCreate( THandle< CResource > res, const THandle< FactoryOptions >& options ) override
	{
		return true;
	}
};

//////////////////////////////////////////////////////////////////////////////////////////////////
class MultilayerSetupImporter : public IImporter
{
	RTTI_DECLARE_TYPE( MultilayerSetupImporter );

public:
	MultilayerSetupImporter();

	virtual ImportResult DoImport( THandle< CResource > inputResource, const THandle<ResourceImportOptions>& options ) override;
	virtual THandle<ResourceImportOptions> CreateOptions( const red::AbsolutePath& sourcePath ) override;

private:
};

//////////////////////////////////////////////////////////////////////////////////////////////////
class MultilayerSetupImportOptions : public ResourceImportOptions
{
	RTTI_DECLARE_TYPE( MultilayerSetupImportOptions );
	RED_BASE_CLASS( ResourceImportOptions );

public:
	MultilayerSetupImportOptions();

	virtual ~MultilayerSetupImportOptions();

	virtual Bool ParseFromCommandLine( const red::CommandLine& stringOptions ) override;

	virtual Bool NeedOptions() const override;

	RED_INLINE const String& GetDataString() const { return m_data; }

private:
	String m_data;     // This can be used to pass whole contents of the source JSON file as string.
	                   // Using this we can import *.mlsetup without actual JSON file on disk.
};

//////////////////////////////////////////////////////////////////////////////////////////////////
MultilayerSetupImportOptions::MultilayerSetupImportOptions()
{
}

MultilayerSetupImportOptions::~MultilayerSetupImportOptions()
{
}

Bool MultilayerSetupImportOptions::ParseFromCommandLine( const red::CommandLine& stringOptions )
{
	stringOptions.GetFirstOption( "data", m_data );
	return true;
}

Bool MultilayerSetupImportOptions::NeedOptions() const
{
	return false;
}

//////////////////////////////////////////////////////////////////////////////////////////////////
MultilayerSetupImporter::MultilayerSetupImporter()
{
	m_resourceClass = ClassID< Multilayer_Setup >();
	m_formats.PushBack( CFileFormat( "json", "MultiLayer Setup JSON File" ) );
	m_formats.PushBack( CFileFormat( "mlsetup", "MultiLayer Setup File" ) );
}

THandle<ResourceImportOptions> MultilayerSetupImporter::CreateOptions( const red::AbsolutePath& sourcePath )
{
	THandle<MultilayerSetupImportOptions> options = CreateHandle<MultilayerSetupImportOptions>();
	return options;
}


ImportResult MultilayerSetupImporter::DoImport( THandle< CResource > inputResource, const THandle<ResourceImportOptions>& importOptions )
{
	// Either newly created, or previous version of this resource.
	THandle< Multilayer_Setup > multiLayerSetup = Cast< Multilayer_Setup >( inputResource );
	if ( !multiLayerSetup )
	{
		return ImportResult::Error( "Core: Unable to import mesh into something that is not a mesh" );
	}

	THandle< MultilayerSetupImportOptions > options = Cast< MultilayerSetupImportOptions >( importOptions );
	RED_FATAL_ASSERT( options != nullptr );

	const red::AbsolutePath sourceFilePath = options->GetSourcePath();
	const String& sourceDataString = options->GetDataString();

	String sourceFileData;
	if ( !sourceDataString.Empty() )
	{
		// We can use the JSON data from options directly, skipping loading source file entirely.
		sourceFileData = sourceDataString;
	}
	else
	{
		if ( !red::LoadFileToString( sourceFilePath, sourceFileData ) )
		{
			return ImportResult::Error( "Import: Failed to read '%hs', unable to import Multilayer_Setup", sourceFilePath.AsChar() );
		}
	}

	res::json::JsonParser jsonParser;
	jsonParser.SetRoot( multiLayerSetup );
	if ( !jsonParser.ParseJsonFile( std::move(sourceFileData) ) )
	{
		return ImportResult::Error( "Import: Failed to parse '%hs' as a JSON resource", sourceFilePath.AsChar() );
	}

	if ( !jsonParser.CreateImportTable() )
	{
		return ImportResult::Error( "Import: Failed to create import table for '%hs'", sourceFilePath.AsChar() );
	}

	{
		auto& dependencies = jsonParser.GetDependencies();
		for ( Uint32 i = 0; i < dependencies.Size(); ++i )
		{
			const auto& dependency = dependencies[i];
			res::ResourceTokenHandle handle = red::CreateSharedPtr< res::ResourceToken >( dependency.GetPath() );
			jsonParser.StoreLoadingToken( i, handle );
		}
	}

	if ( !jsonParser.CreateExportTable() )
	{
		return ImportResult::Error( "Import: Failed to create exports for '%hs'", sourceFilePath.AsChar() );
	}

	PostLoadContext postLoadContext;
	postLoadContext.flags.skipUploadRenderData = true;
	if ( !jsonParser.LoadObjectStructure( postLoadContext, true ) )
	{
		return ImportResult::Error( "Import: Failed to load object structure for '%hs'", sourceFilePath.AsChar() );
	}

	return ImportResult::Success( multiLayerSetup );
}

//////////////////////////////////////////////////////////////////////////////////////////////////
RTTI_BEGIN_TYPE( MultilayerSetupFactory );
	RTTI_PARENT_TYPE( IFactory );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE( MultilayerSetupImporter )
	RTTI_PARENT_TYPE( IImporter )
RTTI_END_TYPE()

RTTI_BEGIN_TYPE( MultilayerSetupImportOptions );
	RTTI_PARENT_TYPE( ResourceImportOptions );
	RTTI_PROPERTY( m_data ).editable();
RTTI_END_TYPE();
