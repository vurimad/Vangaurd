/**
* Copyright (c) 2016-2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "treeInterop.h"


namespace tools
{
	namespace
	{
		void ParseNodeIDs( const String& nodesString, tools::TTreeNodeIDs& nodeIDs )
		{
			red::DynArray< String > tokens;
			nodesString.GetTokens( ',', false, tokens );

			nodeIDs.Reserve( tokens.Size() );
			for ( const auto& token : tokens )
			{
				nodeIDs.PushBack( FromStringDirect< TreeNodeID >( token ) );
			}
		}

		void RecurseExpand( ITree* tree, TreeNodeID id )
		{
			if ( tree->HasChildren( id ) )
			{
				TTreeNodeIDs children{ red::PoolFrame() };
				tree->GetNodeChildren( id, children );
				tree->ExpandNodes( children );

				for ( TreeNodeID childId : children )
				{
					RecurseExpand( tree, childId );
				}
			}
		}

		void RecurseCollapse( ITree* tree, TreeNodeID id )
		{
			if ( tree->HasChildren( id ) )
			{
				TTreeNodeIDs children{ red::PoolFrame() };
				tree->GetNodeChildren( id, children );
				tree->CollapseNodes( children );

				for ( TreeNodeID childId : children )
				{
					RecurseCollapse( tree, childId );
				}
			}
		}
	}

	AutoFlushingStream::AutoFlushingStream( interop::InteropNotifier* notifier )
		: m_onTreeChanged( notifier )
	{
		m_message.Reserve( m_maxMsgSize );
	}

	Bool AutoFlushingStream::PushMessage( void* message, Uint32 length )
	{
		if ( length > m_maxMsgSize )
			return false;

		if ( GetMessageSize() + length > m_maxMsgSize )
			Flush();

		red::Memcpy( m_buffer.AsChar() + m_currBufferPos, message, length );
		m_currBufferPos += length;
		return true;
	}

	void AutoFlushingStream::Flush()
	{
		if ( m_currBufferPos > 0 )
		{
			PopMessage( m_message.AsChar() );
			m_onTreeChanged->Notify( m_message );
		}
	}

	//////////////////////////////////////////////////////////////////////////

	ITreeSelectionWrapper::ITreeSelectionWrapper() = default;

	ITreeSelectionWrapper::~ITreeSelectionWrapper() = default;

	void ITreeSelectionWrapper::SetupInterop( ITreeSelectionWrapper* ptr, ::interop::utils::Object& op )
	{
	}

	//////////////////////////////////////////////////////////////////////////

	void GetNodeDataStructs( const ITree* tree, const TTreeNodeIDs& collectedNodes, red::DynArray< THandle< ISerializable > >& outData )
	{
		for ( const TreeNodeID id : collectedNodes )
		{
			THandle< TreeNodeData > nodeData = tree->GetNodeDataStruct( id );

			if ( nodeData )
				outData.PushBack( nodeData );
		}
	}

	TreeContainerInterop::TreeContainerInterop( ITree* tree, ObjectCollection* sharedSelection )
		: InteropControlBase( "TreeContainerInterop", sharedSelection )
		, m_timer( "TreeRefreshTimer" )
		, m_tree( tree )
	{
		// register for outcoming notifications
		m_tree->AttachListener( this );

		Reload();

		// tree structure
		{
			RegisterValue<Uint64>( "rootId", [this]( interop::IContext& ctx, Uint64& outValue )
			{
				outValue = m_tree->GetRootID();
			} );

			RegisterMap( "nodes", [this]( interop::IContext& ctx, const String name )
			{
				TreeNodeID nodeId = FromStringDirect< TreeNodeID >( name );
				return interop::ObjectPtr( RED_NEW( TempshitNodeWrapper )( m_tree, nodeId ) );
			} );

			RegisterFunctor<Bool, String>( "hasChildren", [this]( interop::IContext& ctx, const String& nodeIdString )
			{
				TreeNodeID nodeId = FromStringDirect< TreeNodeID >( nodeIdString );
				return m_tree->HasChildren( nodeId );
			} );

			RegisterObject( "data", [this]( interop::IContext& ctx )
			{
				return m_tree->GetData();
			} );
		}

		// tree actions
		{
			RegisterFunctor<void, red::DynArray< TreeNodeID >>( "selectNodes", [this]( interop::IContext& ctx, const red::DynArray< TreeNodeID >& nodesIds )
			{
				m_tree->SetSelection( nodesIds );
			} );

			RegisterValueRO<Bool>( "hasSelection", [this]()
			{
				return m_tree->HasSelection();
			} );

			RegisterValueRO<TTreeNodeIDs>( "selection", [this]()
			{
				TTreeNodeIDs selection;
				m_tree->GetSelection().GetElements( selection );
				return selection;
			} );

			RegisterFunctor<void>( "selectAll", [this]( interop::IContext& ctx )
			{
				const tools::TreeNodeID lastSelectedNodeId = m_tree->GetLastSelectedNode();
				const tools::TreeNodeID parentNodeId = m_tree->GetNodeParentId( lastSelectedNodeId );

				tools::TTreeNodeIDs idsToSelect;
				if ( parentNodeId == tools::InvalidTreeID )
				{
					m_rootSnapshotNew->GetNodesIds( idsToSelect );
				}
				else
				{
					m_tree->GetNodeChildren( parentNodeId, idsToSelect );
				}

				m_tree->SetSelection( idsToSelect );
			} );

			RegisterFunctor<void, Uint64>( "selectSingle", [this]( interop::IContext& ctx, Uint64 nodeId )
			{
				SelectSingle( nodeId );
			} );

			RegisterFunctor<void, Uint64>( "selectAdditive", [this]( interop::IContext& ctx, Uint64 nodeId )
			{
				TTreeNodeIDs selection;
				m_tree->GetSelection().GetElements( selection );

				Int32 index = selection.GetIndex( nodeId );
				if ( index != red::INVALID_INDEX )
					selection.RemoveAt( index );
				else
					selection.PushBack( nodeId );

				m_tree->SetSelection( selection );
			} );

			RegisterFunctor<void, Uint64>( "selectRange", [ this ]( interop::IContext& ctx, Uint64 toId )
			{
				red::DynArray< Uint64 > resultSelection;

				TreeNodeID fromId = m_tree->GetLastSelectedNode();
				if ( fromId == InvalidTreeID )
					return;

				Uint32 fromPosition = m_rootSnapshotNew->GetNodePosition( fromId );
				Uint32 toPosition = m_rootSnapshotNew->GetNodePosition( toId );

				if ( fromPosition > toPosition )
				{
					m_collector->CollectNodesAbove( fromId, fromPosition - toPosition, resultSelection );
				}
				else
				{
					m_collector->CollectNodesBelow( fromId, toPosition - fromPosition, resultSelection );
				}

				resultSelection.InsertAt( 0, fromId );

				m_tree->SetSelection( resultSelection );
			} );

			RegisterFunctor<void>( "moveSelectionUp", [this]( interop::IContext& ctx )
			{
				//TODO: duplicated in DataTree
				red::DynArray< tools::TreeNodeID > nodeToSelect;
				if ( m_tree->HasSelection() )
				{
					tools::TreeNodeID lastSelectedId = m_tree->GetLastSelectedNode();
					if ( !m_rootSnapshotNew->GetSnapshot( lastSelectedId ) )
						lastSelectedId = m_rootSnapshotNew->GetNodeIdAtPosition( 0 );

					m_collector->CollectNodesAbove( lastSelectedId, 1, nodeToSelect );

					if ( nodeToSelect.Empty() )
						nodeToSelect.PushBack( m_tree->GetLastSelectedNode() );
				}
				else
				{
					tools::TreeNodeID firstNodeId = m_rootSnapshotNew->GetNodeIdAtPosition( 0 );
					nodeToSelect.PushBack( firstNodeId );
				}

				m_tree->SetSelection( nodeToSelect );
				m_tree->FocusOnNode( nodeToSelect.Front() );
			} );

			RegisterFunctor<void>( "moveSelectionDown", [this]( interop::IContext& ctx )
			{
				//TODO: duplicated in DataTree
				red::DynArray< tools::TreeNodeID > nodeToSelect;
				if ( m_tree->HasSelection() )
				{
					tools::TreeNodeID lastSelectedId = m_tree->GetLastSelectedNode();
					if ( !m_rootSnapshotNew->GetSnapshot( lastSelectedId ) )
						lastSelectedId = m_rootSnapshotNew->GetNodeIdAtPosition( 0 );

					m_collector->CollectNodesBelow( lastSelectedId, 1, nodeToSelect );

					if ( nodeToSelect.Empty() )
						nodeToSelect.PushBack( lastSelectedId );
				}
				else
				{
					tools::TreeNodeID firstNodeId = m_rootSnapshotNew->GetNodeIdAtPosition( 0 );
					nodeToSelect.PushBack( firstNodeId );
				}

				m_tree->SetSelection( nodeToSelect );
				m_tree->FocusOnNode( nodeToSelect.Front() );
			} );

			RegisterFunctor<void>( "expandOrDescend", [this]( interop::IContext& ctx )
			{
				const red::HashSet< tools::TreeNodeID >& selection = m_tree->GetSelection();
				if ( selection.Empty() )
					return;

				const tools::TreeNodeID nodeId = *selection.Begin();

				if ( m_tree->IsNodeExpanded( nodeId ) && m_tree->HasChildren( nodeId ) )
				{
					const tools::TreeNodeID childId = m_tree->GetNodeChildId( nodeId, 0 );
					SelectSingle( childId );
				}
				else
				{
					m_tree->ExpandNodes( tools::TTreeNodeIDs{ nodeId } );
				}
			} );

			RegisterFunctor<void>( "collapseOrAscend", [this]( interop::IContext& ctx )
			{
				const red::HashSet< tools::TreeNodeID >& selection = m_tree->GetSelection();
				if ( selection.Empty() )
					return;

				const tools::TreeNodeID nodeId = *selection.Begin();

				if ( m_tree->IsNodeExpanded( nodeId ) )
				{
					m_tree->CollapseNodes( tools::TTreeNodeIDs{ nodeId } );
				}
				else
				{
					const tools::TreeNodeID parentId = m_tree->GetNodeParentId( nodeId );
					if ( parentId != tools::InvalidTreeID )
					{
						SelectSingle( parentId );
					}
				}
			} );

			RegisterFunctor<void>( "collapseAllNodes", [this]( interop::IContext& ctx )
			{
				m_tree->CollapseAllNodes();
			} );

			RegisterFunctor<void>( "activateSelection", [this]( interop::IContext& ctx )
			{
				if ( !m_tree->HasSelection() )
					return;

				m_tree->ActivateNode( m_tree->GetLastSelectedNode() );
			} );

			RegisterFunctor<void, Uint64>( "activateNode", [this]( interop::IContext& ctx, const Uint64 nodeId )
			{
				m_tree->ActivateNode( nodeId );
			} );

			RegisterFunctor<void, String>( "filter", [this]( interop::IContext& ctx, const String& filterString )
			{
				m_tree->FilterNodes( filterString );
				
				Refresh();

				tools::TTreeNodeIDs ids;
				m_collector->CollectNodesBelow( m_tree->GetRootID(), 1, ids );

				if ( !ids.Empty() )
				{
					SelectSingle( ids.Front() );
				}

				m_onSearchFinished.Notify();
			} );

			RegisterFunctor<void, red::DynArray< String >, red::DynArray< String >>( "filterMap", [ this ]( interop::IContext& ctx, const red::DynArray< String >& keys, const red::DynArray< String >& values )
			{
				red::HashMap< String, String > filterMap;
				filterMap.Reserve( keys.Size() );

				for ( Uint32 index : keys.Indices() )
				{
					filterMap.Insert( keys[ index ], index < values.Size() ? values[ index ] : String() );
				}

				m_tree->FilterNodes( filterMap );

				m_onSearchFinished.Notify();
			} );

			RegisterFunctor<void, String>( "filter_DEPRECATED", [this]( interop::IContext& ctx, const String& filterString )
			{
				utils::NameTreeTokenFilter filter( filterString );
				m_tree->FilterNodes_DEPRECATED( filter );

				String message = String::Printf( "[ea%d]", m_tree->GetRootID() );
				m_onNodesChanged_DEPRECATED.Notify( message );
			} );

			RegisterFunctor<void, String, String>( "dragAndDrop", [this] ( interop::IContext& ctx, const String& sourceNodeString, const String& targetNodeString )
			{
				TreeNodeID sourceNodeId = FromStringDirect< tools::TreeNodeID >( sourceNodeString );
				TreeNodeID targetNodeId = FromStringDirect< tools::TreeNodeID >( targetNodeString );
				if ( !m_tree->DragAndDropNode_DEPRECATED( sourceNodeId, targetNodeId ) )
				{
					ctx.ThrowException( "Drag & drop failed" );
				}
			} );

			RegisterFunctor< String, Uint64 >( "createDragData", [this]( interop::IContext& ctx, const Uint64 nodeId )
			{
				rtti::ValuePtr data = m_tree->CreateDragData( nodeId );
				if ( data )
					return data->ToString();

				return String::EMPTY();
			} );

			RegisterFunctor< Bool, TreeDragData, String >( "canDropData", [this]( interop::IContext& ctx, const TreeDragData& data, const String& dataStr )
			{
				TreeDragData deserializedData( data );
				deserializedData.m_data = rtti::ValueHolder::Parse( dataStr.AsChar() );

				return m_tree->CanDropData( deserializedData );
			} );

			RegisterFunctor< void, TreeDragData, String >( "dropData", [this]( interop::IContext& ctx, const TreeDragData& data, const String& dataStr )
			{
				TreeDragData deserializedData( data );
				deserializedData.m_data = rtti::ValueHolder::Parse( dataStr.AsChar() );

				if ( !m_tree->DropData( deserializedData ) )
				{
					ctx.ThrowException( "Drag & drop failed" );
				}
			} );

			// Request amount of nodes for front end when moving up ( towards root node ) in a tree
			RegisterFunctor<red::DynArray< THandle< ISerializable > >, Uint32, Uint32 >( "mainLineAbove", [ this ]( const interop::IContext& ctx, const Uint32 position, const Uint32 amount )
			{
				red::DynArray< THandle< ISerializable > > returnVector;

				if ( amount == 0 )
					return returnVector;

				TreeNodeID id = m_rootSnapshotNew->GetNodeIdAtPosition( position );
				red::DynArray< TreeNodeID > collectedNodes;

				m_collector->CollectNodesAbove( id, amount, collectedNodes );

				GetNodeDataStructs( m_tree, collectedNodes, returnVector );

				return returnVector;
			} );

			// Request amount of nodes for front end when moving down ( away from root node ) in a tree
			RegisterFunctor<red::DynArray< THandle< ISerializable > >, Uint32, Uint32 >( "mainLineBelow", [ this ]( const interop::IContext& ctx, const Uint32 position, const Uint32 amount )
			{
				red::DynArray< THandle< ISerializable > > returnVector;

				if ( amount == 0 )
					return returnVector;

				TreeNodeID id = m_rootSnapshotNew->GetNodeIdAtPosition( position );
				red::DynArray< TreeNodeID > collectedNodes;

				m_collector->CollectNodesBelow( id, amount, collectedNodes );

				GetNodeDataStructs( m_tree, collectedNodes, returnVector );

				return returnVector;
			} );

			// Request amount of child nodes for given node id
			RegisterFunctor<red::DynArray< THandle< ISerializable > >, Uint64, Uint32 >( "getChildren", [ this ]( const interop::IContext& ctx, const Uint64 id, const Uint32 amount )
			{
				red::DynArray< TreeNodeID > collectedNodes;
				m_collector->CollectChildren( id, amount, collectedNodes );

				red::DynArray< THandle< ISerializable > > returnVector;
				GetNodeDataStructs( m_tree, collectedNodes, returnVector );

				return returnVector;
			} );

			RegisterFunctor<red::DynArray< THandle< ISerializable > >, Uint32, Uint32>( "getBufferAtPosition", [ this ]( const interop::IContext& ctx, const Int32 position, const Uint32 amount )
			{
				red::DynArray< THandle< ISerializable > > returnVector;

				if ( amount == 0 )
					return returnVector;

				TreeNodeID id = m_rootSnapshotNew->GetNodeIdAtPosition( position );

				red::DynArray< TreeNodeID > collectedNodes;
				collectedNodes.PushBack( id );
				m_collector->CollectNodesBelow( id, amount, collectedNodes );

				GetNodeDataStructs( m_tree, collectedNodes, returnVector );

				return returnVector;
			} );

			RegisterFunctor<red::DynArray< THandle< ISerializable > >, TTreeNodeIDs>( "getNodesData", [this]( const interop::IContext& ctx, const TTreeNodeIDs& nodesIds )
			{
				red::DynArray< THandle< ISerializable > > result;
				GetNodeDataStructs( m_tree, nodesIds, result );

				return result;
			} );

			RegisterFunctor<void, THandle< tools::TreeNodeData >>( "setNodeData", [this]( const interop::IContext& ctx, const THandle< tools::TreeNodeData >& data )
			{
				m_tree->SetNodeDataStruct( data->m_id, data );
			} );

			RegisterFunctor<TTreeNodeIDs, TreeNodeID>( "getNodesChildrenIds", [this] ( const interop::IContext& ctx, const TreeNodeID& parentId )
			{
				TTreeNodeIDs result;
				m_tree->GetNodeChildren( parentId, result );
				return result;
			} );

			RegisterFunctor< THandle< ISerializable > >( "getContextMenuData", [this]( const interop::IContext& ctx )
			{
				THandle< tools::ContextMenuData > contextMenuData = m_tree->CreateContextMenuData();
				m_tree->FillContextMenuData( contextMenuData );
				return contextMenuData;
			} );

			RegisterFunctor<Uint32, Uint64>( "getNodePosition", [ this ]( const interop::IContext& ctx, const Uint64 id )
			{
				return m_rootSnapshotNew->GetNodePosition( id );
			} );

			RegisterFunctor<void, Uint64, Bool>( "expand", [ this ]( const interop::IContext& ctx, const Uint64 nodeId, const Bool expand )
			{
				if ( expand )
				{
					m_tree->ExpandNodes( tools::TTreeNodeIDs{ nodeId } );
				}
				else
				{
					m_tree->CollapseNodes( tools::TTreeNodeIDs{ nodeId } );
				}
			} );

			RegisterFunctor<void, Uint64, Bool>( "expandAll", [ this ]( const interop::IContext& ctx, const Uint64 id, Bool expand )
			{
				if ( expand )
				{
					m_tree->ExpandNodes( TTreeNodeIDs{ id } );
					RecurseExpand( m_tree, id );
				}
				else
				{
					m_tree->CollapseNodes( tools::TTreeNodeIDs{ id } );
					RecurseCollapse( m_tree, id );
				}
			} );

			RegisterFunctor<void, Uint64>( "scopeToNode", [this] ( const interop::IContext& ctx, const Uint64 id )
			{
				Reload( id );
				m_tree->ExpandNodes( tools::TTreeNodeIDs{ id } );
			} );

			RegisterFunctor<void>( "scopeToRoot", [this] ( const interop::IContext& ctx )
			{
				const TreeNodeID rootId = m_tree->GetRootID();

				Reload( rootId );
				m_tree->ExpandNodes( tools::TTreeNodeIDs{ rootId } );
			} );

			RegisterValueRO<Uint32>( "size", [ this ]()
			{
				return m_rootSnapshotNew->GetSize();
			} );

			RegisterValueRO< red::DynArray< THandle< tools::TreeNodeData > > >( "selectedNodes",
				[ this ]( interop::IContext& ctx, red::DynArray< THandle< tools::TreeNodeData > >& outValues )
				{
					const red::HashSet< TreeNodeID >& selection = m_tree->GetSelection();
					for ( const TreeNodeID id : selection )
					{
						outValues.PushBack( m_tree->GetNodeDataStruct( id ) );
					}
				},
				&m_onSelectionChanged
			);

			RegisterObject( "selectionWrapper", [this]( interop::IContext& ctx )
			{
				return m_selectionWrapper->CreateInterop();
			} );
		}

		// tree updating
		{
			RegisterNotifier( "_onChanged_DEPRECATED", m_onNodesChanged_DEPRECATED );
			RegisterNotifier( "_onChanged", m_onNodesChanged );
			RegisterNotifier( "_onSelectionChanged", m_onSelectionChanged );
			RegisterNotifier( "_onSizeChanged", m_onSizeChanged );
			RegisterNotifier( "_onActivePathChanged", m_onActivePathChanged );
			RegisterNotifier( "_onNodeDoubleClicked", m_onNodeDoubleClicked );
			RegisterNotifier( "_onSearchFinished", m_onSearchFinished );

			// safety measure, it helps to make sure that notifications are being sent only after the notificator has been registered on the other side
			RegisterFunctor<void, Bool>( "_notifierRegistered", [this]( interop::IContext& ctx, Bool isOldTree )
			{
				m_onSizeChanged.Notify( m_rootSnapshotNew->GetSize() );

				// TEMP solution
				m_timer.ExecuteEvery( 0.01f, [this, isOldTree]()
				{
					if ( isOldTree )
					{
						Refresh_DEPRECATED();
					}
					else
					{
						Refresh();
					}
				} );

				m_stream = red::CreateUniquePtr< AutoFlushingStream >( &m_onNodesChanged_DEPRECATED );
			} );
		}
	}

	TreeContainerInterop::~TreeContainerInterop()
	{
		//m_tree->DetachListener( this );
	}

	void TreeContainerInterop::Reload()
	{
		Reload( m_tree->GetRootID() );
	}

	void TreeContainerInterop::Reload( const TreeNodeID& snapshotRootId )
	{
		m_rootSnapshot = red::CreateUniquePtr< utils::NodeSnapshot >( snapshotRootId );
		m_rootSnapshotNew = red::CreateUniquePtr< utils::RootNodeSnapshot >( snapshotRootId );

		m_updater = red::CreateUniquePtr< utils::MessagingTreeUpdater >( m_tree, m_rootSnapshot.Get() );
		m_updaterNew = red::CreateUniquePtr< utils::SimpleTreeUpdater >( m_tree, m_rootSnapshotNew.Get() );

		m_collector = red::CreateUniquePtr< utils::SimpleTreeCollector >( m_rootSnapshotNew.Get() );
	}

	void TreeContainerInterop::Refresh_DEPRECATED()
	{
		m_updater->UpdateTree();
		IMessageStream* messageStream = m_updater->GetUpdateMessages();
		if ( messageStream )
		{
			Uint32 size = messageStream->GetMessageSize();
			if ( size > 0 )
			{
				String message;
				message.Resize( size );
				messageStream->PopMessage( message.AsChar() );
				m_onNodesChanged_DEPRECATED.Notify( message );
			}
		}
	}

	void TreeContainerInterop::Refresh()
	{
		if ( m_updaterNew->UpdateTree() )
		{
			m_onSizeChanged.Notify( m_rootSnapshotNew->GetSize() );
		}

		if ( !m_changesQueue.Empty() )
		{
			m_onNodesChanged.Notify( m_changesQueue );

			m_idToIndex.Clear();
			m_changesQueue.Clear();
		}
	}

	void TreeContainerInterop::SelectSingle( const TreeNodeID nodeId )
	{
		m_tree->SetSelection( TTreeNodeIDs{ nodeId } );
	}

	String TreeContainerInterop::BuildMessage( const TTreeNodeIDs& nodesIds, const AnsiChar actionType )
	{
		red::DynArray< String > stringIDs;
		stringIDs.Reserve( nodesIds.Size() );

		for ( auto id : nodesIds )
			stringIDs.PushBack( ToStringDirect( id ) );

		return String::Printf( "[%c%s]", actionType, String::Join( stringIDs, "," ).AsChar() );
	}

	void TreeContainerInterop::OnInvalidateNodes_DEPRECATED( TreeNodeID parentId, const TTreeNodeIDs& invalidChildrenIDs )
	{
		Refresh_DEPRECATED();

		red::DynArray< String > stringIDs;
		stringIDs.Reserve( invalidChildrenIDs.Size() + 1 );

		stringIDs.PushBack( ToStringDirect( parentId ) );

		for ( auto id : invalidChildrenIDs )
			stringIDs.PushBack( ToStringDirect( id ) );

		m_onNodesChanged_DEPRECATED.Notify( String::Printf( "[%s]", String::Join( stringIDs, ",i" ).AsChar() ) );
	}

	void TreeContainerInterop::OnExpandNodes( const TTreeNodeIDs& orderedNodeIds )
	{
		m_onNodesChanged_DEPRECATED.Notify( BuildMessage( orderedNodeIds, 'e' ) );
	}

	void TreeContainerInterop::OnSelectNodes( const TTreeNodeIDs& selected, const TTreeNodeIDs& deselected )
	{
		// Backward compatibility
		{
			TTreeNodeIDs currentSelection;
			m_tree->GetSelection().GetElements( currentSelection );

			m_onNodesChanged_DEPRECATED.Notify( BuildMessage( currentSelection, 's' ) );
		}

		interop::SelectionChangeInfo selectionChangeInfo;
		selectionChangeInfo.m_selected = selected;
		selectionChangeInfo.m_deselected = deselected;
		m_onSelectionChanged.Notify( selectionChangeInfo );

		if ( m_selectionWrapper )
			m_selectionWrapper->NotifyOnChanged();
	}

	void TreeContainerInterop::OnFocusNode( const TreeNodeID nodeId )
	{
		TTreeNodeIDs nodesToFocus{ nodeId };
		m_onNodesChanged_DEPRECATED.Notify( BuildMessage( nodesToFocus, 'f' ) );

		QueueChange( nodeId, TreeNodeChangeType::Focus );
	}

	void TreeContainerInterop::OnHideNodes_DEPRECATED( TreeNodeID parentId, const TTreeNodeIDs& childrenToHideIDs )
	{
		red::DynArray< String > stringIDs;
		stringIDs.Reserve( childrenToHideIDs.Size() + 1 );

		stringIDs.PushBack( ToStringDirect( parentId ) );

		for ( auto id : childrenToHideIDs )
			stringIDs.PushBack( ToStringDirect( id ) );

		m_onNodesChanged_DEPRECATED.Notify( String::Printf( "[%s]", String::Join( stringIDs, ",h" ).AsChar() ) );
	}

	void TreeContainerInterop::OnActivateNode( const TTreeNodeIDs& activated, const TTreeNodeIDs& deactivated )
	{
		interop::SelectionChangeInfo activePathChangeInfo;
		activePathChangeInfo.m_selected = activated;
		activePathChangeInfo.m_deselected = deactivated;
		m_onActivePathChanged.Notify( activePathChangeInfo );
	}

	void TreeContainerInterop::OnInvalidateNode( const TreeNodeID nodeId )
	{
		QueueChange( nodeId, TreeNodeChangeType::Invalidate );
	}

	void TreeContainerInterop::OnInvalidateNodes( const TTreeNodeIDs& nodeIds )
	{
		for ( const TreeNodeID id : nodeIds )
		{
			QueueChange( id, TreeNodeChangeType::Invalidate );
		}
	}

	void TreeContainerInterop::QueueChange( const TreeNodeID nodeId, const TreeNodeChangeType changeType )
	{
		red::ScopedLock< red::LightMutex > lock( m_lock );

		Uint32* index = m_idToIndex.FindPtr( nodeId );
		if ( index )
		{
			tools::TreeNodeChange& change = m_changesQueue[ *index ];
			change.m_changeType |= changeType;
		}
		else
		{
			m_changesQueue.PushBack( tools::TreeNodeChange() );
			m_idToIndex.Insert( nodeId, m_changesQueue.Size() - 1 );

			tools::TreeNodeChange& change = m_changesQueue.Back();
			change.m_id = nodeId;
			change.m_changeType = changeType;
		}
	}

	TreeContainerInterop::TempshitNodeWrapper::TempshitNodeWrapper( ITree* tree, TreeNodeID nodeId )
		: interop::utils::Object( "TempshitNodeWrapper" )
		, m_tree( tree )
		, m_nodeId( nodeId )
	{
		RegisterObject( "data", [this](const interop::IContext& ctx) { return m_tree->GetNodeData( m_nodeId ); } );
		RegisterObject( "children", [this](const interop::IContext& ctx)
		{
			const Uint32 numNodes = m_tree->GetNodeChildrenCount( m_nodeId );
			return interop::ObjectPtr( RED_NEW( interop::utils::Array )( "Uint64", numNodes, [this]( interop::IContext& ctx, Uint32 element ) -> interop::ObjectPtr
			{
				const auto id = m_tree->GetNodeChildId( m_nodeId, element );
				if ( id != 0 )
					return interop::ObjectPtr( RED_NEW( interop::utils::ConstValueObject<Uint64> )( id ) );

				ctx.ThrowException( "Accessing invalid node" );
				return interop::ObjectPtr();
			} ) );
		} );
		RegisterValue<Uint32>( "childrenCount", [ this ]( const interop::IContext& ctx, Uint32& outvalue ) { outvalue = m_tree->GetNodeChildrenCount( m_nodeId ); } );
	}
}