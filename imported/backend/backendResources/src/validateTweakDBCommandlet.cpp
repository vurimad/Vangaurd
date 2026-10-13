/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "../../../common/redCore/include/commandline.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../backendEngine/include/commandletUtils.h"
#include "../../../common/redJobs2/include/jobBuilder.h"
#include "../../../common/redReflection/include/resourceDepot.h"
#include "../../backendData/include/resourceDatabase.h"
#include "../../backendData/include/fileLinkMap.h"
#include "../../../common/gameTweakDB/include/tweakDB.h"
#include "../../../common/gameTweakDB/include/tweakDBLoader.h"
#include "../../backendEngine/include/interactiveCommandletCommunicationHandler.h"
#include "../../../common/core/include/commandlet.h"

namespace
{
	enum class ResourceState
	{
		Ok,
		Missing,
		NonProduction
	};

	Bool IsProduction( const res::ResourcePath& path )
	{
		auto str = path.ToStringView();
		return str.StartsWith( "engine\\" ) || str.StartsWith( "base\\" );
	}

	class LogWriter
	{
	public:
		LogWriter( const red::DynArray< String >& dataPaths, const red::AbsolutePath& logFilePath )
		{
			if ( !logFilePath.Empty() )
			{
				m_logFile = GFileManager->CreateFileWriter( logFilePath, FOF_AbsolutePath );
			}

			for ( const String& path : dataPaths )
			{
				WriteLine( String::Printf( "Validating TweakDB: %s", path.AsChar() ) );
			}
		}
		
		void Write( const red::StringView& line )
		{
			if ( m_stream )
			{
				fprintf( m_stream, "%hs", line.Data() );
				fflush( m_stream );
			}

			if ( m_logFile != nullptr )
			{
				m_logFile->Serialize( const_cast<char*>( line.Data() ), line.Length() );
			}
		}

		void WriteLine( const red::StringView& line )
		{
			static red::String eol( "\r\n" );

			if ( m_stream )
			{
				fprintf( m_stream, "%hs%hs", line.Data(), eol.AsChar() );
				fflush( m_stream );
			}

			if ( m_logFile != nullptr )
			{
				m_logFile->Serialize( const_cast<char*>( line.Data() ), line.Length() );
				m_logFile->Serialize( const_cast<void*>( eol.Data() ), eol.Length() );
			}
		}

		void WriteInfoLines( const red::DynArray< String >& lines )
		{
			WriteLine( "-- Info" );
			for ( auto& line : lines )
			{
				WriteLine( line );
			}
		}

		void WriteErrorLines( const red::DynArray< String >& lines )
		{
			WriteLine( "-- Errors" );
			for ( auto& line : lines )
			{
				WriteLine( line );
			}
		}

		void WriteSuccessInfo()
		{
			WriteLine( "No errors found" );
		}

	private:
		red::UniquePtr< IFile > m_logFile;
		FILE* m_stream = stdout;
	};
}

//////////////////////////////////////////////////////////////////////////

class ValidateTweakDBCommandlet : public red::ICommandlet
{
	RTTI_DECLARE_TYPE( ValidateTweakDBCommandlet );

public:

	ValidateTweakDBCommandlet();
	~ValidateTweakDBCommandlet();

	// ICommandlet interface
	virtual const char* GetOneLiner() const override { return "Validate TweakDB"; }
	virtual Bool Execute( const red::CommandLine& options ) override;
	virtual void PrintHelp() const override;

private:
	Bool ValidateTweakFiles( red::DynArray< String >& errorLogLines, Uint64* hash = nullptr );
	void ListResourceReferences( red::DynArray< String >& infoLogLines );
	Bool ValidateResourceReferences( red::DynArray< String >& infoLogLines, red::DynArray< String >& errorLogLines );
	ResourceState ValidateResourceReference( const res::ResourcePath& resourcePath );
	red::DynArray< res::ResourcePath > GetResRefs( const game::data::VariableNode& variable );
	red::DynArray< res::ResourcePath > GetResRefs( const game::data::GroupNode::GroupVariable& variable );
	red::String FormatResRefEntry( const res::ResourcePath& reference, const ResourceState state );
	red::String FormatResRefError( const red::String& file, const Uint32 line, const res::ResourcePath& reference, const ResourceState state );

	void InitialiseDatabase( commandlet::BackendDepotHolder& depotHolder );
	res::IBackendResourceDepot* m_depot;
};

RTTI_BEGIN_TYPE( ValidateTweakDBCommandlet )
	RTTI_PARENT_TYPE( red::ICommandlet )
