/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "../../../common/core/include/commandlet.h"
#include "../../../common/redCore/include/commandline.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/resource/include/basicTextBuilder.h"
#include "../../backendEngine/include/commandletEngineInstance.h"
#include "../../backendEngine/include/commandletRenderInstance.h"
#include "../../backendEngine/include/commandletUtils.h"
#include "../../backendEngine/include/importer.h"
#include "../../backendEngine/include/resourceImportCommand.h"
#include "../../backendEngine/include/environmentPresetsManager.h"


class MultilayerCreateMaskCommandlet : public red::ICommandlet
{
	RTTI_DECLARE_TYPE(MultilayerCreateMaskCommandlet);

public:
	MultilayerCreateMaskCommandlet();
	~MultilayerCreateMaskCommandlet();

	virtual Bool Execute(const red::CommandLine& options) override;
	virtual const char* GetOneLiner() const override;
	virtual void PrintHelp() const override;

	virtual Bool InitializeEnvironment() override;
	virtual void TeardownEnvironment() override;

private:

	struct Settings
	{
		red::DynArray<red::AbsolutePath> sourceImages;
		res::ResourcePath exportPath;

		Bool Parse( const red::CommandLine& options );
	};



	commandlet::EngineInstance m_engineInstance;
	commandlet::RenderInstance m_renderInstance;
};

RTTI_BEGIN_TYPE(MultilayerCreateMaskCommandlet);
RTTI_PARENT_TYPE(ICommandlet);
RTTI_END_TYPE();


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


MultilayerCreateMaskCommandlet::MultilayerCreateMaskCommandlet()
	: ICommandlet( RED_NAME_CONSTEXPR("multilayerCreateMask") )
{
}

MultilayerCreateMaskCommandlet::~MultilayerCreateMaskCommandlet()
{
}

const char* MultilayerCreateMaskCommandlet::GetOneLiner() const
{
	return "Create a Multilayer Mask resource from a list of input layer mask images.";
}

void MultilayerCreateMaskCommandlet::PrintHelp() const
{
	RED_LOG("Usage:");
	RED_LOG("  multilayerCreateMask -input=<layer1> [<layer2> ...] -output=<outFile>");
	RED_LOG("");
	RED_LOG("Parameters:");
	RED_LOG("  -input=<layer1> [<layer2> ...]   - Paths of the input images to build the mask from.");
	RED_LOG("  -output=<outFile>                - Path of the resource to create.");
	RED_LOG("");
	RED_LOG("Description:");
	RED_LOG("  Create a Multilayer Mask resource (.mlmask) from a set of source images. The source images must have the");
	RED_LOG("  same dimensions, and should be a power-of-2 size. Each image corresponds to a layer in a multilayer");
	RED_LOG("  material, where the images are ordered from deepest layer to outermost. No image is provided for layer 0,");
	RED_LOG("  as it is assumed this layer is fully solid.");
}


Bool MultilayerCreateMaskCommandlet::InitializeEnvironment()
{
	if ( !m_engineInstance.Initialise() )
		return false;

	if ( !m_renderInstance.Initialise() )
		return false;

	return true;
}

void MultilayerCreateMaskCommandlet::TeardownEnvironment()
{
	m_renderInstance.Shutdown();
	m_engineInstance.Shutdown();
}


Bool MultilayerCreateMaskCommandlet::Execute(const red::CommandLine& options)
{
	Settings settings;
	if ( !settings.Parse( options ) )
	{
		RED_LOG("");
		PrintHelp();
		return false;
	}

	//
	// Create temporary file to be imported.
	//
	red::AbsolutePath importInputPath = red::paths::ReplaceExtension( GFileManager->GenerateTemporaryFilePath(), "json" );

	CBasicTextBuilder inputJsonText;
	inputJsonText.Write( "{ \"layers\": [\n" );
	for ( const red::AbsolutePath& path : settings.sourceImages )
	{
		String pathString = path.AsString();
		pathString.ReplaceAll("\\", "\\\\");
		pathString.ReplaceAll("\"", "\\\"");
		inputJsonText.Writef( "\t{ \"source\": \"%hs\" },\n", pathString.AsChar() );
	}
	inputJsonText.Write( "] }\n" );
	if ( !inputJsonText.SaveToFile( importInputPath, false ) )
	{
		RED_LOG_ERROR("Import: Failed to save temporary file to %hs", importInputPath.AsChar());
		return false;
	}

	//
	// Do the import.
	//
	tools::EnvironmentPresetsManager envPresetsManager;
	tools::ResourceImportCommand command{ settings.exportPath, importInputPath, envPresetsManager, commandlet::GetEditorDepot() };

	THandle<ResourceCommandOptions> parsedOptions = command.CreateOptions();
	if ( parsedOptions == nullptr )
	{
		RED_LOG_ERROR("Import: Error creating the import command options");
		return false;
	}

	ResourceCommandResult result = command.Execute( parsedOptions );
	if ( result.HasFailed() )
	{
		RED_LOG_ERROR("Import: Error while importing: %hs", result.GetErrorMessage().AsChar());
		return false;
	}


	// Delete the temporary file, now we're done with it.
	if ( !GFileManager->DeleteFile( importInputPath ) )
	{
		RED_LOG_WARNING("Import: Failed to delete temporary file at %hs", importInputPath.AsChar());
	}

	RED_LOG("Import: Multilayer Mask successfully created!");

	return true;
}


Bool MultilayerCreateMaskCommandlet::Settings::Parse( const red::CommandLine& options )
{
	red::DynArray<String> inputPaths;
	if ( options.GetAllParams( "input", inputPaths ) )
	{
		sourceImages.Reserve( inputPaths.Size() );
		for ( Uint32 i = 0; i < inputPaths.Size(); ++i )
		{
			red::AbsolutePath inputPath = red::AbsolutePath::ParseFilePath( inputPaths[i] );
			if ( !GFileManager->FileExist( inputPath ) )
			{
				RED_LOG_ERROR("Import: Input file not found: %hs (resolved to %hs)", inputPaths[i].AsChar(), inputPath.AsChar());
				return false;
			}

			sourceImages.PushBack( red::AbsolutePath::ParseFilePath( inputPaths[i] ) );
		}
	}
	else
	{
		RED_LOG_ERROR("Import: Missing input paths");
		return false;
	}

	String outputFile;
	if ( options.GetFirstParam( "output", outputFile ) )
	{
		exportPath = res::ResourcePath::Build( outputFile );
	}
	else
	{
		RED_LOG_ERROR("Import: Missing output path");
		return false;
	}

	return true;
}
