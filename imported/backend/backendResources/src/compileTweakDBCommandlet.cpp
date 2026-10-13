/*
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "../../../common/core/include/commandlet.h"
#include "../../../common/redCore/include/commandline.h"
#include "../../../common/gameTweakDB/include/tweakDB.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../backendData/include/commandletTaskReporter.h"
#include "../../backendEngine/include/commandletUtils.h"
#include "../../backendEngine/include/interactiveCommandletCommunicationHandler.h"
#include "../../../common/redSystem/include/redThreadsThread.h"

class CompileTweakDBCommandlet : public red::ICommandlet
{
	RTTI_DECLARE_TYPE( CompileTweakDBCommandlet );

public:

	CompileTweakDBCommandlet();
	~CompileTweakDBCommandlet();

	// ICommandlet interface
	virtual const char* GetOneLiner() const override { return "Compile TweakDB"; }
	virtual Bool Execute( const red::CommandLine& options ) override;
	virtual void PrintHelp() const override;
};

RTTI_BEGIN_TYPE( CompileTweakDBCommandlet )
	RTTI_PARENT_TYPE( red::ICommandlet )
RTTI_END_TYPE()

//////////////////////////////////////////////////////////////////////////

namespace
{
	class ErrorLogWriter
	{
	public:
		ErrorLogWriter( const red::DynArray< String >& dataPaths, const red::AbsolutePath& logFilePath )
		{
			if ( !logFilePath.Empty() )
			{
				m_logFile = GFileManager->CreateFileWriter( logFilePath, FOF_AbsolutePath );
			}

			Write( "Compiling TweakDB: " );
			for ( const String& path : dataPaths )
			{
				WriteLine( path );
			}
		}

		void WriteErrorLines( const red::DynArray< String >& lines )
		{
			for ( auto& line : lines )
			{
				WriteLine( line, stderr );
			}
		}

		void WriteSuccessInfo()
		{
			WriteLine( "No errors found" );
		}

	private:
		void Write( const red::StringView& line, FILE* stream = stdout )
		{
			fprintf( stream, "%hs", line.Data() );
			fflush( stream );

			if ( m_logFile != nullptr )
			{
				m_logFile->Serialize( const_cast< char* >( line.Data() ), line.Length() );
			}
		}

		void WriteLine( const red::StringView& line, FILE* stream = stdout )
		{
			static red::String eol( "\n" );
		
			fprintf( stream, "%hs\n", line.Data() );
			fflush( stream );

			if ( m_logFile != nullptr )
			{
				m_logFile->Serialize( const_cast< char* >( line.Data() ), line.Length() );
				m_logFile->Serialize( const_cast< void* >( eol.Data() ), eol.Length() );
			}
		}

		red::UniquePtr< IFile > m_logFile;
	};
}

//////////////////////////////////////////////////////////////////////////

CompileTweakDBCommandlet::CompileTweakDBCommandlet()
	: ICommandlet( RED_NAME_CONSTEXPR( "compileTweakDB" ) )
{
}

CompileTweakDBCommandlet::~CompileTweakDBCommandlet()
{
}

//////////////////////////////////////////////////////////////////////////

Bool CompileTweakDBCommandlet::Execute( const red::CommandLine& options )
{
	red::InteractiveCommandletCommunicationHandler comm{ options };

	red::Stages stages = comm.DeclareStages( { "Initialize TweakDB", "Compile TweakDB", " Compile TweakDB extras" } );

	Bool silent = options.HasOption( "silent" );

	red::DynArray< String > dlcs;
	if ( options.HasOption( "dlc" ) )
	{
		options.GetAllParams( "dlc", dlcs );
	}

	String outputPath;
	if ( options.HasOption( "output" ) )
	{
		options.GetFirstOption( "output", outputPath );
	}
	red::AbsolutePath output;
	if ( !outputPath.Empty() )
	{
		output = red::AbsolutePath::CreateDirPath( outputPath );
	}

	red::AbsolutePath logFilePath;
	if ( options.HasOption( "log" ) )
	{
		red::String path;
		options.GetFirstOption( "log", path );
		logFilePath = red::AbsolutePath::ParseFilePath( path );
	}
	
	///////

	commandlet::BackendDepotHolder depotHolder = commandlet::BackendDepotHolder();

	red::DynArray< String > errorLogLines;
	Bool success = true;

	// initialize TweakDB
	game::data::InitTweakDB();

	for ( const String& dlc : dlcs )
	{
		game::data::GetTweakDB().EnableDLC( dlc );
	}
	stages.FinishCurrentStage();

	// compile base blob
	success = game::data::GetTweakDB().Compile( silent, errorLogLines, output, &comm );
	stages.FinishCurrentStage();

	if ( success )
	{
		// compile the debug blobs
		game::data::GetTweakDB().LoadRawFiles();
		success = game::data::GetTweakDB().CompileExtras( silent, errorLogLines, output, &comm );
		stages.FinishCurrentStage();
	}

	ErrorLogWriter errorLogWriter( game::data::TweakDB::GetDataPaths( dlcs ), logFilePath );

	if ( success )
	{
		errorLogWriter.WriteSuccessInfo();
	}
	else
	{
		errorLogWriter.WriteErrorLines( errorLogLines );
	}

	return success;
}

void CompileTweakDBCommandlet::PrintHelp() const
{
	RED_LOG( "Usage:"  );
	RED_LOG( "    compileTweakDB <options>" );
	RED_LOG( "Options:" );
	RED_LOG( "  -output=<path>  - Path for the compiled binary blob (without the filename). If not specified, default one will be used." );
	RED_LOG( "  -log=<path>     - Path for the output log file. If not specified, no log file will be created." );
	RED_LOG( "  -silent         - No display of the parsing/validation/processing errors window" );
}