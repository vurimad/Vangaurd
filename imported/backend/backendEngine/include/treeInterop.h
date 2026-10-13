/**
* Copyright (c) 2016-2018 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../../common/generalEditorBackend/include/tree.h"
#include "../../../common/generalEditorBackend/include/treeUtils.h"

#include "../../../common/interop/include/interopUtilsManual.h"
#include "../../../common/interop/include/interopNotifier.h"

#include "backendTimer.h"
#include "interopControlBase.h"

namespace tools
{
	class ITree;
	class TreeUpdater;
	
	class BACKEND_ENGINE_API AutoFlushingStream : public utils::StringMessageStream
	{
	public:
		AutoFlushingStream( interop::InteropNotifier* notifier );

		virtual Bool PushMessage( void* message, Uint32 length ) override;

		void Flush();

	private:
		String					m_message;
		interop::InteropNotifier*	m_onTreeChanged;
	};

	// basic interop handlers

	class BACKEND_ENGINE_API ITreeSelectionWrapper
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
		DECLARE_INTEROP( ITreeSelectionWrapper )

	public:
		ITreeSelectionWrapper();
		virtual ~ITreeSelectionWrapper();

		virtual void NotifyOnChanged() = 0;
	};

	class BACKEND_ENGINE_API TreeContainerInterop : public InteropControlBase, public ITreeListener
	{
	public:
		TreeContainerInterop( ITree* tree, ObjectCollection* sharedSelection );
		~TreeContainerInterop();

		class TempshitNodeWrapper : public interop::utils::Object
		{
		public:
			TempshitNodeWrapper( ITree* tree, TreeNodeID nodeId );

		private:
			ITree*		m_tree;
			TreeNodeID	m_nodeId;
		};

		// ITreeListener interface
		virtual void OnSelectNodes( const TTreeNodeIDs& selected, const TTreeNodeIDs& deselected ) override;
		virtual void OnFocusNode( const TreeNodeID nodeId ) override;
		virtual void OnActivateNode( const TTreeNodeIDs& activated, const TTreeNodeIDs& deactivated ) override;
		virtual void OnInvalidateNode( const TreeNodeID nodeId ) override;
		virtual void OnInvalidateNodes( const TTreeNodeIDs& nodeIds ) override;

		// deprecated methods
		virtual void OnInvalidateNodes_DEPRECATED( TreeNodeID parentId, const TTreeNodeIDs& invalidChildrenIDs ) override;
		virtual void OnHideNodes_DEPRECATED( TreeNodeID parentId, const TTreeNodeIDs& childrenToHideIDs ) override;
		virtual void OnExpandNodes( const TTreeNodeIDs& orderedNodeIds ) override;

		void SetSelectionWrapper( const red::SharedPtr< ITreeSelectionWrapper >& selectionWrapper ) { m_selectionWrapper = selectionWrapper; }

		void Reload();

	private:
		// valid messages :
		// add nodes : [<parentId>,a<id>...] --> valid: [12,a12,a34,a41] // TODO: change types are not mixed anymore, redundant letters may be removed
		// remove nodes : [<parentId>,r<id>...]
		// invalidate nodes : [<parentId>,i<id>...]
		// hide nodes : [<parentId>,h<id>...]
		// select nodes : [s<id>...] --> valid: [s1,2,3,4,5]
		// expand nodes : [e<id>...]
		// expand all : [ea<id>...]

		void SelectSingle( const TreeNodeID nodeId );

		String BuildMessage( const TTreeNodeIDs& nodesIds, const AnsiChar actionType );

		void Refresh();
		void Refresh_DEPRECATED();

		void Reload( const TreeNodeID& snapshotRootId );
		
		void QueueChange( const TreeNodeID nodeId, const TreeNodeChangeType changeType );

		interop::InteropNotifier m_onNodesChanged_DEPRECATED;
		interop::InteropNotifier m_onNodesChanged;
		interop::InteropNotifier m_onSizeChanged;
		interop::InteropNotifier m_onSelectionChanged;
		interop::InteropNotifier m_onActivePathChanged;
		interop::InteropNotifier m_onNodeDoubleClicked;
		interop::InteropNotifier m_onSearchFinished;

		ITree* m_tree;

		// DEPRECATED
		red::UniquePtr< utils::MessagingTreeUpdater > m_updater;
		red::UniquePtr< ITreeNodeSnapshot > m_rootSnapshot;
		red::UniquePtr< AutoFlushingStream > m_stream;
		// New interface
		red::UniquePtr< ITreeUpdater > m_updaterNew;
		red::UniquePtr< utils::RootNodeSnapshot > m_rootSnapshotNew;
		red::UniquePtr< ITreeCollector > m_collector;
		red::SharedPtr< ITreeSelectionWrapper > m_selectionWrapper;

		Timer m_timer;

		red::DynArray< tools::TreeNodeChange > m_changesQueue;
		red::HashMap< TreeNodeID, Uint32 > m_idToIndex;

		red::LightMutex m_lock;
	};
}