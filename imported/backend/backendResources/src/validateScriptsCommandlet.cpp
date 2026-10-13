/*
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "../../../common/core/include/commandlet.h"
#include "../../../common/redCore/include/commandline.h"
#include "../../../common/redReflection/include/scriptingSystem.h"
#include "../../../common/redReflection/include/scriptDataEnvironment.h"
#include "../../../common/engine/include/scriptHelper.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../backendEngine/include/interactiveCommandletCommunicationHandler.h"

class ValidateScriptsCommandlet : public red::ICommandlet
{
	RTTI_DECLARE_TYPE( ValidateScriptsCommandlet );

public:

	ValidateScriptsCommandlet();
	~ValidateScriptsCommandlet();

	// ICommandlet interface
	virtual const char* GetOneLiner() const override { return "Validate scripts"; }
	virtual Bool Execute( const red::CommandLine& options ) override;
	virtual void PrintHelp() const override;
};

RTTI_BEGIN_TYPE( ValidateScriptsCommandlet )
	RTTI_PARENT_TYPE( red::ICommandlet )
RTTI_END_TYPE()

//////////////////////////////////////////////////////////////////////////

namespace
{
	class ErrorReporter : public IScriptDataErrorReporter
	{
	public:

		RED_INLINE ErrorReporter( const red::AbsolutePath& blobPath, const red::AbsolutePath& logFilePath )
			: m_noErrors( true )
			, m_logFile()
		{
			if ( !logFilePath.Empty() )
			{
				m_logFile = GFileManager->CreateFileWriter( logFilePath, FOF_AbsolutePath );
			}

			Write( "Validating scripts: " );
			WriteLine( blobPath.AsString() );
		}

		RED_INLINE ~ErrorReporter()
		{
			if ( m_noErrors )
			{
				WriteLine( "No errors found" );
			}
		}

		virtual void ValidationError( STATIC_CHECK_PRINTF_MSC const char* format, ... ) override final
		{
			va_list args;
			va_start( args, format );
			red::String msg = red::String::PrintfV( format, args );
			va_end( args );

			WriteLine( msg, stderr );
			fflush( stderr );

			m_noErrors = false;
		}

		virtual void BindingError( STATIC_CHECK_PRINTF_MSC const char* format ... ) override final
		{
			va_list args;
			va_start( args, format );
			red::String msg = String::PrintfV( format, args );
			va_end( args );

			WriteLine( msg, stderr );
			fflush( stderr );

			m_noErrors = false;
		}

		RED_INLINE Bool NoErrors() const { return m_noErrors; }

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

		Bool m_noErrors;
		red::UniquePtr<IFile> m_logFile;
	};
}

//////////////////////////////////////////////////////////////////////////

ValidateScriptsCommandlet::ValidateScriptsCommandlet()
	: ICommandlet( RED_NAME_CONSTEXPR( "validateScripts" ) )
{}

ValidateScriptsCommandlet::~ValidateScriptsCommandlet()
{}

//////////////////////////////////////////////////////////////////////////

Bool ValidateScriptsCommandlet::Execute( const red::CommandLine& options )
{
	red::InteractiveCommandletCommunicationHandler communicationHandler{ options };
	red::Stages stages = communicationHandler.DeclareStages( { "Validating scripts" } );

	// where to load scripts from
	red::AbsolutePath blobPath;
	if ( options.HasOption( "blob" ) )
	{
		red::String path;
		options.GetFirstOption( "blob", path );
		blobPath = red::AbsolutePath::ParseFilePath( path );
	}
	
	// where to log validation errors
	red::AbsolutePath logFilePath;
	if ( options.HasOption( "log" ) )
	{
		red::String path;
		options.GetFirstOption( "log", path );
		logFilePath = red::AbsolutePath::ParseFilePath( path );
	}
	// else it's empty and we don't log validation errors

	ErrorReporter err( blobPath, logFilePath );

	const Bool res = IScriptingSystem::GetInstance().LoadScripts( blobPath, err );
	
	return res && err.NoErrors();
}

void ValidateScriptsCommandlet::PrintHelp() const
{
	RED_LOG( "Usage:"  );
	RED_LOG( "    validateScripts <options>" );
	RED_LOG( "Options:" );
	RED_LOG( "  -blob=<path>    - Path to the compiled scripts blob" );
	RED_LOG( "  -log=<path>     - Path for the output log file. If not specified, no log file will be created." );
}