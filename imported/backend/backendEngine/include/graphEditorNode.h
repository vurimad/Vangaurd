/*
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "editorGraphTypes.h"
#include "graphEditorConnection.h"

namespace tools
{			
	struct SocketInfo;
	class IGraphDescriptor;
	class GraphValidatorOutput;
	class IGraphValidator;
	class IGraphValidationContext;

	class BACKEND_ENGINE_API SocketsIterator
	{
	public:
		SocketsIterator( const red::DynArray< THandle< tools::SocketDescription > >& sockets );

		operator Bool() const { return m_it != m_end; }
		const SocketDescription& operator*() const { return *(*m_it); }
		SocketDescription& operator*() { return *(*m_it); }
		const SocketDescription* operator->() const { return &(*(*m_it)); }
		SocketDescription* operator->() { return &(*(*m_it)); }
		void operator++() { ++m_it; }

	protected:
		typedef	red::DynArray< THandle< tools::SocketDescription > >::const_iterator Iterator;

		Iterator m_it;
		Iterator m_end;
	};

	class BACKEND_ENGINE_API IGraphNodeDescriptor : public NodeDescription
	{
		friend class IGraphDescriptor;

		RTTI_DECLARE_TYPE( IGraphNodeDescriptor )
	public:
		typedef SocketDescription SocketDescription;
		typedef ConnectionDescription ConnectionDescription;
		typedef GraphSocketId SocketId;
		typedef GraphId ConnectionId;
		typedef Uint32 SocketIndex;
		typedef SocketInfo SocketInfo;

		IGraphNodeDescriptor();
		~IGraphNodeDescriptor();

		virtual void OnPostLoad( const PostLoadContext& context ) override;

		GraphId GetId() const { return m_id; }

		Int32 GetX() const { return m_position.x; }
		Int32 GetY() const { return m_position.y; }

		// get connections
		ConnectionsIterator GetSourceConnections( Bool onlyActive ) const;
		ConnectionsIterator GetDestinationConnections( Bool onlyActive ) const;
		ConnectionsIterator GetSourceConnections( Bool onlyActive, SocketId socketId ) const;
		ConnectionsIterator GetDestinationConnections( Bool onlyActive, SocketId socketId ) const;

		// get sockets
		void GetSocketsButPossiblyCreateThemBeforehand( red::DynArray< SocketId >& outSockets );
		THandle< SocketDescription > GetSocketDescription( SocketId socketId ) const;
		SocketsIterator GetSockets() const;

		IGraphDescriptor* GetGraphDescriptor() const { return m_graph; }

		Bool Create( const THandle< ISerializable >& context, IGraphDescriptor& graph, const String& typeName, Bool mappingOnly );
		Bool Initialize( IGraphDescriptor& graph, Bool mappingOnly );
		void Remove();
		Bool Restore( IGraphDescriptor& graph );
		Bool Place( IGraphDescriptor& graph );
		Bool Refresh();
		Bool Validate( const IGraphValidator& validator, IGraphValidationContext& context, GraphValidatorOutput& output );

		Bool UpdateCaption();
		Bool UpdateSubCaption();
		Bool UpdateColors();

		void RefreshOutputSocket();

		void AppendSockets( const red::DynArray< SocketId >& socketIds, const red::DynArray< CName >& hiddenSockets = red::DynArray< CName >(), const red::DynArray< SocketNameConnectionPair >& socketNameToConnection = red::DynArray< SocketNameConnectionPair >() );
		void InsertSocket( Uint32 index, SocketId socketId );
		void RemoveSockets( const red::DynArray< SocketId >& socketIds );
		void RemoveSocketAt( Uint32 index );
		void RemoveSocket( SocketId socketId );
		void ReorderSocket( SocketId socketId, Uint32 index, Bool notify = true );
		void ReorderSockets( const red::DynArray< GraphSocketId >& graphSockets );
		
		GraphComponentId CreateComponent( const String& type );
		GraphComponentId CreateComponent( SocketId socketId, const String& type );

		void GetNodeComponents( red::DynArray< GraphComponentId >& outComponents ) const;
		void GetSocketComponents( SocketId socketId, red::DynArray< GraphComponentId >& outComponents ) const;
		THandle< GraphComponentDescription > GetNodeComponent( GraphComponentId id ) const;
		THandle< GraphComponentDescription > GetSocketComponent( SocketId socketId, GraphComponentId id ) const;

		// modification - static 
		static Bool CanConnect( const SocketInfo& source, const SocketInfo& destination );
		static void Connect( const SocketInfo& source, const SocketInfo& destination, GraphId connectionId );
		static Bool CanRemoveConnection( const SocketInfo& source, const SocketInfo& destination, GraphId connectionId );
		static void RemoveConnection( const SocketInfo& source, const SocketInfo& destination, GraphId connectionId );

		// virtual descriptor properties
		virtual String GetCaption() const { return GetClass()->GetDescriptiveName(); }
		virtual String GetSubCaption() const { return String::EMPTY(); }
		virtual Color GetBlockColor() const { return Color::DARK_RED(); }
		virtual Color GetBlockTextColor() const { return Color::WHITE(); }

		// sockets interface
		virtual SocketIndex GetSocketsCount() const { return 0; }
		virtual void GetSocketDescription( SocketIndex index, const THandle< SocketDescription >& socket ) const {}
		virtual String GetSocketName( SocketIndex index ) const { return String::EMPTY(); }
		virtual SocketId GetDefaultDestinationSocket() const { return InvalidSocketId; }
		virtual void OnSocketDataChanged( SocketId socketId, const rtti::AccessPath& path ) {}

		// connections interface
		virtual Bool CanConnectTo( SocketId socketId, const SocketInfo& destination, Bool isSource ) const { return false; }
		virtual void ProcessConnectionTo( SocketId socketId, const SocketInfo& destination, Bool isSource, GraphId connectionId ) {}
		virtual Bool CanRemoveConnection( SocketId socketId, const SocketInfo& destination, Bool isSource, GraphId connectionId ) const { return true; }
		virtual void RemoveConnectionFrom( SocketId socketId, const SocketInfo& destination, Bool isSource, GraphId connectionId ) {}

		// subgraphs interface
		virtual IGraphDescriptor* GetSubGraphDescriptor() const { return nullptr; }
		virtual THandle< CResource > GetSubGraphResource() const;

		// node data interface
		virtual const THandle< ISerializable > GetNodeData() const { return nullptr; }
		virtual void GetNodeEventsData( red::DynArray< THandle< ISerializable > >& events ) const {} // szymoniwanski: Remove, this should not be here.

		// invoked every time a property is changed (including nested objects' properties)
		virtual void OnNodeDataChanged( const rtti::AccessPath& path ) {}

		// reaction to frontend drop action
		virtual void OnDropData( const String& data ) {}

		// creation support
		virtual void GatherSupportedNodeClasses( red::DynArray< String >& childrenClassTypes ) const;

		// filtering interface
		virtual	Bool PassesCriteria( const String& data, Uint32 searchType, String& outLabel, String& outType );

		// used by search func
		virtual String ToString() const;

		// used for debug
		virtual String ToDebugString() const;

	protected:
		// invoked once for every node
		virtual Bool OnCreate( const THandle< ISerializable >& context, IGraphDescriptor& graph, const String& typeName, Bool mappingOnly ) { return true; }

		// invoked every time this node is loaded (create/load)
		virtual Bool OnInitialize( IGraphDescriptor& graph, Bool mappingOnly ) { return true; }

		//
		virtual void OnRemoval() {}

		// reaction to restoring this node by undo/redo
		virtual Bool OnRestore( IGraphDescriptor& graph ) { return true; }

		// reaction to clipboard action (paste)
		virtual Bool OnPlace( IGraphDescriptor& graph ) { return false; }

		// nodes can be manually requested to be refreshed in the frontend
		virtual Bool OnRefresh() { return true; }

		// node's internal data can be validated in here and any warnings/errors can be passed to the frontend
		virtual Bool OnValidate( const IGraphValidator& validator, const IGraphValidationContext& context, GraphValidatorOutput& output ) { return true; }

		virtual void OnGraphOpened() {}

		// invoked once (in opposition to GetSocketDescription)
		virtual void OnSocketCreated( SocketIndex index, const THandle< SocketDescription >& socket, const red::DynArray< CName >& hiddenSockets = red::DynArray< CName >(), const red::DynArray< SocketNameConnectionPair >& socketNameToConnection = red::DynArray< SocketNameConnectionPair >() ) const {}

		//
		virtual void OnSocketRemoved( SocketIndex index, const THandle< SocketDescription >& socket ) const {}

		// components interface
		// invoked once on component creation, then it's being cached and updated on Get
		virtual THandle< GraphComponentDescription > OnCreateComponent( const String& type ) { return nullptr; }
		virtual THandle< GraphComponentDescription > OnCreateComponent( SocketId socketId, const String& type ) { return nullptr; }

		// invoked every time a component is being grabbed
		virtual void OnGetNodeComponent( GraphComponentDescription* component ) const {}
		virtual void OnGetSocketComponent( SocketId socketId, GraphComponentDescription* component ) const {}

		//
		virtual void OnGraphPreSave() {}

		//
		virtual Bool FilterRepresentationClass( const rtti::ClassType& childrenClass ) const { return true; }

		// NOTICE: Used if our node supports RTTI based classes.
		virtual	const rtti::ClassType* GetRepresentationClass() const { return GetClass(); }
		virtual const rtti::ClassType* GetRootRepresentationClass() const { return GetRepresentationClass(); }

		virtual void OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& value ) override;

	private:
		IGraphDescriptor* m_graph;

		red::DynArray< THandle< tools::SocketDescription > > m_sockets;

		Components m_nodeComponents;
		red::HashMap< SocketId, Components > m_socketsComponents;
	};

	struct SocketInfo
	{
		SocketInfo(IGraphNodeDescriptor* node, GraphSocketId socketId)
			: m_node(node)
			, m_socketId(socketId)
		{
		}

		SocketInfo( const THandle< IGraphNodeDescriptor >& node, GraphSocketId socketId )
			: m_node( node.Get() )
			, m_socketId( socketId )
		{
		}

		IGraphNodeDescriptor* m_node;
		GraphSocketId m_socketId;
	};
}		// namespace tools
