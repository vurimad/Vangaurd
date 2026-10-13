/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"

#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/core/include/commandlet.h"
#include "../../../common/redCore/include/commandline.h"
#include "../../../common/redReflection/include/serializationLoader.h"
#include "../../../common/redReflection/include/serializationSaver.h"
#include "../../../common/redReflection/include/serializationBinaryStructureMapper.h"
#include "../../../common/redReflection/include/packageCompiler.h"
#include "../../../common/redReflection/include/deferredBufferMapper.h"

#include "../../../common/resource/include/resourceEditorDepotImpl.h"

#include "../../backendEngine/include/commandletEngineInstance.h"
#include "../../backendEngine/include/commandletUtils.h"
#include "../../backendData/include/resourceDatabase.h"
#include "../../backendData/include/linkResolver.h"



class MoveTextureCommandlet : public red::ICommandlet
{
	RTTI_DECLARE_TYPE( MoveTextureCommandlet );
public:

	MoveTextureCommandlet();

	virtual const char* GetOneLiner() const override;
	virtual void PrintHelp() const override;
	virtual bool Execute( const red::CommandLine& options ) override;

	virtual bool InitializeEnvironment() override;
	virtual void TeardownEnvironment() override;

private:
	bool ParseOptions( const red::CommandLine& options );

	res::ResourceEditorDepot* m_editorDepot;
	resdb::Database m_database;

	commandlet::EngineInstance m_engineInstance;

	red::DynArray< res::ResourcePath > m_resources;
	red::String m_changelist;

	bool m_dryrun;
	bool m_onlyOutputActions;
	bool m_processTextures;
	bool m_processMultilayer;
};

RTTI_BEGIN_TYPE( MoveTextureCommandlet )
	RTTI_PARENT_TYPE( red::ICommandlet )
RTTI_END_TYPE()

//------------------------------------------------------------------------------

MoveTextureCommandlet::MoveTextureCommandlet()
	: red::ICommandlet( RED_NAME_CONSTEXPR( "move-textures" ) )
	, m_editorDepot( commandlet::GetEditorDepot() )
	, m_dryrun( false )
	, m_onlyOutputActions( false )
{
}

const char* MoveTextureCommandlet::GetOneLiner() const
{
	return "";
}

void MoveTextureCommandlet::PrintHelp() const
{
}

bool MoveTextureCommandlet::InitializeEnvironment()
{
	return m_engineInstance.Initialise();
}

void MoveTextureCommandlet::TeardownEnvironment()
{
	m_engineInstance.Shutdown();
}

//------------------------------------------------------------------------------

bool MoveTextureCommandlet::ParseOptions( const red::CommandLine& options )
{
	m_resources = commandlet::ParseResourcePathsFromCommandLine( *m_editorDepot, options );
	if ( m_resources.Empty() )
	{
		return false;
	}

	String param;
	if ( options.GetFirstOption( "changelist", param ) )
	{
		m_changelist = param;
	}

	m_dryrun = options.HasOption( "dryrun" );

	m_onlyOutputActions = options.HasOption( "output-changes" );

	m_processTextures = !options.HasOption( "notextures" );
	m_processMultilayer = !options.HasOption( "nomultilayer" );

	return true;
}

//------------------------------------------------------------------------------

