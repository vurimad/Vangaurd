/*
* Copyright (c) 2016-2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "editorGraphTypes.h"

#include "../../../common/redContainers/include/map.h"

namespace tools
{

	class BACKEND_ENGINE_API GraphConnectionsList
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
		RTTI_DECLARE_TYPE( GraphConnectionsList )

		friend class ConnectionsIterator;
	public:
		typedef red::HashMap< GraphConnectionId, THandle< ConnectionDescription > > ConnectionsMap;
		typedef red::DynArray< ConnectionDescription* > Connections;
		typedef std::function< GraphConnectionId() > TIdAllocator;

		GraphConnectionsList();
		~GraphConnectionsList();

		void											Initialize();
		void											Compact();

		void											AddConnection( const THandle< ConnectionDescription >& description );
		void											RemoveConnection( GraphId connectionId );
		THandle< ConnectionDescription >				FindConnection( GraphConnectionId id );
		const THandle< ConnectionDescription >			FindConnection( GraphConnectionId id ) const;

		const ConnectionsMap&							GetConnectionsMap() const													{ return m_connectionsMap; }

	protected:
		ConnectionsMap									m_connectionsMap;
		Connections										m_connectionsBySource;
		Connections										m_connectionsByDestination;

		// LEGACY data, I can't handle this, because this class is not an ISerializable !#$%!$!%!@#$%
		red::DynArray< ConnectionDescription >			m_bySource;
	};


	// Interface for connections iteration
	class BACKEND_ENGINE_API ConnectionsIterator
	{
	public:
		enum ESourceConnection							{ SOURCE_CONNECTION };
		enum EDestinationConnection						{ DESTINATION_CONNECTION };

		ConnectionsIterator() = default;
		ConnectionsIterator( const GraphConnectionsList& graph, Bool onlyActive, GraphNodeId nodeId, GraphSocketId socketId, ESourceConnection );
		ConnectionsIterator( const GraphConnectionsList& graph, Bool onlyActive, GraphNodeId nodeId, GraphSocketId socketId, EDestinationConnection );
		ConnectionsIterator( const GraphConnectionsList& graph, Bool onlyActive, GraphNodeId nodeId, ESourceConnection );
		ConnectionsIterator( const GraphConnectionsList& graph, Bool onlyActive, GraphNodeId nodeId, EDestinationConnection );

		operator										Bool() const																{ return m_it != m_end; }
		const ConnectionDescription&					operator*() const															{ return *(*m_it); }
		const ConnectionDescription*					operator->() const															{ return *m_it; }

		void											operator++()																{ ++m_it; ForwardOnOnlyActive();  }

		// might be used to get number of connections, if used just after initialization
		Uint32											Count() const																{ return Uint32( std::distance( m_it, m_end ) ); }	

	protected:
		typedef GraphConnectionsList::Connections::const_iterator Iterator;

		void											ForwardOnOnlyActive()														{ if ( m_onlyActive ) { while ( m_it != m_end && (*m_it)->m_isDisabled ) ++m_it; }; }

		Iterator										m_it;
		Iterator										m_end;
		Bool											m_onlyActive;
	};

}		// namespace tools
