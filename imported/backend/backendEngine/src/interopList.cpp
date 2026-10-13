// Copyright (c) 2018-2020 CD Projekt Red. All Rights Reserved.

#include "build.h"
#include "interopList.h"
#include "editActionHistory.h"
#include "objectCollection.h"
#include "contextMenu.h"

namespace tools
{
	//-----------------------------------------------------------------------------------------------

	Bool Action_AddListItem::Do()
{
		if ( AddItem() && m_list )
		{
			m_list->NotifyListChanged();
			m_list->SelectItem( GetItemIndex() );
			return true;
		}
		return false;
	}

	Bool Action_AddListItem::Undo()
{
		Uint32 index = GetItemIndex();
		Bool isSelected = m_list->IsItemSelected( index );
		if ( isSelected )
		{
			m_list->DeselectItem( index );
		}
		if ( RemoveItem() && m_list )
		{
			m_list->NotifyListChanged();
			return true;
		}
		else if ( isSelected )
		{
			m_list->SelectItem( index );
		}
		return false;
	}

	//-----------------------------------------------------------------------------------------------

	Bool Action_RemoveListItem::Do()
{
		if ( m_list )
		{
			Uint32 index = GetItemIndex();
			Bool isSelected = m_list->IsItemSelected( index );
			Int32 currentActive = m_list->GetActiveItemIndex();
			if ( isSelected )
			{
				m_list->DeselectItem( index );
			}
			if ( PerformRemoval() )
			{
				if ( currentActive >= (Int32)index )
				{
					m_list->ActivateItem( currentActive == index ? -1 : currentActive - 1 );
				}
				m_list->NotifyListChanged();
				m_wasSelected = isSelected;
				return true;
			}
			else if ( isSelected )
			{
				m_list->SelectItem( index );
			}
		}
		return false;
	}

	Bool Action_RemoveListItem::Undo()
	{
		const red::HashSet< Uint32 > previouslySelected = m_list->GetSelectedItemIndices();
		m_list->DeselectAllItems();

		if ( RevertRemoval() && m_list )
		{
			m_list->NotifyListChanged();
			if ( m_wasSelected )
			{
				m_list->SelectItem( GetItemIndex() );
			}
			return true;
		}
		else
		{
			for ( const Uint32 index : previouslySelected )
			{
				m_list->SelectItem( index );
			}
		}
		return false;
	}

	Uint32 Action_RemoveListItem::GetItemIndex() const
	{
		return m_index;
	}

	//-----------------------------------------------------------------------------------------------

	Bool Action_RenameListItem::Do()
{
		if ( PerformRename() && m_list )
		{
			m_list->NotifyListChanged();
			return true;
		}
		return false;
	}

	Bool Action_RenameListItem::Undo()
{
		if ( RevertRename() && m_list )
		{
			m_list->NotifyListChanged();
			return true;
		}
		return false;
	}

	//-----------------------------------------------------------------------------------------------
	InteropListItem::InteropListItem( Uint32 index, red::SharedPtr<InteropList> list, IEditActionHistory& actionHistory, const String& itemTypeName )
		: ::interop::utils::Object( itemTypeName.Empty() ? "listItem" : itemTypeName.AsChar() )
	{
		::interop::utils::Object::RegisterValue< String >( "name",
			/*GET*/[list, index]( interop::IContext& ctx, String& outValue ) { outValue = list->GetItemName( index ); } );

		if ( list->GetSetupInfo().m_supportItemRemoval )
		{
			::interop::utils::Object::RegisterFunctor<Bool>( "remove",
				[list, index, &actionHistory]( interop::IContext& ctx )
			{
				Bool result = false;
				if ( list->CanRemoveItem( index ) )
				{
					result = list->RemoveItem( index );

					if ( !result )
						ctx.ThrowException( String::Printf( "Couldn't remove item with index %d.", index ).AsChar() );
				}

				return result;
			} );
		}

		if ( list->GetSetupInfo().m_supportItemRename )
		{
			::interop::utils::Object::RegisterFunctor<Bool, String>( "rename",
				[list, index, &actionHistory]( interop::IContext& ctx, const String& newName )
			{
				Bool result = list->RenameItem( index, newName );

				if ( !result )
					ctx.ThrowException( String::Printf( "Couldn't rename item with index %d.", index ).AsChar() );

				return result;
			} );
		}

		::interop::utils::Object::RegisterFunctor<void>( "select",
			[list, index]( interop::IContext& ctx )
		{
			list->SelectItem( index );
		} );

		::interop::utils::Object::RegisterFunctor<void>( "deselect",
			[ list, index ] ( interop::IContext& ctx )
		{
			list->DeselectItem( index );
		} );

		::interop::utils::Object::RegisterFunctor<void>( "activate",
			[list, index]( interop::IContext& ctx )
		{
			list->ActivateItem( index );
		} );

		::interop::utils::Object::RegisterFunctor<void>( "deactivate",
			[list, index]( interop::IContext& ctx )
		{
			list->DeactivateItem( index );
		} );

		if ( ContextMenuBackend* menu = list->GetItemMenu( index ) )
		{
			::interop::utils::Object::RegisterObject( "contextmenu", menu->CreateInterop() );
		}
	}