RTTI_END_TYPE()

//////////////////////////////////////////////////////////////////////////

ValidateTweakDBCommandlet::ValidateTweakDBCommandlet()
	: ICommandlet( RED_NAME_CONSTEXPR( "validateTweakDB" ) )
{
}

ValidateTweakDBCommandlet::~ValidateTweakDBCommandlet()
{
}

//////////////////////////////////////////////////////////////////////////

Bool ValidateTweakDBCommandlet::Execute( const red::CommandLine& options )
{
	red::InteractiveCommandletCommunicationHandler communicationHandler{ options };
	red::Stages stages = communicationHandler.DeclareStages( { "Initialize", "Validate tweak files" } );

	red::DynArray< String > dlcs;
	if ( options.HasOption( "dlc" ) )
	{
		options.GetAllParams( "dlc", dlcs );
	}

	red::AbsolutePath logFilePath;
	if ( options.HasOption( "log" ) )
	{
		red::String path;
		options.GetFirstOption( "log", path );
		logFilePath = red::AbsolutePath::ParseFilePath( path );
	}

	Bool calculateHash = options.HasOption( "calculateHash" );
	
	Bool listResources = options.HasOption( "listResources" );
	Bool validateResources = options.HasOption( "validateResources" );

	LogWriter logWriter( game::data::TweakDB::GetDataPaths( dlcs ), logFilePath );
	red::DynArray< String > infoLogLines;
	red::DynArray< String > errorLogLines;

	Uint64 hash = 0;

	game::data::InitTweakDB();
	stages.FinishCurrentStage();

	for( const String& dlc : dlcs )
	{
		game::data::GetTweakDB().EnableDLC( dlc );
	}

	Bool success = ValidateTweakFiles( errorLogLines, calculateHash ? &hash : nullptr );
	stages.FinishCurrentStage();

	if ( calculateHash )
	{
		logWriter.WriteLine( String::Printf( "TweakDB debug hash: %" RED_PRIu64, hash ) );
	}
	else if ( validateResources )
	{
		red::Stages childStage = communicationHandler.DeclareStages( { "Validate resource references" } );

		success = ValidateResourceReferences( infoLogLines, errorLogLines );
		logWriter.WriteInfoLines( infoLogLines );
	}
	else if ( listResources )
	{
		red::Stages childStage = communicationHandler.DeclareStages( { "List resource references" } );

		ListResourceReferences( infoLogLines );
		logWriter.WriteInfoLines( infoLogLines );
	}

	game::data::DeinitTweakDB();

	if ( success )
	{
		logWriter.WriteSuccessInfo();
	}
	else
	{
		logWriter.WriteErrorLines( errorLogLines );
	}

	return success;
}

Bool ValidateTweakDBCommandlet::ValidateTweakFiles( red::DynArray< String >& errorLogLines, Uint64* hash )
{
	return game::data::GetTweakDB().ValidateOnly( errorLogLines, hash );
}

Bool ValidateTweakDBCommandlet::ValidateResourceReferences( red::DynArray< String >& infoLogLines, red::DynArray< String >& errorLogLines )
{
	commandlet::BackendDepotHolder depotHolder;
	//InitialiseDatabase( depotHolder );

	//if ( !m_linkMap.Initialise( *m_depot ) )
	//{
	//	return false;
	//}

	auto packages = game::data::GetTweakDB().GetLoader().GetPackages();
	for( const auto package : packages.Values() )
	{
		// check by individual files
		for ( const auto& fileNode : package->GetFiles() )
		{
			// check free variables
			for ( const auto& variable : fileNode->GetVariables() )
			{
				for( const auto& path : GetResRefs( *variable ) )
				{
					ResourceState state = ValidateResourceReference( path );
					if ( state != ResourceState::Ok )
					{
						red::String error = FormatResRefError( fileNode->GetFileName(), variable->GetFileContext().m_line, path, state );
						errorLogLines.PushBack( error );
					}
				}
			}
			
			// check group variables
			for ( const auto group : fileNode->GetGroups() )
			{
				for ( auto variable : group->GetVariables() )
				{
					for ( const auto& path : GetResRefs( variable ) )
					{
						ResourceState state = ValidateResourceReference( path );
						if ( state != ResourceState::Ok )
						{
							red::String error = FormatResRefError( fileNode->GetFileName(), variable->GetFileContext().m_line, path, state );
							errorLogLines.PushBack( error );
						}
					}
				}
			}
		}
	}

	return errorLogLines.Empty();
}

