#pragma once

#include "editAction.h"
#include "interopControlBase.h"

#include "../../../common/redReflection/include/serializableValueNotifier.h"

namespace tools
{
	class Action_AddListItem;
	class Action_RemoveListItem;
	class Action_RenameListItem;
	class ContextMenuBackend;
	class IEditActionHistory;

	struct InteropListSetupInfo
	{
		InteropListSetupInfo()
			: m_supportItemCreation( true )
			, m_supportItemRemoval( true )
			, m_supportItemRename( true )
			, m_supportActivationToggle( false )
			, m_mergeDeactivateToActivate( false )
			, m_editOnActivation( false )
		{}

		InteropListSetupInfo( ObjectCollection* sharedObjectCollection )
			: m_supportItemCreation( true )
			, m_supportItemRemoval( true )
			, m_supportItemRename( true )
			, m_supportActivationToggle( false )
			, m_mergeDeactivateToActivate( false )
			, m_editOnActivation( false )
		{}

		InteropListSetupInfo( Bool supportCreation, Bool supportRemoval, Bool supportRename )
			: m_supportItemCreation( supportCreation )
			, m_supportItemRemoval( supportRemoval )
			, m_supportItemRename( supportRename )
			, m_supportActivationToggle( false )
			, m_mergeDeactivateToActivate( false )
			, m_editOnActivation( false )
		{}

		Bool m_supportItemCreation;
		Bool m_supportItemRemoval;
		Bool m_supportItemRename;
		Bool m_supportActivationToggle;
		Bool m_mergeDeactivateToActivate;
		Bool m_editOnActivation;
	};

	class BACKEND_ENGINE_API InteropList : public InteropControlBase, public red::EnableSharedFromThis<InteropList>
	{
	public:
		InteropList( IEditActionHistory& actionHistory, ObjectCollection* sharedSelection, const InteropListSetupInfo& setupInfo = InteropListSetupInfo() );
		virtual ~InteropList();

		RED_INLINE Int32 GetActiveItemIndex() const { return m_activeItemIndex; }

		virtual ISerializable* GetItem( Uint32 index ) const = 0;
		virtual String GetItemName( Uint32 index ) const = 0;
		virtual ContextMenuBackend* GetItemMenu( Uint32 index ) const { return nullptr; }
		virtual Bool IsReadonlyItem( Uint32 index ) const { return false; }

		Bool AddItem( const String& name );
		Bool RenameItem( Uint32 index, const String& newName );

		virtual Bool CanRemoveItem( const Uint32 index ) const;
		Bool RemoveItem( Uint32 index );

		Bool IsItemSelected( Uint32 index ) const;
		void DeselectItem( Uint32 index );
		void SelectItem( Uint32 index );
		void ActivateItem( Int32 index );
		void DeactivateItem( Int32 index );
		void DeselectAllItems();
		void NotifyListChanged( Bool reselectCurrentItem = false );

		const InteropListSetupInfo&  GetSetupInfo() const { return m_setupInfo; }

		THandle< ISerializable > GetUserContext() const;
		void SetUserContext( THandle< ISerializable > userContext );

	protected:
		IEditActionHistory& GetActionHistory() { return *m_actionHistory; }

		virtual Uint32 GetSize() const = 0;
		virtual ::interop::ObjectPtr GetElement( Uint32 ind );

		virtual void OnSelection( Uint32 index ) {}
		virtual void OnDeselection( Uint32 index ) {}
		virtual void OnActivation( Int32 index );
		virtual void OnDeactivation( Int32 index );
		virtual void OnListChanged() {}

		IEditActionHistory& GetActionHistory() const { return *m_actionHistory; }

		void SetSelection( ISerializable* object, Bool readonly );
		void RemoveSelection( ISerializable* object );
		void RefreshSelectionView();

		RED_INLINE Int32 GetLastSelectedIndex() const { return m_lastSelectedIndex; }
		RED_INLINE const red::HashSet< Uint32 >& GetSelectedItemIndices() const { return m_selectedItemIndices; }

