/**
* Copyright (c) 2013-2018 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "objectFwd.h"
#include "selectionFwd.h"
#include "undoFwd.h"
#include "editableCollection.h"

#include "../../../common/redReflection/include/editableView.h"

namespace tools
{
	class SelectionManager;

	/// Collection of object that provide common edition interface and has UNDO/REDO connected
	/// This is the recommended way of editing objects from resource editors as it provides most of the needed mechanics for proper editor
	/// NOTE: the collection can handle only one type of objects (from one object resolver)
	class BACKEND_ENGINE_API ObjectCollection
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		ObjectCollection( IEditActionHistory& actionHistory );
		virtual ~ObjectCollection();

		/// Reset content (remove all objects)
		virtual void Reset();

		/// Create the set of editable objects from a selection set
		void SetObjectsFromSelection( const red::SharedPtr< SelectionState >& selection );

		/// Set new set of editable objects directly
		void SetObjects( const red::DynArray< red::SharedPtr< IEditableProxy > >& proxies );

		/// Set new set of editable objects directly
		void SetObjects( const red::DynArray< THandle< ISerializable > >& objects, THandle< ISerializable > userContext = nullptr, Bool readonly = false );

		/// Set a single object, still with undo/redo for as long as the object exists
		void SetObject( ISerializable* object, Bool readonly = false, THandle< ISerializable > userContext = nullptr );

		/// get interop interface (it's cached - we can do it here)
		interop::ObjectPtr GetRootInterop();

		/// returns true if object with given id is currently in the set of editable objects
		virtual Bool HasObject( const SerializableID& id ) const;

		/// refresh object's properties
		virtual void Refresh( Bool fullRefresh = true );

		virtual void UpdateGlobalSelection();

		virtual void SetViews( const red::DynArray< red::SharedPtr< IEditableView > >& views );

		const red::DynArray< red::SharedPtr< IEditableView > >& GetViews() const;

		// Don't mark/mark resource as modified when property value is changed
		void SetModifyResource( Bool modifyResource );

	private:
		IEditActionHistory& m_actionHistory;							/// Undo/Redo interface
		EditableCollection m_collection;								/// Editable collection (property grid backend)
		red::DynArray< res::ResourcePath > m_objectsParentResources;	/// Resource paths of the edited objects
		red::DynArray< red::SharedPtr< IEditableView > > m_views;

		Bool m_modifiesResource;
	};

	/// SharedObjectCollection is a collection which can handle multiple collections at a time.
	/// Controls in backend (for example "Actors" list) requests it's collections for handling selection state by using IResourceEditor::GetSharedSelection (for example ( GetSharedSelection( RED_NAME_CONSTEXPR( "Actors" ) ).
	/// In frontend we can have one generic selection property page but also user can show separate property pages for each of the controls that requested collections by GetSharedSelection.
	/// If user makes changes to selection in one control (for example in actors list), he will update view in this control's collection but also in global - master collection.
	class SharedObjectCollection : public ObjectCollection
	{
	public:
		SharedObjectCollection( SelectionManager* selectionManager, IEditActionHistory& actionHistory, ObjectCollection* sharedObjectCollection );
		virtual ~SharedObjectCollection();

		virtual void Reset() override;

		virtual Bool HasObject( const SerializableID& id ) const override;

		virtual void Refresh( Bool fullRefresh ) override;

		virtual void UpdateGlobalSelection() override;

	protected:
		virtual void SetViews( const red::DynArray< red::SharedPtr< IEditableView > >& views ) override;

	private:
		SelectionManager* m_selectionManager;
		ObjectCollection* m_masterCollection;
	};

} // tools