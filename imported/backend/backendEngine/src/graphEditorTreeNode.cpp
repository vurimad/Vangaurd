/*
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "graphEditorTreeNode.h"

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( ITreeNodeDescriptor, tools );
	RTTI_PARENT_TYPE( tools::IGraphNodeDescriptor );
RTTI_END_TYPE();




namespace tools
{
	///////////////////////////////////////////////////////////////////////////////
	// ITreeNodeDescriptor
	///////////////////////////////////////////////////////////////////////////////
	ITreeNodeDescriptor::ITreeNodeDescriptor()
		: Super()
	{

	}
	ITreeNodeDescriptor::~ITreeNodeDescriptor()
	{

	}

	IGraphNodeDescriptor::SocketIndex ITreeNodeDescriptor::GetChildrenSocketsCount() const
	{
		return 0;
	}

	void ITreeNodeDescriptor::GetChildrenSocketDescription( IGraphNodeDescriptor::SocketIndex id, const THandle< SocketDescription >& socket ) const
	{
		RED_ASSERT( false, "Implement!" );
	}

	void ITreeNodeDescriptor::GetParentSocketDescription(  const THandle< SocketDescription >& socket ) const
	{
	}

	IGraphNodeDescriptor::SocketId ITreeNodeDescriptor::GetParentSocketId() const
	{
		return GetChildrenSocketsCount();
	}

	Bool ITreeNodeDescriptor::CanAddChild( IGraphNodeDescriptor::SocketId socketId, const rtti::ClassType& childrenClass ) const
	{
		return false;
	}

	void ITreeNodeDescriptor::AddChild( IGraphNodeDescriptor::SocketId socketId, IGraphNodeDescriptor* node )
	{
		RED_ASSERT( false, "Implement!" );
	}

	void ITreeNodeDescriptor::RemoveChild( IGraphNodeDescriptor::SocketId socketId, IGraphNodeDescriptor* node )
	{
		RED_ASSERT( false, "Implement!" );
	}

	Bool ITreeNodeDescriptor::HasParentSocket() const
	{
		return true;
	}

	Bool ITreeNodeDescriptor::CanSetParent( const rtti::ClassType& parentClass ) const
	{
		return true;
	}

	void ITreeNodeDescriptor::SetParentNode( IGraphNodeDescriptor* parentNode )
	{
		// by default we don't track our parent node
	}

	IGraphNodeDescriptor::SocketIndex ITreeNodeDescriptor::GetSocketsCount() const
	{
		return GetChildrenSocketsCount() + ( HasParentSocket() ? 1 : 0 );
	}

	void ITreeNodeDescriptor::GetChildrenSocketStyle( SocketIndex index, const THandle<SocketDescription>& socket ) const
	{
	}
		
	Bool ITreeNodeDescriptor::CanAddChild( SocketId socketId, const IGraphNodeDescriptor& childrenNode ) const 
	{
		return CanAddChild( socketId, *childrenNode.GetClass() );
	}

	void ITreeNodeDescriptor::GetSocketDescription(SocketIndex socketIndex, const THandle< SocketDescription >& socket) const
{
		socket->m_id = socketIndex;
		if ( socketIndex == GetParentSocketId() )
		{
			// parent socket
			socket->m_placement = SocketPlacement::Title;
			socket->m_direction = SocketDirection::Input;			

			socket->m_caption = "";

			GetParentSocketDescription( socket );
		}
		else
		{
			socket->m_placement = SocketPlacement::Bottom;
			socket->m_direction = SocketDirection::Output;
			
			GetChildrenSocketStyle( socketIndex, socket );
			GetChildrenSocketDescription( socketIndex, socket );
		}
	}

	Bool ITreeNodeDescriptor::CanConnectTo( IGraphNodeDescriptor::SocketId socketId, const SocketInfo& other, Bool isSource ) const
	{
		if ( isSource )
		{
			if ( socketId < GetChildrenSocketsCount() )
			{
				if ( ITreeNodeDescriptor* treeNode = Cast< ITreeNodeDescriptor >( other.m_node ) )
				{
					// we can't connect two nodes with 'children' sockets
					if ( other.m_socketId != treeNode->GetParentSocketId() )
					{
						return false;
					}
				}

				if ( !CanAddChild( socketId, *other.m_node ) )
				{
					return false;
				}
				return true;
			}
		}
		else
		{
			// parent socket handling
			if ( socketId == GetParentSocketId() )
			{
				// test if we have parent already
				if ( HasParent() )
				{
					return false;
				}

				if ( ITreeNodeDescriptor* treeNode = Cast< ITreeNodeDescriptor >( other.m_node ) )
				{
					// we can't connect two nodes with 'parent' sockets
					if ( other.m_socketId == treeNode->GetParentSocketId() )
					{
						return false;
					}
				}

				//
				if ( !CanSetParent( *other.m_node->GetClass() ) )
				{
					return false;
				}

				// tree node don't care if it can be connected by some1. All connectivity checks should be done on parent level
				return true;
			}
		}

		return false;
	}

	void ITreeNodeDescriptor::ProcessConnectionTo( SocketId socketId, const SocketInfo& other, Bool isSource, ConnectionId connectionId )
	{
		if ( isSource )
		{
			if ( socketId < GetChildrenSocketsCount() )
			{
				AddChild( socketId, other.m_node );
			}
		}
		else
		{
			if ( socketId == GetParentSocketId() )
			{
				SetParentNode( other.m_node );
			}
		}
	}

	void ITreeNodeDescriptor::RemoveConnectionFrom( SocketId socketId, const SocketInfo& other, Bool isSource, ConnectionId connectionId )
	{
		if ( isSource )
		{
			if ( socketId < GetChildrenSocketsCount() )
			{
				RemoveChild( socketId, other.m_node );
			}
		}
		else
		{
			if ( socketId == GetParentSocketId() )
			{
				SetParentNode( nullptr );
			}
		}
	}

	Bool ITreeNodeDescriptor::HasParent( Bool onlyActive ) const
	{
		tools::ConnectionsIterator it = GetDestinationConnections( onlyActive, GetParentSocketId() );
		return it.Count() > 0;
	}

} // namespace tools