namespace move_textures
{

// Convenience constexpr hash function
constexpr Uint32 Hash( const char* str )
{
	return red::CalculateHash32( str );
}

bool IsTextureResource( const res::ResourcePath& path )
{
	auto ext = red::paths::GetExtension( path.ToStringView() );
	const Uint32 extHash = Hash( ext.Data() );
	switch ( extHash )
	{
	case Hash( "xbm" ):
	case Hash( "xcube" ):
	case Hash( "cubemap" ):
	case Hash( "texarray" ):
		return true;
	};
	return false;
}

bool IsMultilayerResource( const res::ResourcePath& path )
{
	auto ext = red::paths::GetExtension( path.ToStringView() );
	const Uint32 extHash = Hash( ext.Data() );
	switch ( extHash )
	{
	case Hash( "mlmask" ):
	case Hash( "mlsetup" ):
		return true;
	};
	return false;
}

bool ResourceNeedsMoving( const res::ResourcePath& path )
{
	auto parentPath = red::paths::GetParentPath( path.ToStringView() );
	if ( parentPath.EndsWith( "\\textures\\" ) )
	{
		return false;
	}

	return true;
}

void FilterTextures( red::DynArray< res::ResourcePath >& resources, bool processTextures, bool processMultilayer )
{
	auto filterFunc = [processTextures, processMultilayer]( const res::ResourcePath& path ) -> bool
	{
		if ( ( processTextures && IsTextureResource( path ) ) || ( processMultilayer && IsMultilayerResource( path ) ) )
		{
			if ( ResourceNeedsMoving( path ) )
			{
				return false;
			}
		}
		return true;
	};

	auto iter = std::remove_if( resources.Begin(), resources.End(), filterFunc );
	resources.Remove( iter, resources.End() );
}

red::String GetTargetTexturePath( const red::StringView& path )
{
	String filename = red::paths::ExtractFileName( path );
	String targetPath = red::paths::ExtractParentPath( path );
	targetPath += "textures\\";
	targetPath += filename;
	return targetPath;
}

//------------------------------------------------------------------------------

enum class MoveStatus : Uint32
{
	OK,
	TARGET_PATH_TOO_LONG,
	TARGET_ALREADY_EXISTS,
	PERFORCE_LOCKED,
	MOVE_FAILED,
	DEPENDENT_FAILED,
	MAX
};

struct MoveInfo
{
	res::ResourcePath sourcePath;
	res::ResourcePath targetPath;
	red::DynArray< res::ResourcePath > dependentPaths;
	red::AbsolutePath sourceAbsolutePath;
	red::AbsolutePath targetAbsolutePath;
	MoveStatus status = MoveStatus::OK;
};

enum class RelinkStatus : Uint32
{
	OK,
	PERFORCE_LOCKED,
	RELINK_FAILED,
};

//------------------------------------------------------------------------------

bool UpdateLinkFile( const red::AbsolutePath& linkFilePath, const red::HashMap< res::ResourcePath, res::ResourcePath >& mapping )
{
	red::String currentLinkText;
	if ( !red::LoadFileToString( linkFilePath, currentLinkText ) )
	{
		return false;
	}

	const res::ResourcePath currentPath = res::ResourcePath::Build( currentLinkText );
	auto iter = mapping.Find( currentPath );
	if ( iter == mapping.End() )
	{
		return false;
	}

	const res::ResourcePath& targetPath = iter.Value();
	red::String targetLinkText = targetPath.ToString();
	if ( !red::SaveStringToFile( linkFilePath, targetLinkText ) )
	{
		return false;
	}

	return true;
}

bool Relink_LoadResource( const res::ResourcePath& path, THandle< CResource >& resource )
{
	res::IssueLoadingRequestParameter param{ path };
	param.skipImports = true;
	param.skipPostLoad = true;
	param.postLoadFlags.skipUploadRenderData = true;

	auto token = GResourceLoader->IssueLoadingRequest( param );
	if ( token->HasFailed() )
	{
		RED_LOG_ERROR( "Resave: Could not load %hs for reading", path.ToDebugString() );
		return false;
	}

	resource = token->WaitUntilLoaded();
	if ( !resource || token->HasFailed() )
	{
		RED_LOG_ERROR( "Resave: Failed to load %hs", path.ToDebugString() );
		return false;
	}

	// Make sure all buffers are read so that the async source isn't needed anymore
	serialization::DeferredDataBufferLoader bufferLoader;
	bufferLoader.Run( resource );

	return true;
}

bool Relink_SaveResource( const res::ResourcePath& path, const THandle< CResource >& resource )
{
	auto writer = commandlet::GetEditorDepot()->CreateResourceWriter( path );
	if ( !writer )
	{
		RED_LOG_ERROR( "Resave: Could not open %hs for writing", path.ToDebugString() );
		return false;
	}

	const auto extension = red::utils::ExtractExtension( path.ToStringView() );
	auto saver = serialization::ISaver::CreateSaver( extension.AsChar() );
	if ( !saver )
	{
		RED_LOG_ERROR( "Resave: Could not create saver for file %hs with extension %hs", path.ToDebugString(), extension.AsChar() );
		return false;
	}

	serialization::SavingContext context{ resource };
	if ( !saver->SaveObjects( *writer, context ) )
	{
		RED_LOG_ERROR( "Resave: Error saving objects to %hs", path.ToDebugString() );
		return false;
	}

	if ( writer->HasErrors() )
	{
		return false;
	}

	return true;
}

bool RelinkResource( const res::ResourcePath& path )
{
	THandle< CResource > resource;

	if ( !Relink_LoadResource( path, resource ) )
	{
		return false;
	}

	if ( !Relink_SaveResource( path, resource ) )
	{
		return false;
	}

	return true;
}


//------------------------------------------------------------------------------

struct PerforceOptions
{
	String changelist;
	bool dryrun;
};

// Simple function to mark all files that we want to touch for edit in Perforce
// This includes files that we want to move as they need to be marked for edit first
// NOTE: The actual p4 command does not behave well and will always return 0 even if the
// command did not fulfil the desired action. For example if we cannot mark a file for
// editing because it is locked, it will still return 0 as if it succeeded
// Therefore we will just mark all files for editing and we will check the read only
// flag before doing anything else
bool PerforceMarkForEditUsingFile( const red::DynArray< red::AbsolutePath >& filesToMarkForEdit, const PerforceOptions& options )
{
	red::AbsolutePath editListPath = red::paths::GetCurrentWorkingDirectory().AddFilePath( "FilesToEdit.txt" );
	FILE* editListFile = fopen( editListPath.AsChar(), "w" );
	if ( editListFile == nullptr )
	{
		RED_LOG_ERROR( "Could not open %hs for writing Perforce edit file to!", editListPath.AsChar() );
		return false;
	}

	for ( const auto& path : filesToMarkForEdit )
	{
		fputs( path.AsChar(), editListFile );
		fputs( "\n", editListFile );
	}

	fflush( editListFile );
	fclose( editListFile );

	red::String p4EditCommand = String::Printf( "p4 -x %s edit", editListPath.AsChar() );
	if ( options.dryrun )
	{
		p4EditCommand += " -n";
	}
	if ( !options.changelist.Empty() )
	{
		p4EditCommand += " -c ";
		p4EditCommand += options.changelist;
	}

	red::AbsolutePath editOutputPath = red::paths::GetCurrentWorkingDirectory().AddFilePath( "PerforceEditOutput.txt" );
	p4EditCommand += String::Printf( " >%hs 2>&1", editOutputPath.AsChar() );

	RED_LOG_TRACE( "Running: '%hs'", p4EditCommand.AsChar() );
	auto result = std::system( p4EditCommand.AsChar() );
	if ( result != 0 )
	{
		RED_LOG_ERROR( "Failed to execute '%hs', error code %u", p4EditCommand.AsChar(), result );
		return false;
	}

	return true;
}

bool PerforceMoveResources( const red::AbsolutePath& sourcePath, const red::AbsolutePath& targetPath, const red::AbsolutePath& outputCapturePath, const PerforceOptions& options )
{
	red::String p4MoveCommand{ "p4 move" };
	if ( options.dryrun )
	{
		p4MoveCommand += " -n";
	}
	if ( !options.changelist.Empty() )
	{
		p4MoveCommand += " -c ";
		p4MoveCommand += options.changelist;
	}
	p4MoveCommand += String::Printf( " %s %s >%hs 2>&1", sourcePath.AsChar(), targetPath.AsChar(), outputCapturePath.AsChar() );

	RED_LOG_TRACE( "Running: '%hs'", p4MoveCommand.AsChar() );
	auto result = std::system( p4MoveCommand.AsChar() );
	if ( result != 0 )
	{
		RED_LOG_ERROR( "Failed to execute '%hs', error code %u", p4MoveCommand.AsChar(), result );
		return false;
	}

	return true;
}

//------------------------------------------------------------------------------

} // move_textures
using namespace move_textures;

