/**
 * Copyright (c) 2019-2020 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "inGameConfigUtils.h"
#include "inGameConfigVarListener.h"

//#define IGC_ENABLE_CHECKS

namespace InGameConfig
{
	class Var;
	class Group;

	class RED_CONFIG_API Registry final : red::NonCopyable
	{
	public:
		using Groups = red::HashMap< CName, red::UniquePtr< Group > >;
		using UpdatePolicyMapping = red::HashMap< red::String, VarUpdatePolicy >;
		using ImportPolicyMapping = red::HashMap< red::String, VarImportPolicy >;
		using TypeMapping = red::HashMap< red::String, VarType >;
		using Validators = red::HashMap< CName, VarValidator >;
		using Listeners = red::HashMap< CName, red::SortedArray< VarListener > >;
		using Notifications = red::DynArray< NotificationHandler >;

		Registry();
		~Registry();

		void RegisterGroup( CName parentGroupPath, CName groupPath, CName groupName, CName displayName, Int32 groupIndex );

		Bool HasGroup( CName groupPath ) const;
		Bool HasVar( CName groupPath, CName varName ) const;

		Group& GetRootGroup() { return GetGroup( m_rootGroup ); }
		const Group& GetRootGroup() const { return GetGroup( m_rootGroup ); }

		Group& GetGroup( CName groupPath );
		const Group& GetGroup( CName groupPath ) const;

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

		Bool ValidateVar( CName groupPath, CName varName, VarType varType, Source source );
		void NotifyListeners( CName groupPath, CName varName, VarType varType, ChangeReason reason );
		void NotifyStatus( NotificationType status );

		const UpdatePolicyMapping& GetUpdatePolicyMapping() const;
		const ImportPolicyMapping& GetImportPolicyMapping() const;
		const TypeMapping& GetTypeMapping() const;

		const Groups& GetGroups() const { return m_groups; }

		void SortGroup( Group &group );
		void SortGroups();

		void MarkAsSaved();
		void MarkAsNeedRestoreToDefault();

		Bool RestoreToDefaults( Source source, Bool isPreGame, Bool onlyVisible, Bool onlyMarked, const CName &groupPath = CName::NONE() );
		Bool WasModifiedSinceLastSave() const;

		CName GetValidatorPath( CName groupPath, CName varName ) const;

		void ProcessUnregisterListeners();

	private:
		CName m_rootGroup;
		Groups m_groups;

		red::RWSpinLock m_validatorsLock;
		red::RWSpinLock m_listenersToUnregisterLock;
		red::RWSpinLock m_listenerLock;
		red::RWSpinLock m_notificationsLock;
		Validators m_validators;
		red::DynArray< VarListener::ID > m_listenersToUnregister;
		Listeners m_listeners;
		Notifications m_notifications;

		UpdatePolicyMapping m_updatePolicyMapping;
		ImportPolicyMapping m_importPolicyMapping;
		TypeMapping m_typeMapping;
#ifdef IGC_ENABLE_CHECKS
 		red::Atomic< Int32 > m_iteratingOverListeners;
#endif
	};
}