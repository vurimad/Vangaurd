/*
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "editorGraphTypes.h"

namespace tools
{
	class IGraphNodeDescriptor;

	class BACKEND_ENGINE_API GraphNodesList
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
		RTTI_DECLARE_TYPE( GraphNodesList )

	public:
		typedef red::DynArray< THandle< IGraphNodeDescriptor > > NodesList;

											GraphNodesList();
											~GraphNodesList();

		void								AttachNode( const THandle< IGraphNodeDescriptor >& node );
		void								RemoveNode( GraphNodeId id );
		void								RemoveNode( const THandle< IGraphNodeDescriptor >& node );

		const NodesList&					GetNodesList() const											{ return m_nodes; }

		void								Compact();

		NodesList GetNodes() { return m_nodes; }

	protected:

		NodesList							m_nodes;
	};


	class BACKEND_ENGINE_API GraphDescriptorNodesMap
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		typedef red::DynArray< GraphNodeId > NodeIds;

											GraphDescriptorNodesMap( const GraphNodesList& list );
											~GraphDescriptorNodesMap();

		void								AttachNode( const THandle< IGraphNodeDescriptor >& node );
		void								RemoveNode( GraphNodeId id );
		void								RemoveNode( const THandle< IGraphNodeDescriptor >& node );

		const NodeIds&						GetNodesIds() const											{ return m_nodeIds; }
		THandle< IGraphNodeDescriptor >		GetNode( GraphNodeId id ) const;
	protected:
		typedef red::HashMap< GraphNodeId, THandle< IGraphNodeDescriptor > > NodeMapping;
		

		NodeMapping							m_nodeMapping;												// nodes
		NodeIds								m_nodeIds;
	};

}		// namespace tools

