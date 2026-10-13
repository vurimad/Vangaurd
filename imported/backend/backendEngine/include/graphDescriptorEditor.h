/*
* Copyright (c) 2016-2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../../common/redContainers/include/map.h"
#include "../../../common/redReflection/include/backendData.h"

#include "editorGraphBase.h"
#include "objectResolver.h"

namespace tools
{
	class SocketDescription;

	class IGraphNodeDescriptor;
	class IGraphDescriptor;
	class IGraphDescriptorGlobalData;

	class BACKEND_ENGINE_API GraphDescriptorEditor : public EditorGraphBase
	{
		typedef EditorGraphBase Super;
	public:
		GraphDescriptorEditor( const ResourceEditorPtr& resourceEditor );
		~GraphDescriptorEditor();

		virtual Bool Initialize( const THandle< CResource >& resource, const THandle< IGraphDescriptor >& graph, Uint64 highestPreallocatedGraphId = InvalidGraphId, const THandle< IGraphNodeDescriptor >& parentNode = nullptr );

		virtual void Open() override;

		virtual void OnDataItemChanged( GraphId id, const rtti::AccessPath& path ) override;

		virtual red::SharedPtr< GraphObjectTree > GetClassTree() override;

		virtual red::SharedPtr< GraphObjectTree > GetImportTree() const override;

		virtual Bool CanCreateItemFromData( const String& data ) const override;

		virtual void MarkModified() override;

		virtual IGraphValidatorOutput* Validate() const override;

		virtual ISerializable* GetData() const override;

		virtual GraphId GetLastId() const override;
		
		///////////////////////////////////////////////////////////////////////
		// Nodes Graph interface

		// Returns all Nodes
		virtual void GetNodes(red::DynArray< GraphNodeId >& outNodes) const override;

		// Create a new Node. Type have to be supported by EditorGraph
		virtual GraphId CreateNode( const THandle< ISerializable >& context, const String& type, const Point& initialPosition ) override;
		virtual void RecreateNode( GraphNodeId nodeId ) override;
		// Removes node and all connected connections
		virtual Bool RemoveNode( GraphNodeId nodeId ) override;					// TODO

		// Tests if the node can be deleted
		virtual Bool CanRemoveNode( GraphNodeId nodeId, Bool isCut ) const override;
		//
		virtual Bool CanRemoveIntermediateNode( GraphNodeId nodeId, red::DynArray< ConnectionCreationInfo >& outConnectionInfos ) const override;
		//
		virtual Bool RestoreNode( const THandle< NodeDescription >& node ) override;
		//
		virtual Bool PlaceNode( const THandle< NodeDescription >& clone, Bool isInPlace ) override;
		virtual Bool RefreshNode( GraphId nodeId ) override;

		virtual Bool GetNodeDescription( GraphNodeId nodeId, THandle<NodeDescription>& description ) const override;
		// Returns backend data attached to this node that need to be presented somehow by the UI.
		virtual THandle< ISerializable > GetItemData( GraphId id ) override;
		virtual THandle< ISerializable > GetItemDescriptorData( GraphId id ) override;

		virtual void SetSubGraphId( GraphSubGraphId id ) override;
		virtual GraphSubGraphId GetSubGraphId() const override;
		virtual IEditorGraphPtr GetNodeSubGraph( GraphNodeId nodeId ) override;	
		virtual void OnCloseSubgraph( GraphSubGraphId subgraphId ) override;

		virtual void DropData( Point position, const String& data ) override;
		virtual void DropData( GraphId nodeId, const String& data ) override;

		virtual void FilterNodes( red::DynArray< SearchProcessOutput >& outNodes, const String& containedString, Uint32 searchType ) override;
		virtual void SearchNodes( red::DynArray< SearchProcessOutput >& outNodes, const String& queryString, const SearchOptions& options ) override;
		///////////////////////////////////////////////////////////////////////
		// Sockets

		// The socketId is unique local id number per a node. The socketId for a socket object cannot be changed during live time of editor.
		virtual void GetNodeSockets( GraphNodeId nodeID, red::DynArray< GraphSocketId >& outSockets ) const override;
		virtual void GetNodeSockets( const String& nodeClass, red::DynArray< GraphSocketId >& outSockets ) const override;

		virtual void GetEligibleSockets( GraphNodeId outputNodeId, GraphSocketId outputSocketId, const String& nodeClass, red::DynArray< GraphSocketId >& outSockets ) const override;

		// Return number of node sockets
		virtual Uint32 GetNodeSocketsCount( GraphNodeId nodeID ) const override;

		virtual THandle< SocketDescription > GetSocketDescription( GraphNodeId nodeID, GraphSocketId socketID ) const override;
		virtual THandle< SocketDescription > GetSocketDescription( const String& nodeClass, GraphSocketId socketID ) const override;

		// Returns backend data attached to this socket that need to be presented somehow by the UI.
		virtual THandle< ISerializable > GetNodeSocketData( GraphNodeId nodeID, GraphSocketId socketId ) override;

		virtual void CopySocketsConnections( GraphId sourceNodeId, GraphSocketId sourceSocketId, red::DynArray< THandle< SocketBinding > >& outSocketBindings ) override;

		///////////////////////////////////////////////////////////////////////
		// Node's connections sub interface

		virtual Bool CanCreateConnection( const ConnectionCreationInfo& info ) const override;
		virtual Bool CanCreateConnection( GraphNodeId outputNodeId, GraphSocketId outputSocketId, const String& type ) const override;
		virtual Bool CanCreateConnection( GraphNodeId outputNodeId, GraphSocketId outputSocketId, const String& type, GraphSocketId inputSocketId ) const override;
		virtual Bool CanCreateIntermediateNode( const ConnectionCreationInfo& info, const String& type ) const override;
		virtual interop::GraphConnectionCreationData GetConnectionCreationData( const ConnectionCreationInfo& info ) override;
		virtual GraphId CreateConnection( const ConnectionCreationInfo& info ) override;
		virtual Bool RemoveConnection( GraphConnectionId connectionID ) override;
		virtual Bool RestoreConnection( const THandle< ConnectionDescription >& connection, const THandle< ISerializable >& data ) override;
		virtual Bool PlaceConnection( GraphConnectionId connectionId, const THandle< ConnectionDescription >& original, const THandle< ISerializable >& data ) override;
		virtual THandle< ConnectionDescription > GetConnectionDescription( GraphConnectionId connectionID ) override;
		virtual Bool SetConnectionDisabled( GraphConnectionId connectionId, Bool value ) override;

		virtual Uint32 GetNodeConnectionsCount( GraphNodeId nodeID ) override;
		virtual void GetNodeConnections( GraphNodeId nodeID, red::DynArray< GraphConnectionId >& connections ) override;

		// Returns backend data attached to this socket that need to be presented somehow by the UI or null.
		virtual THandle< ISerializable > GetConnectionData( GraphConnectionId connectionID ) override;

		virtual Bool GetNodePosition( const GraphId& nodeId, Point& outPosition ) const override;

		///////////////////////////////////////////////////////////////////////
		// Variables interface
		virtual Bool IsSupportVariables() const;
		virtual void ListVariableTypes(red::DynArray<String>& types) override;
		virtual void GetVariables( red::DynArray< GraphVariableId >& variableIds ) override;

		virtual GraphVariableId CreateVariable( const String& type ) override;
		virtual Bool RemoveVariable( GraphVariableId variableId ) override;
		virtual THandle< ISerializable > GetVariableData( GraphVariableId variableId ) override;

		virtual void GetVariableType( GraphVariableId variableId, String& type ) override;
		virtual void GetVariableName( GraphVariableId variableId, String& name ) override;
		virtual Bool SetVariableName( GraphVariableId variableId, const String& name ) override;

		///////////////////////////////////////////////////////////////////////
		// Components interface
		virtual GraphComponentId CreateNodeComponent( GraphId nodeId, const String& type ) override;
		virtual GraphComponentId CreateSocketComponent( GraphId nodeId, GraphSocketId socketId, const String& type ) override;
		virtual GraphComponentId CreateConnectionComponent( GraphId connectionId, const String& type ) override;

		virtual void GetNodeComponents( GraphId nodeId, red::DynArray< GraphComponentId >& outComponents ) const override;
		virtual void GetSocketComponents( GraphId nodeId, GraphSocketId socketId, red::DynArray< GraphComponentId >& outComponents ) const override;
		virtual void GetConnectionComponents( GraphId connectionId, red::DynArray< GraphComponentId >& outComponents ) const override;

		virtual THandle< GraphComponentDescription > GetNodeComponent( GraphId nodeId, GraphComponentId componentId ) const override;
		virtual THandle< GraphComponentDescription > GetSocketComponent( GraphId nodeId, GraphSocketId socketId, GraphComponentId componentId ) const override;
		virtual THandle< GraphComponentDescription > GetConnectionComponent( GraphId connectionId, GraphComponentId componentId ) const override;

		///////////////////////////////////////////////////////////////////////
		// Custom actions
		virtual const String& GetName() const override;
		virtual const String GetOwnerEditorName() const override;
		virtual Bool IsTopDownTree() const override;
		virtual Bool HasClipboardSupport() const override;
		virtual Bool HasCloningSupport() const override;
		virtual Bool IsReadOnly() const override;
		virtual void SetReadOnly( const Bool isReadonly ) override;

		//////////////////////////////////////////////////////////////////////////
		// Recursive graph listeners
		void AttachRecursiveListener( IEditorGraphListener* listener );
		void DetachRecursiveListener( IEditorGraphListener* listener );
		virtual void DetachListener( IEditorGraphListener* listener ) override;

	protected:				
		virtual GraphDescriptorEditor* GetNodeSubGraphDescriptor();
		typedef red::HashMap< String, const rtti::ClassType* > ClassDescriptorsMap;
		typedef red::HashMap< String, THandle< IGraphNodeDescriptor > > DefaultDescriptorsMap;
		typedef red::Map< GraphNodeId, red::SharedPtr< GraphDescriptorEditor > > SubGraphs;

		// helpers
		void InitializeClassDescriptorsMapping();

		THandle< IGraphNodeDescriptor > GetNode( GraphNodeId nodeId ) const;

		// virtual callbacks
		virtual Bool OnInitialize();
		
		THandle< IGraphDescriptor >					m_graph;

		// definitions class->descriptors mapping
		ClassDescriptorsMap							m_classDescriptors;											// def->descriptor mapping cache
		DefaultDescriptorsMap						m_defaultDescriptors;

		// sub graphs
		SubGraphs									m_subGraphs;

		// recursive graph listeners (automatically attached to opened subgraphs)
		red::DynArray<IEditorGraphListener*>		m_recursiveGraphListners;

	private:
		void HandleExternalGraphNotification( const THandle< CResource >& resource, const THandle< IGraphNodeDescriptor >& parentNode );
	};

}		// namespace tools

