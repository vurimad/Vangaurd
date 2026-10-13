/**
* Copyright (c) 2013-2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "objectCollection.h"

#include "editAction.h"
#include "editActionHistory.h"

#include "../../../common/redReflection/include/rttiAccessPath.h"
#include "../../../common/redReflection/include/objectUtils.hpp"
#include "../../../common/redReflection/include/editableView.h"
#include "../../../common/redReflection/include/resource.h"

#include "selectionState.h"
#include "selectionIterator.h"
#include "editableProxy.h"

#include "../include/selectionManager.h"

using red::HashMap;

namespace tools
{
	namespace helper
	{
		/// The dummy RTTI error reporting context
		class DummyRTTIContext : public IRTTIContext
		{
		public:
			DummyRTTIContext()
				: m_isOK( true )
			{}

			virtual const Bool ReportError( const rtti::IType* typeInfo, STATIC_CHECK_PRINTF_MSC const AnsiChar* txt, ... )
			{
				m_isOK = false;
				return false;
			}

			const Bool IsOK() const
			{
				return m_isOK;
			}

		private:
			Bool	m_isOK;
		};

		/// THE ACTION to edit the properties
		class Action_SetPropertyValue : public IEditAction
		{
			RTTI_DECLARE_TYPE( Action_SetPropertyValue );

		public:
			virtual String GetDescription() const
			{
				return String::Printf( "Property changed \n%hs", m_accessPath.ToString().AsChar() );
			}

			virtual Bool Do()
			{
				DummyRTTIContext ctx;

				Bool result = true;
				for ( Uint32 i=0; i<m_views.Size(); ++i )
				{
					result &= m_views[i]->SetValue( ctx, m_accessPath, m_oldValues[i], m_newValue, false );
				}

				return ctx.IsOK() && result;
			}

			virtual Bool Undo()
			{
				DummyRTTIContext ctx;

				Bool result = true;
				for ( Uint32 i=0; i<m_views.Size(); ++i )
				{
					result &= m_views[i]->SetValue( ctx, m_accessPath, m_newValue, m_oldValues[i], false );
				}

				return ctx.IsOK() && result;
			}

			virtual Bool TryMerge( const IEditAction* lastUndoAction ) 
			{
				if ( m_transactionHelper == IEditActionHistory::ROLLING_TRANSACTION )
				{
					if ( lastUndoAction->GetClass() != GetStaticClass() )
						return false;

					const Action_SetPropertyValue* lastAction = static_cast< const Action_SetPropertyValue* >( lastUndoAction );

					if ( lastAction->m_transactionHelper != m_transactionHelper )
						return false;

					// for now reset rolling transaction
					// - if we manage to merge actions, it will be refreshed to current action transaction
					IEditActionHistory::ROLLING_TRANSACTION = 0;
					
					if ( lastAction->m_accessPath.ToString() != m_accessPath.ToString() )
						return false;

					if ( lastAction->m_views.Size() != m_views.Size() )
						return false;

					for ( auto view : lastAction->m_views )
					{
						if ( !m_views.Exist( view ) )
							return false;
					}

					for ( auto oldVal : m_oldValues )
					{
						if ( oldVal->ToString() != lastAction->m_newValue->ToString() )
							return false;
					}

					// okay, we can merge and update rolling transaction id now
					IEditActionHistory::ROLLING_TRANSACTION = m_transactionHelper;

					for ( Uint32 i = 0; i < m_oldValues.Size(); ++i )
					{
						m_oldValues[i] = lastAction->m_oldValues[i];
					}

					return true;

				}
				return false;
			}

			Bool IsModification() const
			{
				return m_isModification;
			}

			void GetAffectedResources( red::Set< res::ResourcePath >& outResourcePaths ) const
			{
				for ( const auto& resourcePath : m_modifiedResources )
				{
					outResourcePaths.Insert( resourcePath );
				}
			}

			Action_SetPropertyValue( const EditableCollection::ExecuteData& data, const red::DynArray< res::ResourcePath >& modifiedResources, Bool isModification )
				: m_accessPath( data.m_path )
				, m_views( data.m_views )
				, m_newValue( data.m_newValue )
				, m_oldValues( data.m_oldValues )
				, m_transactionHelper( IEditActionHistory::TRANSACTION_COUNTER )
				, m_modifiedResources( modifiedResources )
				, m_isModification( isModification )
			{
			}

			rtti::AccessPath										m_accessPath;
			red::SharedPtr< rtti::ValueHolder >						m_newValue;
			red::DynArray< red::SharedPtr< IEditableView > >		m_views;
			red::DynArray< red::SharedPtr< rtti::ValueHolder > >	m_oldValues;
			red::DynArray< res::ResourcePath >						m_modifiedResources;
			Uint32													m_transactionHelper;


			Bool m_isModification{ true };
		};

		/// THE ACTION to reset properties
		class Action_ResetPropertyValue : public IEditAction
		{
			RTTI_DECLARE_TYPE( Action_ResetPropertyValue );

		public:
			virtual String GetDescription() const 
			{ 
				return String::Printf( "Property reset \n%hs", m_accessPath.ToString().AsChar() );
			}

			virtual Bool Do()
			{
				DummyRTTIContext ctx;

				Bool result = true;
				for ( Uint32 i = 0; i < m_views.Size(); ++i )
				{
					result &= m_views[i]->ResetValue( ctx, m_accessPath, m_oldValues[i] );
				}

				return ctx.IsOK() && result;
			}

			virtual Bool Undo()
			{
				DummyRTTIContext ctx;

				Bool result = true;
				for ( Uint32 i = 0; i < m_views.Size(); ++i )
				{
					result &= m_views[i]->SetValue( ctx, m_accessPath, rtti::ValuePtr(), m_oldValues[i], false );
				}

				return ctx.IsOK() && result;
			}

			virtual Bool TryMerge( const IEditAction* lastUndoAction )
			{
				if ( m_transactionHelper == IEditActionHistory::ROLLING_TRANSACTION )
				{
					if ( lastUndoAction->GetClass() != GetStaticClass() )
						return false;

					const Action_ResetPropertyValue* lastAction = static_cast<const Action_ResetPropertyValue*>( lastUndoAction );

					if ( lastAction->m_transactionHelper != m_transactionHelper )
						return false;

					// for now reset rolling transaction
					// - if we manage to merge actions, it will be refreshed to current action transaction
					IEditActionHistory::ROLLING_TRANSACTION = 0;

					if ( lastAction->m_accessPath.ToString() != m_accessPath.ToString() )
						return false;

					if ( lastAction->m_views.Size() != m_views.Size() )
						return false;

					for ( auto view : lastAction->m_views )
					{
						if ( !m_views.Exist( view ) )
							return false;
					}

					// okay, we can merge and update rolling transaction id now
					IEditActionHistory::ROLLING_TRANSACTION = m_transactionHelper;

					for ( Uint32 i = 0; i < m_oldValues.Size(); ++i )
					{
						m_oldValues[i] = lastAction->m_oldValues[i];
					}

					return true;

				}
				return false;
			}

			Bool IsModification() const
			{
				return true;
			}

			void GetAffectedResources( red::Set< res::ResourcePath >& outResourcePaths ) const
			{
				for ( const auto& resourcePath : m_modifiedResources )
				{
					outResourcePaths.Insert( resourcePath );
				}
			}

			Action_ResetPropertyValue( const EditableCollection::ExecuteData& data, const red::DynArray< res::ResourcePath >& modifiedResources )
				: m_accessPath( data.m_path )
				, m_views( data.m_views )
				, m_oldValues( data.m_oldValues )
				, m_transactionHelper( IEditActionHistory::TRANSACTION_COUNTER )
				, m_modifiedResources( modifiedResources )
			{
			}

			rtti::AccessPath										m_accessPath;
			red::DynArray< red::SharedPtr< IEditableView > >		m_views;
			red::DynArray< red::SharedPtr< rtti::ValueHolder > >	m_oldValues;
			red::DynArray< res::ResourcePath >						m_modifiedResources;
			Uint32													m_transactionHelper;
		};

		class Action_PrePropertyChange : public tools::IEditAction
		{
			RTTI_DECLARE_TYPE( Action_PrePropertyChange );
		public:
			explicit Action_PrePropertyChange( red::UniquePtr< IResolverUserContext::IPrePropertyChange > action )
				: m_action{ std::move( action ) }
			{}

			virtual String GetDescription() const override { return "Pre property change."; }

			virtual Bool Do() override
			{
				return m_action->Do();
			}

			virtual Bool Undo() override
			{
				return m_action->Undo();
			}

		private:
			red::UniquePtr< IResolverUserContext::IPrePropertyChange > m_action;
		};

		const rtti::ClassType* GetCommonClass( const rtti::ClassType* classA, const rtti::ClassType* classB )
		{
			// if classes are the same, it's easy case
			if ( classA == classB )
			{
				return classA;
			}

			// if one of the classes doesn't exist, there's no common class
			if ( classA == nullptr || classB == nullptr )
			{
				return nullptr;
			}

			// check if classes inherit from each other
			if ( classA->IsA( classB ) )
			{
				return classB;
			}

			if ( classB->IsA( classA ) )
			{
				return classA;
			}

			// last case possible - classes have common parent
			const rtti::ClassType* parentClassA = classA->GetBaseClass();
			while ( parentClassA )
			{
				if ( classB->IsA( parentClassA ) )
				{
					return parentClassA;
				}
				parentClassA = parentClassA->GetBaseClass();
			}
			return nullptr;
		}

	} // helper

	ObjectCollection::ObjectCollection( IEditActionHistory& actionHistory )
		: m_actionHistory( actionHistory )
		, m_objectsParentResources{ red::PoolBackend() }
		, m_modifiesResource( true )
	{
		m_collection.BindExecutor( 
			[this](const EditableCollection::ExecuteData& data) -> Bool
			{
				red::UniquePtr< EditGroupAction > groupAction;
				
				for( const Uint32 idx : data.m_views.Indices() )
				{
					const red::SharedPtr< IEditableView > view = data.m_views[idx];
					if( const THandle< IResolverUserContext > userContext = Cast< IResolverUserContext >( view->GetUserContext() ) )
					{
						red::UniquePtr< IResolverUserContext::IPrePropertyChange > action = userContext->CreatePrePropertyChangeAction( view->GetViewClass(), view->GetSerializableID(), data.m_path, data.m_oldValues[idx], data.m_newValue );
						if( action )
						{
							if( !groupAction )
							{
								const char* const action = data.m_newValue ? "changed" : "reset";
								const String actionName = String::Printf( "Property %hs \n%hs", action, data.m_path.ToString().AsChar() );
								groupAction = red::CreateUniquePtr< EditGroupAction >( actionName );
							}

							groupAction->AddAction( red::CreateUniquePtr< helper::Action_PrePropertyChange >( std::move( action ) ) );
						}
					}
				}

				red::UniquePtr< tools::IEditAction > actionToExecute;
				if ( data.m_newValue )
				{
					actionToExecute = red::CreateUniquePtr< helper::Action_SetPropertyValue >( data, m_objectsParentResources, m_modifiesResource );
				}
				else
				{
					actionToExecute = red::CreateUniquePtr< helper::Action_ResetPropertyValue >( data, m_objectsParentResources );
				}

				if( groupAction )
				{
					groupAction->AddAction( std::move( actionToExecute ) );
					actionToExecute = std::move( groupAction );
				}

				return m_actionHistory.Execute( std::move( actionToExecute ) );
			}
		);

		m_collection.BindTransactionHandler(
			[this] ( Bool startTransaction )
			{
				if ( startTransaction )
				{
					m_actionHistory.StartTransaction( "Setting properties" );
				}
				else
				{
					m_actionHistory.EndTransaction();
				}
			}
		);
	}

	ObjectCollection::~ObjectCollection() = default;

	void ObjectCollection::Reset()
	{
		m_collection.Clear();
		m_objectsParentResources.Clear();
		m_views.Clear();
	}

	void ObjectCollection::SetObjectsFromSelection( const red::SharedPtr< SelectionState >& selection )
	{
		red::Set<EditorObjectID> collectedObjetcsIds{ red::PoolBackend() };
		red::DynArray< red::SharedPtr< IEditableProxy > > proxiesList{ red::PoolBackend() };

		// extract editable proxies from selection
		if ( selection )
		{
			for ( auto it = selection->Iterate(); it; ++it )
			{
				auto editableProxy = it.GetCurrentPtr();
				if ( editableProxy )
				{
					// make sure UNIQUE objects are collected (since more than one selection proxy may point to the same object)
					const EditorObjectID id = editableProxy->GetEditorObjectID();
					if ( !collectedObjetcsIds.Exist( id ) )
					{
						collectedObjetcsIds.Insert( id );
						proxiesList.PushBack( editableProxy );
					}
				}
			}
		}

		SetObjects( proxiesList );
	}

	void ObjectCollection::SetObjects( const red::DynArray< THandle< ISerializable > >& objects, THandle< ISerializable > userContext, Bool readonly )
	{
		m_objectsParentResources.Clear();

		red::DynArray< red::SharedPtr< IEditableView > > views{ red::PoolBackend() };

		// create the views
		if ( !objects.Empty() )
		{
			// find common class for all objects
			const rtti::ClassType* commonClass = objects.Front()->GetClass();

			for ( const THandle< ISerializable >& obj : objects )
			{
				commonClass = helper::GetCommonClass( commonClass, obj->GetClass() );
				if ( commonClass == nullptr )
				{
					break;
				}
			}

			if ( commonClass && commonClass->IsA<ISerializable>() && commonClass != ClassID<ISerializable>() )
			{
				// NOTE: failing to create a view even though it was reported compatible is an offense and will be asserted on
				for ( const THandle< ISerializable >& object : objects )
				{
					auto viewPtr = object->CreateView( nullptr, readonly, commonClass->GetName(), userContext );
					RED_FATAL_ASSERT( viewPtr, "Failed to create view from object even though it was reported as compatible." );
					views.PushBack( viewPtr );

					if ( const CResource* parentResource = red::FindParent< CResource >( object.Get() ) )
					{
						m_objectsParentResources.PushBack( parentResource->GetPath() );
					}
				}
			}
		}

		SetViews( views );
	}

	void ObjectCollection::SetObjects( const red::DynArray< red::SharedPtr< IEditableProxy > >& proxies )
	{
		m_objectsParentResources.Clear();

		red::DynArray< red::SharedPtr< IEditableView > > views{ red::PoolBackend() };

		// create the views
		if ( !proxies.Empty() )
		{
			// find common class for all proxies
			const rtti::ClassType* commonClass = GetRttiSystem().FindClass( proxies[0]->GetViewClass() );

			// make the view class compatible with all other proxies (it may change)
			for ( const red::SharedPtr< IEditableProxy >& proxy : proxies )
			{
				commonClass = helper::GetCommonClass( commonClass, GetRttiSystem().FindClass( proxy->GetViewClass() ) );
				if ( commonClass == nullptr )
				{
					break;
				}
			}

			if ( commonClass && commonClass->IsA<ISerializable>() && commonClass != ClassID<ISerializable>() )
			{
				for ( const red::SharedPtr< IEditableProxy >& proxy : proxies )
				{
					auto viewPtr = proxy->CreateView( commonClass->GetName() );
					RED_FATAL_ASSERT( viewPtr, "Failed to create view from proxy even though it was reported as compatible." );

					m_objectsParentResources.PushBack( proxy->GetInstanceEditorObjectID().GetResourcePath() );
					views.PushBack( viewPtr );
				}
			}
		}

		// setup new views
		SetViews( views );
	}

	void ObjectCollection::SetObject( ISerializable* object, Bool readonly, THandle< ISerializable > userContext )
	{
		m_objectsParentResources.Clear();

		red::DynArray< red::SharedPtr< IEditableView > > views{ red::PoolBackend() };

		if ( object )
		{
			auto view = object->CreateView( nullptr, readonly, CName::NONE(), userContext );
			if ( view )
			{
				views.PushBack( view );

				if ( const CResource* parentResource = red::FindParent< CResource >( object ) )
				{
					m_objectsParentResources.PushBack( parentResource->GetPath() );
				}
			}
		}

		// setup new views
		SetViews( views );
	}

	interop::ObjectPtr ObjectCollection::GetRootInterop()
	{
		return m_collection.GetRootInterop();
	}

	Bool ObjectCollection::HasObject( const SerializableID& id ) const
	{
		return m_collection.HasObject( id );
	}

	void ObjectCollection::Refresh( Bool fullRefresh )
	{
		m_collection.RefreshView( fullRefresh );
	}

	void ObjectCollection::SetViews( const red::DynArray< red::SharedPtr< IEditableView > >& views )
	{
		m_views = views;
		m_collection.SetObjects( views );
	}

	const red::DynArray< red::SharedPtr< IEditableView > >& ObjectCollection::GetViews() const
	{
		return m_views;
	}

	void ObjectCollection::UpdateGlobalSelection()
	{
	}

	void ObjectCollection::SetModifyResource( Bool modifyResource )
	{
		m_modifiesResource = modifyResource;
	}

	SharedObjectCollection::SharedObjectCollection( SelectionManager* selectionManager, IEditActionHistory& actionHistory, ObjectCollection* sharedObjectCollection )
		: ObjectCollection( actionHistory )
		, m_selectionManager( selectionManager )
		, m_masterCollection( sharedObjectCollection )
	{
	}

	SharedObjectCollection::~SharedObjectCollection() = default;

	void SharedObjectCollection::Reset()
	{
		ObjectCollection::Reset();
		m_masterCollection->Reset();
		m_selectionManager->ResetSharedViews();
	}

	Bool SharedObjectCollection::HasObject( const SerializableID& id ) const
	{
		return m_masterCollection->HasObject( id ) || ObjectCollection::HasObject( id );
	}

	void SharedObjectCollection::Refresh( Bool fullRefresh )
	{
		ObjectCollection::Refresh( fullRefresh );
		m_masterCollection->Refresh( fullRefresh );
		m_selectionManager->RefreshSharedViews();
	}

	void SharedObjectCollection::UpdateGlobalSelection()
	{
		m_selectionManager->UpdateSharedViews( GetViews() );
	}

	void SharedObjectCollection::SetViews( const red::DynArray< red::SharedPtr< IEditableView > >& views )
	{
		ObjectCollection::SetViews( views );
		m_masterCollection->SetViews( views );
		m_selectionManager->SetViews( this, views );
	}
} // tools

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( Action_SetPropertyValue, tools, helper )
	RTTI_PARENT_TYPE( tools::IEditAction );
RTTI_END_TYPE()

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( Action_ResetPropertyValue, tools, helper )
	RTTI_PARENT_TYPE( tools::IEditAction );
RTTI_END_TYPE()

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( Action_PrePropertyChange, tools, helper )
	RTTI_PARENT_TYPE( tools::IEditAction );
RTTI_END_TYPE()

