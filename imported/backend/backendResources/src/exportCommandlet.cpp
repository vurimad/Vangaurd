/**
* Copyright c 2013 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"

#include "../../../common/core/include/commandlet.h"
#include "../../../common/redCore/include/commandline.h"
#include "../../../common/redReflection/include/serializationLoader.h"
#include "../../../common/redReflection/include/resource.h"
#include "../../../common/redCore/include/commandline.h"
#include "../../../backend/backendEngine/include/exporter.h"
#include "../../../common/redReflection/include/resourceUtils.h"

class CExportCommandlet : public red::ICommandlet
{
	RTTI_DECLARE_TYPE( CExportCommandlet );

public:
	CExportCommandlet();
	~CExportCommandlet();

	// ICommandlet interface
	virtual const char* GetOneLiner() const override { return "Export single assets from the engine"; }
	virtual bool Execute( const red::CommandLine& options ) override;
	virtual void PrintHelp() const override;
};

RTTI_BEGIN_TYPE( CExportCommandlet )
	RTTI_PARENT_TYPE( red::ICommandlet )
RTTI_END_TYPE()



CExportCommandlet::CExportCommandlet()
	: ICommandlet( RED_NAME_CONSTEXPR( "export" ) )
{
}

CExportCommandlet::~CExportCommandlet()
{
}

bool CExportCommandlet::Execute( const red::CommandLine& options )
{	
	String inputPath, outputPath;

	// get file path (absolute)
	if ( options.GetFirstParam( "file", inputPath ) )
	{
		
	}
	else
	{
		RED_LOG_ERROR( "Missing input file path" );
		return false;
	}

	// get input file path

	red::AbsolutePath absPath;
	if ( options.GetFirstParam( "out", outputPath ) )
	{
		absPath = red::AbsolutePath::ParseFilePath( outputPath );
	}
	else
	{
		RED_LOG_ERROR( "Missing output file path" );
		return false;
	}

	// Load file
	THandle< CResource > res = LoadResource_DEPRECATED<CResource>( inputPath );
	if ( !res )
	{
		RED_LOG_ERROR( "Unable to load file '%hs' from depot", inputPath.AsChar() );
		return false;
	}

	// decompose output path
	const String outputFileExt = red::utils::ExtractExtension( outputPath );

	// find exporter
	IExporter* exporter = IExporter::FindExporter( res->GetClass(), outputFileExt );
	if ( !exporter )
	{
		RED_LOG_ERROR( "No exporter found that exports '%hs' into format '%hs'", res->GetClass()->GetName().AsChar(), outputFileExt.AsChar() );
		return false;
	}

	// setup export
	IExporter::ExportOptions exportOptions;

	// extra options
	{
		String lodTxt;
		if ( options.GetFirstParam( "lod", lodTxt ) )
		{
			exportOptions.m_lodToUse = atoi( lodTxt.AsChar() );
		}
	}

	exportOptions.m_saveFileFormat = CFileFormat( outputFileExt, String::EMPTY() );
	exportOptions.m_saveFilePath = absPath.AsString();
	exportOptions.m_resource = res;

	// export the file
	if ( !exporter->DoExport( exportOptions ) )
	{
		RED_LOG_ERROR( "Failed to export '%hs' into format '%hs'", inputPath.AsChar(), outputFileExt.AsChar() );
		return false;
	}

	// exported
	RED_LOG( "Exported '%hs' into '%hs'", inputPath.AsChar(), outputPath.AsChar() );
	return true;
}

void CExportCommandlet::PrintHelp() const
{
	RED_LOG( "Usage:"  );
	RED_LOG( "  export -depot=<depotbasepath> -file=<relativepath> -out=<absoluteoutputpath> [-lod=index]" );
	RED_LOG( ""  );
	RED_LOG( "Supported resource types and formats:" );

	// get all importers
	red::DynArray< const rtti::ClassType* > exporterClasses;
	GetRttiSystem().EnumClasses( ClassID< IExporter >(), exporterClasses );

	// get all supported resource classes
	red::DynArray< const rtti::ClassType* > resourceClasses;
	IExporter::EnumExportClasses( resourceClasses );

	// get all formats for each class
	for ( const auto* resourceClass : resourceClasses )
	{
		red::DynArray< CFileFormat > fileFormats;
		IExporter::EnumExportFormats( resourceClass, fileFormats );

		if ( !fileFormats.Empty() )
		{
			const CResource* defaultResource = resourceClass->GetDefaultObject< CResource >();
			RED_LOG( "  {extension:%hs, deprecated extension:%hs} (%hs) exportable into %d file format(s):", 
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

