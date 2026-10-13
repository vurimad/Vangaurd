/**
* Copyright (c) 2016-2020 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "objectFwd.h"
#include "editorGraphEx.h"
#include "resourceEditorFwd.h"
#include "editorGraphObjectResolver.h"
#include "../../../common/redReflection/include/serializableValueNotifier.h"

namespace tools
{
	class ResourceBackend;
	class EditorGraphInterop;
	class TreeContainerInterop;
	class GraphValidatorOutputEntry;
	class ObjectCollection;
	class GraphObjectTree;
	class GraphImportTree;
	class VariableTree;
	class EditorGraphSelectionHandler;
	class IEditActionHistory;

	using ObjectResolverProviderFunc = red::FixedSizeFunction< red::UniquePtr< IObjectResolver >( IEditorGraphPtr graph ) >;

	struct BACKEND_ENGINE_API EditorGraphInteropCreationInfo
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

		EditorGraphInteropCreationInfo( const ResourceEditorPtr& editor, const IEditorGraphPtr& editorGraph,
								  ObjectCollection* selectionCollection = nullptr, ObjectCollection* variableTreeSelectionCollection = nullptr );

		ResourceEditorPtr m_editor;
		IEditorGraphPtr m_editorGraph;
		ObjectCollection* m_selectionCollection;
		ObjectCollection* m_variableTreeSelectionCollection;
		ObjectResolverProviderFunc m_customObjectResolverProvider;
	};

	struct BACKEND_ENGINE_API GraphInteropCreationInfo
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

		GraphInteropCreationInfo( IEditActionHistory* editHistory, EditorGraphInterop* graphEditorInterop, IEditorGraphPtr editorGraph, 
								  ObjectCollection* selectionObject );

		EditorGraphInterop* m_editorGraphInterop;
		IEditActionHistory* m_editHistory;
		IEditorGraphPtr m_editorGraph;
		ObjectCollection* m_selectionCollection;
	};

	class BACKEND_ENGINE_API GraphInterop
	{
		friend class GraphInteropObject;

		RED_USE_MEMORY_POOL( red::PoolBackend );
		DECLARE_INTEROP( GraphInterop );

	public:
		GraphInterop( const GraphInteropCreationInfo& info );
		~GraphInterop();

		// IEditorGraphListener implementation
		void OnReload( GraphSubGraphId subgraphId, GraphId nodeId );
		//void OnChangeGraphId( GraphSubGraphId newId );
		void OnCreateNode( GraphNodeId nodeId );
		void OnRecreateNode( GraphNodeId nodeId );
		void OnRemoveNode( GraphNodeId nodeId );
		void OnInvalidateNodeDescription( GraphNodeId nodeId );
		void OnInvalidateNodeDescriptionProperty( GraphNodeId nodeId, const red::String& propertyName );
		void OnInvalidateNodeData( GraphNodeId nodeId );
		void OnInvalidateNodeComponent( GraphNodeId nodeId, GraphComponentId index );
		void OnInvalidateSocketComponent( GraphNodeId nodeId, GraphSocketId socketId, GraphComponentId componentId );
		void OnInvalidateConnectionComponent( GraphConnectionId connectionId, GraphComponentId componentId );

		void OnCreateConnection( GraphConnectionId connectionId );
		void OnRemoveConnection( GraphConnectionId connectionId );
		void OnInvalidateConnectionDescription( GraphConnectionId connectionId );
		void OnInvalidateConnectionDescriptionProperty( GraphConnectionId connectionId, const String& propertyName );
		void OnInvalidateConnectionData( GraphConnectionId connectionId );

		void OnNodeSocketsChanged( GraphNodeId nodeId );
		void OnInvalidateSocketDescription( GraphNodeId nodeId, GraphSocketId socketId );
		void OnInvalidateSocketData( GraphNodeId nodeId, GraphSocketId socketId );

		void OnVariableCreated( GraphVariableId variableId );
		void OnVariableRemoved( GraphVariableId variableId );

		void OnClassTreeChanged();
		void OnImportTreeChanged();

		// saves ids in a local array ( ids' content is being moved! )
		const red::HashSet< GraphId >& GetCachedSelection() { return m_cachedSelection; }
		void SetSelectedObjects();
	
		void SetUserContext(THandle<ISerializable> userContext);

		ObjectHandler* GetHandler() const { return m_handler.Get(); }

		const red::SharedPtr< IGraphValidatorOutput >& GetLastValidatorOutput() const { return m_lastValidatorOutput; }

		void Validate();

		Bool ImportGraph( const String& importPath, const Point& position );

		const GraphObjectTree* GetClassTree() const;
		GraphObjectTree* GetClassTree();

	private:
		void ClearSelection();
		Bool RemoveItem( const GraphId id );

		void CreateConnection( const ConnectionCreationInfo& info );
		void CreateNode( const NodeCreationInfo& info );

		void OnNotifyPropertyChange( const SerializableID& id, const rtti::AccessPath& path );

		interop::ObjectPtr SetDataCollection( const THandle< ISerializable >& data );

		void Send( const interop::InteropNotifier& notifier, const AnsiChar* command );
		void Send( const interop::InteropNotifier& notifier, const AnsiChar* command, GraphId id );
		void Send( const interop::InteropNotifier& notifier, const AnsiChar* command, GraphId firstId, GraphId secondId );
		void Send( const interop::InteropNotifier& notifier, const AnsiChar* command, GraphId firstId, GraphId secondId, GraphId thirdId );
		void Send( const interop::InteropNotifier& notifier, const AnsiChar* command, GraphNodeId id, const red::String& additionalParameters );

		EditorGraphInterop* m_graphEditor;
		IEditorGraphPtr m_graph;
		IEditActionHistory* m_editHistory;

		red::UniquePtr< ObjectHandler > m_handler;
		red::UniquePtr< EditorGraphSelectionHandler > m_selectionHandler;
		red::UniquePtr< IObjectResolver > m_objectResolver;

		Bool m_enabledNotifiers;

		interop::ObjectPtr m_nodes;
		interop::ObjectPtr m_connections;
		interop::ObjectPtr m_variables;
		interop::ObjectPtr m_components;
		interop::ObjectPtr m_validationResults;

		red::SharedPtr< GraphObjectTree > m_classTree;
		red::SharedPtr< TreeContainerInterop > m_classTreeContainer;

		red::SharedPtr< GraphObjectTree > m_importTree;
		red::SharedPtr< TreeContainerInterop > m_importTreeContainer;

		red::UniquePtr<ObjectCollection> m_dataCollection;
		ObjectCollection* m_selectionDataObjects;
		red::UniquePtr<ObjectCollection> m_selectionDescriptorObjects;

		red::HashMap< SerializableID, GraphId > m_selectionMapping;
		red::HashSet< GraphId > m_cachedSelection;
		interop::SelectionChangeInfo m_selectionChangeInfo;

		// Notifies about changes of global state of editor
		interop::InteropNotifier m_editorNotifer;
		// Notifies about nodes internal state changes (like adding, removing or complex nodes group modifications etc.)
		interop::InteropNotifier m_nodesNotifier;
		// Notifies about connection internal state changes (like adding, removing connections)
		interop::InteropNotifier m_connectionsNotifier;
		// Notifies about connection internal state changes (like adding, removing connections)
		interop::InteropNotifier m_variableNotifier;
		// Notifies about validation results
		interop::InteropNotifier m_validationNotifier;

		SerializableValueNotifier m_serializableNotifier;

		THandle<ISerializable> m_userContext;
		
		red::SharedPtr< IGraphValidatorOutput > m_lastValidatorOutput;
	};

	// Graph interface exposed by the interop. This is main entry point for graph. It contains all
	// open subgraphs (GraphInterop objects)
	class BACKEND_ENGINE_API EditorGraphInterop : public IEditorGraphListener
	{
		friend GraphInterop;
		friend class GraphInteropObject;

		RED_USE_MEMORY_POOL( red::PoolBackend );
		DECLARE_INTEROP( EditorGraphInterop );

	public:
		EditorGraphInterop( const EditorGraphInteropCreationInfo& info );
		virtual ~EditorGraphInterop();

		// IEditorGraphListener implementation
		virtual void OnCreateNode( GraphSubGraphId subgraphId, GraphNodeId nodeId ) override;
		virtual void OnRecreateNode( GraphSubGraphId subgraphId, GraphNodeId nodeId ) override;
		virtual void OnRemoveNode( GraphSubGraphId subgraphId, GraphNodeId nodeId ) override;
		virtual void OnInvalidateNodeDescription( GraphSubGraphId subgraphId, GraphNodeId nodeId ) override;
		virtual void OnInvalidateNodeDescriptionProperty( GraphSubGraphId subgraphId, GraphNodeId nodeId, const red::String& propertyName ) override;
		virtual void OnInvalidateNodeData( GraphSubGraphId subgraphId, GraphNodeId nodeId ) override;
		virtual void OnInvalidateNodeComponent( GraphSubGraphId subgraphId, GraphNodeId nodeId, GraphComponentId index ) override;
		
		virtual void OnCreateConnection( GraphSubGraphId subgraphId, GraphConnectionId connectionId ) override;
		virtual void OnRemoveConnection( GraphSubGraphId subgraphId, GraphConnectionId connectionId ) override;
		virtual void OnInvalidateConnectionDescription( GraphSubGraphId subgraphId, GraphConnectionId connectionId ) override;
		virtual void OnInvalidateConnectionDescriptionProperty( GraphSubGraphId subgraphId, GraphConnectionId connectionId, const String& propertyName ) override;
		virtual void OnInvalidateConnectionData( GraphSubGraphId subgraphId, GraphConnectionId connectionId ) override;
		virtual void OnInvalidateConnectionComponent( GraphSubGraphId subgraphId, GraphConnectionId connectionId, GraphComponentId componentId ) override;

		virtual void OnNodeSocketsChanged( GraphSubGraphId subgraphId, GraphNodeId nodeId ) override;
		virtual void OnInvalidateSocketDescription( GraphSubGraphId subgraphId, GraphNodeId nodeId, GraphSocketId socketId ) override;
		virtual void OnInvalidateSocketData( GraphSubGraphId subgraphId, GraphNodeId nodeId, GraphSocketId socketId ) override;

		virtual void OnVariableCreated( GraphSubGraphId subgraphId, GraphVariableId variableId ) override;
		virtual void OnVariableRemoved( GraphSubGraphId subgraphId, GraphVariableId variableId ) override;

		virtual void OnGraphReloaded( GraphSubGraphId ownerSubGraphId, GraphSubGraphId reloadedSubGraphId, GraphId nodeId ) override;
		virtual void OnGraphIdChanged( GraphSubGraphId oldId, GraphSubGraphId newId ) override;
		virtual void OnCloseSubgraph( GraphSubGraphId subgraphId ) override;

		virtual void OnSelectionChanged( GraphSubGraphId subgraphId, const red::DynArray< GraphId >& ids ) override;

		virtual void OnEnableConnection( GraphSubGraphId subgraphId, GraphConnectionId connectionId ) override;
		virtual void OnDisableConnection( GraphSubGraphId subgraphId, GraphConnectionId connectionId ) override;

		IEditActionHistory* GetEditHistory() const;
		ResourceBackend* GetBackend() const;
		IResourceEditor* GetEditor() const;

		void SetUserContext(THandle<ISerializable> userContext);

		void AddEditorGraph( const IEditorGraphPtr& editor );
		void RemoveEditorGraph( GraphSubGraphId subGraphId );
		Bool HasGraphEditor( const IEditorGraphPtr& editor );
		GraphInterop* GetGraphInterop( GraphSubGraphId subGraphId );
		IEditorGraphPtr GetEditor( GraphSubGraphId subGraphId );
		ObjectHandler* GetCurrentHandler();
		void ToggleLayer( const String& layer );
		void ActivateLayer( const String& layer );
		void DeactivateLayer( const String& layer );

	protected:
		virtual void GetAvailableLayers( red::DynArray< String >& availableLayers ) const;
		red::UniquePtr< IObjectResolver > ProvideObjectResolver( IEditorGraphPtr graph ) const;

	private:
		GraphSubGraphId AllocateSubgraphId();
		void OnLayersChanged();

		ResourceEditorWeakPtr m_resourceEditor;

		struct GraphInteropInstance
		{
			RED_USE_MEMORY_POOL( red::PoolBackend );

			GraphInteropInstance( const GraphInteropCreationInfo& info );
			~GraphInteropInstance();

			GraphInterop graphInterop;
			IEditorGraphPtr editor;
		};

		typedef red::SharedPtr< GraphInteropInstance > GraphInteropInstancePtr;
		red::Map< GraphSubGraphId, GraphInteropInstancePtr > m_openEditors;

		ObjectCollection* m_sharedDataCollection;

		// Global variable tree
		red::SharedPtr< VariableTree > m_variableTree;
		red::SharedPtr< TreeContainerInterop > m_variableTreeInterop;

		THandle<ISerializable> m_userContext;

		// used to assign runtime graph ids
		static GraphSubGraphId m_nextId;
		GraphSubGraphId m_currentlyEditedId;
		interop::InteropNotifier m_onGraphIdChanged;
		interop::InteropNotifier m_onLayersChanged;

		red::DynArray< String > m_activeLayers;
		ObjectResolverProviderFunc m_objectResolverProvider;
	};
}
