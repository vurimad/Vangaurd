/**
 * Copyright (c) 2019-2020 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "inGameConfigUtils.h"
#include "inGameConfigRegistry.h"
#include "inGameConfigGroup.h"
#include "../../redContainers/include/hashMap.h"
#include "../../redMemory/include/uniquePtr.h"
#include "../../redCore/include/absolutePath.h"

class IFile;

namespace red
{
	class AbsolutePath;
}

namespace services
{
	enum ServiceError : Int32;
}

namespace InGameConfig
{
	class RED_CONFIG_API System : red::NonCopyable
	{
	public:
		~System();

		void LoadTemplates( const red::AbsolutePath& commonSettingsPath, const red::AbsolutePath& platformSettingsPath );

#ifdef RED_PLATFORM_WINPC
		void LoadUserSettings();
		Uint32 SaveUserSettings();

		static red::AbsolutePath GetUserSettingsPath();
#endif

		void LoadUserSettings( IFile* file, services::ServiceError errorCode );
		Uint32 SaveUserSettings( IFile* file, services::ServiceError errorCode );

		void Update();

		VarValidator::ID RegisterValidator( CName groupPath, CName varName, VarValidator::Functor &&functor );
		VarValidator::ID RegisterValidator( CName groupPath, CName varName, const VarValidator::Functor &functor );
		VarValidator::ID RegisterValidator( CName groupPath, CName varName, VarValidator &&handler );
		VarValidator::ID RegisterValidator( CName groupPath, CName varName, const VarValidator &handler );
		void UnregisterValidator( VarValidator::ID id );
		Bool HasValidator( CName groupPath, CName varName ) const;

		VarListener::ID RegisterListener( CName groupPath, VarListener::Functor &&functor, Uint8 priority = 0 );
		VarListener::ID RegisterListener( CName groupPath, const VarListener::Functor &functor, Uint8 priority = 0 );
		VarListener::ID RegisterListener( CName groupPath, VarListener &&listener );
		VarListener::ID RegisterListener( CName groupPath, const VarListener &listener );
		void UnregisterListener( VarListener::ID id );

		NotificationHandler::ID RegisterNotifier( NotificationHandler::Functor &&functor );
		NotificationHandler::ID RegisterNotifier( const NotificationHandler::Functor &functor );
		NotificationHandler::ID RegisterNotifier( NotificationHandler &&handler );
		NotificationHandler::ID RegisterNotifier( const NotificationHandler &handler );
		void UnregisterNotifier( NotificationHandler::ID id );

		Bool HasGroup( CName groupPath ) const;
		Bool HasVar( CName groupPath, CName varName ) const;

		Group& GetRootGroup();
		const Group& GetRootGroup() const;

		Group& GetGroup( CName groupPath );
		const Group& GetGroup( CName groupPath ) const;

		Var& GetVar( CName groupPath, CName configVar );
		const Var& GetVar( CName groupPath, CName configVar ) const;

		VarBool& GetVarBool( CName groupPath, CName configVar );
		const VarBool& GetVarBool( CName groupPath, CName configVar ) const;
		VarInt& GetVarInt( CName groupPath, CName configVar );
		const VarInt& GetVarInt( CName groupPath, CName configVar ) const;
		VarFloat& GetVarFloat( CName groupPath, CName configVar );
		const VarFloat& GetVarFloat( CName groupPath, CName configVar ) const;
		VarName& GetVarName( CName groupPath, CName configVar );
		const VarName& GetVarName( CName groupPath, CName configVar ) const;
		VarListInt& GetVarListInt( CName groupPath, CName configVar );
		const VarListInt& GetVarListInt( CName groupPath, CName configVar ) const;
		VarListFloat& GetVarListFloat( CName groupPath, CName configVar );
		const VarListFloat& GetVarListFloat( CName groupPath, CName configVar ) const;
		VarListString& GetVarListString( CName groupPath, CName configVar );
		const VarListString& GetVarListString( CName groupPath, CName configVar ) const;
		VarListName& GetVarListName( CName groupPath, CName configVar );
		const VarListName& GetVarListName( CName groupPath, CName configVar ) const;

		void ScheduleVarUpdateWithoutConfirmation( Var* configVar );
		void ScheduleVarUpdateWithConfirmation( Var* configVar );
		void ScheduleVarUpdateWithRestart( Var* configVar );
		void ScheduleVarUpdateWithLoadLastCheckpoint( Var* configVar );
		void ScheduleVarLoadFromSettings( Var* configVar );

		void QueueNotifyStatus( NotificationType status );

		Bool NeedsConfirmation() const;
		void ConfirmVarChanges();
		void RejectVarChanges();

		void RestoreToDefaults( Source source, Bool isPreGame, Bool onlyVisible, Bool onlyMarked, const CName &groupName = CName::NONE() );

		void MarkAsSaved();

		Bool NeedsRestart() const;
		void AcceptRestartRequired();
		void RejectRestartRequired();

		Bool NeedsLoadLastCheckpoint() const;
		void ConfirmLoadLastCheckpointVarChanges();
		void RejectLoadLastCheckpointVarChanges();

		void LoadingFinished();
		void LoadingCanceled();

		Bool WasModifiedSinceLastSave() const;

		static System& GetInstance();

		Bool IsFirstRun() const;

		UserSettingsLoadStatus GetUserSettingsLoadStatus() const;
		Bool GetUserSettingsLoadStatusChecked() const;
		void SetUserSettingsLoadStatusChecked();

		UserSettingsSaveStatus GetUserSettingsSaveStatus() const;
		Bool GetUserSettingsSaveStatusChecked() const;
		void SetUserSettingsSaveStatusChecked();

		void SetMode( Mode mode );
		Mode GetMode() const;

		void SetFileVersion( Int32 version );
		Int32 GetFileVersion() const;

		void QueueVarNotification( Var* configVar );
		void QueueVarReset( Var* configVar );

		Bool CanSaveUserSettings() const;
		Bool RequestedSaveDuringLoad() const;
		void ResetRequestedSaveDuringLoad();

	private:
		System();

		void UpdateImmediateQueue();
		void UpdateConfirmationQueue();
		void UpdateRestartQueue();
		void UpdateLoadLastCheckPointQueue();
		void UpdateLoadingQueue();
		void UpdateNotifications();
		void ProcessLoadedVars( red::DynArray< Var* >& vars );
		void ProcessAcceptedVars( red::DynArray< Var* >& vars );
		void ProcessRejectedVars( red::DynArray< Var* >& vars );
		void NotifyVarChanged( Var* configVar );

		Registry m_registry;

		red::AbsolutePath m_baseSettingsPath;
		red::AbsolutePath m_platformSettingsPath;

		red::RWSpinLock m_immediateChangesQueueLock;
		red::DynArray< Var* > m_immediateChangesQueue;

		red::RWSpinLock m_changesWithConfirmationQueueLock;
		red::DynArray< Var* > m_changesWithConfirmationQueue;

		red::RWSpinLock m_changesWithRestartQueueLock;
		red::DynArray< Var* > m_changesWithRestartQueue;

		red::RWSpinLock m_changesWithLoadLastCheckpointQueueLock;
		red::DynArray< Var* > m_changesWithLoadLastCheckpointQueue;

		red::RWSpinLock m_loadFromSettingsQueueLock;
		red::DynArray< Var* > m_loadFromSettingsQueue;

		red::RWSpinLock m_notificationsQueueLock;
		red::DynArray< Var* > m_varNotificationsQueue;
		red::DynArray< Var* > m_varResetQueue;
		red::DynArray< NotificationType > m_notificationsQueue;

		red::Atomic< Bool > m_acceptChangesWithConfirmation;
		red::Atomic< Bool > m_rejectChangesWithConfirmation;
		red::Atomic< Bool > m_acceptRestartWithConfirmation;
		red::Atomic< Bool > m_rejectRestartWithConfirmation;
		red::Atomic< Bool > m_acceptChangesWithLoadLastCheckpoint;
		red::Atomic< Bool > m_rejectChangesWithLoadLastCheckpoint;
		red::Atomic< Bool > m_loadingFinished;
		red::Atomic< Bool > m_loadingCanceled;

		UserSettingsLoadStatus m_userSettingsLoadStatus;
		Bool m_userSettingsLoadStatusChecked;
		UserSettingsSaveStatus m_userSettingsSaveStatus;
		Bool m_userSettingsSaveStatusChecked;

		CanSave m_canSaveUserSettings;
		Bool m_requestedSaveDuringLoad;

		Mode m_mode;
		Int32 m_version;
	};

}