	//-----------------------------------------------------------------------------------------------
	InteropList::InteropList( IEditActionHistory& actionHistory, ObjectCollection* sharedSelection, const InteropListSetupInfo& setupInfo )
		: InteropControlBase( "InteropList", sharedSelection )
		, m_actionHistory( &actionHistory )
		, m_setupInfo( setupInfo )
		, m_activeItemIndex( -1 )
		, m_lastSelectedIndex( -1 )
		, m_selectedItemIndices{ red::PoolBackend() }
		, m_selection( sharedSelection )
	{
		m_selectedItemIndices.Reserve( 32 );           // This should be large enough to avoid dynamic allocations in most cases.

		m_serializableNotifier.SetCallback( [this]( const SerializableID& id, const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& targetValue )
			{ OnNotifyPropertyChange( id, path ); } );

		RegisterInterop();
	}

	InteropList::~InteropList() = default;

	Bool InteropList::AddItem( const String& name )
	{
		if ( m_setupInfo.m_supportItemCreation )
		{
			return GetActionHistory().Execute( GetAddItemAction( name ) );
		}
		return false;
	}

	Bool InteropList::RenameItem( Uint32 index, const String& newName )
	{
		if ( m_setupInfo.m_supportItemRename )
		{
			return GetActionHistory().Execute( GetRenameItemAction( index, newName ) );
		}
		return false;
	}

	Bool InteropList::RemoveItem( Uint32 index )
	{
		if ( m_setupInfo.m_supportItemRemoval )
		{
			return GetActionHistory().Execute( GetRemoveItemAction( index ) );
		}
		return false;
	}

	Bool InteropList::IsItemSelected( Uint32 index ) const
	{
		return m_selectedItemIndices.Exist( index );
	}

	void InteropList::DeselectItem( Uint32 index )
	{
		if ( m_selectedItemIndices.Exist( index ) )
		{
			if ( index == m_lastSelectedIndex )
				m_lastSelectedIndex = -1;
			m_selectedItemIndices.Remove( index );

			if ( ISerializable* item = GetItem( index ) )
			{
				if ( !m_setupInfo.m_editOnActivation )
				{
					RemoveSelection( item );
				}
			}

			OnDeselection( index );
			m_selectionChanged.Notify();
		}
	}

	void InteropList::SelectItem( Uint32 index )
	{
		if ( !m_selectedItemIndices.Exist( index ) )
		{
			if ( ISerializable* item = GetItem( index ) )
			{
				m_lastSelectedIndex = index;
				m_selectedItemIndices.Insert( index );

				if ( !m_setupInfo.m_editOnActivation )
				{
					SetSelection( item, IsReadonlyItem( index ) );
				}

				OnSelection( index );
				m_selectionChanged.Notify();
			}
		}
	}

	void InteropList::DeactivateItem( Int32 index )
	{
		if ( m_setupInfo.m_supportActivationToggle && m_activeItemIndex == index )
		{
			ActivateItem( -1 );
		}
	}

	void InteropList::ActivateItem( Int32 index )
	{
		if ( index != m_activeItemIndex )
		{
			if ( ( m_activeItemIndex >= 0 ) && !m_setupInfo.m_mergeDeactivateToActivate )
			{
				OnDeactivation( index );
			}

			m_activeItemIndex = ( index >= 0 && index < (Int32)GetSize() ) ? index : -1;

			if ( m_setupInfo.m_editOnActivation )
			{
				if ( m_activeItemIndex >= 0 )
				{
					SetSelection( GetItem( m_activeItemIndex ), IsReadonlyItem( m_activeItemIndex ) );
				}
				else
				{
					SetSelection( nullptr, false );
				}
			}
			OnActivation( m_activeItemIndex );
		}
	}

	void InteropList::DeselectAllItems()
	{
		red::DynArray<Uint32> selectedItemIndicesCopy;                     // Making copy of the array, since the hash map will be modified during iteration.
		m_selectedItemIndices.GetElements( selectedItemIndicesCopy );

		for ( Uint32 itemIndex : selectedItemIndicesCopy )
		{
			DeselectItem( itemIndex );
		}
		
		if ( !m_setupInfo.m_editOnActivation )
		{
			m_selection->SetObject( nullptr );
		}
	}

	void InteropList::OnActivation( Int32 index )
	{
		m_listChanged.Notify();
		m_activationChanged.Notify();
	}

	void InteropList::OnDeactivation( Int32 index )
	{
		if ( m_setupInfo.m_supportActivationToggle )
		{
			m_listChanged.Notify();
			m_activationChanged.Notify();
		}
	}

	::interop::ObjectPtr InteropList::GetElement( Uint32 ind )
	{
		return ::interop::ObjectPtr( RED_NEW( InteropListItem )( ind, SharedFromThis(), GetActionHistory() ) );
	}

