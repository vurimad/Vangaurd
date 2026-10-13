/*
* Copyright (c) 2016-2018 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../../common/redSystem/include/types.h"
#include "../../../common/redReflection/include/types.h"
#include "../../../common/interop/include/interopInterchange.h"
#include "editorGraphTypes.h"
#include "../../../common/generalEditorBackend/include/graphData.h"

namespace tools
{
	class GraphObjectTree;
	class GraphImportTree;
	class IGraphValidatorOutput;
	class IGraphObjectConstructor;
	class IGraphImporter;
	class ObjectHandler;
	class IEditAction;

	enum MoveStatus
	{
		Success,
		Fail,
		Continue,
	};

	/// Capture Graph state changes
	class IEditorGraphListener : red::NonCopyable
	{
	public:
		virtual ~IEditorGraphListener() { }

		// Action handlers
		virtual void OnCreateNode( GraphSubGraphId subgraphId, GraphNodeId nodeId ) { }
		virtual void OnRecreateNode( GraphSubGraphId subgraphId, GraphNodeId nodeId ) { }
		virtual void OnRemoveNode( GraphSubGraphId subgraphId, GraphNodeId nodeId ) { }
		virtual void OnInvalidateNodeDescription( GraphSubGraphId subgraphId, GraphNodeId nodeId ) { }
		virtual void OnInvalidateNodeDescriptionProperty( GraphSubGraphId subgraphId, GraphNodeId nodeId, const red::String& propertyName ) { }
		virtual void OnInvalidateNodeData( GraphSubGraphId subgraphId, GraphNodeId nodeId ) { }
		virtual void OnInvalidateNodeComponent( GraphSubGraphId subgraphId, GraphNodeId nodeId, GraphComponentId index ) { }

		virtual void OnNodeSocketsChanged( GraphSubGraphId subgraphId, GraphNodeId nodeId ) { }
		virtual void OnInvalidateSocketDescription( GraphSubGraphId subgraphId, GraphNodeId nodeId, GraphSocketId socketId ) { }
		virtual void OnInvalidateSocketData( GraphSubGraphId subgraphId, GraphNodeId nodeId, GraphSocketId socketId ) { }
		virtual void OnInvalidateSocketComponent( GraphSubGraphId subgraphId, GraphNodeId nodeId, GraphSocketId socketId, GraphComponentId componentId ) { }

		virtual void OnCreateConnection( GraphSubGraphId subgraphId, GraphConnectionId connectionId ) { }
		virtual void OnRemoveConnection( GraphSubGraphId subgraphId, GraphConnectionId connectionId ) { }
		virtual void OnEnableConnection( GraphSubGraphId subgraphId, GraphConnectionId connectionId ) { }
		virtual void OnDisableConnection( GraphSubGraphId subgraphId, GraphConnectionId connectionId ) { }

		virtual void OnInvalidateConnectionDescription( GraphSubGraphId subgraphId, GraphConnectionId connectionId ) { }
		virtual void OnInvalidateConnectionDescriptionProperty( GraphSubGraphId subgraphId, GraphConnectionId connectionId, const String& propertyName ) { }
		virtual void OnInvalidateConnectionData( GraphSubGraphId subgraphId, GraphConnectionId connectionId ) { }
		virtual void OnInvalidateConnectionComponent( GraphSubGraphId subgraphId, GraphConnectionId connectionId, GraphComponentId componentId ) { }

		virtual void OnVariableCreated( GraphSubGraphId subgraphId, GraphVariableId variableId ) { }
		virtual void OnVariableRemoved( GraphSubGraphId subgraphId, GraphVariableId variableId ) { }

		virtual void OnGraphReloaded( GraphSubGraphId ownerSubGraphId, GraphSubGraphId reloadedSubGraphId, GraphId nodeId ) { }
		virtual void OnGraphIdChanged( GraphSubGraphId oldId, GraphSubGraphId newId ) { }
		virtual void OnCloseSubgraph( GraphSubGraphId subgraphId ) { }
		virtual void OnExternalGraphOpened( const THandle< CResource >& resource ) { }

		virtual void OnSelectionChanged( GraphSubGraphId subgraphId, const red::DynArray< GraphId >& ids ) { }
	};

	class IEditorGraphEx;
	typedef red::SharedPtr< IEditorGraphEx > IEditorGraphPtr;

	class IEditorGraphEx
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		virtual ~IEditorGraphEx() { }
		
		virtual void AttachListener( IEditorGraphListener* listener ) = 0;
		virtual void DetachListener( IEditorGraphListener* listener ) = 0;

		virtual void SetError( const String& errorMessage ) = 0;
		virtual const String& GetLastError() = 0;

		// Runtime data exposed by connections or blocks changed on ui side
		virtual void OnDataItemChanged( GraphId id, const rtti::AccessPath& path ) = 0;

		// Hierarchy which stores all nodes available for creation
		virtual red::SharedPtr< GraphObjectTree > GetClassTree() = 0;

		// Checks if the specified string can be translated to a node class
		virtual Bool CanCreateItemFromData( const String& data ) const = 0;

		virtual IGraphValidatorOutput* Validate() const = 0;

		virtual void Open() = 0;

		virtual void MarkModified() = 0;

		virtual void OnPostPlace() = 0;

		virtual ISerializable* GetData() const = 0;

		virtual GraphId GetLastId() const = 0;

		///////////////////////////////////////////////////////////////////////
		// Nodes Graph interface

		// Returns all Nodes
		virtual void GetNodes( red::DynArray< GraphNodeId >& outNodes ) const = 0;
		// Create a new Node. Type have to be supported by EditorGraph
		virtual GraphId CreateNode( const THandle< ISerializable >& context, const red::String& type, const Point& initialPosition ) = 0;
		// Force refresh of already created node
		virtual void RecreateNode( GraphNodeId nodeId ) = 0;
		// Removes node and all connected connections
		virtual Bool RemoveNode( GraphNodeId nodeId ) = 0;
		// Tests if the node can be deleted
		virtual Bool CanRemoveNode( GraphNodeId nodeId, Bool isCut ) const = 0;
		//
		virtual Bool CanRemoveIntermediateNode( GraphNodeId nodeId, red::DynArray< ConnectionCreationInfo >& outConnectionInfos ) const = 0;
		//
		virtual Bool RestoreNode( const THandle< NodeDescription >& node ) = 0;
		//
		virtual Bool PlaceNode( const THandle< NodeDescription >& clone, Bool isInPlace ) = 0;
		//
		virtual Bool RefreshNode( GraphId nodeId ) = 0;
		//
		virtual void ValidateNode( GraphId nodeId ) = 0;

		// Called when we move the node on the graph (i.e. by dragging).
		virtual MoveStatus MoveNode( const GraphId& nodeId, const Point& moveOffset ) = 0;
		virtual Bool ExtractNodePosition( const THandle< GraphBackendData >& node, Point& outPosition ) const = 0;
		virtual Bool GetNodePosition( const GraphId& nodeId, Point& outPosition ) const = 0;

		virtual Bool GetNodeDescription( GraphNodeId nodeId, THandle<NodeDescription>& description ) const = 0;

		// Returns backend data attached to this node that need to be presented somehow by the UI.
		virtual THandle< ISerializable > GetItemData( GraphId id ) = 0;
		virtual Bool IsItemDataReadOnly( Uint64 itemId ) const { return false; }

		virtual THandle< ISerializable > GetItemDescriptorData( GraphId id ) = 0;
		
		virtual void SetSubGraphId( GraphSubGraphId id ) = 0;
		// Returns GraphDescriptor id which is wrapped by this editor
		virtual GraphSubGraphId GetSubGraphId() const = 0;
		// Get Sub-Graph per editor node.
		virtual IEditorGraphPtr GetNodeSubGraph( GraphNodeId nodeId ) = 0;

		// Perform drag&drop finishing operation on either graph area or graph node
		virtual void DropData( Point position, const String& data ) = 0;
		virtual void DropData( GraphId nodeId, const String& data ) = 0;

		virtual void RefreshLayout() = 0;
				
		// Function used for searching, check if the nodes contains string in specific part
		virtual void FilterNodes( red::DynArray< SearchProcessOutput >& outNodes, const String& containedString, Uint32 searchType ) = 0;
		virtual void SearchNodes( red::DynArray< SearchProcessOutput >& outNodes, const String& queryString, const SearchOptions& options ) = 0;
		///////////////////////////////////////////////////////////////////////
		// Node's sockets sub interface

		// Gets number of sockets for given nodeId
		virtual Uint32 GetNodeSocketsCount( GraphNodeId nodeId ) const = 0;
		// The socketId is unique local id number per a node. The socketId for a socket object cannot be changed during live time of the editor.
		virtual void GetNodeSockets( GraphNodeId nodeId, red::DynArray< GraphSocketId >& outSockets ) const = 0;
		virtual void GetNodeSockets( const String& nodeClass, red::DynArray< GraphSocketId >& outSockets ) const = 0;
		// Gets a collection of sockets that are eligible for connection with a specified node-socket pair
		virtual void GetEligibleSockets( GraphNodeId outputNodeId, GraphSocketId outputSocketId, const String& nodeClass, red::DynArray< GraphSocketId >& outSockets ) const = 0;
		// Returns SocketDescription.
		virtual THandle< SocketDescription > GetSocketDescription( GraphNodeId nodeId, GraphSocketId socketId ) const = 0;
		virtual THandle< SocketDescription > GetSocketDescription( const String& nodeClass, GraphSocketId socketId ) const = 0;
		// Returns backend data attached to this socket that will be presented somehow by the UI.
		virtual THandle< ISerializable > GetNodeSocketData( GraphNodeId nodeId, GraphSocketId socketId ) = 0;

		virtual void CopySocketsConnections( GraphId sourceNodeId, GraphSocketId sourceSocketId, red::DynArray< THandle< SocketBinding > >& outSocketBindings ) = 0;

		///////////////////////////////////////////////////////////////////////
		// Node's connections sub interface

		// Returns true if two already existing blocks can be connected together by specified sockets
		virtual Bool CanCreateConnection( const ConnectionCreationInfo& info ) const = 0;
		// Returns true if a block of the specified type can be connected to a specified node-socket pair
		virtual Bool CanCreateConnection( GraphNodeId outputNodeId, GraphSocketId outputSocketId, const String& type ) const = 0;
		virtual Bool CanCreateConnection( GraphNodeId outputNodeId, GraphSocketId outputSocketId, const String& type, GraphSocketId inputSocketId ) const = 0;
		// Returns true if a block of the specified type can be created with connections to both node-socket pairs in the specified connection description
		// NOTE: connections should be plugged into separate sockets in the new block
		virtual Bool CanCreateIntermediateNode( const ConnectionCreationInfo& info, const String& type ) const = 0;
		// Extension point function for creating connections with additional parameters
		virtual interop::GraphConnectionCreationData GetConnectionCreationData( const ConnectionCreationInfo& info ) = 0;
		virtual GraphId CreateConnection( const ConnectionCreationInfo& info ) = 0;
		virtual Bool RemoveConnection( GraphConnectionId connectionId ) = 0;
		//
		virtual Bool RestoreConnection( const THandle< ConnectionDescription >& connection, const THandle< ISerializable >& data ) = 0;
		virtual Bool PlaceConnection( GraphConnectionId connectionId, const THandle< ConnectionDescription >& original, const THandle< ISerializable >& data ) = 0;
		
		virtual THandle< ConnectionDescription > GetConnectionDescription( GraphConnectionId connectionId ) = 0;
		// Returns backend data attached to this socket that will be presented somehow by the UI or null.
		virtual THandle< ISerializable > GetConnectionData( GraphConnectionId connectionId ) = 0;
		// Enable or Disable connection (make it active or not). Returns true if backend changed connection state.
		virtual Bool SetConnectionDisabled( GraphConnectionId connectionId, Bool value ) = 0;

		virtual Uint32 GetNodeConnectionsCount( GraphNodeId nodeId ) = 0;
		// Returns all Graphs connections
		virtual void GetNodeConnections( GraphNodeId nodeId, red::DynArray< GraphConnectionId >& connections ) = 0;
		
		///////////////////////////////////////////////////////////////////////
		// Variables interface

		virtual Bool IsSupportVariables() const = 0;

		virtual void ListVariableTypes(red::DynArray<String>& types) = 0;
		virtual void GetVariables( red::DynArray< GraphVariableId >& variableIds ) = 0;

		virtual GraphVariableId CreateVariable( const String& type ) = 0;
		virtual Bool RemoveVariable( GraphVariableId variableId ) = 0;
		virtual THandle< ISerializable > GetVariableData( GraphVariableId variableId ) = 0;

		virtual void GetVariableType( GraphVariableId variableId, String& type ) = 0;
		virtual void GetVariableName( GraphVariableId variableId, String& name ) = 0;
		virtual Bool SetVariableName( GraphVariableId variableId, const String& name ) = 0;

		virtual void Select( const red::DynArray< GraphId >& selectionIds ) = 0;

		///////////////////////////////////////////////////////////////////////
		// Components interface

		virtual GraphComponentId CreateNodeComponent( GraphId nodeId, const String& type ) = 0;
		virtual GraphComponentId CreateSocketComponent( GraphId nodeId, GraphSocketId socketId, const String& type ) = 0;
		virtual GraphComponentId CreateConnectionComponent( GraphId connectionId, const String& type ) = 0;

		virtual void GetNodeComponents( GraphId nodeId, red::DynArray< GraphComponentId >& outComponents ) const = 0;
		virtual void GetSocketComponents( GraphId nodeId, GraphSocketId socketId, red::DynArray< GraphComponentId >& outComponents ) const = 0;
		virtual void GetConnectionComponents( GraphId connectionId, red::DynArray< GraphComponentId >& outComponents ) const = 0;

		virtual THandle< GraphComponentDescription > GetNodeComponent( GraphId nodeId, GraphComponentId componentId ) const = 0;
		virtual THandle< GraphComponentDescription > GetSocketComponent( GraphId nodeId, GraphSocketId socketId, GraphComponentId componentId ) const = 0;
		virtual THandle< GraphComponentDescription > GetConnectionComponent( GraphId connectionId, GraphComponentId componentId ) const = 0;

		///////////////////////////////////////////////////////////////////////
		// Properties

		virtual const String& GetName() const = 0;
		virtual const String GetOwnerEditorName() const = 0;
		virtual Bool IsTopDownTree() const = 0;
		virtual Bool HasClipboardSupport() const = 0;
		virtual Bool HasValidationSupport() const = 0;
		virtual Bool HasCloningSupport() const = 0;
		virtual Bool HasRemoveIntermediateNodeSupport() const = 0;
		virtual Bool IsPostDeleteActionNeeded( const red::DynArray< GraphId >& nodesToDelete ) const = 0;
		virtual red::UniquePtr< IEditAction > GetPostDeleteAction( ObjectHandler& objectHandler, const red::DynArray< GraphId >& nodesToDelete ) = 0;
		virtual Bool IsReadOnly() const = 0;
		virtual void SetReadOnly( const Bool value ) = 0;

		virtual void ProvideConstructors( red::DynArray< IGraphObjectConstructor* >& outConstructors ) = 0;

		//////////////////////////////////////////////////////////////////////////
		// Graph importer

		// Hierarchy which stores all graphs available for import
		virtual red::SharedPtr< GraphObjectTree > GetImportTree() const = 0;

		virtual red::UniquePtr< IGraphImporter > GetGraphImporter() const = 0;
	};
}
