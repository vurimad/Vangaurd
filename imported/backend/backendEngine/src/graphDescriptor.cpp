/*
* Copyright (c) 2016-2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "graphDescriptor.h"
#include "graphEditorNode.h"
#include "editorGraphBase.h"
#include "editorGraphNodeClassesDescriptor.h"
#include "editorGraphObjectTree.h"
#include "graphEditorConnection.h"
#include "editorGraphTypes.h"
#include "../../common/redReflection/include/resource.h"
#include "../../common/resource/include/2darray.h"

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( IGraphDescriptor, tools );
	RTTI_PARENT_TYPE( IBackendData );
	RTTI_PROPERTY( m_connections );
	RTTI_PROPERTY( m_nodes );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( IGraphDescriptorGlobalData, tools );
	RTTI_PARENT_TYPE( IBackendData );
RTTI_END_TYPE();

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( IGraphValidator, tools );
RTTI_END_TYPE();

namespace tools
{
	namespace
	{
		const String c_ignored = "ignored";
		const String c_internal = "internal";
		
		Bool ExtractProperIncommingAndOutgoingConnections( const IGraphNodeDescriptor& node, const IGraphDescriptor& graph, const red::DynArray< GraphId >& connectionIds,
			red::DynArray< ConnectionDescription >& incomming, red::DynArray< ConnectionDescription >& outgoing )
		{
			for( const GraphId connectionId : connectionIds )
			{
				THandle< ConnectionDescription > connection = graph.GetConnection( connectionId );
				if( !connection )
				{
					return false;
				}

				ConnectionDescription newConnection = *connection;

				if( newConnection.m_sourceNode == node.GetId() )
				{
					if( THandle< SocketDescription > socket = node.GetSocketDescription( connection->m_sourceSocketIndex ) )
					{
						if( socket->m_direction == SocketDirection::Input )
						{
							std::swap( newConnection.m_sourceNode, newConnection.m_destinationNode );
							std::swap( newConnection.m_sourceSocketIndex, newConnection.m_destinationSocketIndex );
						}
					}
					else
					{
						return false;
					}
				}
				else
				{
					if( THandle< SocketDescription > socket = node.GetSocketDescription( connection->m_destinationSocketIndex ) )
					{
						if( socket->m_direction == SocketDirection::Output )
						{
							std::swap( newConnection.m_sourceNode, newConnection.m_destinationNode );
							std::swap( newConnection.m_sourceSocketIndex, newConnection.m_destinationSocketIndex );
						}
					}
					else
					{
						return false;
					}
				}

				if( newConnection.m_sourceNode == node.GetId() )
				{
					outgoing.EmplaceBack( newConnection );
				}
				else
				{
					incomming.EmplaceBack( newConnection );
				}
			}

			return true;
		}
	}

	IGraphDescriptorGlobalData::IGraphDescriptorGlobalData()
		: m_id( 0 ) 
	{ 
	}

	Bool IGraphDescriptorGlobalData::Initialize(const THandle< CResource >& graphRes)
	{
		return OnInitialize(graphRes);
	}

	//----

	IGraphDescriptor::IGraphDescriptor()
		: m_nodesMap( nullptr )
		, m_isInitializedForEdition( false )
		, m_subGraphId( InvalidSubGraphId )
		, m_idFactory( 0 )
		, m_graphListner{ red::PoolBackend() }
		, m_connectionsComponents{ red::PoolBackend() }
	{
	}

	IGraphDescriptor::~IGraphDescriptor()
	{
		if ( m_nodesMap )
		{
			RED_DELETE( m_nodesMap );
			m_nodesMap = nullptr;
		}
	}

	void IGraphDescriptor::MarkModified()
	{
		THandle< CResource > resource = m_parentResource.ToHandle();
		if ( resource )
			resource->MarkModified();
	}

	const res::ResourcePath IGraphDescriptor::GetResourcePath() const
	{
		THandle< CResource > resource = m_parentResource.ToHandle();
		if ( resource )
			return resource->GetPath();
		return res::ResourcePath();
	}

	void IGraphDescriptor::AttachListener( IEditorGraphListener* listener )
	{
		RED_ASSERT( !m_graphListner.Exist( listener ), "Listener already registered" );

		if ( listener && !m_graphListner.Exist( listener ) )
		{
			m_graphListner.PushBack( listener );
		}
	}

	void IGraphDescriptor::DetachListener( IEditorGraphListener* listener )
	{
		m_graphListner.Remove( listener );
	}

	void IGraphDescriptor::DetachRecursiveListener( IEditorGraphListener* listener )
	{
		DetachListener( listener );
		
		for ( const THandle< IGraphNodeDescriptor >& node : m_nodes.GetNodesList() )
		{
			if ( IGraphDescriptor* subgraph = node->GetSubGraphDescriptor() )
			{
				subgraph->DetachRecursiveListener( listener );
			}
		}
	}

	void IGraphDescriptor::InitializeIdAllocator( Uint64 highestPreallocatedGraphId )
	{
		// find current ID, if it's not serialized it's safer
		for ( auto& node : m_nodes.GetNodesList() )
		{
			if ( node->GetId() > m_idFactory )
				m_idFactory = node->GetId();
		}

		for ( auto& connection : m_connections.GetConnectionsMap() )
		{
			if ( connection.Key() > m_idFactory )
				m_idFactory = connection.Key();
		}
		
		if ( highestPreallocatedGraphId != InvalidGraphId )
		{
			m_idFactory = std::max( m_idFactory, highestPreallocatedGraphId );
		}
		
		++m_idFactory;
	}

	Bool IGraphDescriptor::Initialize( const THandle< CResource >& graphResource, const THandle< IGraphNodeDescriptor > parentNode, Uint64 highestPreallocatedGraphId )
	{
		// this stuff may vary between repeated initializations
		m_ownerNode = parentNode;

		// set a readable name for this (sub)graph instance, WCTODO: it may be required to make it more unique than just caption
		if ( parentNode )
			m_name = parentNode->GetCaption();
		else
			m_name = "Root";

		if ( parentNode && parentNode->GetGraphDescriptor() && parentNode->GetGraphDescriptor()->GetGlobalData() )
			PropagateGlobalData( parentNode->GetGraphDescriptor()->GetGlobalData() );

		THandle< CResource > parentResource = m_parentResource.ToHandle();
		if ( !parentResource )
		{
			SetParentResource( graphResource );
		}

		if ( m_isInitializedForEdition )
		{
			return true;
		}

		// the rest should be done only once

		InitializeIdAllocator( highestPreallocatedGraphId );

		if ( m_nodesMap )
		{
			RED_DELETE( m_nodesMap );
			m_nodesMap = nullptr;
		}

		m_nodesMap = RED_NEW( GraphDescriptorNodesMap )( m_nodes );

		// inspect nodes, so they all can initialize themselves
		struct Inspector : public NodeInspector
		{
			IGraphDescriptor&			m_graph;
			Bool						m_failed;

			Inspector( IGraphDescriptor& graph )
				: m_graph( graph )
				, m_failed( false ) {}

			virtual void InspectNode( IGraphNodeDescriptor* node ) override
			{
				m_failed |= ( node->Initialize( m_graph, false ) != true );
				m_graph.m_nodesMap->AttachNode( node->HandleFromThis< IGraphNodeDescriptor >() );
			}
		} inspector( *this );

		InspectNodes( inspector );

		if ( !OnInitialized( graphResource ) )
		{
			return false;
		}

		if ( inspector.m_failed )
		{
			return false;
		}

		m_isInitializedForEdition = true;
		
		return true;
	}

	Bool IGraphDescriptor::OnInitialized( const THandle< CResource >& graphRes )
	{
		return true;
	}

	Bool IGraphDescriptor::Create( const THandle< CResource >& graphRes, THandle< IGraphDescriptorGlobalData > globalData, Bool mappingOnly )
	{
		SetParentResource( graphRes );

		// WCTODO: this should be removed from Create, PropagateGlobalData should be used instead
		//ASSERT( globalData != nullptr );
		m_globalData = globalData;

		// Here could go a virtual code that would validate the graph, and forcefully recreate it if necessary
		return OnCreate( graphRes, mappingOnly );
	}

	void IGraphDescriptor::SetParentResource( const THandle< CResource >& graphResource )
	{
		m_parentResource = graphResource;

		m_resourceMonitor = red::CreateUniquePtr< res::Monitor >();
		m_resourceMonitor->Bind( graphResource->GetPath() );
		m_resourceMonitor->SetCallback( [ this ]( const res::ResourcePath& path, const CName& message, const res::MonitorUserDataPtr& userData )
		{
			if ( message == RED_NAME_CONSTEXPR_NOREG( "reload" ) )
			{
				auto newResource = res::GetMonitorUserData< THandle< CResource > >( userData );
				m_parentResource = newResource;
			}
		} );
	}

	Bool IGraphDescriptor::Validate( const IGraphValidator& validator, IGraphValidationContext& context, GraphValidatorOutput& output )
	{
		Bool result = OnValidate( validator, context, output );

		for ( const auto& node : m_nodes.GetNodesList() )
		{
			result &= node->Validate( validator, context, output );
		}

		return result;
	}

	void IGraphDescriptor::Open()
	{
		OnOpened();
	}

	Bool IGraphDescriptor::OnCreate( const THandle< CResource >& graphRes, Bool mappingOnly )
	{
		return true;
	}

	void IGraphDescriptor::PropagateGlobalData( const THandle< IGraphDescriptorGlobalData >& globalData )
	{
		// we can access the same graph descriptor from two separate editors, we only let the first one assign its global data, because it's the owner
		if ( !m_globalData.ToHandle() )
		{
			m_globalData = globalData;
			OnPropagateGlobalData( globalData );
		}
	}

	void IGraphDescriptor::OnPropagateGlobalData( const THandle< IGraphDescriptorGlobalData >& globalData )
	{
	}

	Bool IGraphDescriptor::OnValidate( const IGraphValidator& validator, const IGraphValidationContext& context, GraphValidatorOutput& output )
	{
		return true;
	}

	void IGraphDescriptor::OnOpened()
	{
		class QuestNodeInspector : public NodeInspector
		{
			virtual void InspectNode( IGraphNodeDescriptor* node ) override
			{
				node->OnGraphOpened();
			}
		} inspector;

		InspectNodes( inspector );
	}

	Bool IGraphDescriptor::IsClassSupported( const rtti::ClassType* classType )
	{
		red::DynArray< const rtti::ClassType* > rootClasses{ red::PoolFrame() };

		GatherRootNodesClasses( rootClasses );
		Bool hasRootClass = false;
		for ( const rtti::ClassType* rootClassType : rootClasses )
		{
			if ( classType->IsA( rootClassType ) )
			{
				hasRootClass = true;
				break;
			}
		}

		red::DynArray< const rtti::ClassType* > directClasses{ red::PoolFrame() };
		GatherNodesClasses(directClasses);
		for (auto theClass : directClasses)
		{
			if (classType->IsA(theClass))
			{
				hasRootClass = true;
				break;
			}
		}

		if ( !hasRootClass )
		{
			return false;
		}

		if ( !IsRootSubClassSupported( classType ) )
		{
			return false;
		}

		return true;
	}

	Bool IGraphDescriptor::IsRootSubClassSupported(const rtti::ClassType* classType ) const
	{
		return true;
	}

	Bool IGraphDescriptor::IsRuntimeNodeSupported( const THandle< IGraphNodeDescriptor >& defaultNode ) const
	{
		return true;
	}

	Bool IGraphDescriptor::IsValidatorClassSupported( const rtti::ClassType* ) const
	{
		return false;
	}

	THandle< CNodeClassesDescriptorStructure > IGraphDescriptor::GenerateSupportedNodesStructure() const
	{
		struct Local
		{
			const IGraphDescriptor&								m_this;
			CNodeClassesDescriptorStructure::Builder&			m_builder;
			red::DynArray< const rtti::ClassType* >				m_subClasses;

			Local( const IGraphDescriptor& me, CNodeClassesDescriptorStructure::Builder& builder )
				: m_this( me )
				, m_builder( builder )
				, m_subClasses{ red::PoolFrame() }
			{
			}

			void IterateNodeDescriptorClassesRec( const rtti::ClassType& nodeDescriptorClass )
			{
				IGraphNodeDescriptor* nodeDescriptor = nodeDescriptorClass.GetDefaultObject< IGraphNodeDescriptor >();
				if ( nodeDescriptor )
				{
					const rtti::ClassType* rootRepresentationClass = nodeDescriptor->GetRootRepresentationClass();
					if ( rootRepresentationClass )
					{
						m_builder.MapRttiClassHierarchy( *rootRepresentationClass );
					}

					const rtti::ClassType* representationClass = nodeDescriptor->GetRepresentationClass();
					if ( representationClass )
					{
						if ( m_this.IsRootSubClassSupported( &nodeDescriptorClass ) )
						{
							m_builder.MarkRttiRepresentationClass( *representationClass, nodeDescriptorClass.GetName() );
						}
						else
						{
							m_builder.MarkRttiRepresentationClassDisabled( *representationClass );
						}
					
					}
				}
			
				// recursive calls
				{
					Uint32 subclassesSize = m_subClasses.Size();

					GetRttiSystem().EnumDerivedClasses( &nodeDescriptorClass, m_subClasses );

					for ( Uint32 i = subclassesSize, n = m_subClasses.Size(); i != n; ++i )
					{
						IterateNodeDescriptorClassesRec( *m_subClasses[ i ] );
					}

					m_subClasses.Resize( subclassesSize );
				}
			}
		};


		// allocate structure
		THandle< CNodeClassesDescriptorStructure > structurePtr = CreateHandle< CNodeClassesDescriptorStructure >();

		// build
		{
			CNodeClassesDescriptorStructure::Builder builder( *structurePtr.Get() );

			{
				Local local( *this, builder );

				GatherRootNodesClasses( local.m_subClasses );

				for ( Uint32 i = 0, n = local.m_subClasses.Size(); i !=n; ++i )
				{
					local.IterateNodeDescriptorClassesRec( *local.m_subClasses[ i ] );
				}
			}

			builder.SpreadMarking();
		}

		return structurePtr;
	}

	void IGraphDescriptor::FillClassTree( GraphObjectTreeBuilder& treeBuilder )
	{
		THandle< CNodeClassesDescriptorStructure > structure = GenerateSupportedNodesStructure();

		for ( auto& root : structure->GetRoots() )
		{
			treeBuilder.AddItem( root->GetName(), root->GetClassName().AsChar() );
			treeBuilder.AddChildren( root, root->GetName() );
		}
	}

	void IGraphDescriptor::FillImportTree( GraphObjectTreeBuilder& treeBuilder ) const
	{
	}

	void IGraphDescriptor::InspectNodes( NodeInspector& nodeInspector )
	{
		for ( const THandle< IGraphNodeDescriptor >& node : m_nodes.GetNodesList() )
		{
			if ( IGraphNodeDescriptor* n = node.Get() )
			{
				nodeInspector.InspectNode( n );
			}
		}
	}
	
	void IGraphDescriptor::IterateNodes( std::function<void( IGraphNodeDescriptor* )> preVisitor, std::function<void( IGraphNodeDescriptor* )> postVisitor )
	{
		auto nodes = m_nodes.GetNodesList();
		for ( THandle< IGraphNodeDescriptor > node : nodes )
		{
			if ( preVisitor )
			{
				preVisitor( node.Get() );
			}

			IGraphDescriptor* subgraph = node->GetSubGraphDescriptor();
			if ( subgraph )
			{
				subgraph->IterateNodes( preVisitor, postVisitor );
			}

			if ( postVisitor )
			{
				postVisitor( node.Get() );
			}
		}
	}

	// Node creation
	THandle< IGraphNodeDescriptor > IGraphDescriptor::CreateNode( const THandle< ISerializable >& context, const rtti::ClassType* descriptorClass, const String& actualType, Bool forceOperation, Point position )
	{
		// check if given class is supported
		if ( !forceOperation && !IsClassSupported( descriptorClass ) )
		{
			return nullptr;
		}

		// first lets create corresponding descriptor node class
		THandle< IGraphNodeDescriptor > node( descriptorClass->CreateObject< IGraphNodeDescriptor >() );

		if ( !node )
		{
			return nullptr;
		}

		node->m_id = AllocateGraphId();
		node->m_position = position;

		if ( !node->Create( context, *this, actualType, false ) )
		{
			return nullptr;
		}

		if ( !node->Initialize( *this, false ) )
		{
			return nullptr;
		}

		AttachNode( node );

		OnNodeCreated( node );

		MarkModified();

		return node;
	}

	Bool IGraphDescriptor::CanRemoveNode( GraphNodeId nodeId, Bool isCut ) const
	{
		THandle< IGraphNodeDescriptor > node = GetNode( nodeId );
		if ( !node )
		{
			return false;
		}

		return CanRemoveNode( node, isCut );
	}

	Bool IGraphDescriptor::CanRemoveNode( const THandle< IGraphNodeDescriptor >& node, Bool isCut ) const
	{
		if ( !node )
		{
			return false;
		}

		if ( !node->CanBeRemoved( isCut ) )
		{
			return false;
		}

		return true;
	}

	void IGraphDescriptor::AttachNode( const THandle< IGraphNodeDescriptor >& node )
	{
		m_nodes.AttachNode( node );

		// If possible, cache it (if not, then it will be cached anyways when the graph is initialized
		if ( m_nodesMap )
		{
			m_nodesMap->AttachNode( node );
		}
	}

	THandle<tools::IGraphNodeDescriptor> IGraphDescriptor::GetNode(GraphNodeId id)
	{
		if (m_nodesMap)
		{
			return m_nodesMap->GetNode(id);
		}
		else
		{
			auto nodes = m_nodes.GetNodesList();
			for (auto node : nodes)
			{
				if (node->GetId() == id)
				{
					return node;
				}
			}
		}

		return THandle<tools::IGraphNodeDescriptor>(nullptr);
	}

	const THandle<tools::IGraphNodeDescriptor> IGraphDescriptor::GetNode(GraphNodeId id) const
	{
		if (m_nodesMap)
		{
			return m_nodesMap->GetNode(id);
		}
		else
		{
			auto nodes = m_nodes.GetNodesList();
			for (auto node : nodes)
			{
				if (node->GetId() == id)
				{
					return node;
				}
			}
		}

		return THandle<tools::IGraphNodeDescriptor>(nullptr);
	}

	Bool IGraphDescriptor::RemoveNode(GraphNodeId nodeId, Bool forceOperation)
	{
		THandle< IGraphNodeDescriptor > node = GetNode( nodeId );
		if ( !node )
		{
			return false;
		}

		if ( !forceOperation && !CanRemoveNode( node, false ) )
		{
			return false;
		}

		if ( !RemoveAllConnections( node ) )
		{
			return false;
		}

		node->Remove();

		// remove node from cache structures
		DetachNode( node );

		OnNodeRemoved( node );

		MarkModified();

		return true;
	}

	Bool IGraphDescriptor::RestoreNode( const THandle< IGraphNodeDescriptor >& node )
	{
		AttachNode( node );

		if ( !node->Restore( *this ) )
		{
			DetachNode( node );
			return false;
		}

		return true;
	}

	void IGraphDescriptor::DistributeNode( const THandle< IGraphNodeDescriptor >& node )
	{
		// TODO: this is super slow and should be rewritten ( optimized space search is required )
		for ( const THandle< IGraphNodeDescriptor >& otherNode : m_nodes.GetNodesList() )
		{
			if ( otherNode == node )
				continue;

			if ( otherNode->m_position == node->m_position )
			{
				node->m_position += Point( 10, 10 );
				DistributeNode( node );
				break;
			}
		}
	}

	void IGraphDescriptor::ReadImportsFile( const THandle< C2dArray >& itemsArray, GraphObjectTreeBuilder& treeBuilder ) const
	{
		const Uint32 numberOfRows = itemsArray->GetNumberOfRows();
		for ( Uint32 rowIndex = 0; rowIndex < numberOfRows; ++rowIndex )
		{
			const String name = itemsArray->GetValue( "name", rowIndex );
			const String importPath = itemsArray->GetValue( "importPath", rowIndex );
			const String category = itemsArray->GetValue( "category", rowIndex );

			if ( !( importPath.Empty() || category.Empty() ) )
			{
				if ( !name.Empty() && category != c_ignored && category != c_internal )
				{
					treeBuilder.AddItem( name, importPath, category );
				}
			}
		}
	}


	Bool IGraphDescriptor::PlaceNode( const THandle< IGraphNodeDescriptor >& clone, Bool isInPlace )
	{
		clone->m_id = AllocateGraphId();
		AttachNode( clone );

		if ( !clone->Place( *this ) )
		{
			DetachNode( clone );
			return false;
		}

		if ( isInPlace )
			DistributeNode( clone );

		return true;
	}

	void IGraphDescriptor::DetachNode( const THandle< IGraphNodeDescriptor >& node )
	{
		m_nodes.RemoveNode( node );
		if ( m_nodesMap )
		{
			m_nodesMap->RemoveNode( node );
		}
	}

	void IGraphDescriptor::SetSubGraphId( GraphSubGraphId subGraphId )
	{
		GraphSubGraphId oldId = m_subGraphId;
		m_subGraphId = subGraphId;
		NotifyGraphIdChanged( oldId, subGraphId );
	}

	GraphId	IGraphDescriptor::AllocateGraphId()
	{		
		return m_idFactory++;
	}

	GraphId IGraphDescriptor::GetLastId() const
	{
		return m_idFactory - 1;
	}

	Bool IGraphDescriptor::CanCreateConnection( const ConnectionCreationInfo& info ) const
	{
		THandle< IGraphNodeDescriptor > n1 = GetNode( info.m_sourceNodeId );
		THandle< IGraphNodeDescriptor > n2 = GetNode( info.m_destinationNodeId );

		if ( !n1 || !n2 )
		{
			return false;
		}

		// WCTODO: there should be a proper way to refresh sockets in runtime nodes
		n1->GetSocketsCount();
		n2->GetSocketsCount();

		// forward test to nodes implementation
		return IGraphNodeDescriptor::CanConnect( SocketInfo( n1, info.m_sourceSocketId ), SocketInfo( n2, info.m_destinationSocketId ) );
	}

	Bool IGraphDescriptor::CanCreateConnection( GraphNodeId outputNodeId, GraphSocketId outputSocketId, const THandle< IGraphNodeDescriptor >& defaultDescriptor ) const
	{
		THandle< IGraphNodeDescriptor > node = GetNode( outputNodeId );

		if ( !node )
		{
			return false;
		}

		for ( IGraphNodeDescriptor::SocketIndex index = 0; index < defaultDescriptor->GetSocketsCount(); ++index )
		{
			if ( IGraphNodeDescriptor::CanConnect( SocketInfo( node, outputSocketId ), SocketInfo( defaultDescriptor, index ) ) )
				return true;
		}

		return false;
	}

	Bool IGraphDescriptor::CanCreateConnection( GraphNodeId outputNodeId, GraphSocketId outputSocketId, const THandle< IGraphNodeDescriptor >& defaultDescriptor, GraphSocketId inputSocketId ) const
	{
		THandle< IGraphNodeDescriptor > node = GetNode( outputNodeId );

		if ( !node )
		{
			return false;
		}

		return IGraphNodeDescriptor::CanConnect( SocketInfo( node, outputSocketId ), SocketInfo( defaultDescriptor, inputSocketId ) )
			|| IGraphNodeDescriptor::CanConnect( SocketInfo( defaultDescriptor, inputSocketId ), SocketInfo( node, outputSocketId ) );
	}

	Bool IGraphDescriptor::CanCreateIntermediateNode( const ConnectionCreationInfo& info, const THandle< IGraphNodeDescriptor >& defaultDescriptor ) const
	{
		THandle< IGraphNodeDescriptor > sourceNode = GetNode( info.m_sourceNodeId );
		THandle< IGraphNodeDescriptor > destinationNode = GetNode( info.m_destinationNodeId );

		if ( !sourceNode || !destinationNode )
		{
			return false;
		}

		Uint32 socketsCount = defaultDescriptor->GetSocketsCount();

		if ( socketsCount <= 1 )
		{
			return false;
		}

		SocketInfo sourceInfo( sourceNode, info.m_sourceSocketId );
		SocketInfo destinationInfo( destinationNode, info.m_destinationSocketId );

		for ( IGraphNodeDescriptor::SocketIndex index1 = 0; index1 < socketsCount; ++index1 )
		{
			for ( IGraphNodeDescriptor::SocketIndex index2 = 0; index2 < socketsCount; ++index2 )
			{
				if ( IGraphNodeDescriptor::CanConnect( sourceInfo, SocketInfo( defaultDescriptor, index1 ) ) &&
					 IGraphNodeDescriptor::CanConnect( SocketInfo( defaultDescriptor, index2 ), destinationInfo ) )
				{
					return true;
				}
			}
		}

		return false;
	}

	Bool IGraphDescriptor::CanRemoveIntermediateNode( GraphNodeId nodeId, red::DynArray< ConnectionCreationInfo >& outConnectionInfos ) const
	{
		if( !CanRemoveNode( nodeId, false ) )
		{
			return false;
		}

		THandle< IGraphNodeDescriptor > node = GetNode( nodeId );

		// Conditions to be able to remove intermediate node:
		//		1. We need to be able to remove all current connections.
		//		2. All output connections need to go to the same node.
		//		3. All input connections need to be connected to that node (we need to be able to create them)

		red::DynArray< GraphId > allConnectionIds;

		// When making a connection, direction in which user does the connection matters, so we can have both connections as source or destination.
		for( auto sourceConnections = node->GetSourceConnections( false ); sourceConnections; ++sourceConnections )
		{
			allConnectionIds.PushBack( sourceConnections->m_id );
		}
		for( auto destinationConnections = node->GetDestinationConnections( false ); destinationConnections; ++destinationConnections )
		{
			allConnectionIds.PushBack( destinationConnections->m_id );
		}

		// Condition 1
		for( const GraphId connectionId : allConnectionIds )
		{
			THandle< const ConnectionDescription > connection = GetConnection( connectionId );
			if( !connection || connection->m_isDirectional || !CanRemoveConnection( connectionId ) )
			{
				return false;
			}
		}

		red::DynArray< ConnectionDescription > outConnections;
		outConnections.Reserve( allConnectionIds.Size() );
		red::DynArray< ConnectionDescription > inConnections;
		inConnections.Reserve( allConnectionIds.Size() );

		// Store connection descriptions that have m_sourceNode as the node that generates the signal and m_destinationNode as the node that receives the signal.
		if( !ExtractProperIncommingAndOutgoingConnections( *node, *this, allConnectionIds, inConnections, outConnections ) )
		{
			return false;
		}

		if( inConnections.Empty() || outConnections.Empty() )
		{
			return false;
		}

		// We only support remove intermediate if we have one incomming or one outgoing connection
		// Other cases would create too many connections and may not be helpful for users (implementation supports it)
#if 1
		// Try yo merge outgoing connections into one
		for( Uint32 i = outConnections.Size() - 1; i > 1; --i )
		{
			const ConnectionDescription& connectionA = outConnections[i];
			const ConnectionDescription& connectionB = outConnections[i - 1];

			// Done by ExtractProperIncommingAndOutgoingConnections
			RED_ASSERT( connectionA.m_sourceNode == nodeId );
			RED_ASSERT( connectionB.m_sourceNode == nodeId );

			// Connected to same node, we can treat them as one connection
			if( connectionA.m_destinationNode == connectionB.m_destinationNode &&
				connectionA.m_destinationSocketIndex == connectionB.m_destinationSocketIndex )
			{
				outConnections.RemoveAtReorder( i );
			}
		}
		
		// Try yo merge incomming connections into one
		for( Uint32 i = inConnections.Size() - 1; i > 1; --i )
		{
			const ConnectionDescription& connectionA = inConnections[i];
			const ConnectionDescription& connectionB = inConnections[i - 1];

			// Done by ExtractProperIncommingAndOutgoingConnections
			RED_ASSERT( connectionA.m_destinationNode == nodeId );
			RED_ASSERT( connectionB.m_destinationNode == nodeId );

			// Connected to same node, we can treat them as one connection
			if( connectionA.m_sourceNode == connectionB.m_sourceNode &&
				connectionA.m_sourceSocketIndex == connectionB.m_sourceSocketIndex )
			{
				inConnections.RemoveAtReorder( i );
			}
		}

		// Only enabled if we have one incomming or one outgoing connection
		// Otherwise the number of connections to create would be ( numIncomming * numOutgoing ), this can mess up the graph
		if( inConnections.Size() != 1 && outConnections.Size() != 1 )
		{
			return false;
		}
#endif

		// Connect all incomming connections to the node that we are connected to
		outConnectionInfos.Reserve( inConnections.Size() * outConnections.Size() );

		const ConnectionDescription outConnection = outConnections.Front();	// only one
		for( const ConnectionDescription& inConnection : inConnections )
		{
			RED_ASSERT( inConnection.m_destinationNode == nodeId );	// Done by ExtractProperIncommingAndOutgoingConnections

			for( const ConnectionDescription& outConnection : outConnections )
			{
				RED_ASSERT( outConnection.m_sourceNode == nodeId );	// Done by ExtractProperIncommingAndOutgoingConnections

				ConnectionCreationInfo newConnection;
				newConnection.m_sourceNodeId = inConnection.m_sourceNode;
				newConnection.m_sourceSocketId = inConnection.m_sourceSocketIndex;
				newConnection.m_destinationNodeId = outConnection.m_destinationNode;
				newConnection.m_destinationSocketId = outConnection.m_destinationSocketIndex;
				newConnection.m_isDisabled = inConnection.m_isDisabled || outConnection.m_isDisabled;
				newConnection.m_isInactive = inConnection.m_isInactive || outConnection.m_isInactive;

				const auto it = std::find_if( outConnectionInfos.Begin(), outConnectionInfos.End(),
					[newConnection]( const ConnectionCreationInfo& toCreateConnection )
				{
					return toCreateConnection.m_sourceNodeId == newConnection.m_sourceNodeId && toCreateConnection.m_sourceSocketId == newConnection.m_sourceSocketId &&
						toCreateConnection.m_destinationNodeId == newConnection.m_destinationNodeId && toCreateConnection.m_destinationSocketId == newConnection.m_destinationSocketId;
				} );

				if( it != outConnectionInfos.End() )	// We already pushed this connection to be created
				{
					it->m_isDisabled |= newConnection.m_isDisabled;
					it->m_isInactive |= newConnection.m_isInactive;
				}
				else
				{
					if( !CanCreateConnection( newConnection ) )
					{
						outConnectionInfos.Clear();
						return false;
					}

					outConnectionInfos.EmplaceBack( newConnection );
				}
			}
		}

		return true;
	}

	GraphId IGraphDescriptor::CreateConnection( const ConnectionCreationInfo& info, Bool force )
	{
		// double check
		if ( !force && !CanCreateConnection( info ) )
		{
			return InvalidGraphId;
		}

		// get node's by their ids
		THandle< IGraphNodeDescriptor > n1 = GetNode( info.m_sourceNodeId );
		THandle< IGraphNodeDescriptor > n2 = GetNode( info.m_destinationNodeId );

		THandle< ConnectionDescription > descriptor = CreateHandle< ConnectionDescription >( info.m_sourceNodeId, info.m_sourceSocketId, info.m_destinationNodeId, info.m_destinationSocketId );
		descriptor->m_id = AllocateGraphId();
		descriptor->m_isInactive = info.m_isInactive;
		descriptor->m_isDisabled = info.m_isDisabled;

		// let the graph user fill all the necessary data
		OnConnectionCreated( descriptor );

		// update cache structure before we run node-level callback
		m_connections.AddConnection( descriptor );

		// run callback on graph nodes
		if( !info.m_isDisabled )
		{
			IGraphNodeDescriptor::Connect( SocketInfo(n1, descriptor->m_sourceSocketIndex), SocketInfo(n2, descriptor->m_destinationSocketIndex), descriptor->m_id );
		}

		NotifyConnectionCreated( descriptor->m_id );

		MarkModified();

		return descriptor->m_id;
	}

	Bool IGraphDescriptor::RemoveAllConnections( const THandle< IGraphNodeDescriptor >& node )
	{
		red::DynArray< GraphId > ids{ red::PoolFrame() };

		// collect all outcoming connection ids
		for ( ConnectionsIterator it = node->GetSourceConnections( false ); it; ++it )
			ids.PushBack( it->m_id );
			
		// collect all incoming connection ids
		for ( ConnectionsIterator it = node->GetDestinationConnections( false ); it; ++it )
			ids.PushBack( it->m_id );

		Bool result = true;
		for ( GraphConnectionId id : ids )
			result &= RemoveConnection( id );

		return result;
	}

	Bool IGraphDescriptor::RemoveAllConnectionsExcept(const THandle< IGraphNodeDescriptor >& node, GraphId exceptionConnectionId)
	{
		red::DynArray< GraphId > ids{ red::PoolFrame() };

		// collect all outcoming connection ids
		for (ConnectionsIterator it = node->GetSourceConnections(false); it; ++it)
			ids.PushBack(it->m_id);

		// collect all incoming connection ids
		for (ConnectionsIterator it = node->GetDestinationConnections(false); it; ++it)
			ids.PushBack(it->m_id);

		ids.Remove(exceptionConnectionId);

		Bool result = true;
		for (GraphConnectionId id : ids)
			result &= RemoveConnection(id);

		return result;
	}

	Bool IGraphDescriptor::CanRemoveConnection( GraphConnectionId connectionId ) const
	{
		THandle< ConnectionDescription > desc = m_connections.FindConnection( connectionId );
		if ( !desc )
		{
			return false;
		}

		THandle< IGraphNodeDescriptor > n1 = GetNode( desc->m_sourceNode );
		THandle< IGraphNodeDescriptor > n2 = GetNode( desc->m_destinationNode );
		if ( !n1 || !n2 )
		{
			return false;
		}

		// run callback on graph nodes
		return IGraphNodeDescriptor::CanRemoveConnection( SocketInfo( n1, desc->m_sourceSocketIndex ), SocketInfo( n2, desc->m_destinationSocketIndex ), connectionId );
	}

	Bool IGraphDescriptor::RemoveConnection( GraphConnectionId connectionId )
	{
		THandle< ConnectionDescription > desc = m_connections.FindConnection( connectionId );
		if ( !desc )
		{
			return false;
		}
		return RemoveConnection( connectionId, desc );
	}

	Bool IGraphDescriptor::RemoveConnection( GraphConnectionId connectionId, const THandle< ConnectionDescription >& desc )
	{
		if (CanRemoveConnection(connectionId) == false)
			return false;

		// update cache structure before we run node-level callback
		m_connections.RemoveConnection( connectionId );

		// check if the node is already disabled (so the logic is already not-effected)
		if ( !desc->m_isDisabled )
		{
			// get node's by their ids
			THandle< IGraphNodeDescriptor > n1 = GetNode( desc->m_sourceNode );
			THandle< IGraphNodeDescriptor > n2 = GetNode( desc->m_destinationNode );
			if ( !n1 || !n2 )
			{
				return false;
			}

			// run callback on graph nodes
			IGraphNodeDescriptor::RemoveConnection( SocketInfo( n1, desc->m_sourceSocketIndex ), SocketInfo( n2, desc->m_destinationSocketIndex ), connectionId );
		}

		NotifyConnectionRemoved( connectionId );

		MarkModified();

		return true;
	}

	Bool IGraphDescriptor::RestoreConnection( const THandle< ConnectionDescription >& connection, const THandle< ISerializable >& data )
	{
		// get node's by their ids
		THandle< IGraphNodeDescriptor > n1 = m_nodesMap->GetNode( connection->m_sourceNode );
		THandle< IGraphNodeDescriptor > n2 = m_nodesMap->GetNode( connection->m_destinationNode );

		if ( !( n1 && n2 ) )
			return false;

		// update cache structure before we run node-level callback
		m_connections.AddConnection( connection );

		if( !connection->m_isDisabled )
		{
			// Connect graph nodes
			if ( !OnRestoreConnection( SocketInfo( n1, connection->m_sourceSocketIndex ), SocketInfo( n2, connection->m_destinationSocketIndex ), connection->m_id, data ) )
			{
				m_connections.RemoveConnection( connection->m_id );
				return false;
			}
		}

		NotifyConnectionCreated( connection->m_id );

		MarkModified();

		return true;
	}

	Bool IGraphDescriptor::OnRestoreConnection( const SocketInfo& sourceInfo, const SocketInfo& destinationInfo, GraphId connectionId, const THandle< ISerializable >& data )
	{
		IGraphNodeDescriptor::Connect( sourceInfo, destinationInfo, connectionId );
		return true;
	}

	Bool IGraphDescriptor::OnPlaceConnection(const SocketInfo& sourceInfo, const SocketInfo& destinationInfo, GraphId connectionId, const THandle< ISerializable >& data)
	{
		return true;
	}

	Bool IGraphDescriptor::PlaceConnection(GraphConnectionId connectionId, const THandle< ConnectionDescription >& original, const THandle< ISerializable >& data)
	{
		THandle< ConnectionDescription > connection = m_connections.FindConnection( connectionId );
		
		if ( !connection )
			return false;

		connection->m_highlight = original->m_highlight;
		connection->m_highlightColor = original->m_highlightColor;
		connection->m_isDirectional = original->m_isDirectional;
		connection->m_isDisabled = original->m_isDisabled;
		connection->m_thickness = original->m_thickness;

		// get node's by their ids
		THandle< IGraphNodeDescriptor > n1 = m_nodesMap->GetNode(connection->m_sourceNode);
		THandle< IGraphNodeDescriptor > n2 = m_nodesMap->GetNode(connection->m_destinationNode);

		if (!(n1 && n2))
			return false;

		return OnPlaceConnection(SocketInfo(n1, connection->m_sourceSocketIndex), SocketInfo(n2, connection->m_destinationSocketIndex), connectionId, data);
	}

	Bool IGraphDescriptor::SetConnectionDisabled( GraphConnectionId connectionId, Bool disabled, TErrorFunc onError )
	{
		THandle< ConnectionDescription > desc = m_connections.FindConnection( connectionId );
		if ( !desc )
		{
			onError( "Connection not found in the cache." );
			return false;
		}

		if( desc->m_isDisabled == disabled )
		{
			// this is not an error
			return true;
		}

		// get node's by their ids
		THandle< IGraphNodeDescriptor > n1 = GetNode( desc->m_sourceNode );
		THandle< IGraphNodeDescriptor > n2 = GetNode( desc->m_destinationNode );
		if ( !n1 || !n2 )
		{
			onError( "Source or destination node does not exist." );
			return false;
		}

		desc->m_isDisabled = disabled;

		SocketInfo s1 = SocketInfo(n1, desc->m_sourceSocketIndex);
		SocketInfo s2 = SocketInfo(n2, desc->m_destinationSocketIndex);

		if ( disabled )
		{
			// run callback on graph nodes
			OnConnectionDisabled(s1, s2, connectionId);
			IGraphNodeDescriptor::RemoveConnection( s1, s2, connectionId );

			NotifyConnectionDisabled( connectionId );
		}
		else
		{
			// run callback on graph nodes
			OnConnectionEnabled(s1, s2, connectionId);
			IGraphNodeDescriptor::Connect( s1, s2, connectionId );

			NotifyConnectionEnabled( connectionId );
		}

		InvalidateConnectionDescriptionProperty( connectionId, "isDisabled" );

		MarkModified();

		return true;
	}

	Bool IGraphDescriptor::SetConnectionHighlight( GraphConnectionId connectionId, Color highlightColor )
	{
		THandle< ConnectionDescription > description = m_connections.FindConnection( connectionId );
		if ( !description )
		{
			return false;
		}

		description->m_highlight = true;
		description->m_highlightColor = highlightColor;

		InvalidateConnectionDescriptionProperty( connectionId, "highlight" );
		InvalidateConnectionDescriptionProperty( connectionId, "highlightColor" );
		return true;
	}

	Bool IGraphDescriptor::ClearConnectionHighlight( GraphConnectionId connectionId )
	{
		THandle< ConnectionDescription > description = m_connections.FindConnection( connectionId );
		if ( !description || !description->m_highlight )
		{
			return false;
		}

		description->m_highlight = false;
		description->m_highlightColor = Color::CLEAR();

		InvalidateConnectionDescriptionProperty( connectionId, "highlight" );
		InvalidateConnectionDescriptionProperty( connectionId, "highlightColor" );
		return true;
	}

	THandle< ISerializable > IGraphDescriptor::GetConnectionData( GraphConnectionId connectionId )
	{
		THandle< ConnectionDescription > desc = m_connections.FindConnection( connectionId );
		if ( !desc )
		{
			return nullptr;
		}

		THandle< IGraphNodeDescriptor > n1 = GetNode( desc->m_sourceNode );
		THandle< IGraphNodeDescriptor > n2 = GetNode( desc->m_destinationNode );
		if ( !n1 || !n2 )
		{
			return nullptr;
		}

		return GetConnectionData( SocketInfo( n1, desc->m_sourceSocketIndex ), SocketInfo( n2, desc->m_destinationSocketIndex ), connectionId);
	}

	THandle< ISerializable > IGraphDescriptor::GetConnectionData( const SocketInfo& source, const SocketInfo& destination, GraphConnectionId connectionId )
	{
		return nullptr;
	}

	THandle< ConnectionDescription > IGraphDescriptor::GetConnection( GraphConnectionId connectionId )
	{
		return m_connections.FindConnection( connectionId );
	}

	const THandle< ConnectionDescription > IGraphDescriptor::GetConnection( GraphConnectionId connectionId ) const
	{
		return m_connections.FindConnection( connectionId );
	}

	void IGraphDescriptor::OnPreSave( const PreSaveContext& context )
	{
		Super::OnPreSave( context );

		if ( IsInitialized() )
		{
			const GraphNodesList::NodesList& nodesList = m_nodes.GetNodesList();
			for ( auto it = nodesList.Begin(), end = nodesList.End(); it != end; ++it )
			{
				IGraphNodeDescriptor* node = it->Get();
				if ( node )
				{
					node->OnGraphPreSave();
				}
			}
		}
	}

	void IGraphDescriptor::OnPostLoad( const PostLoadContext& context )
	{
		m_nodes.Compact();
	}

	void IGraphDescriptor::OnSerialize( IFile& file )
	{
		if ( file.IsWriter() )
		{
			m_nodes.Compact();
			m_connections.Compact();
		}

		Super::OnSerialize( file );

		if ( file.IsReader() )
		{
			m_connections.Initialize();
		}
	}

	void IGraphDescriptor::OnNodeCreated( const THandle< IGraphNodeDescriptor >& node )
	{
	}

	void IGraphDescriptor::OnNodeRemoved( const THandle< IGraphNodeDescriptor >& node )
	{
	}

	void IGraphDescriptor::OnConnectionCreated( const THandle< ConnectionDescription >& description )
	{
	}

	void IGraphDescriptor::OnConnectionDataChanged( GraphId connectionId, const rtti::AccessPath& path )
	{
	}

	void IGraphDescriptor::NotifyGraphReloaded()
	{
		THandle< IGraphNodeDescriptor > ownerNode = GetGraphOwnerNode().ToHandle();
		GraphId nodeId = ownerNode ? ownerNode->GetId() : InvalidGraphId;
		GraphSubGraphId ownerSubgraphId = ownerNode ? ownerNode->GetGraphDescriptor()->GetSubGraphId() : InvalidGraphId;

		Forward( m_graphListner, [ this, ownerSubgraphId, nodeId ]( IEditorGraphListener* l ) { l->OnGraphReloaded( ownerSubgraphId, m_subGraphId, nodeId ); } );
	}

	void IGraphDescriptor::NotifyGraphIdChanged( GraphSubGraphId oldId, GraphSubGraphId newId )
	{
		Forward( m_graphListner, [ oldId, newId ] ( IEditorGraphListener* l ) { l->OnGraphIdChanged( oldId, newId ); } );
	}

	void IGraphDescriptor::NotifyNodeCreated( GraphNodeId nodeId )
	{
		Forward( m_graphListner, [ this, nodeId ] ( IEditorGraphListener* l ) { l->OnCreateNode( m_subGraphId, nodeId ); } );
	}

	void IGraphDescriptor::NotifyNodeRemoved( GraphNodeId nodeId )
	{
		Forward( m_graphListner, [ this, nodeId ] ( IEditorGraphListener* l ) { l->OnRemoveNode( m_subGraphId, nodeId ); } );
	}

	void IGraphDescriptor::NotifyConnectionCreated( GraphId connectionId )
	{
		Forward( m_graphListner, [ this, connectionId ] ( IEditorGraphListener* l ) { l->OnCreateConnection( m_subGraphId, connectionId ); } );
	}

	void IGraphDescriptor::NotifyConnectionRemoved( GraphId connectionId )
	{
		Forward( m_graphListner, [ this, connectionId ] ( IEditorGraphListener* l ) { l->OnRemoveConnection( m_subGraphId, connectionId ); } );
	}

	void IGraphDescriptor::NotifyConnectionEnabled( GraphId connectionId )
	{
		Forward( m_graphListner, [ this, connectionId ] ( IEditorGraphListener* l ) { l->OnEnableConnection( m_subGraphId, connectionId ); } );
	}
	
	void IGraphDescriptor::NotifyConnectionDisabled( GraphId connectionId )
	{
		Forward( m_graphListner, [ this, connectionId ] ( IEditorGraphListener* l ) { l->OnDisableConnection( m_subGraphId, connectionId ); } );
	}

	void IGraphDescriptor::NotifyNodeSocketsCreated( GraphId nodeId )
	{
		Forward( m_graphListner, [ this, nodeId ] ( IEditorGraphListener* l ) { l->OnNodeSocketsChanged( m_subGraphId, nodeId ); } );
	}

	void IGraphDescriptor::NotifyNodeSocketsChanged( GraphId nodeId )
	{
		Forward( m_graphListner, [ this, nodeId ]( IEditorGraphListener* l ) { l->OnNodeSocketsChanged( m_subGraphId, nodeId ); } );
	}

	void IGraphDescriptor::NotifyNodeSocketsRemoved( GraphId nodeId, const red::DynArray< GraphSocketId >& socketIds )
	{
		THandle< IGraphNodeDescriptor > node = GetNode( nodeId );

		if ( !node )
			return;

		red::DynArray< GraphConnectionId > connectionIds{ red::PoolFrame() };
		// WCTODO: this needs to be untangled
		for ( GraphSocketId socketId : socketIds )
		{
			for ( ConnectionsIterator it = node->GetSourceConnections( false, socketId ); it; ++it )
				connectionIds.PushBack( it->m_id );

			for ( ConnectionsIterator it = node->GetDestinationConnections( false, socketId ); it; ++it )
				connectionIds.PushBack( it->m_id );
		}

		for ( const GraphConnectionId connectionId : connectionIds )
			RemoveConnection( connectionId );

		Forward( m_graphListner, [ this, nodeId ] ( IEditorGraphListener* l ) { l->OnNodeSocketsChanged( m_subGraphId, nodeId ); } );
	}

	void IGraphDescriptor::NotifySelectionChanged( const red::DynArray< GraphId >& nodeIds )
	{
		Forward( m_graphListner, [ this, nodeIds ] ( IEditorGraphListener* l ) { l->OnSelectionChanged( m_subGraphId, nodeIds ); } );
	}

	void IGraphDescriptor::InvalidateNodeDescription( GraphNodeId nodeId )
	{
		Forward( m_graphListner, [ this, nodeId ] ( IEditorGraphListener* l ) { l->OnInvalidateNodeDescription( m_subGraphId, nodeId ); } );
	}

	void IGraphDescriptor::InvalidateNodeData( GraphNodeId nodeId )
	{
		Forward( m_graphListner, [ this, nodeId ] ( IEditorGraphListener* l ) { l->OnInvalidateNodeData( m_subGraphId, nodeId ); } );
	}

	void IGraphDescriptor::InvalidateNodeComponent( GraphNodeId nodeId, GraphComponentId componentId )
	{
		Forward( m_graphListner, [ this, nodeId, componentId ] ( IEditorGraphListener* l ) { l->OnInvalidateNodeComponent( m_subGraphId, nodeId, componentId ); } );
	}

	void IGraphDescriptor::InvalidateSocketDescription( GraphNodeId nodeId, GraphSocketId socketId )
	{
		Forward( m_graphListner, [ this, nodeId, socketId ] ( IEditorGraphListener* l ) { l->OnInvalidateSocketData( m_subGraphId, nodeId, socketId ); } );
	}

	void IGraphDescriptor::InvalidateNodeDescriptionProperty( GraphNodeId nodeId, const red::String& propertyName )
	{
		Forward( m_graphListner, [ this, nodeId, propertyName ] ( IEditorGraphListener* l ) { l->OnInvalidateNodeDescriptionProperty( m_subGraphId, nodeId, propertyName ); } );
	}
	
	void IGraphDescriptor::InvalidateSocketData( GraphNodeId nodeId, GraphSocketId socketId )
	{
		Forward( m_graphListner, [ this, nodeId, socketId ] ( IEditorGraphListener* l ) { l->OnInvalidateSocketDescription( m_subGraphId, nodeId, socketId ); } );
	}

	void IGraphDescriptor::InvalidateSocketComponent( GraphNodeId nodeId, GraphSocketId socketId, GraphComponentId componentId )
	{
		Forward( m_graphListner, [ this, nodeId, socketId, componentId ] ( IEditorGraphListener* l ) { l->OnInvalidateSocketComponent( m_subGraphId, nodeId, socketId, componentId ); } );
	}

	void IGraphDescriptor::InvalidateConnectionDescription( GraphConnectionId connectionId )
	{
		Forward( m_graphListner, [ this, connectionId ] ( IEditorGraphListener* l ) { l->OnInvalidateConnectionDescription( m_subGraphId, connectionId ); } );
	}

	void IGraphDescriptor::InvalidateConnectionDescriptionProperty( GraphConnectionId connectionId, const String& propertyName )
	{
		Forward( m_graphListner, [ this, connectionId, propertyName ] ( IEditorGraphListener* l ) { l->OnInvalidateConnectionDescriptionProperty( m_subGraphId, connectionId, propertyName ); } );
	}

	void IGraphDescriptor::InvalidateConnectionData( GraphConnectionId connectionId )
	{
		Forward( m_graphListner, [ this, connectionId ] ( IEditorGraphListener* l ) { l->OnInvalidateConnectionData( m_subGraphId, connectionId ); } );
	}

	void IGraphDescriptor::InvalidateConnectionComponent( GraphConnectionId connectionId, GraphComponentId componentId )
	{
		Forward( m_graphListner, [ this, connectionId, componentId ] ( IEditorGraphListener* l ) { l->OnInvalidateConnectionComponent( m_subGraphId, connectionId, componentId ); } );
	}

	void IGraphDescriptor::CloseSubgraph( GraphSubGraphId subgraphId )
	{
		Forward( m_graphListner, [ this, subgraphId ]( IEditorGraphListener* l ) { l->OnCloseSubgraph( subgraphId ); } );
	}

	void IGraphDescriptor::DropData( Point position, const String& data )
	{
	}

	// Variables
	Bool IGraphDescriptor::IsSupportVariable() const
	{
		return static_cast< Bool >( m_globalData.ToHandle() );
	}

	void IGraphDescriptor::ListVariableTypes( red::DynArray<String>& types )
	{
		m_globalData.ToHandle()->ListVariableTypes( types );
	}

	void IGraphDescriptor::GetVariables( red::DynArray< GraphVariableId >& variableIds )
	{
		m_globalData.ToHandle()->GetVariables( variableIds );
	}
		
	THandle< ISerializable > IGraphDescriptor::GetVariableData( GraphVariableId variableId )
	{
		return m_globalData.ToHandle()->GetVariableData( variableId );
	}

	GraphVariableId	IGraphDescriptor::CreateVariable( const String& type )
	{
		return m_globalData.ToHandle()->CreateVariable( type );
	}

	Bool IGraphDescriptor::RemoveVariable( GraphVariableId variableId )
	{
		return m_globalData.ToHandle()->RemoveVariable( variableId );
	}
	
	void IGraphDescriptor::GetVariableType( GraphVariableId variableId, String& type )
	{
		m_globalData.ToHandle()->GetVariableType( variableId, type );
	}

	void IGraphDescriptor::GetVariableName(GraphVariableId variableId, String& name )
	{
		m_globalData.ToHandle()->GetVariableName( variableId, name );
	}

	Bool IGraphDescriptor::SetVariableName( GraphVariableId variableId, const String& name )
	{
		return m_globalData.ToHandle()->SetVariableName( variableId, name );
	}

	ISerializable* IGraphDescriptor::GetData() const
	{
		if ( THandle< CResource > resource = m_parentResource.ToHandle() )
			return resource.Get();

		return nullptr;
	}

	const String& IGraphDescriptor::GetName() const
	{
		return m_name;
	}

	Bool IGraphDescriptor::IsTopDownTree() const
	{
		return false;
	}

	Bool IGraphDescriptor::HasClipboardSupport() const
	{
		return false;
	}

	Bool IGraphDescriptor::HasCloningSupport() const
	{
		return false;
	}

	// Components

	GraphComponentId IGraphDescriptor::CreateNodeComponent( GraphId nodeId, const String& type )
	{
		THandle< IGraphNodeDescriptor > node = GetNode( nodeId );
		if ( !node )
		{
			return false;
		}

		return node->CreateComponent( type );
	}

	GraphComponentId IGraphDescriptor::CreateSocketComponent( GraphId nodeId, GraphSocketId socketId, const String& type )
	{
		THandle< IGraphNodeDescriptor > node = GetNode( nodeId );
		if ( !node )
		{
			return InvalidComponentIndex;
		}

		return node->CreateComponent( socketId, type );
	}

	GraphComponentId IGraphDescriptor::CreateConnectionComponent( GraphId connectionId, const String& type )
	{
		THandle< GraphComponentDescription > component = OnCreateConnectionComponent( connectionId, type );

		if ( !component )
			return InvalidComponentIndex;

		Components* components = m_connectionsComponents.FindPtr( connectionId );
		if ( !components )
		{
			components = &( m_connectionsComponents.Insert( connectionId, Components() ).Iterator() ).Value();
		}

		components->Insert( component->m_id, component );

		return component->m_id;
	}

	THandle< GraphComponentDescription > IGraphDescriptor::OnCreateConnectionComponent( GraphId connectionId, const String& type )
	{
		return nullptr;
	}

	void IGraphDescriptor::GetNodeComponents( GraphId nodeId, red::DynArray< GraphComponentId >& outComponents ) const
	{
		THandle< IGraphNodeDescriptor > node = GetNode( nodeId );
		if ( !node )
		{
			return;
		}

		node->GetNodeComponents( outComponents );
	}

	void IGraphDescriptor::GetSocketComponents( GraphId nodeId, GraphSocketId socketId, red::DynArray< GraphComponentId >& outComponents ) const
	{
		THandle< IGraphNodeDescriptor > node = GetNode( nodeId );
		if ( !node )
		{
			return;
		}

		node->GetSocketComponents( socketId, outComponents );
	}

	void IGraphDescriptor::GetConnectionComponents( GraphConnectionId connectionId, red::DynArray< GraphComponentId >& outComponents ) const
	{
		const Components* components = m_connectionsComponents.FindPtr( connectionId );
		if ( !components )
		{
			return;
		}

		for ( auto& component : *components )
		{
			outComponents.PushBack( component.Value()->m_id );
		}
	}

	THandle< GraphComponentDescription > IGraphDescriptor::GetNodeComponent( GraphId nodeId, GraphComponentId componentId )
	{
		THandle< IGraphNodeDescriptor > node = GetNode( nodeId );
		if ( !node )
		{
			return nullptr;
		}

		return node->GetNodeComponent( componentId );
	}

	THandle< GraphComponentDescription > IGraphDescriptor::GetSocketComponent( GraphId nodeId, GraphSocketId socketId, GraphComponentId componentId )
	{
		THandle< IGraphNodeDescriptor > node = GetNode( nodeId );
		if ( !node )
		{
			return nullptr;
		}

		return node->GetSocketComponent( socketId, componentId );
	}

	THandle< GraphComponentDescription > IGraphDescriptor::GetConnectionComponent( GraphConnectionId connectionId, GraphComponentId componentId )
	{
		const Components* components = m_connectionsComponents.FindPtr( connectionId );
		if ( !components )
		{
			return nullptr;
		}

		const THandle< GraphComponentDescription >* component = ( *components ).FindPtr( componentId );

		if ( !component )
			return nullptr;

		OnGetConnectionComponent( connectionId, component->Get() );
		return *component;
	}

	//////////////////////////////////////////////////////////////////////////

	GraphValidatorOutput::GraphValidatorOutput( IGraphDescriptor* graphDescriptor )
		: m_warningsCount( 0 )
		, m_errorsCount( 0 )
		, m_state( GraphValidationState::Unknown )
		, m_graphIdChainPerNode{ red::PoolBackend() }
	{
		GenerateGraphIdChainPerNodeDictionary( graphDescriptor );
	}

	void GraphValidatorOutput::LogWarning( STATIC_CHECK_PRINTF_MSC const AnsiChar* message, ... )
	{
		va_list args;
		va_start( args, message );

		Log( GraphValidatorOutputEntryType::Warning, red::DynArray< GraphId >(), ValidatorEntryAction(), message, args );

		va_end( args );
	}

	void GraphValidatorOutput::LogError( STATIC_CHECK_PRINTF_MSC const AnsiChar* message, ... )
	{
		va_list args;
		va_start( args, message );

		Log( GraphValidatorOutputEntryType::Error, red::DynArray< GraphId >(), ValidatorEntryAction(), message, args );

		va_end( args );
	}

	void GraphValidatorOutput::LogWarning( const IGraphNodeDescriptor* node, STATIC_CHECK_PRINTF_MSC const AnsiChar* message, ... )
	{
		va_list args;
		va_start( args, message );

		const auto& entryInterator = m_graphIdChainPerNode.Find( node );
		if ( entryInterator != m_graphIdChainPerNode.End() )
		{
			Log( GraphValidatorOutputEntryType::Warning, entryInterator.Value(), ValidatorEntryAction(), message, args );
		}

		va_end( args );
	}

	void GraphValidatorOutput::LogError( const IGraphNodeDescriptor* node, STATIC_CHECK_PRINTF_MSC const AnsiChar* message, ... )
	{
		va_list args;
		va_start( args, message );

		const auto& entryInterator = m_graphIdChainPerNode.Find( node );
		if ( entryInterator != m_graphIdChainPerNode.End() )
		{
			Log( GraphValidatorOutputEntryType::Error, entryInterator.Value(), ValidatorEntryAction(), message, args );
		}

		va_end( args );
	}

	void GraphValidatorOutput::LogWarning( ValidatorEntryAction fixAction, STATIC_CHECK_PRINTF_MSC const AnsiChar* message, ... )
	{
		va_list args;
		va_start( args, message );

		Log( GraphValidatorOutputEntryType::Warning, red::DynArray< GraphId >(), fixAction, message, args );

		va_end( args );
	}

	void GraphValidatorOutput::LogError( ValidatorEntryAction fixAction, STATIC_CHECK_PRINTF_MSC const AnsiChar* message, ... )
	{
		va_list args;
		va_start( args, message );

		Log( GraphValidatorOutputEntryType::Error, red::DynArray< GraphId >(), fixAction, message, args );

		va_end( args );
	}

	void GraphValidatorOutput::UpdateState()
	{
		if ( GetErrorsCount() > 0 )
		{
			m_state = GraphValidationState::Errors;
		}
		else if ( GetWarningsCount() > 0 )
		{
			m_state = GraphValidationState::Warnings;
		}
		else
		{
			m_state = GraphValidationState::Success;
		}
	}

	void GraphValidatorOutput::Log( GraphValidatorOutputEntryType type, const red::DynArray< GraphId >& graphIdChain, ValidatorEntryAction fixAction, STATIC_CHECK_PRINTF_MSC const AnsiChar* message, va_list args )
	{
		switch ( type )
		{
		case GraphValidatorOutputEntryType::Warning:
			++m_warningsCount;
			break;
		case GraphValidatorOutputEntryType::Error:
			++m_errorsCount;
			break;
		}
		
		AnsiChar buf[ 1024 ];
		red::VSNPrintF( buf, RED_ARRAY_COUNT( buf ), message, args );
		m_entries.Insert( GraphValidatorOutputEntry( type, buf, graphIdChain, fixAction ) );

		UpdateState();
	}

	void GraphValidatorOutput::GenerateGraphIdChainPerNodeDictionary( IGraphDescriptor* graphDescriptor )
	{
		if ( graphDescriptor )
		{
			m_graphIdChainPerNode.Clear();

			red::DynArray< GraphId > graphIdChain{ red::PoolFrame() };
			auto preVisitor = [this, &graphIdChain]( IGraphNodeDescriptor* nodeDescriptor )
			{
				graphIdChain.PushBack( nodeDescriptor->GetId() );
				m_graphIdChainPerNode[nodeDescriptor] = graphIdChain;
			};
			auto postVisitor = [&graphIdChain]( IGraphNodeDescriptor* nodeDescriptor )
			{
				graphIdChain.PopBack();
			};
			graphDescriptor->IterateNodes( preVisitor, postVisitor );
			RED_FATAL_ASSERT( graphIdChain.Empty(), "graphIdChain should be empty, if algorithm is implemented correctly" ); // To make sure pre and post visitors are implemented correctly
		}
	}

	void IGraphImporter::GatherPaths( const tools::IGraphDescriptor& graphDescriptor, red::DynArray< tools::EditorObjectIDPath >& outPaths ) const
	{
		const auto& nodesList = graphDescriptor.GetNodesList().GetNodesList();
		for ( const auto& node : nodesList )
		{
			outPaths.PushBack( tools::EditorObjectIDPath( node->GetEditorObjectID() ) );
		}

		const auto& connectionsMap = graphDescriptor.GetConnectionsMap().GetConnectionsMap();
		for ( const auto& connection : connectionsMap )
		{
			outPaths.PushBack( tools::EditorObjectIDPath( graphDescriptor.GetConnection( connection.Key() )->GetEditorObjectID() ) );
		}
	}

	void IGraphImporter::UpdateNodePositions( tools::IGraphDescriptor& graphDescriptor, const Point& offset )
	{
		auto nodesList = graphDescriptor.GetNodesList().GetNodesList();
		for ( auto node : nodesList )
		{
			node->m_position += offset;
		}
	}

}		// namespace tools