		/// Reset of the list setup info during list lifetime.
		/// Be careful with this, it requires manual call of InteropListCtrl::RefreshSetupInfo on the editor side.
		void ResetSetupInfo( const InteropListSetupInfo& newSetupInfo );

		virtual void OnItemPropertyChanged( const String& name ) {}
		virtual void OnNotifyPropertyChange( const SerializableID& id, const rtti::AccessPath& path );

		void Update();

		void SetObjects( const red::DynArray< THandle< ISerializable > >& objects, Bool readonly );

	private:
		void RegisterInterop();

		// if action is not supported, return null
		virtual red::UniquePtr< Action_AddListItem > GetAddItemAction( const String& name ) = 0;
		virtual red::UniquePtr< Action_RenameListItem > GetRenameItemAction( Uint32 index, const String& newName ) = 0;
		virtual red::UniquePtr< Action_RemoveListItem > GetRemoveItemAction( Uint32 index ) = 0;

		IEditActionHistory* m_actionHistory;
		interop::InteropNotifier m_listChanged;
		interop::InteropNotifier m_activationChanged;
		interop::InteropNotifier m_selectionChanged;
		ObjectCollection* m_selection;
		Int32 m_activeItemIndex;
		Int32 m_lastSelectedIndex;
		SerializableID m_lastSelectedID;
		red::HashSet< Uint32 > m_selectedItemIndices;

		THandle< ISerializable > m_userContext;

		InteropListSetupInfo m_setupInfo;
		SerializableValueNotifier m_serializableNotifier;

		friend class Action_AddListItem;
		friend class Action_RemoveListItem;
	};

	class BACKEND_ENGINE_API InteropListItem : public ::interop::utils::Object
	{
	public:
		explicit InteropListItem( Uint32 index, red::SharedPtr<InteropList> list, IEditActionHistory& actionHistory, const String& itemTypeName = String::EMPTY() );
	};

	class BACKEND_ENGINE_API Action_AddListItem : public IEditAction
	{
	public:
		Action_AddListItem( red::SharedPtr<InteropList> list )
			: m_list( list )
		{}

		virtual ~Action_AddListItem()
		{}

		virtual String GetDescription() const override { return "Add new list item"; }
		virtual Bool Do() override final;
		virtual Bool Undo() override final;
		virtual Bool AddItem() = 0;
		virtual Bool RemoveItem() = 0;
		virtual Uint32 GetItemIndex() const = 0;   // This is needed for proper selection tracking inside InteropList.

	protected:
		red::SharedPtr<InteropList> m_list;
	};

	class BACKEND_ENGINE_API Action_RemoveListItem : public IEditAction
	{
	public:
		Action_RemoveListItem( red::SharedPtr<InteropList> list, Uint32 index )
			: m_list( list )
			, m_index( index )
			, m_wasSelected( false )
		{}

		virtual ~Action_RemoveListItem()
		{}

		virtual String GetDescription() const override { return "Remove list item"; }
		virtual Bool Do() override final;
		virtual Bool Undo() override final;
		virtual Bool PerformRemoval() = 0;
		virtual Bool RevertRemoval() = 0;

		/// This is needed for proper selection/activation tracking inside InteropList.
		/// There is a default implementation that returns the index passed to the constructor of Action_RemoveListItem.
		/// Override this if you expect the order of elements on the list to change externally (which could invalidate the index from the constructor).
		virtual Uint32 GetItemIndex() const;

	protected:
		red::SharedPtr<InteropList> m_list;
		Uint32 m_index;
		Bool m_wasSelected;
	};

	class BACKEND_ENGINE_API Action_RenameListItem : public IEditAction
	{
	public:
		Action_RenameListItem( red::SharedPtr<InteropList> list )
			: m_list( list )
		{}

		virtual ~Action_RenameListItem()
		{}

		virtual String GetDescription() const override { return "Rename list item"; }
		virtual Bool Do() override final;
		virtual Bool Undo() override final;
		virtual Bool PerformRename() = 0;
		virtual Bool RevertRename() = 0;

	protected:
		red::SharedPtr<InteropList> m_list;
	};

}