/**
 * Copyright (c) 2019-2020 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "inGameConfigRegistry.h"
#include "inGameConfigVar.h"
#include "inGameConfigGroup.h"
#include "inGameConfigVarListener.h"
#include "../../../common/redContainers/include/string/stringUtils.h"

namespace InGameConfig
{
	Registry::~Registry() = default;

	Registry::Registry()
		: m_groups( PoolInGameConfig() )
		, m_validators( PoolInGameConfig() )
		, m_listenersToUnregister( PoolInGameConfig() )
		, m_listeners( PoolInGameConfig() )
		, m_notifications( PoolInGameConfig() )
		, m_updatePolicyMapping( PoolInGameConfig() )
		, m_importPolicyMapping( PoolInGameConfig() )
		, m_typeMapping( PoolInGameConfig() )
#ifdef IGC_ENABLE_CHECKS
 		, m_iteratingOverListeners( 0 )
#endif
	{
		m_updatePolicyMapping[ "update_disabled" ] = VarUpdatePolicy::Disabled;
		m_updatePolicyMapping[ "immediately" ] = VarUpdatePolicy::Immediately;
		m_updatePolicyMapping[ "require_confirmation" ] = VarUpdatePolicy::ConfirmationRequired;
		m_updatePolicyMapping[ "require_restart" ] = VarUpdatePolicy::RestartRequired;
		m_updatePolicyMapping[ "require_load_last_checkpoint" ] = VarUpdatePolicy::LoadLastCheckpointRequired;

		m_importPolicyMapping[ "read_value" ] = VarImportPolicy::ReadValue;
		m_importPolicyMapping[ "ignore" ] = VarImportPolicy::Ignore;

		m_typeMapping[ "bool" ] = VarType::Bool;
		m_typeMapping[ "int" ] = VarType::Int;
		m_typeMapping[ "float" ] = VarType::Float;
		m_typeMapping[ "name" ] = VarType::Name;
		m_typeMapping[ "int_list" ] = VarType::IntList;
		m_typeMapping[ "float_list" ] = VarType::FloatList;
		m_typeMapping[ "string_list" ] = VarType::StringList;
		m_typeMapping[ "name_list" ] = VarType::NameList;
	}

	void Registry::RegisterGroup(
		const CName parentGroupPath,
		const CName groupPath,
		const CName groupName,
		const CName displayName,
		const Int32 groupIndex )
	{
		if ( m_groups.Find( groupPath ) != m_groups.End() )
		{
			return;
		}

		m_groups.Insert( groupPath, red::CreateUniquePtr< Group >(
			parentGroupPath,
			groupPath,
			groupName,
			displayName,
			groupIndex ) );

		if ( !parentGroupPath.Empty() )
		{
			Group &parentGroup = GetGroup( parentGroupPath );
			parentGroup.AddGroup( groupPath );
		}
		else
		{
			m_rootGroup = groupPath;
		}
	}

	Bool Registry::HasGroup( CName groupPath ) const
	{
		return m_groups.Find( groupPath ) != m_groups.End();
	}

	Bool Registry::HasVar( CName groupPath, CName varName ) const
	{
		if ( !HasGroup( groupPath ) )
		{
			return false;
		}

		return GetGroup( groupPath ).HasVar( varName );
	}

	Group& Registry::GetGroup( CName groupPath )
	{
		auto it = m_groups.Find( groupPath );
		ALWAYSENABLED_RED_FATAL_ASSERT( it != m_groups.End(), "Could not find group '%s' in in game config system", groupPath.AsChar() );
		return *( it.Value() );
	}

	const Group& Registry::GetGroup( CName groupPath ) const
	{
		auto it = m_groups.Find( groupPath );
		ALWAYSENABLED_RED_FATAL_ASSERT( it != m_groups.End(), "Could not find group '%s' in in game config system", groupPath.AsChar() );
		return *( it.Value() );
	}

	VarValidator::ID Registry::RegisterValidator( CName groupPath, CName varName, VarValidator::Functor &&functor )
	{
		return RegisterValidator( groupPath, varName, VarValidator{ std::move( functor ) } );
	}

	VarValidator::ID Registry::RegisterValidator( CName groupPath, CName varName, const VarValidator::Functor &functor )
	{
		return RegisterValidator( groupPath, varName, VarValidator{ functor } );
	}

	VarValidator::ID Registry::RegisterValidator( CName groupPath, CName varName, VarValidator &&handler )
	{
		RED_SCOPE_LOCK( m_validatorsLock );

		const CName validatorPath = GetValidatorPath( groupPath, varName );

		auto it = m_validators.Find( validatorPath );

		ALWAYSENABLED_RED_FATAL_ASSERT( it == m_validators.End(), "Validator for \'%hs\' already registered", validatorPath.AsChar() );

		it = m_validators.Insert( validatorPath, std::move( handler ) ).Iterator();

		return it.Value().GetID();
	}

	VarValidator::ID Registry::RegisterValidator( CName groupPath, CName varName, const VarValidator &handler )
	{
		RED_SCOPE_LOCK( m_validatorsLock );

		const CName validatorPath = GetValidatorPath( groupPath, varName );

		auto it = m_validators.Find( validatorPath );

		ALWAYSENABLED_RED_FATAL_ASSERT( it == m_validators.End(), "Validator for \'%hs\' already registered", validatorPath.AsChar() );

		it = m_validators.Insert( validatorPath, handler ).Iterator();

		return it.Value().GetID();
	}

	void Registry::UnregisterValidator( VarValidator::ID id )
	{
		RED_SCOPE_LOCK( m_validatorsLock );

		for ( auto entry : m_validators )
		{
			const VarValidator &validator = entry.Value();
			if ( validator.GetID() == id )
			{
				m_validators.RemoveValue( validator );
				return;
			}
		}
	}

	Bool Registry::HasValidator( CName groupPath, CName varName ) const
	{
		const CName validatorPath = GetValidatorPath( groupPath, varName );
		return m_validators.Find( validatorPath ) != m_validators.End();
	}

	VarListener::ID Registry::RegisterListener( CName groupPath, VarListener::Functor &&functor, Uint8 priority )
	{
		return RegisterListener( groupPath, VarListener{ std::move( functor ), priority } );
	}

	VarListener::ID Registry::RegisterListener( CName groupPath, const VarListener::Functor &functor, Uint8 priority )
	{
		return RegisterListener( groupPath, VarListener{ functor, priority } );
	}

	VarListener::ID Registry::RegisterListener( CName groupPath, VarListener &&listener )
	{
#ifdef IGC_ENABLE_CHECKS
 		RED_FATAL_ASSERT( m_iteratingOverListeners.GetValue() == 0 );
#endif
		RED_SCOPE_LOCK( m_listenerLock );

		auto it = m_listeners.Find( groupPath );
		if ( it == m_listeners.End() )
		{
			it = m_listeners.Insert( groupPath, { red::PoolEngine() } ).Iterator();
		}

		red::alg::PushBackUnique( it.Value(), std::move( listener ) );

		return it.Value().Back().GetID();
	}

	VarListener::ID Registry::RegisterListener( CName groupPath, const VarListener &listener )
	{
#ifdef IGC_ENABLE_CHECKS
 		RED_FATAL_ASSERT( m_iteratingOverListeners.GetValue() == 0 );
#endif
		RED_SCOPE_LOCK( m_listenerLock );

		auto it = m_listeners.Find( groupPath );
		if ( it == m_listeners.End() )
		{
			it = m_listeners.Insert( groupPath, { red::PoolEngine() } ).Iterator();
		}

		red::alg::PushBackUnique( it.Value(), listener );

		return it.Value().Back().GetID();
	}

	void Registry::UnregisterListener( const VarListener::ID id )
	{
		RED_SCOPE_LOCK( m_listenersToUnregisterLock );

		m_listenersToUnregister.PushBack( id );
	}

	NotificationHandler::ID Registry::RegisterNotifier( NotificationHandler::Functor &&functor )
	{
		return RegisterNotifier( NotificationHandler{ std::move( functor ) } );
	}

	NotificationHandler::ID Registry::RegisterNotifier( const NotificationHandler::Functor &functor )
	{
		return RegisterNotifier( NotificationHandler{ functor } );
	}

	NotificationHandler::ID Registry::RegisterNotifier( NotificationHandler &&handler )
	{
		RED_SCOPE_LOCK( m_notificationsLock );

		const auto id = handler.GetID();
		auto it = std::find_if( m_notifications.Begin(), m_notifications.End(), [ id ]( const NotificationHandler &temp )
		{
			return temp.GetID() == id;
		} );

		if ( it == m_notifications.End() )
		{
			m_notifications.PushBack( std::move( handler ) );
			return m_notifications.Back().GetID();
		}

		return handler.GetID();
	}

	NotificationHandler::ID Registry::RegisterNotifier( const NotificationHandler &handler )
	{
		RED_SCOPE_LOCK( m_notificationsLock );

		const auto id = handler.GetID();
		auto it = std::find_if( m_notifications.Begin(), m_notifications.End(), [ id ]( const NotificationHandler &temp )
		{
			return temp.GetID() == id;
		} );

		if ( it == m_notifications.End() )
		{
			m_notifications.PushBack( handler );
			return m_notifications.Back().GetID();
		}

		return handler.GetID();
	}

	void Registry::UnregisterNotifier( NotificationHandler::ID id )
	{
		RED_SCOPE_LOCK( m_notificationsLock );

		auto it = std::find_if( m_notifications.Begin(), m_notifications.End(), [ id ]( const NotificationHandler &handler )
		{
			return handler.GetID() == id;
		} );

		if ( it != m_notifications.End() )
		{
			m_notifications.Remove( it );
		}
	}

	Bool Registry::ValidateVar( CName groupPath, CName varName, const VarType varType, const Source source )
	{
		RED_SCOPE_SHARED_LOCK( m_validatorsLock );

		const CName validatorPath = GetValidatorPath( groupPath, varName );
		auto it = m_validators.Find( validatorPath );

		ALWAYSENABLED_RED_FATAL_ASSERT( it != m_validators.End(), "Validator for \'%hs\' don't exists", validatorPath.AsChar() );

		return it.Value()( groupPath, varName, varType, source );
	}

	void Registry::NotifyListeners( const CName groupPath, const CName varName, const VarType varType, const ChangeReason reason )
	{
#ifdef IGC_ENABLE_CHECKS
 		RED_FATAL_ASSERT( m_iteratingOverListeners.GetValue() == 0 );
#endif
		RED_SCOPE_SHARED_LOCK( m_listenerLock );

#ifdef IGC_ENABLE_CHECKS
 		m_iteratingOverListeners.ExchangeAdd( 1 );
#endif

		auto it = m_listeners.Find( groupPath );
		if ( it == m_listeners.End() )
		{
#ifdef IGC_ENABLE_CHECKS
 			m_iteratingOverListeners.ExchangeAdd( -1 );
#endif

			return;
		}

		for ( auto& listener : it.Value() )
		{
			listener( groupPath, varName, varType, reason );
		}

#ifdef IGC_ENABLE_CHECKS
 		m_iteratingOverListeners.ExchangeAdd( -1 );
#endif
	}

	void Registry::NotifyStatus( const NotificationType status )
	{
		RED_SCOPE_SHARED_LOCK( m_notificationsLock );

		for ( auto& notifier : m_notifications )
		{
			notifier( status );
		}
	}

	const Registry::UpdatePolicyMapping& Registry::GetUpdatePolicyMapping() const
	{
		return m_updatePolicyMapping;
	}

	const Registry::ImportPolicyMapping& Registry::GetImportPolicyMapping() const
	{
		return m_importPolicyMapping;
	}

	const Registry::TypeMapping& Registry::GetTypeMapping() const
	{
		return m_typeMapping;
	}

	void Registry::SortGroup( Group &group )
	{
		constexpr Int32 c_defaultIndex = -1;

		red::DynArray< CName > &groups = group.GetGroups();
		std::sort( groups.Begin(), groups.End(), [this]( const CName groupPath1, const CName groupPath2 )
		{
			const Group& group1 = GetGroup( groupPath1 );
			const Group& group2 = GetGroup( groupPath2 );

			const Int32 order1 = group1.GetOrder();
			const Int32 order2 = group2.GetOrder();

			if ( order1 >= 0 && order2 >= 0 )
			{
				return order1 < order2;
			}
			else if ( order1 >= 0 )
			{
				return true;
			}
			else if ( order2 >= 0 )
			{
				return false;
			}

			const auto groupName1 = group1.GetName().AsStringView();
			const auto groupName2 = group2.GetName().AsStringView();
			return groupName1 < groupName2;
		} );

		for ( const CName groupPath : groups )
		{
			Group& subGroup = GetGroup( groupPath );
			SortGroup( subGroup );
		}
	}

	void Registry::SortGroups()
	{
		SortGroup( GetRootGroup() );
	}

	void Registry::MarkAsSaved()
	{
		for ( const auto &groupEntry : m_groups )
		{
			groupEntry.Value()->MarkAsSaved();
		}
	}

	void Registry::MarkAsNeedRestoreToDefault()
	{
		for ( const auto &groupEntry : m_groups )
		{
			groupEntry.Value()->MarkAsNeedRestoreToDefault();
		}
	}

	Bool Registry::RestoreToDefaults(
		const Source source,
		const Bool isPreGame,
		const Bool onlyVisible,
		const Bool onlyMarked,
		const CName &groupPath )
	{
		Bool restored{};

		for (const auto &groupEntry : m_groups)
		{
			const auto &group = groupEntry.Value();
			const auto currentGroupPath = group->GetPath();
			const auto currentGroupParentPath = group->GetParentPath();

			if ( groupPath == CName::NONE() || currentGroupPath == groupPath || currentGroupParentPath == groupPath )
			{
				restored |= group->RestoreToDefaults( source, isPreGame, onlyVisible, onlyMarked );
			}
		}

		return restored;
	}

	Bool Registry::WasModifiedSinceLastSave() const
	{
		for ( const auto &groupEntry : m_groups )
		{
			if ( groupEntry.Value()->WasModifiedSinceLastSave() )
			{
				return true;
			}
		}

		return false;
	}

	CName Registry::GetValidatorPath( CName groupPath, CName varName ) const
	{
		const String path = red::StrCat( groupPath.AsStringView(), { "/" }, varName.AsStringView() );
		return RED_NAME( path );
	}

	void Registry::ProcessUnregisterListeners()
	{
		RED_SCOPE_SHARED_LOCK( m_listenerLock );
		RED_SCOPE_SHARED_LOCK( m_listenersToUnregisterLock );

		for ( const VarListener::ID &id : m_listenersToUnregister )
		{
			for ( auto entry : m_listeners )
			{
				red::SortedArray< VarListener > &listeners = entry.Value();
				auto it = std::find_if( listeners.Begin(), listeners.End(), [ id ]( const VarListener &listener )
				{
					return listener.GetID() == id;
				} );
				if ( it != listeners.End() )
				{
					listeners.Remove( it );
				}
			}
		}

		m_listenersToUnregister.Clear();
	}
}
