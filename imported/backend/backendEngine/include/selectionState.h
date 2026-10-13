/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "editableProxy.h"
#include "selectionFwd.h"

namespace tools
{
	class SelectionIterator;

	/// (cached) list of selected object
	class BACKEND_ENGINE_API SelectionState : public red::NonCopyable
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		explicit SelectionState();

		static SelectionStatePtr EmptySelection();

		// create copy of the selection state
		SelectionStatePtr Clone() const;

		// get selection iterator, passing empty name returns the whole selection
		// you can narrow the iteration by specifying the proxy type you want to iterate over
		SelectionIterator Iterate( const CName selectionFilter = CName::NONE() ) const;

		// is this an empty selection state ?
		Bool IsEmpty() const { return m_proxyList.Empty(); }

		// Returns false if selected object cannot be edited
		// NOTE: this can depend on current edit mode (e.g. active object resolver)
		Bool IsReadOnly() const;

		// restore selection state - make sure all proxies are connected to the valid object
		// returns false if at least one proxy failed to restore (this would indicate errors in handling the object deletion/creation undo/redo)
		Bool Restore();

		// clear current selection (all objects are deselected)
		void Clear();

		// add proxy to selection, adding object twice does nothing
		void Add( EditableProxyPtr ptr );

		// remove proxy from selection by ID
		void Remove( const EditorObjectIDPath& path );

		void RemoveAll( const CName type );

		// is object in the selection list ?
		bool Contains( const EditorObjectIDPath& path ) const;

		// how many items are selected.
		const Uint32 Size() const;

		// list all the selected objects' paths
		void ListObjects( red::DynArray< EditorObjectIDPath >& paths ) const;
		void ListObjects( red::Set< EditorObjectIDPath >& paths ) const;

		// check if the selection has common root
		// e.g. selection contains object from a single prefab
		Bool HasCommonParent() const;
		EditorObjectIDPath GetCommonParent() const;

		// compare two selection states
		Bool operator == ( const SelectionState& other ) const;
		Bool operator != ( const SelectionState& other ) const;

	private:
		// proxies for selected objects
		typedef red::DynArray< EditableProxyPtr > TProxyList;
		TProxyList m_proxyList;

		// map of selected proxies
		typedef red::HashMap< EditorObjectIDPath, EditableProxyPtr > TProxyMap;
		TProxyMap m_proxyMap;

		friend class SelectionIterator;
	};


} // tools