void ValidateTweakDBCommandlet::ListResourceReferences( red::DynArray< String >& infoLogLines )
{
	commandlet::BackendDepotHolder depotHolder;
	//InitialiseDatabase( depotHolder );

	//if ( !m_linkMap.Initialise( *m_depot ) )
	//{
	//	return false;
	//}

	auto packages = game::data::GetTweakDB().GetLoader().GetPackages();
	for ( const auto package : packages.Values() )
	{
		// check by individual files
		for ( const auto& fileNode : package->GetFiles() )
		{
			// check free variables
			for ( const auto& variable : fileNode->GetVariables() )
			{
				for ( const auto& path : GetResRefs( *variable ) )
				{
					ResourceState state = ValidateResourceReference( path );
					red::String entry = FormatResRefEntry( path, state );
					infoLogLines.PushBack( entry );
				}
			}

			// check group variables
			for ( const auto group : fileNode->GetGroups() )
			{
				for ( auto variable : group->GetVariables() )
				{
					for ( const auto& path : GetResRefs( variable ) )
					{
						ResourceState state = ValidateResourceReference( path );
						red::String entry = FormatResRefEntry( path, state );
						infoLogLines.PushBack( entry );
					}
				}
			}
		}
	}
}

red::String ValidateTweakDBCommandlet::FormatResRefError( const red::String& file, const Uint32 line, const res::ResourcePath& reference, const ResourceState state )
{
	const char* issue = "";
	switch ( state )
	{
	case ResourceState::Missing:
		issue = "[MISSING]";
		break;
	case ResourceState::NonProduction:
		issue = "[NONPROD]";
		break;
	}

	return String::Printf( "File: %s (%u)\r\n\t%s \"%s\"", file.AsChar(), line, issue, reference.ToDebugString() );
}

red::String ValidateTweakDBCommandlet::FormatResRefEntry( const res::ResourcePath& reference, const ResourceState state )
{
	const char* prefix = "";
	switch ( state )
	{
	case ResourceState::Ok:
		prefix = "[V]";
		break;
	case ResourceState::Missing:
		prefix = "[M]";
		break;
	case ResourceState::NonProduction:
		prefix = "[N]";
		break;
	}

	return String::Printf( "%s %s", prefix, reference.ToDebugString() );
}

ResourceState ValidateTweakDBCommandlet::ValidateResourceReference( const res::ResourcePath& resourcePath )
{
	if ( !GetResourceDepot()->ResourceExists( resourcePath ) )
	{
		return ResourceState::Missing;
	}

	if ( !IsProduction( resourcePath ) )
	{
		return ResourceState::NonProduction;
	}

	return ResourceState::Ok;
}

red::DynArray< res::ResourcePath > ValidateTweakDBCommandlet::GetResRefs( const game::data::GroupNode::GroupVariable& variable )
{
	if ( variable.GetDeriveInfo() != game::data::GroupNode::GroupVariable::DeriveInfo::FullyDerived )
	{
		return GetResRefs( *( variable.GetNode() ) );
	}

	return red::DynArray< res::ResourcePath >();
}

red::DynArray< res::ResourcePath > ValidateTweakDBCommandlet::GetResRefs( const game::data::VariableNode& variable )
{
	red::DynArray< res::ResourcePath > paths;

	if ( variable.GetTypeEnum() == game::data::TweakDBType::ResRef )
	{
		const auto& values = variable.GetValues();
		for ( const auto& value : values )
		{
			const red::String& path = value->GetSimpleValueData()->GetData();

			if ( path.Empty() )
				continue;

			paths.PushBack( res::ResourcePath::Build( path ) );
		}
	}

	return paths;
}

void ValidateTweakDBCommandlet::InitialiseDatabase( commandlet::BackendDepotHolder& depotHolder )
{
	//m_depot = depotHolder.GetDepot();

	//job::Counter counter;
	//red::AbsolutePath resdbCachePath = depotHolder.GetCachePath().AddFilePath( resdb::Database::GetDefaultFilename() );
	//m_database.Initialise( *m_depot, resdbCachePath, counter );
	//job::FlushCounter( counter );
}

void ValidateTweakDBCommandlet::PrintHelp() const
{
	RED_LOG( "Usage:"  );
	RED_LOG( "    validateTweakDB <options>" );
	RED_LOG( "Options:" );
	RED_LOG( "  -log=<path>        - Path for the output log file. If not specified, no log file will be created." );
	RED_LOG( "  -calculateHash     - Calculate hash for the data and print it to output." );
	RED_LOG( "  -listResources     - ?" );
	RED_LOG( "  -validateResources - ?" );
}