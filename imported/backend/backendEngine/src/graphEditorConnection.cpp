/*
* Copyright (c) 2016-2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "graphEditorConnection.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( GraphConnectionsList, tools );
	RTTI_PROPERTY( m_bySource );
RTTI_END_TYPE();

namespace tools
{
	struct Helper
	{
		struct NodeSocket
		{
			NodeSocket( GraphNodeId nodeId, GraphSocketId socketId )
				: m_nodeId( nodeId )
				, m_socketId( socketId ) {}

			GraphNodeId				m_nodeId;
			GraphSocketId			m_socketId;
		};

		struct OrderBySource
		{
			Bool operator()( const ConnectionBindings* c0, const ConnectionBindings* c1 ) const
			{
				return
					c0->m_sourceNode == c1->m_sourceNode
					? c0->m_sourceSocketIndex == c1->m_sourceSocketIndex
					? c0->m_destinationNode == c1->m_destinationNode
					? c0->m_destinationSocketIndex == c1->m_destinationSocketIndex
					? c0->m_id < c1->m_id
					: c0->m_destinationSocketIndex < c1->m_destinationSocketIndex
					: c0->m_destinationNode < c1->m_destinationNode
					: c0->m_sourceSocketIndex < c1->m_sourceSocketIndex
					: c0->m_sourceNode < c1->m_sourceNode;
			}

			// for lower bound
			Bool operator()( const ConnectionBindings* c, const NodeSocket& n ) const
			{
				return
					c->m_sourceNode == n.m_nodeId
					? c->m_sourceSocketIndex == n.m_socketId
					? false
					: c->m_sourceSocketIndex < n.m_socketId
					: c->m_sourceNode < n.m_nodeId;
			}

			// for upper bound
			Bool operator()( const NodeSocket& n, const ConnectionBindings* c ) const
			{
				return
					c->m_sourceNode == n.m_nodeId
					? c->m_sourceSocketIndex == n.m_socketId
					? false
					: c->m_sourceSocketIndex > n.m_socketId
					: c->m_sourceNode > n.m_nodeId;
			}

			// for lower bound
			Bool operator()( const ConnectionBindings* c, GraphNodeId nodeId ) const
			{
				return c->m_sourceNode < nodeId;
			}

			// for upper bound
			Bool operator()( GraphNodeId nodeId, const ConnectionBindings* c ) const
			{
				return c->m_sourceNode > nodeId;
			}
		};

		struct OrderByDestination
		{
			Bool operator()( const ConnectionBindings* c0, const ConnectionBindings* c1 ) const
			{
				return
					c0->m_destinationNode == c1->m_destinationNode
					? c0->m_destinationSocketIndex == c1->m_destinationSocketIndex
					? c0->m_sourceNode == c1->m_sourceNode
					? c0->m_sourceSocketIndex == c1->m_sourceSocketIndex
					? c0->m_id < c1->m_id
					: c0->m_sourceSocketIndex < c1->m_sourceSocketIndex
					: c0->m_sourceNode < c1->m_sourceNode
					: c0->m_destinationSocketIndex < c1->m_destinationSocketIndex
					: c0->m_destinationNode < c1->m_destinationNode;
			}

			// for lower bound
			Bool operator()( const ConnectionBindings* c, const NodeSocket& n ) const
			{
				return
					c->m_destinationNode == n.m_nodeId
					? c->m_destinationSocketIndex == n.m_socketId
					? false
					: c->m_destinationSocketIndex < n.m_socketId
					: c->m_destinationNode < n.m_nodeId;
			}

			// for upper bound
			Bool operator()( const NodeSocket& n, const ConnectionBindings* c ) const
			{
				return
					c->m_destinationNode == n.m_nodeId
					? c->m_destinationSocketIndex == n.m_socketId
					? false
					: c->m_destinationSocketIndex > n.m_socketId
					: c->m_destinationNode > n.m_nodeId;
			}

			// for lower bound
			Bool operator()( const ConnectionBindings* c, GraphNodeId nodeId ) const
			{
				return c->m_destinationNode < nodeId;
			}

			// for upper bound
			Bool operator()( GraphNodeId nodeId, const ConnectionBindings* c ) const
			{
				return c->m_destinationNode > nodeId;
			}
		};

		
	};



	GraphConnectionsList::GraphConnectionsList()
		: m_connectionsMap{ red::PoolBackend() }
		, m_connectionsBySource{ red::PoolBackend() }
		, m_connectionsByDestination{ red::PoolBackend() }
		, m_bySource{ red::PoolBackend() }
	{
	}

	GraphConnectionsList::~GraphConnectionsList()
	{
	}

	void GraphConnectionsList::Initialize()
	{
		for ( const auto& description : m_bySource )
		{
			AddConnection( CreateHandle< ConnectionDescription >( description ) );
		}
	}

	void GraphConnectionsList::Compact()
	{
		m_bySource.Clear();

		for ( const auto& pair : m_connectionsMap )
		{
			m_bySource.PushBack( *pair.Value().Get() );
		}
	}

	void GraphConnectionsList::AddConnection( const THandle< ConnectionDescription >& description )
	{
		m_connectionsMap.Insert( description->m_id, description );

		// update connections by source
		{
			auto it = std::lower_bound( m_connectionsBySource.Begin(), m_connectionsBySource.End(), description.Get(), Helper::OrderBySource() );
			m_connectionsBySource.Insert( it, description.Get() );
		}
		// update connections by destinations
		{
			auto it = std::lower_bound( m_connectionsByDestination.Begin(), m_connectionsByDestination.End(), description.Get(), Helper::OrderByDestination() );
			m_connectionsByDestination.Insert( it, description.Get() );
		}
	}

	void GraphConnectionsList::RemoveConnection( GraphId connectionId )
	{
		auto it = m_connectionsMap.Find( connectionId );
		if ( it == m_connectionsMap.End() )
			return;

		THandle< ConnectionDescription > desc = it.Value();
		m_connectionsMap.Remove( it );

		{
			auto it = std::lower_bound( m_connectionsBySource.Begin(), m_connectionsBySource.End(), desc.Get(), Helper::OrderBySource() );
			if ( it != m_connectionsBySource.End() && desc.Get() == (*it) )
			{
				m_connectionsBySource.Remove( it );
			}
		}
		{
			auto it = std::lower_bound( m_connectionsByDestination.Begin(), m_connectionsByDestination.End(), desc.Get(), Helper::OrderByDestination() );
			if ( it != m_connectionsByDestination.End() && desc.Get() == (*it) )
			{
				m_connectionsByDestination.Remove( it );
			}
		}
	}

	THandle< ConnectionDescription > GraphConnectionsList::FindConnection( GraphConnectionId id )
	{
		auto it = m_connectionsMap.Find( id );
		if ( it != m_connectionsMap.End() )
			return it.Value();

		return nullptr;
	}

	const THandle< ConnectionDescription > GraphConnectionsList::FindConnection( GraphConnectionId id ) const
	{
		return ( const_cast< GraphConnectionsList* >( this )->FindConnection( id ) );
	}

	ConnectionsIterator::ConnectionsIterator( const GraphConnectionsList& graph, Bool onlyActive, GraphNodeId nodeId, GraphSocketId socketId, ESourceConnection )
		: m_onlyActive( onlyActive )
	{
		Helper::NodeSocket n( nodeId, socketId );

		const GraphConnectionsList::Connections& map = graph.m_connectionsBySource;
		// setup begin
		m_it = std::lower_bound( map.Begin(), map.End(), n, Helper::OrderBySource() );
		m_end = std::upper_bound( map.Begin(), map.End(), n, Helper::OrderBySource() );
		
		ForwardOnOnlyActive();
	}
	ConnectionsIterator::ConnectionsIterator( const GraphConnectionsList& graph, Bool onlyActive, GraphNodeId nodeId, GraphSocketId socketId, EDestinationConnection )
		: m_onlyActive( onlyActive )
	{
		Helper::NodeSocket n( nodeId, socketId );

		const GraphConnectionsList::Connections& map = graph.m_connectionsByDestination;
		// setup begin
		m_it = std::lower_bound( map.Begin(), map.End(), n, Helper::OrderByDestination() );
		m_end = std::upper_bound( map.Begin(), map.End(), n, Helper::OrderByDestination() );

		ForwardOnOnlyActive();
	}
	ConnectionsIterator::ConnectionsIterator( const GraphConnectionsList& graph, Bool onlyActive, GraphNodeId nodeId, ESourceConnection )
		: m_onlyActive( onlyActive )
	{
		const GraphConnectionsList::Connections& map = graph.m_connectionsBySource;
		// setup begin
		m_it = std::lower_bound( map.Begin(), map.End(), nodeId, Helper::OrderBySource() );
		m_end = std::upper_bound( map.Begin(), map.End(), nodeId, Helper::OrderBySource() );

		ForwardOnOnlyActive();
	}
	ConnectionsIterator::ConnectionsIterator( const GraphConnectionsList& graph, Bool onlyActive, GraphNodeId nodeId, EDestinationConnection )
		: m_onlyActive( onlyActive )
	{
		const GraphConnectionsList::Connections& map = graph.m_connectionsByDestination;
		// setup begin
		m_it = std::lower_bound( map.Begin(), map.End(), nodeId, Helper::OrderByDestination() );
		m_end = std::upper_bound( map.Begin(), map.End(), nodeId, Helper::OrderByDestination() );

		ForwardOnOnlyActive();
	}

}		// namespace tools

