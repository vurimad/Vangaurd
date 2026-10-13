/**
 * Copyright (c) 2019-2020 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "inGameConfigSystem.h"
#include "inGameConfigReader.h"
#include "inGameConfigWriter.h"
#include "inGameConfigGroup.h"
#include "inGameConfigVar.h"
#include "inGameConfigFileUtils.h"
#include "../../redCore/include/absolutePath.h"
#include "../../redFileSystem/include/file.h"
#include "../../redFileSystem/include/fileSys.h"
#include "../../redMemory/include/uniquePtr.h"
#include "../../gameServices/include/gameServicesErrorCodes.h"

namespace
{
	constexpr auto c_inGameConfigFilename = "UserSettings.json";
}

namespace InGameConfig
{
	System::System()
		: m_immediateChangesQueue( PoolInGameConfig() )
		, m_changesWithConfirmationQueue( PoolInGameConfig() )
		, m_changesWithRestartQueue( PoolInGameConfig() )
		, m_changesWithLoadLastCheckpointQueue( PoolInGameConfig() )
		, m_loadFromSettingsQueue( PoolInGameConfig() )
		, m_varNotificationsQueue( PoolInGameConfig() )
		, m_varResetQueue( PoolInGameConfig() )
		, m_notificationsQueue( PoolInGameConfig() )
		, m_acceptChangesWithConfirmation( false )
		, m_rejectChangesWithConfirmation( false )
		, m_acceptRestartWithConfirmation( false )
		, m_rejectRestartWithConfirmation( false )
		, m_acceptChangesWithLoadLastCheckpoint( false )
		, m_rejectChangesWithLoadLastCheckpoint( false )
		, m_loadingFinished( false )
		, m_loadingCanceled( false )
		, m_userSettingsLoadStatus( UserSettingsLoadStatus::NotLoaded )
		, m_userSettingsLoadStatusChecked( false )
		, m_userSettingsSaveStatus( UserSettingsSaveStatus::NotSaved )
		, m_userSettingsSaveStatusChecked( false )
		, m_canSaveUserSettings( CanSave::No )
		, m_requestedSaveDuringLoad( false )
		, m_mode( Mode::Headless )
		, m_version( -1 )
	{
	}

	System::~System() = default;

	void System::LoadTemplates( const red::AbsolutePath& commonSettingsPath, const red::AbsolutePath& platformSettingsPath )
	{
		RED_FATAL_ASSERT( ::SIsMainThread(), "In-game config loading has to be done on main thread only." );

		Reader configReader{ m_registry };
		configReader.Init( commonSettingsPath, platformSettingsPath );
	}

#ifdef RED_PLATFORM_WINPC
	void System::LoadUserSettings()
	{
		const red::AbsolutePath path = GetUserSettingsPath();

		auto file = GFileManager->CreateFileReader( path, FOF_AbsolutePath );

		using services::ServiceError;
		const auto errorCode = file ? ServiceError::ServiceError_OK : ServiceError::ServiceError_FileNotFound;

		LoadUserSettings( file.Get(), errorCode );
	}
#endif

	void System::LoadUserSettings( IFile* file, services::ServiceError errorCode )
	{
		RED_FATAL_ASSERT( ::SIsMainThread(), "In-game config loading has to be done on main thread only." );

		// Reset checked flag. We are loading again so we should check the status once again.
		m_userSettingsLoadStatusChecked = false;
		// We are loading new settings so clearing save status
		m_userSettingsSaveStatus = UserSettingsSaveStatus::NotSaved;
		m_userSettingsSaveStatusChecked = false;

		// First call to load settings, enables saving [CYB-634502]
		m_canSaveUserSettings = CanSave::Yes;

		using services::ServiceError;
		if ( errorCode == ServiceError::ServiceError_InternalError ) //< Here is error handling for loading settings
		{
			m_userSettingsLoadStatus = UserSettingsLoadStatus::InternalError;
			QueueNotifyStatus( NotificationType::LoadInternalError );
		}
		else if ( errorCode == ServiceError::ServiceError_FileNotFound )
		{
			m_userSettingsLoadStatus = UserSettingsLoadStatus::FileIsMissing;
		}
		else
		{
			Reader configReader{ m_registry };
			m_userSettingsLoadStatus = configReader.LoadUserSettings( file );

			if ( m_userSettingsLoadStatus == UserSettingsLoadStatus::Loaded || m_userSettingsLoadStatus == UserSettingsLoadStatus::ImportedFromOldVersion )
			{
				m_canSaveUserSettings = CanSave::ProcessingUserSettings;
				LoadingFinished();
				return;
			}
			else if ( m_userSettingsLoadStatus == UserSettingsLoadStatus::FileIsCorrupted )
			{
				LoadingCanceled();
			}
		}

		RestoreToDefaults( Source::LoadSettings, true, false, false );
		LoadingFinished();
	}

#ifdef RED_PLATFORM_WINPC
	Uint32 System::SaveUserSettings()
	{
		const red::AbsolutePath path = GetUserSettingsPath();

		auto file = GFileManager->CreateFileWriter( path, FOF_AbsolutePath );

		services::ServiceError errorCode = services::ServiceError::ServiceError_OK;

		return SaveUserSettings( file.Get(), errorCode );
	}
#endif

	Uint32 System::SaveUserSettings( IFile* file, services::ServiceError errorCode )
	{
		if ( m_canSaveUserSettings != CanSave::Yes)
		{
			m_requestedSaveDuringLoad = true;
			return 0;
		}

		// Reset checked flag. We are saving again so we should check the status once again.
		m_userSettingsSaveStatusChecked = false;

		Writer configWriter{ m_registry };

		using services::ServiceError;
		if ( errorCode != ServiceError::ServiceError_OK )
		{
			RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Engine, "Failed to save in-game settings" );
			m_userSettingsSaveStatus = UserSettingsSaveStatus::InternalError;
			QueueNotifyStatus( NotificationType::ErrorSaving );
			return 0;
		}

		const Uint32 written = configWriter.SaveUserSettings( file );
		if ( written == 0 )
		{
			RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Engine, "Failed to save in-game settings" );
			m_userSettingsSaveStatus = UserSettingsSaveStatus::InternalError;
			QueueNotifyStatus( NotificationType::ErrorSaving );
		}
		else
		{
			MarkAsSaved();
			m_userSettingsSaveStatus = UserSettingsSaveStatus::Saved;
			QueueNotifyStatus( NotificationType::Saved );
		}

		return written;
	}

#ifdef RED_PLATFORM_WINPC
	red::AbsolutePath System::GetUserSettingsPath()
	{
		return red::paths::GetLocalAppDataDirectory().AddFilePath( c_inGameConfigFilename );
	}
#endif

	void System::Update()
	{
		m_registry.ProcessUnregisterListeners();

		UpdateLoadingQueue();
		UpdateImmediateQueue();
		UpdateConfirmationQueue();
		UpdateRestartQueue();
		UpdateLoadLastCheckPointQueue();
		UpdateNotifications();
	}

	VarValidator::ID System::RegisterValidator( CName groupPath, CName varName, VarValidator::Functor &&functor )
	{
		return m_registry.RegisterValidator( groupPath, varName, std::move( functor ) );
	}

	VarValidator::ID System::RegisterValidator( CName groupPath, CName varName, const VarValidator::Functor &functor )
	{
		return m_registry.RegisterValidator( groupPath, varName, functor );
	}

	VarValidator::ID System::RegisterValidator( CName groupPath, CName varName, VarValidator &&handler )
	{
		return m_registry.RegisterValidator( groupPath, varName, std::move( handler ) );
	}

	VarValidator::ID System::RegisterValidator( CName groupPath, CName varName, const VarValidator &handler )
	{
		return m_registry.RegisterValidator( groupPath, varName, handler );
	}

	void System::UnregisterValidator( VarValidator::ID id )
	{
		m_registry.UnregisterValidator( id );
	}

	Bool System::HasValidator( CName groupPath, CName varName ) const
	{
		return m_registry.HasValidator( groupPath, varName );
	}

	VarListener::ID System::RegisterListener( CName groupPath, VarListener::Functor &&functor, Uint8 priority )
	{
		return m_registry.RegisterListener( groupPath, std::move( functor ), priority );
	}

	VarListener::ID System::RegisterListener( CName groupPath, const VarListener::Functor &functor, Uint8 priority )
	{
		return m_registry.RegisterListener( groupPath, functor, priority );
	}

	VarListener::ID System::RegisterListener( CName groupPath, VarListener &&listener )
	{
		return m_registry.RegisterListener( groupPath, std::move( listener ) );
	}

	VarListener::ID System::RegisterListener( CName groupPath, const VarListener &listener )
	{
		return m_registry.RegisterListener( groupPath, listener );
	}

	void System::UnregisterListener( VarListener::ID id )
	{
		m_registry.UnregisterListener( id );
	}

	NotificationHandler::ID System::RegisterNotifier( NotificationHandler::Functor &&functor )
	{
		return m_registry.RegisterNotifier( std::move( functor ) );
	}

	NotificationHandler::ID System::RegisterNotifier( const NotificationHandler::Functor &functor )
	{
		return m_registry.RegisterNotifier( functor );
	}

	NotificationHandler::ID System::RegisterNotifier( NotificationHandler &&handler )
	{
		return m_registry.RegisterNotifier( std::move( handler ) );
	}

	NotificationHandler::ID System::RegisterNotifier( const NotificationHandler &handler )
	{
		return m_registry.RegisterNotifier( handler );
	}

	void System::UnregisterNotifier( NotificationHandler::ID id )
	{
		m_registry.UnregisterNotifier( id );
	}

	Bool System::HasGroup( CName groupPath ) const
	{
		return m_registry.HasGroup( groupPath );
	}

	Bool System::HasVar( CName groupPath, CName varName ) const
	{
		return m_registry.HasVar( groupPath, varName );
	}

	Group& System::GetRootGroup()
	{
		return m_registry.GetRootGroup();
	}

	const Group& System::GetRootGroup() const
	{
		return m_registry.GetRootGroup();
	}

	Group& System::GetGroup( CName groupPath )
	{
		return m_registry.GetGroup( groupPath );
	}

	const Group& System::GetGroup( CName groupPath ) const
	{
		return m_registry.GetGroup( groupPath );
	}

	Var& System::GetVar( CName groupPath, CName configVar )
	{
		return GetGroup( groupPath ).GetVar( configVar );
	}

	const Var& System::GetVar( CName groupPath, CName configVar ) const
	{
		return GetGroup( groupPath ).GetVar( configVar );
	}

	VarBool& System::GetVarBool( CName groupPath, CName configVar )
	{
		return GetGroup( groupPath ).GetVarBool( configVar );
	}

	const VarBool& System::GetVarBool( CName groupPath, CName configVar ) const
	{
		return GetGroup( groupPath ).GetVarBool( configVar );
	}

	VarInt& System::GetVarInt( CName groupPath, CName configVar )
	{
		return GetGroup( groupPath ).GetVarInt( configVar );
	}

	const VarInt& System::GetVarInt( CName groupPath, CName configVar ) const
	{
		return GetGroup( groupPath ).GetVarInt( configVar );
	}

	VarFloat& System::GetVarFloat( CName groupPath, CName configVar )
	{
		return GetGroup( groupPath ).GetVarFloat( configVar );
	}

	const VarFloat& System::GetVarFloat( CName groupPath, CName configVar ) const
	{
		return GetGroup( groupPath ).GetVarFloat( configVar );
	}

	VarName& System::GetVarName( CName groupPath, CName configVar )
	{
		return GetGroup( groupPath ).GetVarName( configVar );
	}

	const VarName& System::GetVarName( CName groupPath, CName configVar ) const
	{
		return GetGroup( groupPath ).GetVarName( configVar );
	}

	VarListInt& System::GetVarListInt( CName groupPath, CName configVar )
	{
		return GetGroup( groupPath ).GetVarListInt( configVar );
	}

	const VarListInt& System::GetVarListInt( CName groupPath, CName configVar ) const
	{
		return GetGroup( groupPath ).GetVarListInt( configVar );
	}

	VarListFloat& System::GetVarListFloat( CName groupPath, CName configVar )
	{
		return GetGroup( groupPath ).GetVarListFloat( configVar );
	}

	const VarListFloat& System::GetVarListFloat( CName groupPath, CName configVar ) const
	{
		return GetGroup( groupPath ).GetVarListFloat( configVar );
	}

	VarListString& System::GetVarListString( CName groupPath, CName configVar )
	{
		return GetGroup( groupPath ).GetVarListString( configVar );
	}

	const VarListString& System::GetVarListString( CName groupPath, CName configVar ) const
	{
		return GetGroup( groupPath ).GetVarListString( configVar );
	}

	VarListName& System::GetVarListName( CName groupPath, CName configVar )
	{
		return GetGroup( groupPath ).GetVarListName( configVar );
	}

	const VarListName& System::GetVarListName( CName groupPath, CName configVar ) const
	{
		return GetGroup( groupPath ).GetVarListName( configVar );
	}

	void System::ScheduleVarUpdateWithoutConfirmation( Var* configVar )
	{
		RED_SCOPE_LOCK( m_immediateChangesQueueLock );

		const Int32 index = m_immediateChangesQueue.GetIndex( configVar );

		if ( index == -1 && configVar->HasRequestedValue() )
		{
			m_immediateChangesQueue.PushBack( configVar );
		}
	}

	void System::ScheduleVarUpdateWithConfirmation( Var* configVar )
	{
		RED_SCOPE_LOCK( m_changesWithConfirmationQueueLock );

		const Int32 index = m_changesWithConfirmationQueue.GetIndex( configVar );

		const CName groupPath = configVar->GetGroup();
		const CName varName = configVar->GetName();
		const VarType varType = configVar->GetType();
		const Source source = configVar->SourceOfChange();

		ChangeReason reason = ChangeReason::NeedsConfirmation;

		if ( m_registry.HasValidator( groupPath, varName ) )
		{
			if ( !m_registry.ValidateVar( groupPath, varName, varType, source ) )
			{
				configVar->InternalRejectValue();
				reason = ChangeReason::Rejected;
			}
		}

		if ( index >= 0 )
		{
			if ( !configVar->HasRequestedValue() || reason == ChangeReason::Rejected )
			{
				m_changesWithConfirmationQueue.RemoveAt( index );
				reason = ChangeReason::Rejected;
			}
		}
		else if ( reason == ChangeReason::NeedsConfirmation )
		{
			m_changesWithConfirmationQueue.PushBack( configVar );
		}

		configVar->SetReasonOfChange( reason );

		QueueVarNotification( configVar );
	}

	void System::ScheduleVarUpdateWithRestart( Var* configVar )
	{
		RED_SCOPE_LOCK( m_changesWithRestartQueueLock );

		ChangeReason reason = ChangeReason::NeedsRestart;

		const Int32 index = m_changesWithRestartQueue.GetIndex( configVar );
		if ( index >= 0 )
		{
			Var *const var = m_changesWithRestartQueue[ index ];
			if ( !var->HasRequestedValue() )
			{
				m_changesWithRestartQueue.RemoveAt( index );
				reason = ChangeReason::Rejected;
			}
		}
		else
		{
			m_changesWithRestartQueue.PushBack( configVar );
		}

		configVar->SetReasonOfChange( reason );

		QueueVarNotification( configVar );
	}

	void System::ScheduleVarUpdateWithLoadLastCheckpoint( Var* configVar )
	{
		RED_SCOPE_LOCK( m_changesWithLoadLastCheckpointQueueLock );

		ChangeReason reason = ChangeReason::NeedsLoadLastCheckpoint;

		const Int32 index = m_changesWithLoadLastCheckpointQueue.GetIndex( configVar );
		if( index >= 0 )
		{
			Var *const var = m_changesWithLoadLastCheckpointQueue[ index ];
			if( !var->HasRequestedValue() )
			{
				m_changesWithLoadLastCheckpointQueue.RemoveAt( index );
				reason = ChangeReason::Rejected;
			}
		}
		else
		{
			m_changesWithLoadLastCheckpointQueue.PushBack( configVar );
		}

		configVar->SetReasonOfChange( reason );

		QueueVarNotification( configVar );
	}

	void System::ScheduleVarLoadFromSettings( Var* configVar )
	{
		RED_SCOPE_LOCK( m_loadFromSettingsQueueLock );

		const Int32 index = m_loadFromSettingsQueue.GetIndex( configVar );
		if ( index == -1 )
		{
			m_loadFromSettingsQueue.PushBack( configVar );
		}
	}

	void System::NotifyVarChanged( Var* configVar )
	{
		const CName groupPath = configVar->GetGroup();
		const CName varName = configVar->GetName();
		const VarType varType = configVar->GetType();
		const ChangeReason reason = configVar->ReasonOfChange();

		m_registry.NotifyListeners( groupPath, varName, varType, reason );
	}

	void System::QueueNotifyStatus( const NotificationType status )
	{
		RED_SCOPE_LOCK( m_notificationsQueueLock );
		m_notificationsQueue.PushBack( status );
	}

	Bool System::NeedsConfirmation() const
	{
		return !m_changesWithConfirmationQueue.Empty();
	}

	void System::ConfirmVarChanges()
	{
		m_acceptChangesWithConfirmation.SetValue( true );
	}

	void System::RejectVarChanges()
	{
		m_rejectChangesWithConfirmation.SetValue( true );
	}

	void System::RestoreToDefaults(
		const Source source,
		const Bool isPreGame,
		const Bool onlyVisible,
		const Bool onlyMarked,
		const CName &groupName )
	{
		const Bool restored = m_registry.RestoreToDefaults( source, isPreGame, onlyVisible, onlyMarked, groupName );

		if ( restored )
		{
			QueueNotifyStatus( NotificationType::Refresh );
		}
	}

	void System::MarkAsSaved()
	{
		m_registry.MarkAsSaved();
	}

	Bool System::NeedsRestart() const
	{
		return !m_changesWithRestartQueue.Empty();
	}

	void System::AcceptRestartRequired()
	{
		m_acceptRestartWithConfirmation.SetValue( true );
	}

	void System::RejectRestartRequired()
	{
		m_rejectRestartWithConfirmation.SetValue( true );
	}

	Bool System::NeedsLoadLastCheckpoint() const
	{
		return !m_changesWithLoadLastCheckpointQueue.Empty();
	}

	void System::ConfirmLoadLastCheckpointVarChanges()
	{
		m_acceptChangesWithLoadLastCheckpoint.SetValue( true );
	}

	void System::RejectLoadLastCheckpointVarChanges()
	{
		m_rejectChangesWithLoadLastCheckpoint.SetValue( true );
	}

	void System::LoadingFinished()
	{
		m_loadingFinished.SetValue( true );
	}

	void System::LoadingCanceled()
	{
		m_loadingCanceled.SetValue( true );
	}

	Bool System::WasModifiedSinceLastSave() const
	{
		return m_registry.WasModifiedSinceLastSave();
	}

	System& System::GetInstance()
	{
		static System instance;
		return instance;
	}

	Bool System::IsFirstRun() const
	{
		auto loadStatus = GetUserSettingsLoadStatus();
		return ( loadStatus == UserSettingsLoadStatus::FileIsCorrupted || loadStatus == UserSettingsLoadStatus::FileIsMissing );
	}

	UserSettingsLoadStatus System::GetUserSettingsLoadStatus() const
	{
		return m_userSettingsLoadStatus;
	}

	Bool System::GetUserSettingsLoadStatusChecked() const
	{
		return m_userSettingsLoadStatusChecked;
	}

	void System::SetUserSettingsLoadStatusChecked()
	{
		m_userSettingsLoadStatusChecked = true;
	}

	UserSettingsSaveStatus System::GetUserSettingsSaveStatus() const
	{
		return m_userSettingsSaveStatus;
	}

	Bool System::GetUserSettingsSaveStatusChecked() const
	{
		return m_userSettingsSaveStatusChecked;
	}

	void System::SetUserSettingsSaveStatusChecked()
	{
		m_userSettingsSaveStatusChecked = true;
	}

	void System::SetMode( const Mode mode )
	{
		m_mode = mode;
	}

	Mode System::GetMode() const
	{
		return m_mode;
	}

	void System::SetFileVersion( Int32 version )
	{
		m_version = version;
	}

	Int32 System::GetFileVersion() const
	{
		return m_version;
	}

	void System::QueueVarNotification( Var* configVar )
	{
		RED_SCOPE_LOCK( m_notificationsQueueLock );

		if ( m_varNotificationsQueue.GetIndex( configVar ) == -1 )
		{
			m_varNotificationsQueue.PushBack( configVar );
		}
	}

	void System::QueueVarReset( Var* configVar )
	{
		RED_SCOPE_LOCK( m_notificationsQueueLock );

		if ( m_varResetQueue.GetIndex( configVar ) == -1 )
		{
			m_varResetQueue.PushBack( configVar );
		}
	}

	Bool System::CanSaveUserSettings() const
	{
		return m_canSaveUserSettings == CanSave::Yes;
	}

	Bool System::RequestedSaveDuringLoad() const
	{
		return m_requestedSaveDuringLoad;
	}

	void System::ResetRequestedSaveDuringLoad()
	{
		m_requestedSaveDuringLoad = false;
	}

	void System::UpdateImmediateQueue()
	{
		Bool needToProcessImmediateChangesQueue = false;

		{
			RED_SCOPE_SHARED_LOCK( m_immediateChangesQueueLock );
			if ( !m_immediateChangesQueue.Empty() )
			{
				needToProcessImmediateChangesQueue = true;
			}
		}

		if ( needToProcessImmediateChangesQueue )
		{
			red::DynArray< Var* > vars{ PoolInGameConfig() };

			{
				RED_SCOPE_LOCK( m_immediateChangesQueueLock );
				vars = std::move( m_immediateChangesQueue );
			}

			ProcessAcceptedVars( vars );
		}
	}

	void System::UpdateConfirmationQueue()
	{
		const Bool acceptChangesWithConfirmation = m_acceptChangesWithConfirmation.Exchange( false );
		const Bool rejectChangesWithConfirmation = m_rejectChangesWithConfirmation.Exchange( false );

		if ( acceptChangesWithConfirmation || rejectChangesWithConfirmation )
		{
			red::DynArray< Var* > vars{ PoolInGameConfig() };
			{
				RED_SCOPE_LOCK( m_changesWithConfirmationQueueLock );
				vars = std::move( m_changesWithConfirmationQueue );
			}

			if ( !vars.Empty() )
			{
				if ( rejectChangesWithConfirmation )
				{
					ProcessRejectedVars( vars );

					QueueNotifyStatus( NotificationType::ChangesRejected );
				}
				else if ( acceptChangesWithConfirmation )
				{
					ProcessAcceptedVars( vars );

					QueueNotifyStatus( NotificationType::ChangesApplied );
				}
			}
		}
	}

	void System::UpdateRestartQueue()
	{
		const Bool acceptRestartWithConfirmation = m_acceptRestartWithConfirmation.Exchange( false );
		const Bool rejectRestartWithConfirmation = m_rejectRestartWithConfirmation.Exchange( false );

		if ( acceptRestartWithConfirmation || rejectRestartWithConfirmation )
		{
			red::DynArray< Var* > vars{ PoolInGameConfig() };
			{
				RED_SCOPE_LOCK( m_changesWithRestartQueueLock );
				vars = std::move( m_changesWithRestartQueue );
			}

			if ( !vars.Empty() )
			{
				if ( rejectRestartWithConfirmation )
				{
					ProcessRejectedVars( vars );

					QueueNotifyStatus( NotificationType::RestartRequiredRejected );
				}
				else if ( acceptRestartWithConfirmation )
				{
					ProcessAcceptedVars( vars );

					QueueNotifyStatus( NotificationType::RestartRequiredConfirmed );
				}
			}
		}
	}

	void System::UpdateLoadLastCheckPointQueue()
	{
		const Bool acceptChangesWithLoadLastCheckpoint = m_acceptChangesWithLoadLastCheckpoint.Exchange( false );
		const Bool rejectChangesWithLoadLastCheckpoint = m_rejectChangesWithLoadLastCheckpoint.Exchange( false );

		if( acceptChangesWithLoadLastCheckpoint || rejectChangesWithLoadLastCheckpoint )
		{
			red::DynArray< Var* > vars{ PoolInGameConfig() };

			{
				RED_SCOPE_LOCK( m_changesWithLoadLastCheckpointQueueLock );
				vars = std::move( m_changesWithLoadLastCheckpointQueue );
			}

			if ( !vars.Empty() )
			{
				if ( rejectChangesWithLoadLastCheckpoint )
				{
					ProcessRejectedVars( vars );

					QueueNotifyStatus( NotificationType::ChangesLoadLastCheckpointRejected );
				}
				else if ( acceptChangesWithLoadLastCheckpoint )
				{
					ProcessAcceptedVars( vars );

					QueueNotifyStatus( NotificationType::ChangesLoadLastCheckpointApplied );
				}
			}
		}
	}

	void System::UpdateLoadingQueue()
	{
		const Bool loadingFinished = m_loadingFinished.Exchange( false );

		if ( loadingFinished )
		{
			const Bool loadingCanceled = m_loadingCanceled.Exchange( false );

			red::DynArray< Var* > vars{ PoolInGameConfig() };

			{
				RED_SCOPE_LOCK( m_loadFromSettingsQueueLock );
				vars = std::move( m_loadFromSettingsQueue );
			}

			if ( !vars.Empty() )
			{
				if ( loadingCanceled )
				{
					ProcessRejectedVars( vars );

					QueueNotifyStatus( NotificationType::LoadCanceled );
				}
				else
				{
					ProcessLoadedVars( vars );

					QueueNotifyStatus( NotificationType::Loaded );
				}
			}

			m_canSaveUserSettings = CanSave::Yes;
		}
	}

	void System::UpdateNotifications()
	{
		red::DynArray< Var* > varsToNotify{ PoolInGameConfig() };
		red::DynArray< Var* > varsToReset{ PoolInGameConfig() };
		red::DynArray< NotificationType > notifications{ PoolInGameConfig() };

		{
			RED_SCOPE_LOCK( m_notificationsQueueLock );
			varsToNotify = std::move( m_varNotificationsQueue );
			varsToReset = std::move( m_varResetQueue );
			notifications = std::move( m_notificationsQueue );
		}

		while ( !varsToNotify.Empty() )
		{
			Var *var = varsToNotify.PopBack();
			NotifyVarChanged( var );
		}

		while ( !varsToReset.Empty() )
		{
			Var *var = varsToReset.PopBack();
			var->ResetReasonOfChange();
			var->ResetSourceOfChange();
		}

		while ( !notifications.Empty() )
		{
			const NotificationType status = notifications.PopBack();
			m_registry.NotifyStatus( status );
		}
	}

	void System::ProcessLoadedVars( red::DynArray< Var* > &vars )
	{
		while ( !vars.Empty() )
		{
			Var* var = vars.PopBack();

			const CName groupPath = var->GetGroup();
			const CName varName = var->GetName();

			if ( m_registry.HasValidator( groupPath, varName ) )
			{
				const VarType varType = var->GetType();
				const Source source = var->SourceOfChange();
				const Bool varHandled = m_registry.ValidateVar( groupPath, varName, varType, source );
				ALWAYSENABLED_RED_FATAL_ASSERT( varHandled );
			}

			var->InternalAcceptValue();
			var->InternalMarkAsSaved();
			var->SetReasonOfChange( ChangeReason::Accepted );

			QueueVarNotification( var );
			QueueVarReset( var );
		}
	}

	void System::ProcessAcceptedVars( red::DynArray< Var* >& vars )
	{
		while ( !vars.Empty() )
		{
			Var* var = vars.PopBack();

			const CName groupPath = var->GetGroup();
			const CName varName = var->GetName();
			ChangeReason reason = ChangeReason::Accepted;

			if ( m_registry.HasValidator( groupPath, varName ) )
			{
				const VarType varType = var->GetType();
				const Source source = var->SourceOfChange();
				if ( m_registry.ValidateVar( groupPath, varName, varType, source ) )
				{
					var->InternalAcceptValue();
				}
				else
				{
					var->InternalRejectValue();
					reason = ChangeReason::Rejected;
				}
			}
			else
			{
				var->InternalAcceptValue();
			}

			var->SetReasonOfChange( reason );

			QueueVarNotification( var );
			QueueVarReset( var );
		}
	}

	void System::ProcessRejectedVars( red::DynArray< Var* >& vars )
	{
		while ( !vars.Empty() )
		{
			Var* var = vars.PopBack();

			var->InternalRejectValue();
			var->SetReasonOfChange( ChangeReason::Rejected );

			QueueVarNotification( var );
			QueueVarReset( var );
		}
	}
}
