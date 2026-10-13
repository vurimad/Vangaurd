/*
* Copyright (c) 2016-2020 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "graphDescriptorEditor.h"
#include "graphDescriptor.h"
#include "graphEditorNode.h"
#include "editorGraphObjectTree.h"
#include "resourceEditor.h"
#include "resourceBackend.h"

namespace tools
{
	GraphDescriptorEditor::GraphDescriptorEditor( const ResourceEditorPtr& resourceEditor )
		: Super( resourceEditor )
	{
	}

	GraphDescriptorEditor::~GraphDescriptorEditor()
	{
		m_graph->DetachListener( this );
	}

	void GraphDescriptorEditor::InitializeClassDescriptorsMapping()
	{
		struct Local
		{
			GraphDescriptorEditor& m_this;
			red::DynArray< String > m_supportedClasses;
			red::DynArray< const rtti::ClassType* > m_derivedClasses;

			// NOTE: we are assigning default descriptors some ids, because some backends require this (e.g. scenes)
			// ids are assigned from the last one to avoid situations like having some ids assigned for default descs (1,2,3,4,5)
			// giving next ids to graph's nodes (6,7,8...) and then increasing the number of default descs, in such situation
			// id 6 would have a collision
			GraphId defaultDescriptorIdFactory = InvalidGraphId - 1;

			Local( GraphDescriptorEditor* me )
				: m_this( *me ) {}

			void ProcessClassMappings( const rtti::ClassType* descriptorClass )
			{
				if ( descriptorClass->IsAbstract() )
				{
					return;
				}

				if ( !m_this.m_graph->IsRootSubClassSupported( descriptorClass ) )
				{
					return;
				}

				IGraphNodeDescriptor* graphNode = descriptorClass->GetDefaultObject< IGraphNodeDescriptor >();
				if ( !graphNode )
				{
					return;
				}

				graphNode->GatherSupportedNodeClasses( m_supportedClasses );

				for ( const String& s : m_supportedClasses )
				{
					THandle< IGraphNodeDescriptor > defaultNode = descriptorClass->CreateHandle< IGraphNodeDescriptor >();
					defaultNode->m_id = defaultDescriptorIdFactory--;
					defaultNode->Create( nullptr, *m_this.m_graph, s, true );
					defaultNode->Initialize( *m_this.m_graph, true );

					if ( !m_this.m_graph->IsRuntimeNodeSupported( defaultNode ) )
					{
						continue;
					}

					RED_ASSERT( m_this.m_classDescriptors.Find( s ) == m_this.m_classDescriptors.End(), "Two graph descriptor nodes are mapping same class name!" );
					m_this.m_classDescriptors.Insert( s, descriptorClass );
					m_this.m_defaultDescriptors.Insert( s, defaultNode );
				}

				m_supportedClasses.Clear();
			}

			void ProcessRootClassRec( const rtti::ClassType* rootClass )
			{
				ProcessClassMappings( rootClass );

				Uint32 derivedClassesCount = m_derivedClasses.Size();

				GetRttiSystem().EnumDerivedClasses( rootClass, m_derivedClasses );

				for ( Uint32 i = derivedClassesCount, n = m_derivedClasses.Size(); i != n; ++i )
				{
					const rtti::ClassType* classType = m_derivedClasses[ i ];
					ProcessRootClassRec( classType );
				}

				m_derivedClasses.Resize( derivedClassesCount );
			}
		} local( this );

		// Note: this shouldn't be mixed with version below this code (see next comment), which is not recursive and doesn't take root classes as inputs
		red::DynArray< const rtti::ClassType* > rootDescriptorClasses;
		m_graph->GatherRootNodesClasses( rootDescriptorClasses );
		for ( const rtti::ClassType* rootClass : rootDescriptorClasses )
		{
			local.ProcessRootClassRec( rootClass );
		}

		// Note: this shouldn't be mixed with above version, it's taking all classes as input, so if you want all derivatives of root classes then you have to do it yourself
		red::DynArray< const rtti::ClassType* > descriptorClasses;
		m_graph->GatherNodesClasses(descriptorClasses);
		for (const rtti::ClassType* theClass : descriptorClasses)
		{
			local.ProcessClassMappings(theClass);
		}
	}

	Bool GraphDescriptorEditor::Initialize( const THandle< CResource >& resource, const THandle< IGraphDescriptor >& graph, Uint64 highestPreallocatedGraphId, const THandle< IGraphNodeDescriptor >& parentNode )
	{
		m_graph = graph;
		if ( !m_graph )
		{
			return false;
		}

		m_graph->AttachListener( this );
		for ( auto l : m_recursiveGraphListners )
		{
			m_graph->AttachListener( l );
		}

		// graph can cache some editor-only data
		if ( !graph->Initialize( resource, parentNode, highestPreallocatedGraphId ) )
		{
			return false;
		}

		// initialize cached data
		InitializeClassDescriptorsMapping();

		if ( !OnInitialize() )
		{
			return false;
		}

		return true;
	}

	void GraphDescriptorEditor::Open()
	{
		m_graph->Open();
	}

	void GraphDescriptorEditor::OnDataItemChanged( GraphId id, const rtti::AccessPath& path )
	{
		THandle< IGraphNodeDescriptor > node = m_graph->GetNodesMap().GetNode( id );
		if ( node )
		{
			node->OnNodeDataChanged( path );
		}
		else
		{
			THandle< ConnectionDescription > connection = m_graph->GetConnection( id );
			if ( connection )
			{
				m_graph->OnConnectionDataChanged( id, path );
			}
		}
	}

	red::SharedPtr< GraphObjectTree > GraphDescriptorEditor::GetClassTree()
	{
		red::SharedPtr< GraphObjectTree > classTree = red::CreateSharedPtr<GraphObjectTree>( this );

		GraphObjectTreeBuilder builder( classTree.Get() );
		m_graph->FillClassTree( builder );
		return classTree;
	}

	red::SharedPtr< GraphObjectTree > GraphDescriptorEditor::GetImportTree() const
	{
		red::SharedPtr< GraphObjectTree > importTree = red::CreateSharedPtr< GraphObjectTree >( this );

		GraphObjectTreeBuilder builder( importTree.Get() );
		m_graph->FillImportTree( builder );
		return importTree;
	}

	Bool GraphDescriptorEditor::CanCreateItemFromData( const String& data ) const
	{
		auto it = m_classDescriptors.Find( data );
		return it != m_classDescriptors.End();
	}

	void GraphDescriptorEditor::MarkModified()
	{
		m_graph->MarkModified();
	}

	Bool GraphDescriptorEditor::IsReadOnly() const
	{
		const IResourceEditor* editor = GetEditor();
		return m_isReadonly || ( editor && !editor->IsResourceOpened( m_graph->GetResourcePath() ) );
	}

	void GraphDescriptorEditor::SetReadOnly( const Bool isReadonly )
	{
		m_isReadonly = isReadonly;
	}

	IGraphValidatorOutput* GraphDescriptorEditor::Validate() const
	{
		GraphValidatorOutput* output = RED_NEW( GraphValidatorOutput )( m_graph.Get() );

		red::DynArray< const rtti::ClassType* > validatorClasses;
		GetRttiSystem().EnumClasses( ClassID< IGraphValidator >(), validatorClasses );
		for ( const rtti::ClassType* validatorClass : validatorClasses )
		{
			if ( m_graph->IsValidatorClassSupported( validatorClass ) )
			{
				IGraphValidator* validator = validatorClass->CreateObject< IGraphValidator >();
				red::UniquePtr< IGraphValidationContext > context = validator->CreateContext();
				m_graph->Validate( *validator, *context.Get(), *output );
				validatorClass->DestroyObject( validator );
			}
		}

		return output;
	}

	ISerializable* GraphDescriptorEditor::GetData() const
	{
		return m_graph->GetData();
	}

	tools::GraphId GraphDescriptorEditor::GetLastId() const
	{
		return m_graph->GetLastId();
	}

	void GraphDescriptorEditor::HandleExternalGraphNotification( const THandle< CResource >& resource, const THandle< IGraphNodeDescriptor >& parentNode )
	{
		if ( !parentNode )
			return;

		IGraphDescriptor* desc = parentNode->GetGraphDescriptor();
		if ( !desc )
			return;

		THandle< CResource > parentResource = desc->GetParentResource().ToHandle();
		if ( !parentResource )
			return;

		if ( parentResource != resource )
		{
			OnExternalGraphOpened( resource );
		}
	}

	Bool GraphDescriptorEditor::OnInitialize()
	{
		return true;
	}

	THandle< IGraphNodeDescriptor > GraphDescriptorEditor::GetNode( GraphNodeId nodeId ) const
	{
		return m_graph->GetNodesMap().GetNode( nodeId );
	}

	void GraphDescriptorEditor::GetNodes(red::DynArray< GraphNodeId >& outNodes) const
	{
		outNodes = m_graph->GetNodesMap().GetNodesIds();
	}

	GraphId GraphDescriptorEditor::CreateNode( const THandle< ISerializable >& context, const String& type, const Point& initialPosition )
	{
		auto itFind = m_classDescriptors.Find( type );
		if ( itFind == m_classDescriptors.End() )
		{
			// node type not supported
			return InvalidGraphId;
		}

		const rtti::ClassType* descriptorClass = itFind.Value();

		// request graph to create node
		THandle< IGraphNodeDescriptor > node = m_graph->CreateNode( context, descriptorClass, type, false, initialPosition );
		if ( !node )
		{
			return InvalidGraphId;
		}

		// generic listeners callback
		GraphNodeId id = node->GetId();
		OnCreateNode( GetSubGraphId(), id );

		return id;
	}

	void GraphDescriptorEditor::RecreateNode( GraphNodeId nodeId )
	{
		OnRecreateNode( GetSubGraphId(), nodeId );
	}

	Bool GraphDescriptorEditor::GetNodeDescription( GraphNodeId nodeID, THandle<NodeDescription>& description ) const
	{
		RED_FATAL_ASSERT( nodeID != InvalidGraphId, "Invalid node id provided." );

		THandle< IGraphNodeDescriptor > node = GetNode( nodeID );
		if ( node != nullptr )
		{
			description = node;
			return true;
		}
		return false;
	}

	Bool GraphDescriptorEditor::RemoveNode( GraphNodeId nodeId )
	{
		// TODO: this should be transact
		auto itFind = m_subGraphs.Find( nodeId );
		if ( itFind != m_subGraphs.End() )
		{
			m_subGraphs.Remove( itFind );
		}

		if ( m_graph->RemoveNode( nodeId ) )
		{
			OnRemoveNode( GetSubGraphId(), nodeId );
			return true;
		}

		return false;
	}

	Bool GraphDescriptorEditor::CanRemoveNode( GraphNodeId nodeId, Bool isCut ) const
	{
		return m_graph->CanRemoveNode( nodeId, isCut );
	}

	Bool GraphDescriptorEditor::CanRemoveIntermediateNode( GraphNodeId nodeId, red::DynArray< ConnectionCreationInfo >& outConnectionInfos ) const
	{
		return HasRemoveIntermediateNodeSupport() && m_graph->CanRemoveIntermediateNode( nodeId, outConnectionInfos );
	}

	Bool GraphDescriptorEditor::RestoreNode( const THandle< NodeDescription >& node )
	{
		if ( m_graph->RestoreNode( Cast< IGraphNodeDescriptor >( node ) ) )
		{
			OnCreateNode( GetSubGraphId(), node->m_id );
			return true;
		}

		return false;
	}

	Bool GraphDescriptorEditor::PlaceNode( const THandle< NodeDescription >& clone, Bool isInPlace )
	{
		if ( m_graph->PlaceNode( Cast< IGraphNodeDescriptor >( clone ), isInPlace ) )
		{
			OnCreateNode( GetSubGraphId(), clone->m_id );
			return true;
		}

		return false;
	}

	Bool GraphDescriptorEditor::RefreshNode( GraphId nodeId )
	{
		THandle< IGraphNodeDescriptor > node = GetNode( nodeId );

		if ( !node )
			return false;

		return node->Refresh();
	}

	THandle< ISerializable > GraphDescriptorEditor::GetItemDescriptorData( GraphId id )
	{
		THandle< IGraphNodeDescriptor > node = GetNode( id );
		if ( node )
			return node;
		else
			return GetConnectionDescription( id );
	}

	void GraphDescriptorEditor::SetSubGraphId( GraphSubGraphId id )
	{
		m_graph->SetSubGraphId( id );
	}

	GraphSubGraphId GraphDescriptorEditor::GetSubGraphId() const
	{
		return m_graph->GetSubGraphId();
	}

	IEditorGraphPtr GraphDescriptorEditor::GetNodeSubGraph( GraphNodeId nodeId )
	{
		// TODO: we can't cache these, because it would cause desync when reloading quest resources
		// it's really sensitive and hard to do, so I'd rather disable this for now, until opening graphs is a real problem
		//auto itFind = m_subGraphs.Find( nodeId );
		//if ( itFind != m_subGraphs.End() )
		//{
		//	return itFind.Value();
		//}

		THandle< IGraphNodeDescriptor > node = GetNode( nodeId );
		if ( !node )
		{
			return nullptr;
		}

		THandle< IGraphDescriptor > graphDescriptor = HandleFromPtr( node->GetSubGraphDescriptor() );
		if ( !graphDescriptor )
		{
			return nullptr;
		}

		red::SharedPtr< GraphDescriptorEditor > descEditor( GetNodeSubGraphDescriptor() );

		if ( !descEditor->Initialize( node->GetSubGraphResource(), graphDescriptor, InvalidGraphId, node ) )
		{
			return nullptr;
		}

		for ( auto l : m_recursiveGraphListners )
		{
			descEditor->AttachRecursiveListener( l );
		}

		descEditor->HandleExternalGraphNotification( node->GetSubGraphResource(), node );

		//m_subGraphs.Insert( nodeId, descEditor );

		return descEditor;
	}

	GraphDescriptorEditor* GraphDescriptorEditor::GetNodeSubGraphDescriptor()
	{
		return RED_NEW( GraphDescriptorEditor )( m_resourceEditor.Lock() );
	}

	void GraphDescriptorEditor::OnCloseSubgraph( GraphSubGraphId subgraphId )
	{
		for ( auto it = m_subGraphs.Begin(); it != m_subGraphs.End(); ++it )
		{
			if ( it.Value()->GetSubGraphId() == subgraphId )
			{
				m_subGraphs.Remove( it );
				break;
			}
		}

		Super::OnCloseSubgraph( subgraphId );
	}

	void GraphDescriptorEditor::DropData( Point position, const String& data )
	{
		m_graph->DropData( position, data );
	}

	void GraphDescriptorEditor::DropData( GraphId nodeId, const String& data )
	{
		THandle< IGraphNodeDescriptor > node = GetNode( nodeId );
		if ( !node )
		{
			return;
		}

		node->OnDropData( data );
	}

	void GraphDescriptorEditor::FilterNodes( red::DynArray< SearchProcessOutput >& outNodes, const String& containedString, Uint32 searchType )
	{
		for ( GraphId nodeId : m_graph->GetNodesMap().GetNodesIds() )
		{
			const auto& node = GetNode( nodeId );
			String	outLabel;
			String	outType;

			if ( node->PassesCriteria( containedString, searchType, outLabel, outType ) )
			{
				outNodes.PushBack( SearchProcessOutput{ nodeId, outLabel, outType } );
			}
		}
	}

	void GraphDescriptorEditor::SearchNodes( red::DynArray< SearchProcessOutput >& outNodes, const String& queryString, const SearchOptions& options )
	{
		const red::FilterTokenizer tokenizer( queryString );
		red::FilterExecutor filter( tokenizer, static_cast<ed::CaseSensitive>( options.m_isCaseSensitive ) );

		for ( GraphId nodeId : m_graph->GetNodesMap().GetNodesIds() )
		{
			const THandle< IGraphNodeDescriptor > node = GetNode( nodeId );
			const String stringifiedNode = node->ToString();

			if ( filter.Execute( stringifiedNode ) )
			{
				outNodes.PushBack( { nodeId, node->GetCaption() } );
			}
		}
	}

	THandle< ISerializable > GraphDescriptorEditor::GetItemData( GraphId id )
	{
		THandle< IGraphNodeDescriptor > node = GetNode( id );
		if ( node )
			return node->GetNodeData();

		return GetConnectionData( id );
	}

	void GraphDescriptorEditor::GetNodeSockets( GraphNodeId nodeID, red::DynArray< GraphSocketId >& outSockets ) const
	{
		THandle< IGraphNodeDescriptor > node = GetNode( nodeID );
		if ( !node )
		{
			return;
		}

		node->GetSocketsButPossiblyCreateThemBeforehand( outSockets );
	}

	void GraphDescriptorEditor::GetNodeSockets( const String& nodeClass, red::DynArray< GraphSocketId >& outSockets ) const
	{
		auto itFind = m_defaultDescriptors.Find( nodeClass );
		if ( itFind == m_defaultDescriptors.End() )
		{
			// node type not supported
			return;
		}

		itFind.Value()->GetSocketsButPossiblyCreateThemBeforehand( outSockets );
	}

	void GraphDescriptorEditor::GetEligibleSockets( GraphNodeId outputNodeId, GraphSocketId outputSocketId, const String& nodeClass, red::DynArray< GraphSocketId >& outSockets ) const
	{
		auto itFind = m_defaultDescriptors.Find( nodeClass );
		if ( itFind == m_defaultDescriptors.End() )
		{
			// node type not supported
			return;
		}

		itFind.Value()->GetSocketsButPossiblyCreateThemBeforehand( outSockets );

		for ( auto it = outSockets.Begin(); it != outSockets.End(); )
		{
			if ( !CanCreateConnection( outputNodeId, outputSocketId, nodeClass, *it ) )
				it = outSockets.Remove( it ).Iterator();
			else
				++it;
		}
	}

	Uint32 GraphDescriptorEditor::GetNodeSocketsCount( GraphNodeId nodeID ) const
	{
		THandle< IGraphNodeDescriptor > node = GetNode( nodeID );
		if ( !node )
		{
			return 0;
		}

		return node->GetSocketsCount();
	}

	THandle< SocketDescription > GraphDescriptorEditor::GetSocketDescription( GraphNodeId nodeID, GraphSocketId socketID ) const
	{
		RED_FATAL_ASSERT( nodeID != InvalidGraphId, "Invalid socket id provided." );

		THandle< IGraphNodeDescriptor > node = GetNode( nodeID );
		if ( !node )
		{
			return nullptr;
		}

		return node->GetSocketDescription( socketID );
	}

	THandle< SocketDescription > GraphDescriptorEditor::GetSocketDescription( const String& nodeClass, GraphSocketId socketId ) const
	{
		auto itFind = m_defaultDescriptors.Find( nodeClass );
		if ( itFind == m_defaultDescriptors.End() )
		{
			// node type not supported
			return nullptr;
		}

		return itFind.Value()->GetSocketDescription( socketId );
	}

	THandle< ISerializable > GraphDescriptorEditor::GetNodeSocketData( GraphNodeId nodeID, GraphSocketId socketId )
	{
		return nullptr;
	}

	void GraphDescriptorEditor::CopySocketsConnections( GraphId sourceNodeId, GraphSocketId sourceSocketId, red::DynArray< THandle< SocketBinding > >& outSocketBindings )
	{
		const GraphConnectionsList& connectionsMap = m_graph->GetConnectionsMap();
		ConnectionsIterator itSource ( connectionsMap, false, sourceNodeId, sourceSocketId, ConnectionsIterator::SOURCE_CONNECTION );
		ConnectionsIterator itDestination ( connectionsMap, false, sourceNodeId, sourceSocketId, ConnectionsIterator::DESTINATION_CONNECTION );

		for (; itSource; ++itSource )
		{
			outSocketBindings.PushBack( CreateHandle< SocketBinding >( itSource->m_destinationNode, itSource->m_destinationSocketIndex ) );
		}

		for (; itDestination; ++itDestination )
		{
			outSocketBindings.PushBack( CreateHandle< SocketBinding >( itDestination->m_sourceNode, itDestination->m_sourceSocketIndex ) );
		}
	}

	Bool GraphDescriptorEditor::CanCreateConnection( const ConnectionCreationInfo& info ) const
	{
		return m_graph->CanCreateConnection( info );
	}

	Bool GraphDescriptorEditor::CanCreateConnection( GraphNodeId outputNodeId, GraphSocketId outputSocketId, const String& type ) const
	{
		auto itFind = m_defaultDescriptors.Find( type );
		if ( itFind == m_defaultDescriptors.End() )
		{
			// node type not supported
			return false;
		}

		return m_graph->CanCreateConnection( outputNodeId, outputSocketId, itFind.Value() );
	}

	Bool GraphDescriptorEditor::CanCreateConnection( GraphNodeId outputNodeId, GraphSocketId outputSocketId, const String& type, GraphSocketId inputSocketId ) const
	{
		auto itFind = m_defaultDescriptors.Find( type );
		if ( itFind == m_defaultDescriptors.End() )
		{
			// node type not supported
			return false;
		}

		return m_graph->CanCreateConnection( outputNodeId, outputSocketId, itFind.Value(), inputSocketId );
	}

	Bool GraphDescriptorEditor::CanCreateIntermediateNode( const ConnectionCreationInfo& info, const String& type ) const
	{
		auto itFind = m_defaultDescriptors.Find( type );
		if ( itFind == m_defaultDescriptors.End() )
		{
			// node type not supported
			return false;
		}

		return m_graph->CanCreateIntermediateNode( info, itFind.Value() );
	}

	interop::GraphConnectionCreationData GraphDescriptorEditor::GetConnectionCreationData( const ConnectionCreationInfo& info )
	{
		return interop::GraphConnectionCreationData();
	}

	GraphId GraphDescriptorEditor::CreateConnection( const ConnectionCreationInfo& info )
	{
		// Can create connection test is done before actual connection creation, so there is no way to break consistency.
		//RED_ASSERT( CanCreateConnection( descriptor ), "High level editor inconsistency (creating connection, not checking if its ever possible." );

		return m_graph->CreateConnection( info );
	}

	Bool GraphDescriptorEditor::RemoveConnection( GraphConnectionId connectionID )
	{
		return m_graph->RemoveConnection( connectionID );
	}

	Bool GraphDescriptorEditor::RestoreConnection( const THandle< ConnectionDescription >& connection, const THandle< ISerializable >& data )
	{
		return m_graph->RestoreConnection( connection, data );
	}

	Bool GraphDescriptorEditor::PlaceConnection( GraphConnectionId connectionId, const THandle< ConnectionDescription >& original, const THandle< ISerializable >& data )
	{
		return m_graph->PlaceConnection( connectionId, original, data );
	}

	THandle< ConnectionDescription > GraphDescriptorEditor::GetConnectionDescription( GraphConnectionId connectionID )
	{
		return m_graph->GetConnectionsMap().FindConnection( connectionID );
	}

	Uint32 GraphDescriptorEditor::GetNodeConnectionsCount( GraphNodeId nodeID )
	{
		const GraphConnectionsList& connectionsMap = m_graph->GetConnectionsMap();
		ConnectionsIterator itSource ( connectionsMap, false, nodeID, ConnectionsIterator::SOURCE_CONNECTION );
		ConnectionsIterator itDestination ( connectionsMap, false, nodeID, ConnectionsIterator::DESTINATION_CONNECTION );

		return itSource.Count() + itDestination.Count();
	}

	void GraphDescriptorEditor::GetNodeConnections( GraphNodeId nodeID, red::DynArray< GraphConnectionId >& connections )
	{
		THandle< IGraphNodeDescriptor > editorNode = GetNode( nodeID );
		if ( !editorNode )
		{
			return;
		}
		const GraphConnectionsList& connectionsMap = m_graph->GetConnectionsMap();
		ConnectionsIterator itSource ( connectionsMap, false, nodeID, ConnectionsIterator::SOURCE_CONNECTION );
		ConnectionsIterator itDestination ( connectionsMap, false, nodeID, ConnectionsIterator::DESTINATION_CONNECTION );

		Uint32 baseIndex = connections.Size();
		connections.Reserve( itSource.Count() + itDestination.Count() );
		while ( itSource )
		{
			connections.PushBack( itSource->m_id );
			++itSource;
		}

		while ( itDestination )
		{
			connections.PushBack( itDestination->m_id );
			++itDestination;
		}
	}

	Bool GraphDescriptorEditor::SetConnectionDisabled( GraphConnectionId connectionId, Bool value )
	{
		return m_graph->SetConnectionDisabled( connectionId, value, m_onError );
	}

	THandle< ISerializable > GraphDescriptorEditor::GetConnectionData( GraphConnectionId connectionID )
	{
		return m_graph->GetConnectionData( connectionID );
	}

	Bool GraphDescriptorEditor::GetNodePosition( const GraphId& nodeId, Point& outPosition ) const
	{
		if ( THandle< IGraphNodeDescriptor > node = GetNode( nodeId ) )
		{
			return ExtractNodePosition( node, outPosition );
		}
		return false;
	}

	///////////////////////////////////////////////////////////////////////
	// Variables interface

	Bool GraphDescriptorEditor::IsSupportVariables() const
	{
		return m_graph->IsSupportVariable();
	}

	void GraphDescriptorEditor::ListVariableTypes(red::DynArray<String>& types)
	{
		m_graph->ListVariableTypes(types);
	}

	void GraphDescriptorEditor::GetVariables( red::DynArray< GraphVariableId >& variableIds )
	{
		m_graph->GetVariables(variableIds);
	}

	GraphVariableId GraphDescriptorEditor::CreateVariable( const String& type )
	{
		return m_graph->CreateVariable(type);
	}

	Bool GraphDescriptorEditor::RemoveVariable( GraphVariableId variableId )
	{
		return m_graph->RemoveVariable(variableId);
	}

	THandle< ISerializable > GraphDescriptorEditor::GetVariableData( GraphVariableId variableId )
	{
		return m_graph->GetVariableData(variableId);
	}

	void GraphDescriptorEditor::GetVariableType( GraphVariableId variableId, String& type )
	{
		m_graph->GetVariableType(variableId, type);
	}

	void GraphDescriptorEditor::GetVariableName( GraphVariableId variableId, String& name )
	{
		m_graph->GetVariableName( variableId, name );
	}

	Bool GraphDescriptorEditor::SetVariableName( GraphVariableId variableId, const String& name )
	{
		return m_graph->SetVariableName( variableId, name );
	}

	const String& GraphDescriptorEditor::GetName() const
	{
		return m_graph->GetName();
	}

	const String GraphDescriptorEditor::GetOwnerEditorName() const
	{
		const IResourceEditor* editor = GetEditor();
		if ( editor )
		{
			ResourceEditorPtr owner = editor->GetBackend()->GetResourceEditorByPath( m_graph->GetResourcePath() );
			if ( owner )
			{
				return owner->GetPath().ToString();
			}
		}

		return EditorGraphBase::GetOwnerEditorName();
	}

	Bool GraphDescriptorEditor::IsTopDownTree() const
	{
		return m_graph->IsTopDownTree();
	}

	Bool GraphDescriptorEditor::HasClipboardSupport() const
	{
		return m_graph->HasClipboardSupport();
	}

	Bool GraphDescriptorEditor::HasCloningSupport() const
	{
		return m_graph->HasCloningSupport();
	}

	///////////////////////////////////////////////////////////////////////
	// Components interface

	GraphComponentId GraphDescriptorEditor::CreateNodeComponent( GraphId nodeId, const String& type )
	{
		return m_graph->CreateNodeComponent( nodeId, type );
	}

	GraphComponentId GraphDescriptorEditor::CreateSocketComponent( GraphId nodeId, GraphSocketId socketId, const String& type )
	{
		return m_graph->CreateSocketComponent( nodeId, socketId, type );
	}

	GraphComponentId GraphDescriptorEditor::CreateConnectionComponent( GraphId connectionId, const String& type )
	{
		return m_graph->CreateConnectionComponent( connectionId, type );
	}

	void GraphDescriptorEditor::GetNodeComponents( GraphId nodeId, red::DynArray< GraphComponentId >& outComponents ) const
	{
		return m_graph->GetNodeComponents( nodeId, outComponents );
	}

	void GraphDescriptorEditor::GetSocketComponents( GraphId nodeId, GraphSocketId socketId, red::DynArray< GraphComponentId >& outComponents ) const
	{
		return m_graph->GetSocketComponents( nodeId, socketId, outComponents );
	}

	void GraphDescriptorEditor::GetConnectionComponents( GraphId connectionId, red::DynArray< GraphComponentId >& outComponents ) const
	{
		return m_graph->GetConnectionComponents( connectionId, outComponents );
	}

	THandle< GraphComponentDescription > GraphDescriptorEditor::GetNodeComponent( GraphId nodeId, GraphComponentId componentId ) const
	{
		return m_graph->GetNodeComponent( nodeId, componentId );
	}

	THandle< GraphComponentDescription > GraphDescriptorEditor::GetSocketComponent( GraphId nodeId, GraphSocketId socketId, GraphComponentId componentId ) const
	{
		return m_graph->GetSocketComponent( nodeId, socketId, componentId );
	}

	THandle< GraphComponentDescription > GraphDescriptorEditor::GetConnectionComponent( GraphId connectionId, GraphComponentId componentId ) const
	{
		return m_graph->GetConnectionComponent( connectionId, componentId );
	}

	//////////////////////////////////////////////////////////////////////////
	// Recursive graph listeners

	void GraphDescriptorEditor::AttachRecursiveListener( IEditorGraphListener* listener )
	{
		if ( listener )
		{
			AttachListener( listener );
			red::alg::PushBackUnique( m_recursiveGraphListners, listener );
		}
	}

	void GraphDescriptorEditor::DetachRecursiveListener( IEditorGraphListener* listener )
	{
		DetachListener( listener );

		if ( m_graph )
			m_graph->DetachRecursiveListener( listener );
	}

	void GraphDescriptorEditor::DetachListener( IEditorGraphListener* listener )
	{
		m_recursiveGraphListners.Remove( listener );
		Super::DetachListener( listener );
	}

} // namespace tools

