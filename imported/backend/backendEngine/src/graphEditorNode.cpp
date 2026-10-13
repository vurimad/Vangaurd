/*
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "graphEditorNode.h"

#include "../../../common/redReflection/include/rttiPathParser.h"
#include "graphDescriptor.h"
#include "../../common/redReflection/include/resource.h"
#include "../../common/redContainers/include/string/stringBuilder.h"
#include "editorGraphBase.h"

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( IGraphNodeDescriptor, tools );
	RTTI_PARENT_TYPE( NodeDescription );
	RTTI_PROPERTY( m_sockets );
RTTI_END_TYPE();

namespace consts
{
	const tools::GraphSocketId ANIM_OUTPUT_SOCKET_ID = 0;		// AA_TODO: probably have to change that to something else
}

namespace tools
{
	SocketsIterator::SocketsIterator( const red::DynArray< THandle< tools::SocketDescription > >& sockets )
	{
		m_it = sockets.Begin();
		m_end = sockets.End();
	}

	//////////////////////////////////////////////////////////////////////////

	IGraphNodeDescriptor::IGraphNodeDescriptor()
		: m_nodeComponents{ red::PoolBackend() }
		, m_socketsComponents{ red::PoolBackend() }
		, m_sockets{ red::PoolBackend() }
	{
	}

	IGraphNodeDescriptor::~IGraphNodeDescriptor()
	{
	}

	void IGraphNodeDescriptor::OnPostLoad( const PostLoadContext& context )
	{
		NodeDescription::OnPostLoad( context );

		// Filter out sockets with same id
		auto socketsCopy = std::move(m_sockets);
		m_sockets.Clear();
		for (Uint32 i = 0; i < socketsCopy.Size(); ++i)
		{
			const auto& socketToAdd = socketsCopy[i];
			auto iterator = std::find_if(m_sockets.Begin(), m_sockets.End(), [&socketToAdd](const THandle<tools::SocketDescription>& socket)
			{
				return socketToAdd->m_id == socket->m_id;
			});
			if (iterator == m_sockets.End())
			{
				m_sockets.PushBack(socketToAdd);
			}
		}
	}

	Bool IGraphNodeDescriptor::Create( const THandle< ISerializable >& context, IGraphDescriptor& graph, const String& typeName, Bool mappingOnly )
	{
		m_graph = &graph;

		if ( OnCreate( context, graph, typeName, mappingOnly ) )
		{
			m_caption = GetCaption();
			m_subtitle = GetSubCaption();
			m_headerColor = GetBlockColor();
			m_captionColor = GetBlockTextColor();
			return true;
		}
		else
		{
			return false;
		}
	}

	Bool IGraphNodeDescriptor::Initialize( IGraphDescriptor& graph, Bool mappingOnly )
	{
		m_graph = &graph;

		if ( !OnInitialize( graph, mappingOnly ) )
		{
			return false;
		}

		UpdateCaption();	// Some data may have changed during OnPostLoad, and caption may need to be updated.
		UpdateSubCaption();

		if ( tools::IGraphDescriptor* subGraph = GetSubGraphDescriptor() )
		{
			subGraph->Initialize( GetSubGraphResource(), HandleFromThis< IGraphNodeDescriptor >() );
		}

		return true;
	}

	void IGraphNodeDescriptor::Remove()
	{
		//RED_ASSERT( CanBeRemoved(), "Non removable node being removed!" );
		OnRemoval();
	}

	Bool IGraphNodeDescriptor::Restore( IGraphDescriptor& graph )
	{
		m_graph = &graph;

		return OnRestore( graph );
	}

	Bool IGraphNodeDescriptor::Place( IGraphDescriptor& graph )
	{
		m_graph = &graph;

		return OnPlace( graph );
	}

	Bool IGraphNodeDescriptor::Refresh()
	{
		return OnRefresh();
	}

	Bool IGraphNodeDescriptor::Validate( const IGraphValidator& validator, IGraphValidationContext& context, GraphValidatorOutput& output )
	{
		context.OnStartedNodeValidation( this );

		Bool result = true;
		result &= m_graph != nullptr;

		if ( IGraphDescriptor* subGraph = GetSubGraphDescriptor() )
		{
			subGraph->Validate( validator, context, output );
		}
		result &= OnValidate( validator, context, output );

		context.OnEndedNodeValidation( this );

		return result;
	}

	void IGraphNodeDescriptor::GetSocketsButPossiblyCreateThemBeforehand( red::DynArray< SocketId >& outSockets )
	{
		// szymoniwanski: Dear author, please consider reworking this.
		if ( m_sockets.Empty() )
		{
			Uint32 socketsCount = GetSocketsCount();
			for ( SocketIndex index = 0; index < socketsCount; ++index )
			{
				THandle< SocketDescription > socket = CreateHandle< SocketDescription >();
				socket->m_id = index;

				GetSocketDescription( index, socket );

				OnSocketCreated( index, socket );
				m_sockets.PushBack( socket );
			}
		}
			
		for ( const auto& socket : m_sockets )
		{
			outSockets.PushBack( socket->m_id );
		}
	}

	THandle< SocketDescription > IGraphNodeDescriptor::GetSocketDescription( SocketId socketId ) const
	{
		SocketIndex index = 0;
		for ( auto& socket : m_sockets )
		{
			if ( socket->m_id == socketId )
			{
				GetSocketDescription( index, socket );
				return socket;
			}
			index++;
		}

		return nullptr;
	}

	SocketsIterator IGraphNodeDescriptor::GetSockets() const
	{
		return SocketsIterator( m_sockets );
	}

	void IGraphNodeDescriptor::RefreshOutputSocket()
	{
		const THandle<tools::SocketDescription>& outputSocket = m_sockets[ consts::ANIM_OUTPUT_SOCKET_ID ];
		GetSocketDescription( consts::ANIM_OUTPUT_SOCKET_ID, outputSocket );
		OnSocketCreated( consts::ANIM_OUTPUT_SOCKET_ID, outputSocket );

		m_graph->NotifyNodeSocketsCreated( m_id );
	}

	ConnectionsIterator IGraphNodeDescriptor::GetSourceConnections( Bool onlyActive ) const
	{
		if ( !m_graph )
			return ConnectionsIterator();

		return ConnectionsIterator( m_graph->GetConnectionsMap(), onlyActive, m_id, ConnectionsIterator::SOURCE_CONNECTION );
	}
	ConnectionsIterator IGraphNodeDescriptor::GetDestinationConnections( Bool onlyActive  ) const
	{
		if ( !m_graph )
			return ConnectionsIterator();

		return ConnectionsIterator( m_graph->GetConnectionsMap(), onlyActive, m_id, ConnectionsIterator::DESTINATION_CONNECTION );
	}
	ConnectionsIterator IGraphNodeDescriptor::GetSourceConnections( Bool onlyActive, IGraphNodeDescriptor::SocketId socketId ) const
	{
		if ( !m_graph )
			return ConnectionsIterator();

		return ConnectionsIterator( m_graph->GetConnectionsMap(), onlyActive, m_id, socketId, ConnectionsIterator::SOURCE_CONNECTION );
	}
	ConnectionsIterator IGraphNodeDescriptor::GetDestinationConnections( Bool onlyActive, IGraphNodeDescriptor::SocketId socketId ) const
	{
		if ( !m_graph )
			return ConnectionsIterator();

		return ConnectionsIterator( m_graph->GetConnectionsMap(), onlyActive, m_id, socketId, ConnectionsIterator::DESTINATION_CONNECTION );
	}

	Bool IGraphNodeDescriptor::CanConnect( const SocketInfo& source, const SocketInfo& destination )
	{
		if ( !source.m_node->CanConnectTo( source.m_socketId, destination, true ) )
		{
			return false;
		}

		if ( !destination.m_node->CanConnectTo( destination.m_socketId, source, false ) )
		{
			return false;
		}

		return true;
	}

	void IGraphNodeDescriptor::Connect( const SocketInfo& source, const SocketInfo& destination, GraphId connectionId )
	{
		source.m_node->ProcessConnectionTo( source.m_socketId, destination, true, connectionId );
		destination.m_node->ProcessConnectionTo( destination.m_socketId, source, false, connectionId );
	}

	Bool IGraphNodeDescriptor::CanRemoveConnection( const SocketInfo& source, const SocketInfo& destination, GraphId connectionId )
	{
		return
			source.m_node->CanRemoveConnection( source.m_socketId, destination, true, connectionId )
			&& destination.m_node->CanRemoveConnection( destination.m_socketId, source, false, connectionId );
	}

	void IGraphNodeDescriptor::RemoveConnection( const SocketInfo& source, const SocketInfo& destination, GraphId connectionId )
	{
		source.m_node->RemoveConnectionFrom( source.m_socketId, destination, true, connectionId );
		destination.m_node->RemoveConnectionFrom( destination.m_socketId, source, false, connectionId );
	}

	void IGraphNodeDescriptor::AppendSockets( const red::DynArray< SocketId >& socketIds, const red::DynArray< CName >& hiddenSockets /*= red::DynArray< CName >()*/, const red::DynArray< SocketNameConnectionPair >& socketNameToConnection /*= red::DynArray< SocketNameConnectionPair >()*/ )
	{
		Uint32 count = GetSocketsCount();
		Uint32 alreadyExisting = count - socketIds.Size();
		RED_ASSERT( alreadyExisting >= 0 );

		for ( SocketIndex index : socketIds.Indices() )
		{
			m_sockets.PushBack( CreateHandle< SocketDescription >() );
			m_sockets.Back()->m_id = socketIds[index];

			GetSocketDescription( alreadyExisting + index, m_sockets.Back() );
			OnSocketCreated( alreadyExisting + index, m_sockets.Back(), hiddenSockets, socketNameToConnection );
		}

		m_graph->NotifyNodeSocketsCreated( m_id );
	}

	void IGraphNodeDescriptor::InsertSocket( Uint32 index, SocketId socketId )
	{
		auto result = m_sockets.InsertAt( index, CreateHandle< SocketDescription >() );
		if ( result.IsSuccessful() )
		{
			auto it = result.Iterator();
			( *it )->m_id = socketId;

			GetSocketDescription( index, *it );
			OnSocketCreated( index, *it );
		}

		 m_graph->NotifyNodeSocketsCreated( m_id );
	}

	void IGraphNodeDescriptor::RemoveSockets( const red::DynArray< SocketId >& socketIds )
	{
		// WCTODO: something better than n^2?
		for ( SocketId socketId : socketIds )
		{
			for ( auto index : m_sockets.ReverseIndices() )
			{
				if ( m_sockets[index]->m_id == socketId )
				{
					OnSocketRemoved( index, m_sockets[index] );
					m_sockets.RemoveAt( index );
				}
			}
		}

		m_graph->NotifyNodeSocketsRemoved( m_id, socketIds );
	}

	void IGraphNodeDescriptor::RemoveSocketAt( Uint32 index )
	{
		if ( index >= m_sockets.Size() )
			return;

		OnSocketRemoved( index, m_sockets[index] );
		SocketId id = m_sockets[index]->m_id;

		m_sockets.RemoveAt( index );

		m_graph->NotifyNodeSocketsRemoved( m_id, red::DynArray< SocketId >{ id } );
	}

	void IGraphNodeDescriptor::ReorderSocket( SocketId socketId, Uint32 index, Bool notify )
	{
		if ( index >= m_sockets.Size() )
		{
			return;
		}

		THandle< SocketDescription > description;
		Uint32 oldIndex = -1;

		for ( auto i : m_sockets.ReverseIndices() )
		{
			if ( m_sockets[ i ]->m_id == socketId )
			{
				description = m_sockets[ i ];
				oldIndex = i;
				break;
			}
		}

		if ( description && oldIndex != -1 )
		{			
			m_sockets.RemoveAt( oldIndex );
			if ( index == m_sockets.Size() )
			{
				m_sockets.PushBack( description );
			}
			else
			{
				m_sockets.InsertAt( index, description );
			}
		}

		if ( notify )
		{
			m_graph->NotifyNodeSocketsChanged( m_id );
		}
	}

	void IGraphNodeDescriptor::ReorderSockets( const red::DynArray< GraphSocketId >& graphSockets )
	{
		red::DynArray< Uint32 > indices;

		Uint32 optionNo = -1;
		for ( auto id : graphSockets )
		{
			for ( Uint32 index = 0; index < m_sockets.Size(); ++index )
			{
				if ( m_sockets[ index ]->m_id == id )
				{
					indices.PushBack( index );
					break;
				}
			}
		}

		std::sort( indices.Begin(), indices.End(), []( Uint32 a, Uint32 b ) { return a < b; } );

		for ( Uint32 i = 0; i < graphSockets.Size(); ++i )
		{
			ReorderSocket( graphSockets[ i ], indices[ i ] );
		}

		m_graph->NotifyNodeSocketsChanged( m_id );
	}

	void IGraphNodeDescriptor::RemoveSocket( SocketId socketId )
	{
		for ( Int32 i=m_sockets.Size()-1; i >= 0; --i )
		{
			if ( m_sockets[i]->m_id == socketId )
			{
				OnSocketRemoved( i, m_sockets[i] );
				m_sockets.RemoveAt( i );

				m_graph->NotifyNodeSocketsRemoved( m_id, red::DynArray< SocketId >{ socketId } );
			}
		}
	}

	void IGraphNodeDescriptor::GatherSupportedNodeClasses( red::DynArray< String >& childrenClassTypes ) const
	{
		// Gather all definition classes that can be spawned by that descriptor class instance.
		// NOTICE: Currently method is extremely costly. There are two main issues. One is allocations that could be addressed with introduction of 'Inspectors' to ITypeSystem interface.
		// Other issues is costly implementation of EnumClasses - looks like it will get addressed in some future - as with detachment of scripts from RTTI, system will be stable and class hierarchy can be computed and easily traversable.
		struct Local
		{
			const IGraphNodeDescriptor&			m_this;

			Local( const IGraphNodeDescriptor& desc )
				: m_this( desc ) {}

			void CollectDescriptorClassesRec( const rtti::ClassType* descType, red::DynArray< const rtti::ClassType* >& descTypeSubClasses )
			{
				Int32 baseSize = descTypeSubClasses.Size();
				GetRttiSystem().EnumDerivedClasses( descType, descTypeSubClasses );
				for ( Int32 i = descTypeSubClasses.Size()-1; i >= baseSize; --i )
				{
					// for abstract classes move down the hierarchy
					if ( descTypeSubClasses[ i ]->IsAbstract() )
					{
						const rtti::ClassType* classType = descTypeSubClasses[ i ];
						descTypeSubClasses.RemoveAt( i );
						CollectDescriptorClassesRec( classType, descTypeSubClasses );
					}
				}
			}

			void CollectDefinitionsRec( const rtti::ClassType* defClass, red::DynArray< String >& childrenClassTypes, const red::DynArray< const rtti::ClassType* >& descTypeSubClasses, red::DynArray< const rtti::ClassType* >& defTypeSubClasses )
			{
				// stop the recustion if possible
				for ( const rtti::ClassType* descClass : descTypeSubClasses )
				{
					IGraphNodeDescriptor* nodeDesc = descClass->GetDefaultObject< IGraphNodeDescriptor >();
					if ( nodeDesc->GetRepresentationClass() == defClass )
					{
						return;
					}
				}

				// collect particular class
				if ( !defClass->IsAbstract() && m_this.FilterRepresentationClass( *defClass ) )
				{
					childrenClassTypes.PushBack( defClass->GetName().AsChar() );
				}

				// collect class immediate children
				{
					Uint32 baseListSize = defTypeSubClasses.Size();

					GetRttiSystem().EnumDerivedClasses( defClass, defTypeSubClasses );
					for ( Uint32 i = baseListSize, n = defTypeSubClasses.Size(); i < n; ++i )
					{
						CollectDefinitionsRec( defTypeSubClasses[ i ], childrenClassTypes, descTypeSubClasses, defTypeSubClasses );
					}

					defTypeSubClasses.Resize( baseListSize );
				}
			}
		} local( *this );

		const rtti::ClassType* descType = GetClass();
		const rtti::ClassType* defClass = GetRepresentationClass();

		if ( descType->IsAbstract() || !defClass )
		{
			return;
		}

		red::DynArray< const rtti::ClassType* > descTypeSubClasses{ red::PoolFrame() };
		red::DynArray< const rtti::ClassType* > defTypeSubClasses{ red::PoolFrame() };

		// collect descriptor immediate children classes
		local.CollectDescriptorClassesRec( descType, descTypeSubClasses );

		// collect definition classes
		local.CollectDefinitionsRec( defClass, childrenClassTypes, descTypeSubClasses, defTypeSubClasses );
	}

	THandle< CResource > IGraphNodeDescriptor::GetSubGraphResource() const
	{
		return m_graph ? m_graph->GetParentResource().ToHandle() : THandle< CResource >();
	}

	Bool IGraphNodeDescriptor::UpdateCaption()
	{
		String caption = GetCaption();
		if ( caption != m_caption )
		{
			m_caption = caption;
			m_graph->InvalidateNodeDescriptionProperty( GetId(), "caption" );
			return true;
		}
		return false;
	}

	Bool IGraphNodeDescriptor::UpdateSubCaption()
	{
		String subCaption = GetSubCaption();
		if ( subCaption != m_subtitle )
		{
			m_subtitle = subCaption;
			m_graph->InvalidateNodeDescriptionProperty( GetId(), "subtitle" );
			return true;
		}
		return false;
	}

	Bool IGraphNodeDescriptor::UpdateColors()
	{
		Color headerColor = GetBlockColor();
		Color captionColor = GetBlockTextColor();

		Bool retVal = false;

		if ( !(headerColor == m_headerColor) )
		{
			m_headerColor = headerColor; 
			retVal = true;
			m_graph->InvalidateNodeDescriptionProperty( GetId(), "headerColor" );
		}

		if ( !(captionColor == m_captionColor) )
		{
			m_captionColor = captionColor;
			retVal = true;
			m_graph->InvalidateNodeDescriptionProperty( GetId(), "captionColor" );
		}
		
		return retVal;
	}

	void IGraphNodeDescriptor::OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& value )
	{
		CName propName;
		if ( rtti::PathParser(path).EatName( propName ) )
		{
			m_graph->InvalidateNodeDescriptionProperty( GetId(), propName.AsChar() );
		}

		ISerializable::OnPropertyPostChange( path, oldValue, value );
	}

	Bool IGraphNodeDescriptor::PassesCriteria( const String& data, Uint32 searchType, String& outLabel, String& outType )
	{
		auto searchProcessType = static_cast< SearchProcessType >( searchType );
		switch( searchProcessType )
		{
		case tools::SearchProcessType::Label:
			if( GetCaption().Contains( data ) )
			{
				outLabel = GetCaption();
				outType = GetClass()->GetDescriptiveName();
				return true;
			}

			break;
		default:
			break;
		}
		
		return false;
	}

	String IGraphNodeDescriptor::ToString() const
	{
		red::DynArray< THandle< ISerializable > > events{ red::PoolBackend() };
		red::StringBuilder<String> builder;
		String output;

		const THandle< ISerializable > data = GetNodeData();
		DataToString( builder, data );
		output += builder.ToString();

		GetNodeEventsData( events );
		for ( const auto& event : events )
		{
			builder.Reset();
			DataToString( builder, event );
			output += builder.ToString();
		}

		return output;
	}

	String IGraphNodeDescriptor::ToDebugString() const
	{
		return String::EMPTY();
	}

	GraphComponentId IGraphNodeDescriptor::CreateComponent( const String& type )
	{
		THandle< GraphComponentDescription > component = OnCreateComponent( type );
		
		if ( !component )
			return false;

		m_nodeComponents.Insert( component->m_id, component );

		return component->m_id;
	}

	GraphComponentId IGraphNodeDescriptor::CreateComponent( SocketId socketId, const String& type )
	{
		THandle< GraphComponentDescription > component = OnCreateComponent( socketId, type );

		if ( !component )
			return InvalidComponentIndex;

		Components* components = m_socketsComponents.FindPtr( socketId );
		if ( !components )
		{
			components = &( m_socketsComponents.Insert( socketId, Components() ).Iterator() ).Value();
		}

		components->Insert( component->m_id, component );

		return component->m_id;
	}

	void IGraphNodeDescriptor::GetNodeComponents( red::DynArray< GraphComponentId >& outComponents ) const
	{
		for ( auto& component : m_nodeComponents )
		{
			outComponents.PushBack( component.Value()->m_id );
		}
	}

	void IGraphNodeDescriptor::GetSocketComponents( SocketId socketId, red::DynArray< GraphComponentId >& outComponents ) const
	{
		const Components* components = m_socketsComponents.FindPtr( socketId );

		if ( !components )
			return;

		for ( auto& component : *components )
		{
			outComponents.PushBack( component.Value()->m_id );
		}
	}

	THandle< GraphComponentDescription > IGraphNodeDescriptor::GetNodeComponent( GraphComponentId id ) const
	{
		const THandle< GraphComponentDescription >* component = m_nodeComponents.FindPtr( id );

		if ( !component )
			return nullptr;

		OnGetNodeComponent( component->Get() );
		return *component;
	}

	THandle< GraphComponentDescription > IGraphNodeDescriptor::GetSocketComponent( SocketId socketId, GraphComponentId id ) const
	{
		const Components* components = m_socketsComponents.FindPtr( socketId );

		if ( !components )
			return nullptr;

		const THandle< GraphComponentDescription >* component = ( *components ).FindPtr( id );

		if ( !component )
			return nullptr;

		OnGetSocketComponent( socketId, component->Get() );
		return *component;
	}

} // namespace tools