bool MoveTextureCommandlet::Execute( const red::CommandLine& options )
{
	if ( !ParseOptions( options ) )
	{
		return false;
	}

	PerforceOptions p4Options{ m_changelist, m_dryrun };

	//
	// Filter input resources to just textures
	//

	FilterTextures( m_resources, m_processTextures, m_processMultilayer );
	if ( m_resources.Empty() )
	{
		RED_LOG( "Nothing to move!" );
		return true;
	}

	//
	// Figure out what other files need to be relinked when we move the textures
	//

	// List of resources to move and the related information, organised by source path
	red::HashMap< res::ResourcePath, MoveInfo > moveActions;
	moveActions.Reserve( m_resources.Size() );

	red::HashMap< res::ResourcePath, res::ResourcePath > resourcesToMove;
	red::Set< res::ResourcePath > resourcesToRelink;

	red::AbsolutePath cachePath = GFileManager->GetCacheDirectory().AddFilePath( resdb::Database::GetDefaultFilename() );
	m_database.Initialise( *m_editorDepot, cachePath );

	for ( const auto& path : m_resources )
	{
		MoveInfo moveInfo;
		moveInfo.sourcePath = path;

		red::String targetPathStr = GetTargetTexturePath( path.ToStringView() );
		if ( targetPathStr.Length() >= res::ResourcePath::MAX_LENGTH )
		{
			moveInfo.status = MoveStatus::TARGET_PATH_TOO_LONG;
			continue;
		}

		moveInfo.targetPath = res::ResourcePath::Build( targetPathStr );
		if ( m_editorDepot->ResourceExists( moveInfo.targetPath ) )
		{
			moveInfo.status = MoveStatus::TARGET_ALREADY_EXISTS;
			continue;
		}

		resourcesToMove.Insert( moveInfo.sourcePath, moveInfo.targetPath );

		moveInfo.dependentPaths = resdb::Database::ExtractFromPaths( m_database.GetBackwardLinks( path ) );
		std::sort( moveInfo.dependentPaths.Begin(), moveInfo.dependentPaths.End(), res::SortByDirectory );
		for ( const auto& depPath : moveInfo.dependentPaths )
		{
			resourcesToRelink.InsertUnsorted( moveInfo.dependentPaths );
		}

		moveActions.Set( path, moveInfo );
	}

	if ( resourcesToMove.Empty() && resourcesToRelink.Empty() )
	{
		RED_LOG( "Nothing to move!" );
		return true;
	}

	resourcesToRelink.MakeClean();

	if ( m_onlyOutputActions )
	{
		red::AbsolutePath actionsPath = red::paths::GetCurrentWorkingDirectory().AddFilePath( "TextureMoveActions.txt" );
		FILE* actionsFile = fopen( actionsPath.AsChar(), "w" );
		if ( actionsFile == nullptr )
		{
			return false;
		}

		for ( const auto& action : moveActions.GetValues() )
		{
			fprintf( actionsFile, "*\tmove:\t%s -> %s\n", action.sourcePath.ToDebugString(), action.targetPath.ToDebugString() );
			for ( const auto& path : action.dependentPaths )
			{
				fprintf( actionsFile, "\trelink:\t%s\n", path.ToDebugString() );
			}
		}

		fflush( actionsFile );
		fclose( actionsFile );
		return true;
	}

	// List of resources to relink and the action information, organised by path
	red::HashMap< res::ResourcePath, RelinkStatus > relinkActions;
	relinkActions.Reserve( resourcesToRelink.Size() );
	for ( const auto& path : resourcesToRelink )
	{
		relinkActions.Set( path, RelinkStatus::OK );
	}

	//
	// Mark all files that we need to touch for edit in Perforce
	//

	// Simple cache for translating between resource and absolute paths
	red::HashMap< res::ResourcePath, red::AbsolutePath > resourceToAbsoluteMap;
	resourceToAbsoluteMap.Reserve( resourcesToRelink.Size() + resourcesToMove.Size() );

	{
		red::DynArray< red::AbsolutePath > filesToMarkForEdit;
		filesToMarkForEdit.Reserve( resourcesToRelink.Size() + resourcesToMove.Size() );

		for ( const auto& path : resourcesToRelink )
		{
			auto absPath = m_editorDepot->TranslateResourcePath( path );
			resourceToAbsoluteMap.Set( path, absPath );
			filesToMarkForEdit.PushBack( absPath );
		}
		for ( const auto& path : resourcesToMove.GetKeys() )
		{
			auto absPath = m_editorDepot->TranslateResourcePath( path );
			resourceToAbsoluteMap.Set( path, absPath );
			filesToMarkForEdit.PushBack( absPath );
		}
		// Manually removing duplicates because we want the resulting array sorted by directory
		std::sort( filesToMarkForEdit.Begin(), filesToMarkForEdit.End(), red::SortByDirectory );
		auto iter = std::unique( filesToMarkForEdit.Begin(), filesToMarkForEdit.End() );
		filesToMarkForEdit.Remove( iter, filesToMarkForEdit.End() );

		if ( !PerforceMarkForEditUsingFile( filesToMarkForEdit, p4Options ) )
		{
			RED_LOG_ERROR( "Failed to run the perforce edit command!" );
			return false;
		}
	}

	//
	// Relink affected files and change links to point to updated paths
	//

	commandlet::FileLinkResolver linkResolver{ resourcesToMove };
	serialization::StructureMapper::SetPathResolver( &linkResolver );
	red::PackageCompiler::Internal_SetPathResolver( &linkResolver );

	for ( const auto& path : resourcesToRelink )
	{
		red::AbsolutePath absPath = resourceToAbsoluteMap[ path ];
		if ( GFileManager->IsFileReadOnly( absPath ) )
		{
			relinkActions[ path ] = RelinkStatus::PERFORCE_LOCKED;
			continue;
		}

		auto str = path.ToStringView();
		bool relinked = false;
		if ( str.EndsWith( ".link" ) )
		{
			// We can just change the contents of a link file, no need to create the new one
			relinked = UpdateLinkFile( absPath, resourcesToMove );
		}
		else
		{
			// Perform relink
			relinked = RelinkResource( path );
		}

		if ( !relinked )
		{
			relinkActions[ path ] = RelinkStatus::RELINK_FAILED;
			continue;
		}
	}

	red::PackageCompiler::Internal_SetPathResolver( nullptr );
	serialization::StructureMapper::SetPathResolver( nullptr );

	//
	// Move the files in Perforce that we can
	//

	{
		const red::AbsolutePath baseOutputPath = red::paths::GetCurrentWorkingDirectory().AddFilePath( "PerforceMoveOutput_" );

		for ( auto& movePair : moveActions )
		{
			auto& action = movePair.Value();
			if ( action.status != MoveStatus::OK )
			{
				continue;
			}

			action.sourceAbsolutePath = resourceToAbsoluteMap[ action.sourcePath ];
			if ( GFileManager->IsFileReadOnly( action.sourceAbsolutePath ) )
			{
				action.status = MoveStatus::PERFORCE_LOCKED;
				continue;
			}

			action.targetAbsolutePath = m_editorDepot->TranslateResourcePath( action.targetPath );
			resourceToAbsoluteMap[ action.targetPath ] = action.targetAbsolutePath;

			auto suffix = String::Printf( "%016llX.txt", action.sourcePath.GetHash() );
			red::AbsolutePath outputCapturePath = baseOutputPath.AddFilePath( suffix );
			if ( !PerforceMoveResources( action.sourceAbsolutePath, action.targetAbsolutePath, outputCapturePath, p4Options ) )
			{
				action.status = MoveStatus::MOVE_FAILED;
			}
		}

		for ( auto& movePair : moveActions )
		{
			auto& action = movePair.Value();
			if ( action.status != MoveStatus::OK )
			{
				continue;
			}

			if ( GFileManager->FileExist( action.sourceAbsolutePath ) ||
				!GFileManager->FileExist( action.targetAbsolutePath ) )
			{
				action.status = MoveStatus::MOVE_FAILED;
				continue;
			}

			bool allDependentsRelinked = std::all_of( action.dependentPaths.Begin(), action.dependentPaths.End(), 
				[&relinkActions]( const res::ResourcePath& path ) -> bool
			{
				return relinkActions[ path ] == RelinkStatus::OK;
			} );
			if ( !allDependentsRelinked )
			{
				action.status = MoveStatus::DEPENDENT_FAILED;
			}
		}
	}

	//
	// Report what was attempted to be done and the results
	//

	using MoveInfoArray = red::DynArray< const MoveInfo* >;
	red::FixedArray< MoveInfoArray, static_cast< Uint32 >( MoveStatus::MAX ) > moveReport;

	for ( const auto& moveItem : moveActions )
	{
		const auto& action = moveItem.Value();
		moveReport[ static_cast< Uint32 >( action.status ) ].PushBack( &action );
	}

	{
		red::AbsolutePath reportPath = red::paths::GetCurrentWorkingDirectory().AddFilePath( "Report.txt" );
		FILE* reportFile = fopen( reportPath.AsChar(), "wt" );
		if ( reportFile == nullptr )
		{
			RED_LOG_ERROR( "Failed to open the report file '%s' for writing", reportPath.AsChar() );
			return false;
		}

		fputs( "**** Failed Moves:\n\n", reportFile );

		const auto& alreadyExists = moveReport[ static_cast< Uint32 >( MoveStatus::TARGET_ALREADY_EXISTS ) ];
		if ( !alreadyExists.Empty() )
		{
			fputs( "** Target already exists:\n\n", reportFile );
			for ( const MoveInfo* info : alreadyExists )
			{
				fprintf( reportFile, "target: %s\nsource: %s\n\n", 
					info->targetAbsolutePath.AsChar(), info->sourceAbsolutePath.AsChar() );
			}
			fputs( "\n", reportFile );
		}

		const auto& pathTooLong = moveReport[ static_cast< Uint32 >( MoveStatus::TARGET_PATH_TOO_LONG ) ];
		if ( !pathTooLong.Empty() )
		{
			fprintf( reportFile, "** Target resource path too long: (Max: %u)\n\n", res::ResourcePath::MAX_LENGTH );
			for ( const MoveInfo* info : pathTooLong )
			{
				fprintf( reportFile, "source: %s\ntarget: %s\nlength: %u\n\n",
					info->sourceAbsolutePath.AsChar(), info->targetPath.ToDebugString(),
					info->targetPath.ToStringView().Length() );
			}
			fputs( "\n", reportFile );
		}

		const auto& perforceLocked = moveReport[ static_cast< Uint32 >( MoveStatus::PERFORCE_LOCKED ) ];
		if ( !perforceLocked.Empty() )
		{
			fputs( "** File (to Move) locked in Perforce:\n\n", reportFile );
			for ( const MoveInfo* info : perforceLocked )
			{
				fprintf( reportFile, "source: %s\n", info->sourceAbsolutePath.AsChar() );
			}
			fputs( "\n", reportFile );
		}

		const auto& moveFailed = moveReport[ static_cast< Uint32 >( MoveStatus::MOVE_FAILED ) ];
		if ( !moveFailed.Empty() )
		{
			fputs( "** Failed to move file:\n\n", reportFile );
			for ( const MoveInfo* info : moveFailed )
			{
				fprintf( reportFile, "source: %s\n", info->sourceAbsolutePath.AsChar() );
			}
			fputs( "\n", reportFile );
		}

		const auto& dependentFailed = moveReport[ static_cast< Uint32 >( MoveStatus::DEPENDENT_FAILED ) ];
		if ( !dependentFailed.Empty() )
		{
			fputs( "** Dependent file failed to relink:", reportFile );
			for ( const MoveInfo* info : dependentFailed )
			{
				fprintf( reportFile, "source: %s\ntarget: %s\n",
					info->sourcePath.ToDebugString(), info->targetPath.ToDebugString() );
				for ( const auto& depPath : info->dependentPaths )
				{
					auto depStatus = relinkActions[ depPath ];
					const auto& depAbsPath = resourceToAbsoluteMap[ depPath ];
					if ( depStatus == RelinkStatus::PERFORCE_LOCKED )
					{
						fprintf( reportFile, "perforce-locked: %s\n", depAbsPath.AsChar() );
					}
					else if ( depStatus == RelinkStatus::RELINK_FAILED )
					{
						fprintf( reportFile, "relink-failed: %s\n", depAbsPath.AsChar() );
					}
				}
				fputs( "\n", reportFile );
			}
			fputs( "\n", reportFile );
		}

		// Successful moves
		const auto& successfulMoves = moveReport[ static_cast< Uint32 >( MoveStatus::OK ) ];
		if ( !successfulMoves.Empty() )
		{
			fputs( "**** Successful Moves:\n\n", reportFile );
			for ( const MoveInfo* info : successfulMoves )
			{
				fprintf( reportFile, "source: %s\ntarget: %s\n",
					info->sourcePath.ToDebugString(), info->targetPath.ToDebugString() );
				for ( const auto& depPath : info->dependentPaths )
				{
					fprintf( reportFile, "\t%s\n", depPath.ToDebugString() );
				}
				fputs( "\n", reportFile );
			}
			fputs( "\n", reportFile );
		}

		fflush( reportFile );
		fclose( reportFile );
	}

	//
	// Done
	//

	m_database.Shutdown();

	return true;
}

