/*
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "graphEditorNode.h"

namespace tools
{
	class BACKEND_ENGINE_API ITreeNodeDescriptor : public IGraphNodeDescriptor
	{
		typedef IGraphNodeDescriptor Super;
		RTTI_DECLARE_TYPE( ITreeNodeDescriptor )
	
	public:
											ITreeNodeDescriptor();
											~ITreeNodeDescriptor();

		// Tree-node interface
		virtual SocketIndex					GetChildrenSocketsCount() const;
		virtual void						GetChildrenSocketDescription( SocketIndex id, const THandle< SocketDescription >& socket ) const;
		virtual void						GetParentSocketDescription(  const THandle< SocketDescription >& socket ) const;
		SocketId							GetParentSocketId() const;

		ConnectionsIterator					GetChildNodes( Bool onlyActive, SocketId socketId ) const						{ return GetSourceConnections( onlyActive, socketId ); }

		// Tree-node connectivity interface
		virtual Bool						CanAddChild( SocketId socketId, const IGraphNodeDescriptor& childrenNode ) const;
		virtual Bool						CanAddChild( SocketId socketId, const rtti::ClassType& childrenClass ) const;
		virtual void						AddChild( SocketId socketId, IGraphNodeDescriptor* node );
		virtual void						RemoveChild( SocketId socketId, IGraphNodeDescriptor* node );

		virtual Bool						HasParentSocket() const;
		virtual Bool						CanSetParent( const rtti::ClassType& parentClass ) const;
		virtual void						SetParentNode( IGraphNodeDescriptor* parentNode );

		// Generic graph node interface
		virtual SocketIndex					GetSocketsCount() const override;
		virtual void						GetChildrenSocketStyle( SocketIndex index, const THandle< SocketDescription >& socket ) const ;
		virtual void						GetSocketDescription( SocketIndex index, const THandle< SocketDescription >& socket ) const override;
		
		// Generic graph node connectivity interface
		virtual Bool						CanConnectTo( SocketId socketId, const SocketInfo& other, Bool isSource ) const override;
		virtual void						ProcessConnectionTo( SocketId socketId, const SocketInfo& other, Bool isSource, ConnectionId connectionId ) override;

		virtual void						RemoveConnectionFrom( SocketId socketId, const SocketInfo& other, Bool isSource, ConnectionId connectionId ) override;	// TODO
	protected:
		Bool								HasParent( Bool onlyActive = false ) const;
	};

} // namespace tools