/**
* Copyright c 2013 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"

#include "../../../common/core/include/commandlet.h"
#include "../../../common/redCore/include/commandline.h"
#include "../../../backend/backendEngine/include/importer.h"
#include "../../../common/redContainers/include/string/string.h"
#include "../../../common/redReflection/include/resource.h"
#include "../../../common/redFileSystem/include/fileSys.h"

#include "../../backendEngine/include/resourceImportCommand.h"
#include "../../backendEngine/include/environmentPresetsManager.h"

#include "../../backendEngine/include/commandletEngineInstance.h"
#include "../../backendEngine/include/commandletRenderInstance.h"
#include "../../backendEngine/include/commandletUtils.h"

using red::String;
using red::DynArray;
using red::HashMap;

/// Helper commandlet that can be used to import asset into the engine
class ImportCommandlet : public red::ICommandlet
{
	RTTI_DECLARE_TYPE( ImportCommandlet );

public:
	ImportCommandlet();
	~ImportCommandlet();

	virtual bool Execute( const red::CommandLine& options ) override;
	virtual const char* GetOneLiner() const override;
	virtual void PrintHelp() const override;

	virtual Bool InitializeEnvironment() override;
	virtual void TeardownEnvironment() override;

private:
	struct Settings
	{
		struct Entry
		{
			red::AbsolutePath sourcePath;
			res::ResourcePath resourcePath;
		};
		red::DynArray<Entry> importFiles;

		String importerOptions;

		Settings();
		bool Parse( const red::CommandLine& options );
	};

	bool ImportSingleResource( const red::AbsolutePath& sourcePath, const res::ResourcePath& resourcePath, const String& options );

	commandlet::EngineInstance m_engineInstance;
	commandlet::RenderInstance m_renderInstance;
};

RTTI_BEGIN_TYPE( ImportCommandlet )
	RTTI_PARENT_TYPE( red::ICommandlet )
RTTI_END_TYPE()

ImportCommandlet::ImportCommandlet()
	: ICommandlet( RED_NAME_CONSTEXPR( "import" ) )
{
}

ImportCommandlet::~ImportCommandlet()
{
}

const char* ImportCommandlet::GetOneLiner() const
{
	return "Import assets into the engine";
}

void ImportCommandlet::PrintHelp() const
{
	RED_LOG( "Usage:" );
	RED_LOG( "  import -input=<file path> -output=<resource path> -options=\"<importer options>\"" );
	RED_LOG( "  import -input_dir=<dir path> -output=<resource dir path> -options=\"<importer options>\"" );
	RED_LOG( "  import -input_list=<file path> -output=<resource dir path> -options=\"<importer options>\"" );
	RED_LOG( "Params:" );
	RED_LOG( "  -input          - Import a singe file to the output resource path" );
	RED_LOG( "  -input_dir      - Import a single non-recursive directory to the output resource directory" );
	RED_LOG( "  -input_list     - Import a list of files specified inside a listing file to the output resource directory" );
	RED_LOG( "  -output         - Either a path in the resource system or in the -input case can also be a resource file" );
	RED_LOG( "  -options        - A string enclosed in \"\" which will be passed directly to the importer" );
	RED_LOG( "" );
	RED_LOG( "Supported resource types and formats:" );

	// get all supported resource classes
	DynArray< const rtti::ClassType* > resourceClasses;
	IImporter::EnumImportClasses( resourceClasses );

	// get all formats for each class
	for ( const auto* resourceClass : resourceClasses )
	{
		DynArray< CFileFormat > fileFormats;
		IImporter::EnumImportFormats( resourceClass, fileFormats );

		if ( !fileFormats.Empty() )
		{
			const CResource* defaultResource = resourceClass->GetDefaultObject< CResource >();
			RED_LOG( "  {extension:%hs, deprecated extension:%hs} (%hs) importable from %d file format(s):", 
				defaultResource->GetExtension(),
				defaultResource->GetDeprecatedExtension(),
				defaultResource->GetFriendlyDescription(), fileFormats.Size() );

			for ( const auto& format : fileFormats )
			{
				RED_LOG( "    %hs: %hs", 
					format.GetExtension().AsChar(),
					format.GetDescription().AsChar() );
			}
		}
	}
}


namespace
{

// Helper object to get resource extensions from source file extensions
// This is an object because collecting the resource extensions is somewhat expensive
class ResourceExtensionResolver
{
public:
	ResourceExtensionResolver();

	const red::String& GetResourceExtension( const red::String& sourceExtension );

private:
	ResourceExtensionResolver( const ResourceExtensionResolver& ) = delete;
	ResourceExtensionResolver( ResourceExtensionResolver&& ) = delete;
	ResourceExtensionResolver& operator=( const ResourceExtensionResolver& ) = delete;
	ResourceExtensionResolver& operator=( ResourceExtensionResolver&& ) = delete;

	red::Map< red::String, red::String > m_extensionMap;
};

ResourceExtensionResolver::ResourceExtensionResolver()
{
	DynArray< const rtti::ClassType* > importerClasses;
	GetRttiSystem().EnumClasses( ClassID<IImporter>(), importerClasses );

	for ( const rtti::ClassType* importerClass : importerClasses )
	{
		IImporter* importer = importerClass->GetDefaultObject<IImporter>();
		auto resourceExt = importer->GetResourceExtension();
		auto sourceFormats = importer->GetSourceFileFormats();

		for ( const auto& format : sourceFormats )
		{
			m_extensionMap.Insert( format.GetExtension(), resourceExt );
		}
	}
}

const red::String& ResourceExtensionResolver::GetResourceExtension( const red::String& sourceExtension )
{
	auto iter = m_extensionMap.Find( sourceExtension );
	if ( iter != m_extensionMap.End() )
	{
		return iter.Value();
	}
	return String::EMPTY();
}

}


ImportCommandlet::Settings::Settings()
{
}

bool ImportCommandlet::Settings::Parse( const red::CommandLine& options )
{
	bool singleInput = false;
	red::DynArray< red::AbsolutePath > inputFiles;

	String inputString;
	if ( options.GetFirstParam( "input", inputString ) )
	{
		inputFiles.PushBack( red::AbsolutePath::ParseFilePath( inputString ) );
		singleInput = true;
	}
	else if ( options.GetFirstParam( "input_dir", inputString ) )
	{
		red::AbsolutePath basePath = red::AbsolutePath::ParseDirPath( inputString );
		GFileManager->FindFiles( basePath, "*.*", inputFiles, false );
	}
	else if ( options.GetFirstParam( "input_list", inputString ) ) 
	{
		red::AbsolutePath listingFilePath = red::AbsolutePath::ParseFilePath( inputString );
		if ( listingFilePath.Empty() )
		{
			RED_LOG_ERROR( "Invalid listing file specified %hs", inputString.AsChar() );
			return false;
		}
		inputFiles = commandlet::ReadAbsolutePathsFromListingFile( listingFilePath );
	}
	else
	{
		RED_LOG_ERROR( "One of -input, -input_dir, -input_list must be specified" );
		return false;
	}

	if ( inputFiles.Empty() )
	{
		RED_LOG_ERROR( "No files to import!" );
		return false;
	}

	String outputString;
	if ( !options.GetFirstParam( "output", outputString ) )
	{
		RED_LOG_ERROR( "The -output parameter is required" );
		return false;
	}

	if ( red::paths::IsAbsolutePath( outputString ) )
	{
		RED_LOG_ERROR( "Output path must be a relative resource path!" );
		return false;
	}

	const bool outputIsDirectory = ( outputString.Back() == '/' || outputString.Back() == '\\' );
	if ( !singleInput && !outputIsDirectory )
	{
		RED_LOG_ERROR( "Output must be a directory when specifying more than one file" );
		return false;
	}

	if ( singleInput && !outputIsDirectory )
	{
		// Dealing with a single file
		const red::AbsolutePath& sourcePath = inputFiles.Front();
		res::ResourcePath resPath = res::ResourcePath::Build( outputString );
		importFiles.PushBack({ sourcePath, resPath });
	}
	else
	{
		// Dealing with multiple files or a single file with just a directory specified for output
		// Must convert the source path and filename into the output directory
		// and also change the extension to the desired resource extension
		// The desired extension can be passed as a resExt option
		ResourceExtensionResolver resolver;
		String outputExt;
		options.GetFirstParam("resExt", outputExt);

		for ( const auto& sourcePath : inputFiles )
		{
			if (outputExt.Empty())
			{
				outputExt = resolver.GetResourceExtension(red::paths::ExtractExtension(sourcePath));
			}
			
			red::String path = outputString + red::paths::ExtractFileName( sourcePath );
			red::paths::ReplaceExtension( path, outputExt);

			res::ResourcePath resPath = res::ResourcePath::Build( path );

			importFiles.PushBack({ sourcePath, resPath });
		}
	}

	if ( options.HasOption( "options" ) )
	{
		importerOptions = String::Join( options.GetValues( "options" ), " " );
	}

	return true;
}


Bool ImportCommandlet::InitializeEnvironment()
{
	if ( !m_engineInstance.Initialise() )
		return false;

	if ( !m_renderInstance.Initialise() )
		return false;

	return true;
}

void ImportCommandlet::TeardownEnvironment()
{
	m_renderInstance.Shutdown();
	m_engineInstance.Shutdown();
}

bool ImportCommandlet::Execute( const red::CommandLine& options )
{
	Settings settings;
	if ( !settings.Parse( options ) )
	{
		return false;
	}

	// For thumbnails
	tools::EnvironmentPresetsManager envPresetsManager;

	RED_FATAL_ASSERT( !settings.importFiles.Empty() );

	Uint32 successfulImports = 0;
	Uint32 currentImport = 1;
	const Uint32 totalImports = settings.importFiles.Size();

	// Import the first file in order to parse the importer options into an options object
	const Settings::Entry& entry = settings.importFiles.Front();
	RED_LOG_INFO( "Status: Importing %d / %d : %hs -> %hs", currentImport, totalImports, entry.sourcePath.AsChar(), entry.resourcePath.ToDebugString() );

	tools::ResourceImportCommand command{ entry.resourcePath, entry.sourcePath, envPresetsManager, commandlet::GetEditorDepot() };

	THandle<ResourceCommandOptions> parsedOptions = command.CreateOptions();
	if ( parsedOptions == nullptr )
	{
		RED_LOG_ERROR( "Import: Error creating the import command options" );
		return false;
	}
	red::CommandLine importCommandLine = red::CommandLine::ParseInnerString( settings.importerOptions );
	if ( !parsedOptions->ParseFromCommandLine( importCommandLine ) )
	{
		RED_LOG_ERROR( "Import: Error parsing command line parameters in importer" );
		return false;
	}

	ResourceCommandResult result = command.Execute( parsedOptions );
	if ( result.HasFailed() )
	{
		RED_LOG_ERROR( "Import: Error while importing: %hs", result.GetErrorMessage().AsChar() );
	}
	else
	{
		successfulImports += 1;
	}

	if ( totalImports > 1 )
	{
		auto iter = settings.importFiles.Begin();
		++iter; // skip first entry
		const auto end = settings.importFiles.End();
		for ( ; iter != end; ++iter )
		{
			currentImport += 1;
			const Settings::Entry& entry = *iter;
			RED_LOG_INFO( "Status: Importing %d / %d : %hs -> %hs", currentImport, totalImports, entry.sourcePath.AsChar(), entry.resourcePath.ToDebugString() );

			tools::ResourceImportCommand command{ entry.resourcePath, entry.sourcePath, envPresetsManager, commandlet::GetEditorDepot() };

			THandle<ResourceCommandOptions> commandOptions = command.CreateOptions( parsedOptions );
			if ( commandOptions == nullptr )
			{
				RED_LOG_ERROR( "Import: Error creating the import command options" );
				continue;
			}
			else if ( commandOptions->GetClass() != parsedOptions->GetClass() )
			{
				RED_LOG_ERROR( "Import: Attempting to use the same command line importer options to import different resources" );
				continue;
			}

			ResourceCommandResult result = command.Execute( commandOptions );
			if ( result.HasFailed() )
			{
				RED_LOG_ERROR( "Import: Error while importing : %hs", result.GetErrorMessage().AsChar() );
				continue;
			}

			successfulImports += 1;
		}
	}

	RED_LOG_INFO( "Import: Imported %d / %d resources", successfulImports, totalImports );
	RED_LOG_INFO( "Status: Import complete (%d/%d)", successfulImports, totalImports );

	return successfulImports == settings.importFiles.Size();
}
