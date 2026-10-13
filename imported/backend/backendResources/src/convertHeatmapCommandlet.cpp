/**
 * Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "../../../common/core/include/commandlet.h"
#include "../../../common/redCore/include/commandline.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/world/include/worldHeatmap.h"
#include "../../backendEngine/include/commandletEngineInstance.h"
#include "../../backendEngine/include/commandletUtils.h"
#include "../../backendEngine/include/resourceUtils.h"
#include "../../../common/resource/include/resourceSaver.h"
#include "../../backendEngine/include/resourceOptions.h"
#include "../../../common/redCore/include/absolutePath.h"
#include "../../../common/resource/include/jsonSerializer.h"

class ConvertHeatmap : public red::ICommandlet
{
	RTTI_DECLARE_TYPE( ConvertHeatmap );
public:
	ConvertHeatmap();
	virtual ~ConvertHeatmap() = default;

	// ICommandlet interface
	virtual Bool InitializeEnvironment() override;
	virtual void TeardownEnvironment() override;
	virtual void PrintHelp() const override;
	virtual const char* GetOneLiner() const override;
	virtual Bool Execute( const red::CommandLine& options ) override;

private:
	Bool ParseSettings( const red::CommandLine& options );

	commandlet::EngineInstance m_engineInstance;

	String m_inputDir;
	String m_outputDir;
};

RTTI_BEGIN_TYPE( ConvertHeatmap )
	RTTI_PARENT_TYPE( red::ICommandlet )
RTTI_END_TYPE()

ConvertHeatmap::ConvertHeatmap()
	: ICommandlet( RED_NAME_CONSTEXPR( "convertHeatmap" ) )
{
}

Bool ConvertHeatmap::InitializeEnvironment()
{
	if ( !m_engineInstance.Initialise() )
	{
		return false;
	}

	return true;
}

void ConvertHeatmap::TeardownEnvironment()
{
	m_engineInstance.Shutdown();
}

const char* ConvertHeatmap::GetOneLiner() const
{
	return "  convertHeatmap -inputDir=[directory with heatmaps] -outputDir=[output directories]";
}

void ConvertHeatmap::PrintHelp() const
{
	RED_LOG( "  convertHeatmap -inputDir=[directory with heatmaps] -outputDir=[output directories]" );
}

Bool ConvertHeatmap::ParseSettings( const red::CommandLine& options )
{
	if ( !options.GetFirstOption( "inputDir", m_inputDir ) )
	{
		RED_LOG_ERROR( "Please provide inputDir paramter" );
		return false;
	}

	if ( !options.GetFirstOption( "outputDir", m_outputDir ) )
	{
		RED_LOG_ERROR( "Please provide outputDir paramter" );
		return false;
	}
	
	return true;
}

Bool ConvertHeatmap::Execute( const red::CommandLine& options )
{
	if ( !ParseSettings( options ) )
	{
		RED_LOG( "Failed to parse settings from commandline.." );

		PrintHelp();
		RED_LOG_FLUSH_AND_WAIT();
		return false;
	}

	auto* depot = commandlet::GetEditorDepot();

	const red::DynArray< String > patterns{ {"*.heatmap"}, {"*.heatmaplayer"} };
	red::DynArray< res::ResourcePath > allHeatmaps = commandlet::ScanResourceDepotDirectories( *depot, m_inputDir, patterns );

	printf( String::Printf( "Found %u heatmap files\n", allHeatmaps.Size() ).AsChar() );

	for ( const res::ResourcePath& filePath : allHeatmaps )
	{
		printf( String::Printf( "Processing: %s\n", filePath.ToString().AsChar() ).AsChar() );

		auto heatmapToken = GResourceLoader->IssueLoadingRequest(filePath );
		heatmapToken->WaitUntilLoaded();

		if ( heatmapToken->HasFailed() )
		{
			RED_LOG( "Failed loading heatmap..." );
			RED_LOG_FLUSH_AND_WAIT();

			return false;
		}

		String fileName = red::paths::ExtractFileName( filePath.ToString() );
		fileName = red::paths::RemoveExtension( fileName ).ToString();
		fileName = String::Printf( "%s.json", fileName.AsChar() );
		
		// world::HeatmapResource
		const THandle< ISerializable > resource = Cast< ISerializable >( heatmapToken->GetResource() );

		String outData;

		serialization::json::JsonSerializer serializer( resource );
		serializer.Serialize( outData );

		red::StringBuilder< String > outputData;

		if ( const THandle< world::HeatmapLayer >& layer = Cast< world::HeatmapLayer >( resource ) )
		{
			outputData.Append( "\"data\": [" );

			const red::DynArray< Uint32 >& data = layer->GetData();

			printf( String::Printf( "Converting %d entires\n", data.Size() ).AsChar() );

			Uint32 progress = 0;

			for ( Uint32 i = 0; i < data.Size(); ++i )
			{
				if ( ++progress % 500000 == 0 )
				{
					printf( "." );
					progress = 0;
				}

				if ( i != 0  )
				{
					outputData.Append( String::Printf( "," ) );
				}
				
				if ( i % 100 == 0 )
				{
					outputData.Append( String::Printf( "\n" ) );
				}

				outputData.Append( String::Printf( "%d", data[ i ] ) );
			}
			
			outputData.Append( "]" );

			Uint32 index = outData.Size() - 8;
			outData.Insert( index, String::Printf( ",\n        " ) );
			outData.Insert( index + 11, outputData.ToString() );
		}

		const red::UniquePtr< IFile > file = GFileManager->CreateFileWriter( red::AbsolutePath::CreateFilePath( m_outputDir + "\\" + fileName ), FOF_AbsolutePath );
		if ( !red::SaveStringToFile( *file, outData ) )
		{
			return false;
		}
	}

	return true;
}