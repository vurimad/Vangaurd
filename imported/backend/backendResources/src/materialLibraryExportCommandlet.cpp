/**
* Copyright c 2017 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"

#include "../../../common/core/include/commandlet.h"
#include "../../../common/redCore/include/commandline.h"
#include "../../../common/resource/include/resourceEditorDepotImpl.h"

#include "../../backendMaterial/include/materialLibraryDataProvider.h"
#include "../../backendEngine/include/commandletUtils.h"

/// Commandlet that exports the json from matlib resource file
class GetMatlibExportCommandlet : public red::ICommandlet
{
	RTTI_DECLARE_TYPE(GetMatlibExportCommandlet);

public:
	GetMatlibExportCommandlet();
	~GetMatlibExportCommandlet();

	// ICommandlet interface
	virtual const char* GetOneLiner() const override { return "Export a .json file from .matlib resource"; }
	virtual bool Execute(const red::CommandLine& options) override;
	virtual void PrintHelp() const override;
};


RTTI_BEGIN_TYPE(GetMatlibExportCommandlet)
RTTI_PARENT_TYPE(red::ICommandlet)
RTTI_END_TYPE()


GetMatlibExportCommandlet::GetMatlibExportCommandlet()
	: ICommandlet( RED_NAME_CONSTEXPR("get-matlib_export") )
{
}

GetMatlibExportCommandlet::~GetMatlibExportCommandlet()
{
}

bool GetMatlibExportCommandlet::Execute(const red::CommandLine& options)
{	
	red::String relativeMatlibPath, outJsonPathStr;
	if( !options.GetFirstParam( "path_to_matlib", relativeMatlibPath ) || !options.GetFirstParam( "out_path_to_json", outJsonPathStr ) )
	{
		RED_LOG_ERROR( "Matlib export failed: command parsing failed!" );
		PrintHelp();
		return false;
	}

	auto editorDepot = commandlet::GetEditorDepot();

	res::ResourcePath matlibResourcePath = res::ResourcePath::Build( relativeMatlibPath );
	if ( !editorDepot->ResourceExists( matlibResourcePath ) )
	{
		RED_LOG( "Matlib export failed, file not found: %hs", matlibResourcePath.ToDebugString() );
		return false;
	}

	THandle< CResource > res = GResourceLoader->IssueLoadingRequest( matlibResourcePath )->WaitUntilLoaded();
	if (!res)
	{
		RED_LOG_ERROR( "Matlib export failed: file %hs not found!", matlibResourcePath.ToDebugString() );
		return false;
	}

	red::AbsolutePath outputJsonPath = red::AbsolutePath::ParseFilePath( outJsonPathStr );
	red::AbsolutePath matlibAbsolutePath = editorDepot->TranslateResourcePath( matlibResourcePath );

	THandle<CMaterialLayerLibrary> ml = Cast<CMaterialLayerLibrary>( res );

	tools::MaterialLibraryDataProvider matController = tools::MaterialLibraryDataProvider( ml, nullptr, red::String() );
	if ( !matController.UpdateJson( outputJsonPath, matlibAbsolutePath ) )
	{
		RED_LOG_ERROR( "Matlib export failed: unable to update JSON file at: %hs", outputJsonPath.AsChar() );
		return false;
	}

	RED_LOG( "Matlib export OK: %hs", outputJsonPath.AsChar() );
	return true;
}


void GetMatlibExportCommandlet::PrintHelp() const
{
	RED_LOG("Usage:");
	RED_LOG("    get-matlib_export -path_to_matlib=<resource path> -out_path_to_json=<path>");
}

