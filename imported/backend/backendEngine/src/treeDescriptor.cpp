/*
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "treeDescriptor.h"

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( ITreeDescriptor, tools );
	RTTI_PARENT_TYPE( tools::IGraphDescriptor );
RTTI_END_TYPE();



namespace tools
{
	void ITreeDescriptor::GetGraphRootNodes( red::DynArray< ITreeNodeDescriptor* >& outRootNodes ) const
	{
		// no root nodes by default
	}

	 Bool ITreeDescriptor::IsTopDownTree() const
	 {
		 return true;
	 }

	// Will iterate through all nodes that are accessible from the root, as long as they implement ITreeNodeDescriptor interface
	// NOTICE: if there are graph-like nodes, that don't implement tree interface, and worse, if they containt some tree-like branches underneath, user is
	// supposed to extend given InspectNodes implementation!
	void ITreeDescriptor::InspectBranchRec( NodeInspector& nodeInspector, ITreeNodeDescriptor& node )
	{
		nodeInspector.InspectNode( &node );
		for ( ITreeNodeDescriptor::SocketIndex socketId = 0, childrenSocketsCount = node.GetChildrenSocketsCount(); socketId < childrenSocketsCount; ++socketId )
		{
			ConnectionsIterator it = node.GetChildNodes( false, socketId );
			while ( it )
			{
				THandle< IGraphNodeDescriptor > childNode = GetNodesMap().GetNode( it->m_destinationNode );
				THandle< ITreeNodeDescriptor > childTreeNode = Cast< ITreeNodeDescriptor >( childNode );
				if ( childTreeNode )
				{
					InspectBranchRec( nodeInspector, *childTreeNode );
				}

				++it;
			}
		}
	}

}		// namespace tools