	void InteropList::Update()
	{
		m_listChanged.Notify();
	}

	void InteropList::NotifyListChanged( Bool reselectCurrentItem )
	{
		m_listChanged.Notify();

		Int32 cachedLastSelectedIndex = m_lastSelectedIndex;

		DeselectAllItems();
		if ( reselectCurrentItem )
		{
			if ( cachedLastSelectedIndex == -1 )
			{
				SetSelection( nullptr, false );
			}
			else if ( (Uint32)cachedLastSelectedIndex >= GetSize() )
			{
				cachedLastSelectedIndex = -1;
				SetSelection( nullptr, false );
			}
			else
			{
				SelectItem( cachedLastSelectedIndex );
			}
		}

		OnListChanged();
	}

	void InteropList::SetSelection( ISerializable* object, Bool readonly )
	{
		m_selection->SetObject( object, readonly, m_userContext );
		m_lastSelectedID = object ? object->GetID() : SerializableID();
	}

	void InteropList::SetObjects( const red::DynArray< THandle< ISerializable > >& objects, Bool readonly )
	{
		m_selection->SetObjects( objects, m_userContext, readonly );
		m_lastSelectedID = objects.Empty() ? SerializableID() : objects[ 0 ]->GetID();
	}

	void InteropList::RefreshSelectionView()
	{
		m_selection->Refresh( true );
	}

	void InteropList::RemoveSelection( ISerializable* object )
	{
		if ( !object )
			return;

		const SerializableID& id = object->GetID();

		if ( m_selection->HasObject( id ) )
		{
			// we support only single selection for now, 
			// so we can simply reset selection view
			m_selection->Reset();
		}
	}

	void InteropList::ResetSetupInfo( const InteropListSetupInfo& newSetupInfo )
	{
		m_setupInfo = newSetupInfo;
	}

	void InteropList::RegisterInterop()
	{
		::interop::utils::Object::RegisterValueRO<Bool>( "supportItemsCreation", [this]() { return m_setupInfo.m_supportItemCreation; } );
		::interop::utils::Object::RegisterValueRO<Bool>( "supportItemsRemoval", [this]() { return m_setupInfo.m_supportItemRemoval; } );
		::interop::utils::Object::RegisterValueRO<Bool>( "supportItemsRename", [this]() { return m_setupInfo.m_supportItemRename; } );

		::interop::utils::Object::RegisterResizableArray( "content",
			[this]( ::interop::IContext& ctx ) { return GetSize(); },
			[this]( ::interop::IContext& ctx, const Uint32 index ) -> ::interop::ObjectPtr
		{
			return GetElement( index );
		} );

		::interop::utils::Object::RegisterNotifier( "listChanged", m_listChanged );
		::interop::utils::Object::RegisterObject( "selection", interop::ObjectPtr( m_selection->GetRootInterop() ) );

		::interop::utils::Object::RegisterFunctor<void>( "clearSelection",
			[this]( interop::IContext& ctx )
		{
			m_selection->SetObject( nullptr );
		} );

		if ( m_setupInfo.m_supportItemCreation )
		{
			::interop::utils::Object::RegisterFunctor<Bool, String>( "addItem",
				[this]( interop::IContext& ctx, const String& name )
				{
					Bool result = AddItem( name );

					if ( !result )
						ctx.ThrowException( String::Printf( "Couldn't add item with name %hs.", name.AsChar() ).AsChar() );

					return result;
				} );
		}

		::interop::utils::Object::RegisterNotifier( "activationChanged", m_activationChanged );
		::interop::utils::Object::RegisterValueRO<Int32>( "activeItem",
			[this]( ::interop::IContext& ctx, Int32& outValue ) { outValue = m_activeItemIndex; },
			&m_activationChanged );

		::interop::utils::Object::RegisterNotifier( "selectionChanged", m_selectionChanged );
		::interop::utils::Object::RegisterValueRO< red::DynArray< Uint32 > >( "selectedItems",
			[this]( ::interop::IContext& ctx, red::DynArray< Uint32 >& outValue ) 
		{ 
			const red::ArraySpan< const Uint32 > elements = m_selectedItemIndices.Elements();
			outValue.Reserve( elements.Size() );
			for( const Uint32& idx : elements )
			{
				outValue.EmplaceBack( idx );
			}
		},
			&m_selectionChanged );
	}

	Bool InteropList::CanRemoveItem( const Uint32 index ) const
	{
		return true;
	}

	void InteropList::OnNotifyPropertyChange( const SerializableID& id, const rtti::AccessPath& path )
	{
		if ( id == m_lastSelectedID )
		{
			OnItemPropertyChanged( path.ToString() );
		}
	}

	THandle< ISerializable > InteropList::GetUserContext() const
	{
		return m_userContext;
	}

	void InteropList::SetUserContext( THandle< ISerializable > userContext )
	{
		m_userContext = userContext;
	}
}