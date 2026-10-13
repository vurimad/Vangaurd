/**
 * Copyright (c) 2013-2020 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"

#include "../../../common/core/include/commandlet.h"
#include "../../../common/redCore/include/commandline.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/resource/include/resourceEditorDepotImpl.h"
#include "../../../common/worldEntities/include/entityTemplate.h"
#include "../../../common/world/include/prefabUtils.h"
#include "../../../common/generalEditorBackend/include/validationDatabase.h"
#include "../../../common/generalEditorBackend/include/validationUtils.h"
#include "../../backendEngine/include/validator.h"
#include "../../backendEngine/include/commandletEngineInstance.h"
#include "../../backendEngine/include/commandletRenderInstance.h"
#include "../../backendEngine/include/commandletUtils.h"
#include "../../backendEngine/include/resourceUtils.h"

// Commandlet to validate a single file
class ValidateCommandlet : public red::ICommandlet
{
	RTTI_DECLARE_TYPE( ValidateCommandlet );

public:
	ValidateCommandlet();
	~ValidateCommandlet();

	// ICommandlet interface
	virtual const char* GetOneLiner() const override;
	virtual void PrintHelp() const override;
	virtual bool Execute( const red::CommandLine& options ) override;

	virtual Bool InitializeEnvironment() override;
	virtual void TeardownEnvironment() override;

private:
	using Database = validation::Database;
	using Queue = Database::Queue;

	class ErrorHandler;

	void OptimizeDatabase();
	bool ValidateLink( const res::ResourcePath& linkPath, ErrorHandler& errorHandler ) const;
	bool ValidateResource( const res::ResourcePath& resPath, ErrorHandler& errorHandler ) const;

	void ValidateResourcePath( const res::ResourcePath& resourcePath, ErrorHandler& errorHandler, Bool reportInvalidAsErrors ) const;

	void FilterOnlyExtension( red::DynArray< res::ResourcePath >& paths );
	void FilterNotEligibleExtensions( red::DynArray< res::ResourcePath >& paths );
	void ExtractEligibleResourceExtensionsFromRtti();
	Bool IsTargetResource( const res::ResourcePath &path ) const;
	bool ParseSettings( const red::CommandLine& options );
	bool ParseCommonSettings( const red::CommandLine& options );
	bool ParseClientSettings( const red::CommandLine& options );
	bool ParseStandaloneSettings( const red::CommandLine& options );

	Bool CreateGlobalLock();
	void DeleteGlobalLock();

	Bool ValidateFile( const res::ResourcePath &path, ErrorHandler &errorHandler, Queue queue, Bool force );

	Bool IsResourceFile( red::StringView extension ) const;
	Bool IsResourceFile( const res::ResourcePath &path ) const;

	void ListValidators();
	void GenerateFileList();
	Bool ExecuteDaemon();
	Bool ExecuteClient();
	Bool ExecuteStandalone();

	commandlet::EngineInstance m_engineInstance;
	commandlet::RenderInstance m_renderInstance;

	enum class Mode : Uint8
	{
		Daemon,
		Client,
		Standalone,
	};

	struct Settings
	{
		red::DynArray< res::ResourcePath > resources;
		red::AbsolutePath generateFileListPath;
		HANDLE globalLock;
		String extension;
		Bool verbose{};
		Bool generateFileListOnly{};
		Bool listValidatorsOnly{};
		Bool justValidateResourcePaths{};
		Bool disableValidateResourcePaths{};
		Bool recursive{};
		Bool force{};
		Bool optimize{};
		Bool quitDaemon{};
		Mode mode{};
		String modeString{};
	} m_settings{};

	Database m_database;
	red::HashSet< String > m_eligibleExtensions;
	res::ResourceEditorDepot* m_editorDepot{};
};

RTTI_BEGIN_TYPE( ValidateCommandlet )
	RTTI_PARENT_TYPE( red::ICommandlet )
RTTI_END_TYPE()

//------------------------------------------------------------------------------

ValidateCommandlet::ValidateCommandlet()
	: ICommandlet( RED_NAME_CONSTEXPR( "validate" ) )
{
}

ValidateCommandlet::~ValidateCommandlet()
{
}

const char* ValidateCommandlet::GetOneLiner() const
{
	return "Validate resource is free from errors and safe to submit to version control";
}

void ValidateCommandlet::PrintHelp() const
{
	RED_LOG( "Usage:" );
	RED_LOG( "  validate [-daemon|-client|-standalone] [-options]" );
	RED_LOG( "Parameters:" );
	RED_LOG( " Mode:" );
	RED_LOG( "  -daemon                           - run in daemon mode" );
	RED_LOG( "  -client                           - run in client mode" );
	RED_LOG( "  -standalone                       - run in standalone mode" );
	RED_LOG( " Common options:" );
	RED_LOG( "  -verbose                          - be verbose" );
	RED_LOG( "  -list-validators                  - list available validators and exit" );
	RED_LOG( "  -optimize                         - optimize database, remove not existing reports, resources, etc. and exit" );
	RED_LOG( " Client & standalone options:" );
	RED_LOG( "  -all                              - validate all resources" );
	RED_LOG( "  -file=<file1> <file2>             - list of files to be validated" );
	RED_LOG( "  -dir=<dir1> <dir2>                - directories to be validated" );
	RED_LOG( "  -extension=<extension>            - filter files list for this extension only" );
	RED_LOG( "  -filelist=<filelist>              - file that contains list of files to be validated" );
	RED_LOG( "  -recursive                        - validate dependencies (valid only for one -file)" );
	RED_LOG( "  -force                            - skip validation status check in database and force validation" );
	RED_LOG( " Client options:" );
	RED_LOG( "  -quit-daemon                      - request daemon to quit" );
	RED_LOG( " Standalone options:" );
	RED_LOG( "  -generate-filelist=<filename>     - just generate file list from -all/-file/-dir arguments" );
	RED_LOG( "  -just-validate-resource-paths     - do not run any validator, just report wrong resource paths" );
	RED_LOG( "  -disable-validate-resource-paths  - do not report wrong resource paths" );
}

Bool ValidateCommandlet::InitializeEnvironment()
{
	Database::Initialize();

	m_database.Open( Database::Mode::ReadWrite );

	return m_engineInstance.Initialise() && m_renderInstance.Initialise();
}

void ValidateCommandlet::TeardownEnvironment()
{
	m_database.Close();
	Database::Shutdown();

	DeleteGlobalLock();

	m_renderInstance.Shutdown();
	m_engineInstance.Shutdown();
}

//------------------------------------------------------------------------------

class ValidateCommandlet::ErrorHandler final : public tools::IMessageReporter
{
public:
	using tools::IMessageReporter::Report;

	ErrorHandler( const ValidateCommandlet::Settings &settings, Database &database )
		: m_settings{ settings }
		, m_database{ database }
	{
	}

	virtual ~ErrorHandler() = default;

	virtual void Report( tools::Message& message ) override;

	void SetActiveValidator( const String &source );
	void SetActiveResource( const res::ResourcePath& resourcePath );

private:
	const ValidateCommandlet::Settings &m_settings;
	Database &m_database;
	res::ResourcePath m_activeResource;
	String m_activeValidator;
};

void ValidateCommandlet::ErrorHandler::Report( tools::Message& message )
{
	const String messageStr = message.ToString();
	const String locationStr = message.GetLocation() ? message.GetLocation()->SerializeToString() : "";

	m_database.Report( m_activeResource, message, m_activeValidator );
}

void ValidateCommandlet::ErrorHandler::SetActiveValidator( const String &source )
{
	m_activeValidator = source;
}

void ValidateCommandlet::ErrorHandler::SetActiveResource( const res::ResourcePath& resourcePath )
{
	m_activeResource = resourcePath;
}

//------------------------------------------------------------------------------

namespace
{

void FilterResourcePaths( red::DynArray< res::ResourcePath >& paths )
{
	paths.Remove( std::remove_if( paths.Begin(), paths.End(), []( const res::ResourcePath& path )
	{
		return !IsLinkResource( path ) && !IsBinkResource( path ) && !CouldBeResource( path );
	} ), paths.End() );
}

bool ReadResourceDepotFileToString( const res::ResourcePath& path, red::String& result )
{
	auto* editorDepot = commandlet::GetEditorDepot();
	auto linkFile = editorDepot->CreateResourceReader( path, false );
	if ( !linkFile )
	{
		return false;
	}

	const Uint32 size = static_cast< Uint32 >( linkFile->GetSize() );
	result.Resize( size );
	linkFile->Serialize( result.AsChar(), size );
	return !linkFile->HasErrors();
}

}

void ValidateCommandlet::OptimizeDatabase()
{
	const Uint32 totalResources = m_database.CountResources();
	const Uint32 totalReports = m_database.CountReports();

	red::UniquePtr< sql::Query > query = m_database.QueryFirstResource();

	query->Next();

	Uint32 removedResources{};

	while ( !query->End() )
	{
		if ( query->HasError() )
		{
			RED_LOG_ERROR( "[optimize] Query error" );
			break;
		}

		String resourceName;

		const Bool success = query->Value( "path", resourceName );
		RED_FATAL_ASSERT( success, "Error getting resource path from query!" );

		const res::ResourcePath resourcePath = res::ResourcePath::Build( resourceName );
		const auto absolutePath = m_editorDepot->TranslateResourcePath( resourcePath );

		if ( !GFileManager->FileExist( absolutePath ) )
		{
			m_database.ClearReportsFor( resourcePath );
			m_database.RemoveResource( resourcePath );
			removedResources++;
		}

		query->Next();
	}

	RED_LOG_INFO( "[optimize] Cleaned %u from total %u resources", removedResources, totalResources );

	query = m_database.QueryFirstUniqueStatusResource();

	query->Next();

	Uint32 orphanedReports{};

	while ( !query->End() )
	{
		if ( query->HasError() )
		{
			RED_LOG_ERROR( "[optimize] Query error" );
			break;
		}

		Uint64 resourceHash{};

		const Bool success = query->Value( "resourceHash", resourceHash );
		RED_FATAL_ASSERT( success, "[optimize] Error getting resource hash from query!" );

		res::ResourcePath resourcePath = res::ResourcePath::Build( resourceHash );

		if ( !m_database.WasValidated( resourcePath ) )
		{
			orphanedReports += m_database.CountReportsFor( resourcePath );
			m_database.ClearReportsFor( resourcePath );
		}

		query->Next();
	}

	RED_LOG_INFO( "[optimize] Cleaned %u orphaned reports from total %u reports", orphanedReports, totalReports );
}

//------------------------------------------------------------------------------

bool ValidateCommandlet::ValidateLink( const res::ResourcePath& linkPath, ErrorHandler& errorHandler ) const
{
	String linkedPath;
	if ( !ReadResourceDepotFileToString( linkPath, linkedPath ) )
	{
		errorHandler.Report( tools::MessageSeverity::Error, linkPath, RED_LOCATION( linkPath ), "Could not read the link file" );
		return false;
	}

	// Perform these checks regardless if any fails so that we can accumulate as many errors as we can
	// in one validation so people don't try to circumvent the process.
	const auto originalPath = red::paths::GetFileStem( linkPath.ToStringView() );
	const auto originalExt = red::paths::GetExtension( originalPath );

	bool originalValid = true;
	const rtti::ClassType* originalClass = nullptr;
	if ( originalExt.Empty() )
	{
		errorHandler.Report( tools::MessageSeverity::Error, linkPath, RED_LOCATION( linkPath ), "Link file has no real extension" );
		originalValid = false;
	}
	else
	{
		originalClass = ResourceClassByExtension( originalExt.ToString() );
		if ( originalClass == nullptr )
		{
			errorHandler.Report( tools::MessageSeverity::Error, linkPath, RED_LOCATION( linkPath ), "Link file extension is not a recognized resource class format!" );
			originalValid = false;
		}
	}

	const auto linkedExt = red::paths::GetExtension( linkedPath );
	const auto linkedResPath = res::ResourcePath::Build( linkedPath );

	bool linkedValid = true;
	const rtti::ClassType* linkedClass = nullptr;
	if ( linkedResPath.IsEmpty() )
	{
		errorHandler.Report( tools::MessageSeverity::Error, linkPath, RED_LOCATION( linkedResPath ), "Link is empty" );
		linkedValid = false;
	}
	else if ( linkedExt.Empty() )
	{
		errorHandler.Report( tools::MessageSeverity::Error, linkPath, RED_LOCATION( linkedResPath ), "Linked to file has no real extension" );
		linkedValid = false;
	}
	else
	{
		linkedClass = ResourceClassByExtension( linkedExt.ToString() );
		if ( linkedClass == nullptr )
		{
			errorHandler.Report( tools::MessageSeverity::Error, linkPath, RED_LOCATION( linkedResPath ), "Linked to file extension is not a recognised resource class format!" );
			linkedValid = false;
		}

		if ( !m_settings.disableValidateResourcePaths )
		{
			ValidateResourcePath( linkedResPath, errorHandler, true );
		}
	}

	if ( !( originalValid && linkedValid ) )
	{
		return false;
	}

	if ( originalClass != linkedClass )
	{
		errorHandler.Report( tools::MessageSeverity::Error, linkPath, RED_LOCATION( linkedResPath ), "Original and linked to file are not the same type of resource!" );
		return false;
	}

	// In this case we want to follow any additional links because we only want to validate
	// that this immediate link file can resolve to something valid
	// TODO: We should manually follow links here to verify that the chain does not exceed the
	// maximum number of links allowed by the engine - MAX_LINK_FILE_RECURSION
	if ( !GetResourceDepot()->ResourceExists( linkedResPath ) )
	{
		errorHandler.Report( tools::MessageSeverity::Error, linkPath, RED_LOCATION( linkedResPath ), "Linked to file does not exist!" );
		return false;
	}

	return true;
}

bool ValidateCommandlet::ValidateResource( const res::ResourcePath& resPath, ErrorHandler& errorHandler ) const
{
	res::IssueLoadingRequestParameter param( resPath );
	param.postLoadFlags.skipUploadRenderData = true;

	// We want to load the whole thing, dependencies and all to ensure that
	// the engine in the game can load it as well.
	auto loadingToken = GResourceLoader->IssueLoadingRequest( param );
	if ( loadingToken->HasFailed() )
	{
		errorHandler.Report( tools::MessageSeverity::Error, resPath, RED_LOCATION( resPath ), "Failed to load in ResourceLoader" );
		return false;
	}

	// NOTE: For now load the resource one at a time when needed because
	// the current use of this commandlet gets called for a single file at a time
	THandle< CResource > resource = loadingToken->WaitUntilLoaded();
	if ( !resource )
	{
		errorHandler.Report( tools::MessageSeverity::Error, resPath, RED_LOCATION( resPath ), "Failed to load resource" );
		return false;
	}

	// Extra wait required for Entity templates to complete compiling
	if ( resource->IsA< ent::EntityTemplate >() )
	{
		auto templateResource = Cast< ent::EntityTemplate >( resource );
		templateResource->Editor_WaitUntilCompiled();
	}

	Bool result = true;

	// We get all validators that apply to each resource
	red::DynArray< red::UniquePtr< IValidator > > validators = IValidator::FindValidators( resource->GetClass() );

	for ( const auto& validator : validators )
	{
		String validatorName = validator->GetClass()->GetName().AsChar();

		errorHandler.SetActiveValidator( validatorName );
		if ( !validator->Validate( resource, errorHandler ) )
		{
			result = false;
		}
	}

	return result;
}

void ValidateCommandlet::ValidateResourcePath( const res::ResourcePath& resourcePath, ErrorHandler& errorHandler, Bool reportInvalidAsErrors ) const
{
	const auto absolutePath = m_editorDepot->TranslateResourcePath( resourcePath );

	validation::ValidatePath( resourcePath, absolutePath, errorHandler, reportInvalidAsErrors );
}

void ValidateCommandlet::FilterOnlyExtension( red::DynArray< res::ResourcePath >& paths )
{
	const Uint32 total = paths.Size();
	if ( !m_settings.extension.Empty() )
	{
		paths.Remove( std::remove_if( paths.Begin(), paths.End(), [ this ]( const res::ResourcePath& path )
		{
			return !IsTargetResource( path );
		} ), paths.End() );

		const Uint32 filtered = paths.Size();

		if ( m_settings.verbose )
		{
			RED_LOG_INFO( "%hs Found %u of total %u resources matching '%hs' extension.", m_settings.modeString.AsChar(), filtered, total, m_settings.extension.AsChar() );

		}
	}
}

void ValidateCommandlet::FilterNotEligibleExtensions( red::DynArray< res::ResourcePath >& paths )
{
	paths.Remove( std::remove_if( paths.Begin(), paths.End(), [ this ]( const res::ResourcePath& path )
	{
		red::StringView extension = red::paths::GetExtension( path.ToStringView() );
		return !m_eligibleExtensions.Exist( extension.ToString() );
	} ), paths.End() );
}

void ValidateCommandlet::ExtractEligibleResourceExtensionsFromRtti()
{
	red::DynArray< const rtti::ClassType* > resourceClasses;
	GetRttiSystem().EnumClasses( ClassID< CResource >(), resourceClasses );

	m_eligibleExtensions.Reserve( 2 * resourceClasses.Size() );
	for ( const rtti::ClassType* resourceClass : resourceClasses )
	{
		CResource* defaultObject = resourceClass->GetDefaultObject< CResource >();
		m_eligibleExtensions.Insert( defaultObject->GetExtension() );
		m_eligibleExtensions.Insert( defaultObject->GetDeprecatedExtension() );
	}

	m_eligibleExtensions.Remove( "" );
	m_eligibleExtensions.Remove( "wem" );
	m_eligibleExtensions.Remove( "bink" );
}

Bool ValidateCommandlet::IsTargetResource( const res::ResourcePath &path ) const
{
	return red::paths::GetExtension( path.ToStringView() ) == m_settings.extension;
}

bool ValidateCommandlet::ParseSettings( const red::CommandLine& options )
{
	m_editorDepot = commandlet::GetEditorDepot();

	ExtractEligibleResourceExtensionsFromRtti();

	m_settings.verbose = options.HasOption( "verbose" );

	if ( options.HasOption( "optimize" ) )
	{
		m_settings.optimize = true;
		return true;
	}

	if ( options.HasOption( "list-validators" ) )
	{
		m_settings.listValidatorsOnly = true;
		return true;
	}

	if ( options.HasOption( "daemon" ) )
	{
		m_settings.mode = Mode::Daemon;
		m_settings.modeString = "[daemon]";
	}
	else if ( options.HasOption( "client" ) )
	{
		m_settings.mode = Mode::Client;
		m_settings.modeString = "[client]";
	}
	else //if ( options.HasOption( "standalone" ) )
	{
		m_settings.mode = Mode::Standalone;
		m_settings.modeString = "[standalone]";
	}

	if ( m_settings.mode != Mode::Standalone )
	{
		const Bool c_lockCreated = CreateGlobalLock();

		if ( !c_lockCreated && m_settings.mode == Mode::Daemon )
		{
			RED_LOG_ERROR( "Can't run in daemon mode, found already running instance" );
			return false;
		}
		else if ( c_lockCreated && m_settings.mode == Mode::Client )
		{
			RED_LOG_ERROR( "Can't run in client mode, daemon not running" );
			return false;
		}
	}

	switch ( m_settings.mode )
	{
	case Mode::Daemon:
		RED_LOG_INFO( "Validation running in daemon mode" );
		break;
	case Mode::Client:
		RED_LOG_INFO( "Validation running in client mode to already running daemon" );
		if ( !ParseClientSettings( options ) )
		{
			return false;
		}
		if ( !m_settings.quitDaemon )
		{
			if ( !ParseCommonSettings( options ) )
			{
				return false;
			}
		}
		break;
	case Mode::Standalone:
		RED_LOG_INFO( "Validation running in standalone mode" );
		if ( !ParseCommonSettings( options ) )
		{
			return false;
		}
		if ( !ParseStandaloneSettings( options ) )
		{
			return false;
		}
		break;
	}

	return true;
}

bool ValidateCommandlet::ParseCommonSettings( const red::CommandLine& options )
{
	auto &resources = m_settings.resources;

	m_settings.recursive = options.HasOption( "recursive" );
	m_settings.force = options.HasOption( "force" );

	resources = commandlet::ParseResourcePathsFromCommandLine( *m_editorDepot, options );

	if ( m_settings.recursive && resources.Size() != 1 )
	{
		RED_LOG_ERROR( "Can't use -force with more then one file" );
		return false;
	}

	String param;
	if ( options.GetFirstOption( "extension", param ) )
	{
		m_settings.extension = param;
	}

	if ( m_settings.recursive )
	{
		red::DynArray< res::ResourcePath > brokenPrefabs;
		red::HashSet< res::ResourcePath > childPrefabRefs;
		const res::ResourcePath target = resources[ 0 ];
		tools::CollectAllChildPrefabs( false, target, childPrefabRefs, brokenPrefabs );

		const Uint32 depsSize = childPrefabRefs.Size();
		RED_LOG_INFO( "%hs Gathered %u dependencies", m_settings.modeString.AsChar(), depsSize );

		resources.Reserve( resources.Size() + depsSize );

		red::DynArray< res::ResourcePath > dependencies;
		dependencies.Reserve( depsSize );

		for ( const res::ResourcePath &path : childPrefabRefs )
		{
			if ( path != target && m_editorDepot->ResourceExists( path ) )
			{
				dependencies.PushBack( std::move( path ) );
			}
		}

		std::sort( dependencies.Begin(), dependencies.End(), res::SortByPathString );

		resources.PushBack( std::move( dependencies ) );
	}

	const Uint32 c_resourcesBeforeFiltering = resources.Size();

	//FilterResourcePaths( resources );
	FilterOnlyExtension( resources );
	// FilterNotEligibleExtensions( resources );

	const Uint32 c_resourcesAfterFiltering = resources.Size();

	if ( !m_settings.recursive )
	{
		std::sort( resources.Begin(), resources.End(), res::SortByPathString );
	}

	RED_LOG_INFO( "%hs Found %u of %u resources after filtering", m_settings.modeString.AsChar(), c_resourcesAfterFiltering, c_resourcesBeforeFiltering );

	return true;
}

bool ValidateCommandlet::ParseClientSettings( const red::CommandLine& options )
{
	m_settings.quitDaemon = options.HasOption( "quit-daemon" );

	return true;
}

bool ValidateCommandlet::ParseStandaloneSettings( const red::CommandLine& options )
{
	m_settings.justValidateResourcePaths = options.HasOption( "just-validate-resource-paths" );
	m_settings.disableValidateResourcePaths = options.HasOption( "disable-validate-resource-paths" );

	if ( m_settings.justValidateResourcePaths && m_settings.disableValidateResourcePaths )
	{
		RED_LOG_ERROR( "Conflicting settings 'just-validate-resource-paths' & 'disable-validate-resource-paths'" );
		return false;
	}

	String param;
	if ( options.GetFirstOption( "generate-filelist", param ) )
	{
		m_settings.generateFileListOnly = true;
		m_settings.generateFileListPath = red::AbsolutePath::ParseFilePath( param );
	}

	return true;
}

Bool ValidateCommandlet::CreateGlobalLock()
{
	m_settings.globalLock = CreateEventW( nullptr, false, false, L"Global\\RedValidatorCmdletLock" );

	if ( GetLastError() == ERROR_ALREADY_EXISTS )
	{
		DeleteGlobalLock();

		return false;
	}

	return true;
}

void ValidateCommandlet::DeleteGlobalLock()
{
	CloseHandle( m_settings.globalLock );
	m_settings.globalLock = nullptr;
}

Bool ValidateCommandlet::ValidateFile( const res::ResourcePath &resourcePath, ValidateCommandlet::ErrorHandler &errorHandler, const Queue queue, const Bool force )
{
	errorHandler.SetActiveValidator( "cmdlet" );
	errorHandler.SetActiveResource( resourcePath );

	const red::AbsolutePath &c_absolutePath = m_editorDepot->TranslateResourcePath( resourcePath );

	if ( !GFileManager->FileExist( c_absolutePath ) )
	{
		if ( m_settings.verbose )
		{
			RED_LOG_INFO( "%hs Skipping not existing file '%hs'", m_settings.modeString.AsChar(), resourcePath.ToString().AsChar() );
		}
		return false;
	}

	const Uint64 c_timestamp = GFileManager->GetFileTimeStamp( c_absolutePath );
	const Uint64 c_fileSize = GFileManager->GetFileSize( c_absolutePath );

	using Status = validation::Status;

	const Status c_validationStatus = !force
		? m_database.GetStatus( resourcePath, c_timestamp, c_fileSize )
		: Status::NotValidated;

	if ( c_validationStatus == Status::Validated || c_validationStatus == Status::Crashed || c_validationStatus == Status::NotResource )
	{
		if ( m_settings.verbose )
		{
			RED_LOG_INFO( "%hs Skipping already validated file '%hs'", m_settings.modeString.AsChar(), resourcePath.ToString().AsChar() );
		}
		m_database.MarkAsProcessed( resourcePath, queue );

		return false;
	}
	else if ( c_validationStatus == Status::Validating )
	{
		if ( m_settings.verbose )
		{
			RED_LOG_ERROR( "%hs Validation of '%hs' probably crashed, marking it as crashed", m_settings.modeString.AsChar() ,resourcePath.ToString().AsChar() );
		}

		m_database.MarkAs( resourcePath, c_timestamp, c_fileSize, Status::Crashed );
		m_database.MarkAsProcessed( resourcePath, queue );

		return false;
	}

	m_database.MarkAs( resourcePath, c_timestamp, c_fileSize, Status::Validating );
	m_database.ClearReportsFor( resourcePath );

	if ( !m_settings.disableValidateResourcePaths )
	{
		errorHandler.SetActiveValidator( "validateResourcePath" );
		ValidateResourcePath( resourcePath, errorHandler, true );
	}

	if ( !m_settings.justValidateResourcePaths )
	{
		if ( IsLinkResource( resourcePath ) )
		{
			errorHandler.SetActiveValidator( "validateLink" );
			ValidateLink( resourcePath, errorHandler );
			m_database.MarkAs( resourcePath, c_timestamp, c_fileSize, Status::Validated );
		}
		else if ( IsBinkResource( resourcePath ) )
		{
			// we cannot use validator as it loads a resource, and we don't load binks
			// and we don't really need it as we always want to fail
			errorHandler.SetActiveValidator( "validateBink" );
			errorHandler.Report( tools::MessageSeverity::Error, resourcePath, RED_LOCATION( resourcePath ), String::Printf( "Binks cannot be submitted manually. Please read instructions on how to submit your video on Confluence: \"Convert video to a BIK file (Build system)\"." ).AsChar() );
			m_database.MarkAs( resourcePath, c_timestamp, c_fileSize, Status::Validated );
		}
		else if ( IsJsonNotResource( resourcePath ) )
		{
			// NOTE: Ignore for now, later validate this is valid json ?
			m_database.MarkAs( resourcePath, c_timestamp, c_fileSize, Status::Validated );
		}
		else if ( IsResourceFile( resourcePath ) ) // ensured to be resource by FilterResourcePaths
		{
			errorHandler.SetActiveValidator( "validateResource" );
			ValidateResource( resourcePath, errorHandler );
			m_database.MarkAs( resourcePath, c_timestamp, c_fileSize, Status::Validated );
		}
		else
		{
			m_database.MarkAs( resourcePath, c_timestamp, c_fileSize, Status::NotResource );
		}
	}

	m_database.MarkAsProcessed( resourcePath, queue );

	return true;
}

Bool ValidateCommandlet::IsResourceFile( red::StringView extension ) const
{
	return m_eligibleExtensions.Exist( extension.ToString() );
}

Bool ValidateCommandlet::IsResourceFile( const res::ResourcePath &path ) const
{
	red::StringView extension = red::paths::GetExtension( path.ToStringView() );
	return IsResourceFile( extension );
}

void ValidateCommandlet::ListValidators()
{
	red::DynArray< const rtti::ClassType* > validatorClasses;
	GetRttiSystem().EnumClasses( ClassID<IValidator>(), validatorClasses );

	RED_LOG_INFO( "Found %u validators:", validatorClasses.Size() );
	for ( const auto &validatorClass : validatorClasses )
	{
		RED_LOG_INFO( " %hs", validatorClass->GetName().AsChar() );
	}
}

void ValidateCommandlet::GenerateFileList()
{
	const auto &resources = m_settings.resources;

	if ( m_settings.verbose )
	{
		RED_LOG_INFO( "Generating filelist (%hs) for %u resources", m_settings.generateFileListPath.AsChar(), resources.Size() );
	}

	GFileManager->CreatePath( m_settings.generateFileListPath );
	FILE *listFile = fopen( m_settings.generateFileListPath.AsChar(), "w" );

	for ( Uint32 i = 0; i < resources.Size(); ++i )
	{
		const res::ResourcePath& resourcePath = resources[ i ];
		fwrite( resourcePath.ToStringView().Data(), resourcePath.ToStringView().Length(), 1, listFile );
		if ( i < resources.Size() - 1 )
		{
			fwrite( "\n", 1, 1, listFile );
		}
	}

	fclose( listFile );
}

Bool ValidateCommandlet::ExecuteDaemon()
{
	ErrorHandler errorHandler{ m_settings, m_database };

	m_database.ResetQuitDaemonFlag();

	RED_LOG_INFO( "%hs Waiting for jobs..", m_settings.modeString.AsChar() );

	while ( !m_database.DaemonQuitRequested() )
	{
		if ( m_database.CountJobs() == 0 )
		{
			red::SleepOnCurrentThread( 250 );
			continue;
		}

		Uint32 jobsCount{};
		Uint32 skipped{};

		Database::Job job;
		while ( m_database.FetchJob( job ) )
		{
			if ( m_database.DaemonQuitRequested() )
			{
				return true;
			}

			const res::ResourcePath &resourcePath = job.path;
			if ( m_settings.verbose )
			{
				RED_LOG_INFO( "%hs Validating '%hs'", m_settings.modeString.AsChar(), resourcePath.ToString().AsChar() );
			}

			const Bool validated = ValidateFile( resourcePath, errorHandler, job.queue, job.force );

			jobsCount++;
			skipped += static_cast<Uint32>( !validated );

			m_database.PopJob( job );
		}

		if ( jobsCount == skipped )
		{
			RED_LOG_INFO( "%hs Validated %u resources (all skipped)", m_settings.modeString.AsChar(), jobsCount );
		}
		else
		{
			RED_LOG_INFO( "%hs Validated %u resources (%u skipped)", m_settings.modeString.AsChar(), jobsCount, skipped );
		}
	}

	m_database.ResetQuitDaemonFlag();

	return true;
}

Bool ValidateCommandlet::ExecuteClient()
{
	if ( !m_settings.quitDaemon )
	{
		const auto &resources = m_settings.resources;

		m_database.PushJobs( resources, Queue::Background, m_settings.force );

		RED_LOG_INFO( "%hs Queued %u jobs to daemon background queue for validation", m_settings.modeString.AsChar(), resources.Size() );
	}
	else
	{
		m_database.PostQuitDaemonFlag();
		RED_LOG_INFO( "%hs Requested daemon to quit", m_settings.modeString.AsChar() );
	}

	return true;
}

Bool ValidateCommandlet::ExecuteStandalone()
{
	if ( m_settings.generateFileListOnly )
	{
		GenerateFileList();
		return true;
	}

	ErrorHandler errorHandler{ m_settings, m_database };

	Uint32 skipped{};

	const auto &resources = m_settings.resources;

	for ( Uint32 i = 0; i < resources.Size(); ++i )
	{
		const res::ResourcePath& resourcePath = resources[ i ];

		if ( m_settings.verbose )
		{
			RED_LOG_INFO( "%hs [%u/%u] Validating %hs", m_settings.modeString.AsChar(), i + 1, resources.Size(), resourcePath.ToDebugString() );
			RED_LOG_FLUSH_AND_WAIT();
		}

		const Bool c_validated = ValidateFile( resourcePath, errorHandler, Queue::Background, m_settings.force );

		skipped += static_cast< Uint32 >( !c_validated );
	}

	if ( m_settings.resources.Size() == skipped )
	{
		RED_LOG_INFO( "%hs Validated %u resources (all skipped)", m_settings.modeString.AsChar(), m_settings.resources.Size() );
	}
	else
	{
		RED_LOG_INFO( "%hs Validated %u resources (%u skipped)", m_settings.modeString.AsChar(), m_settings.resources.Size(), skipped );
	}

	return true;
}

bool ValidateCommandlet::Execute( const red::CommandLine& commandLine )
{
	if ( !ParseSettings( commandLine ) )
	{
		RED_LOG( "Failed to parse settings from commandline.." );
		RED_LOG_FLUSH_AND_WAIT();

		return false;
	}

	if ( m_settings.optimize )
	{
		OptimizeDatabase();
		return true;
	}

	if ( m_settings.listValidatorsOnly )
	{
		ListValidators();
		return true;
	}

	switch ( m_settings.mode )
	{
	case Mode::Daemon:
		return ExecuteDaemon();
	case Mode::Client:
		return ExecuteClient();
	case Mode::Standalone:
		return ExecuteStandalone();
	}

	return false;
}
