/*
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "graphDescriptorNodesMap.h"

#include "graphEditorNode.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( GraphNodesList, tools );
	RTTI_PROPERTY( m_nodes );
RTTI_END_TYPE();




namespace tools
{
	///////////////////////////////////////////////////////////////////////////////
	// GraphNodesList
	///////////////////////////////////////////////////////////////////////////////
	GraphNodesList::GraphNodesList()
		: m_nodes{ red::PoolBackend() }
	{

	}

	GraphNodesList::~GraphNodesList()
	{

	}

	void GraphNodesList::AttachNode( const THandle< IGraphNodeDescriptor >& node )
	{
		RED_ASSERT( node->GetId() != InvalidGraphId, "Unitialized node attached!" );
		m_nodes.PushBack( node );
	}

	void GraphNodesList::RemoveNode( GraphNodeId id )
	{
		for ( auto it = m_nodes.Begin(), end = m_nodes.End(); it != end; ++it )
		{
			if ( (*it)->GetId() == id )
			{
				m_nodes.Remove( it );
				break;
			}
		}
	}

	void GraphNodesList::RemoveNode( const THandle< IGraphNodeDescriptor >& node )
	{
		RemoveNode( node->GetId() );
	}

	void GraphNodesList::Compact()
	{
		for ( Int32 i = m_nodes.Size()-1; i >= 0; --i )
		{
			if ( !m_nodes[ i ] )
			{
				m_nodes.RemoveAt( i );
			}
		}
	}

	///////////////////////////////////////////////////////////////////////////////
	// GraphDescriptorNodesMap
	///////////////////////////////////////////////////////////////////////////////
	GraphDescriptorNodesMap::GraphDescriptorNodesMap( const GraphNodesList& nodes )
		: m_nodeMapping{ red::PoolBackend() }
		, m_nodeIds{ red::PoolBackend() }
	{
		const GraphNodesList::NodesList& list = nodes.GetNodesList();
		m_nodeMapping.Reserve( list.Size() );
		m_nodeIds.Reserve( list.Size() );

		for ( const THandle< IGraphNodeDescriptor >& node : list )
		{
			if ( node )
			{
				GraphNodeId id = node->GetId();
				m_nodeIds.PushBack( id );
				m_nodeMapping[ id ] = node;
			}
		}
	}

	GraphDescriptorNodesMap::~GraphDescriptorNodesMap()
	{

	}

	void GraphDescriptorNodesMap::AttachNode( const THandle< IGraphNodeDescriptor >& node )
	{
		GraphNodeId id = node->GetId(); 
		RED_ASSERT( id != InvalidGraphId, "Attaching uninitialized graph node!" );
		auto itFind = m_nodeMapping.Find( id );
		if ( itFind != m_nodeMapping.End() )
		{
			return;
		}
		m_nodeMapping.Insert( id, node );
		m_nodeIds.PushBack( id );
	}

	void GraphDescriptorNodesMap::RemoveNode( const THandle< IGraphNodeDescriptor >& node )
	{
		RemoveNode( node->GetId() );
	}

	void GraphDescriptorNodesMap::RemoveNode( GraphNodeId id )
	{
		auto itFind = m_nodeMapping.Find( id );
		if ( itFind != m_nodeMapping.End() )
		{
			m_nodeMapping.Remove( itFind );
			m_nodeIds.Remove( id );
		}
	}

	THandle< IGraphNodeDescriptor > GraphDescriptorNodesMap::GetNode( GraphNodeId id ) const
	{
		auto itFind = m_nodeMapping.Find( id );
		if ( itFind == m_nodeMapping.End() )
		{
			return THandle<IGraphNodeDescriptor>(nullptr);
		}
		return itFind.Value();
	}

}		// namespace tools

