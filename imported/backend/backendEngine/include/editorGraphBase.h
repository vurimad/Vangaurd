/**
* Copyright (c) 2016-2019 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "editorGraphEx.h"
#include "resourceEditorFwd.h"

namespace tools
{
	class IResourceEditor;

	template < class F >
	static void Forward( const red::DynArray<IEditorGraphListener*>& listeners, const F& fun )
	{
		for( IEditorGraphListener* l : listeners )
		{
			fun( l );
		}
	}

	class BACKEND_ENGINE_API EditorGraphBase : public IEditorGraphEx, public IEditorGraphListener
	{
	public:
		EditorGraphBase( const ResourceEditorPtr& resourceEditor );

		virtual void AttachListener( IEditorGraphListener* listener ) override;
		virtual void DetachListener( IEditorGraphListener* listener ) override;

		virtual void SetError( const String& errorMessage ) override;
		virtual const String& GetLastError() override;

		virtual Bool CanCreateItemFromData( const String& data ) const override;

		virtual void MarkModified() override;

		virtual IGraphValidatorOutput* Validate() const override;

		virtual void Open() override;

		virtual void OnPostPlace() override;

		virtual ISerializable* GetData() const override;

		virtual void SearchNodes( red::DynArray<SearchProcessOutput>& outNodes, const String& queryString, const SearchOptions& options ) override;

		virtual GraphId GetLastId() const override;

		//////////////////////////////////////////////////////////////////////////

		virtual void CopySocketsConnections( GraphId sourceNodeId, GraphSocketId sourceSocketId, red::DynArray< THandle< SocketBinding > >& outSocketBindings ) override;

		//////////////////////////////////////////////////////////////////////////

		// graph listener interface
		virtual void OnCreateNode( GraphSubGraphId subgraphId, GraphNodeId nodeId ) override;
		virtual void OnRecreateNode( GraphSubGraphId subgraphId, GraphNodeId nodeId ) override;
		virtual void OnRemoveNode( GraphSubGraphId subgraphId, GraphNodeId nodeId ) override;
		virtual void OnInvalidateNodeDescription( GraphSubGraphId subgraphId, GraphNodeId nodeId ) override;
		virtual void OnInvalidateNodeDescriptionProperty( GraphSubGraphId subgraphId, GraphNodeId nodeId, const red::String& propertyName ) override;
		virtual void OnInvalidateNodeData( GraphSubGraphId subgraphId, GraphNodeId nodeId ) override;
		virtual void OnInvalidateNodeComponent( GraphSubGraphId subgraphId, GraphNodeId nodeId, GraphComponentId index ) override;

		virtual void OnCreateConnection( GraphSubGraphId subgraphId, GraphConnectionId connectionId ) override;
		virtual void OnRemoveConnection( GraphSubGraphId subgraphId, GraphConnectionId connectionId ) override;
		virtual void OnEnableConnection( GraphSubGraphId subgraphId, GraphConnectionId connectionId ) override;
		virtual void OnDisableConnection( GraphSubGraphId subgraphId, GraphConnectionId connectionId ) override;
		virtual void OnInvalidateConnectionDescription( GraphSubGraphId subgraphId, GraphConnectionId connectionId ) override;
		virtual void OnInvalidateConnectionDescriptionProperty( GraphSubGraphId subgraphId, GraphNodeId nodeId, const String& propertyName ) override;
		virtual void OnInvalidateConnectionData( GraphSubGraphId subgraphId, GraphConnectionId connectionId ) override;
		virtual void OnInvalidateSocketComponent( GraphSubGraphId subgraphId, GraphNodeId nodeId, GraphSocketId socketId, GraphComponentId componentId ) override;

		virtual void OnNodeSocketsChanged( GraphSubGraphId subgraphId, GraphNodeId nodeId ) override;
		virtual void OnInvalidateSocketDescription( GraphSubGraphId subgraphId, GraphNodeId nodeId, GraphSocketId socketId ) override;
		virtual void OnInvalidateSocketData( GraphSubGraphId subgraphId, GraphNodeId nodeId, GraphSocketId socketId ) override;
		virtual void OnInvalidateConnectionComponent( GraphSubGraphId subgraphId, GraphConnectionId connectionId, GraphComponentId componentId ) override;

		virtual void OnVariableCreated( GraphSubGraphId subgraphId, GraphVariableId variableId ) override;
		virtual void OnVariableRemoved( GraphSubGraphId subgraphId, GraphVariableId variableId ) override;

		virtual void OnGraphReloaded( GraphSubGraphId ownerSubGraphId, GraphSubGraphId reloadedSubGraphId, GraphId nodeId ) override;
		virtual void OnGraphIdChanged( GraphSubGraphId oldId, GraphSubGraphId newId ) override;
		virtual void OnCloseSubgraph( GraphSubGraphId subgraphId ) override;
		virtual void OnExternalGraphOpened( const THandle< CResource >& resource ) override;

		virtual void OnSelectionChanged( GraphSubGraphId subgraphId, const red::DynArray< GraphId >& ids ) override;

		//////////////////////////////////////////////////////////////////////////
		virtual void RefreshLayout() override;
		virtual Bool RefreshNode( GraphId nodeId ) override;
		virtual void ValidateNode( GraphId nodeId ) override;

		virtual MoveStatus MoveNode( const GraphId& nodeId, const Point& moveOffset ) override;
		virtual Bool ExtractNodePosition( const THandle<GraphBackendData>& node, Point& outPosition ) const override;

		virtual void Select( const red::DynArray< GraphId >& selectionIds ) override;

		virtual const String& GetName() const override;
		virtual const String GetOwnerEditorName() const override;
		virtual Bool IsTopDownTree() const override;
		virtual Bool HasClipboardSupport() const override;
		virtual Bool HasValidationSupport() const override;
		virtual Bool HasRemoveIntermediateNodeSupport() const override;
		virtual Bool IsPostDeleteActionNeeded( const red::DynArray< GraphId >& nodesToDelete ) const override;
		virtual red::UniquePtr< IEditAction > GetPostDeleteAction( ObjectHandler& objectHandler, const red::DynArray< GraphId >& nodesToDelete ) override;
		virtual Bool IsReadOnly() const override;
		virtual void SetReadOnly( const Bool isReadonly ) override;

		virtual void ProvideConstructors( red::DynArray<IGraphObjectConstructor *>& outConstructors ) override;

		virtual red::UniquePtr< IGraphImporter > GetGraphImporter() const override;

		template< typename T = IResourceEditor >
		const T* GetEditor() const
		{
			return static_cast< const T* >( m_resourceEditor.Lock().Get() );
		}

		template< typename T = IResourceEditor >
		T* GetEditor()
		{
			return static_cast< T* >( m_resourceEditor.Lock().Get() );
		}

	protected:
		typedef std::function< void( const String& ) > TErrorFunc;

		ResourceEditorWeakPtr m_resourceEditor;
		red::DynArray<IEditorGraphListener*> m_graphListner;

		String m_errorBuffer;
		TErrorFunc m_onError;
		Bool m_isReadonly{ false };
	};

	extern void BACKEND_ENGINE_API DataToString( red::StringBuilder<String>& builder, const THandle< ISerializable >& data );
}