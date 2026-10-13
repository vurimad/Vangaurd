/**
* Copyright (c) 2017-2020 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "editorGraphObjectResolver.h"
#include "graphDataEditableProxy.h"
#include "objectHandler.h"
#include "objectSerializablePersistentCollection.h"
#include "editorGraphEx.h"
#include "objectActionCreateObject.h"
#include "selectionState.h"

#include "../../../common/redReflection/include/serializationSaver.h"
#include "../../../common/redReflection/include/serializationLoader.h"
#include "../../../common/redFileSystem//include/memoryFileWriter.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( ConnectionDataHolder, tools )
	RTTI_PARENT_TYPE( GraphBackendData )
	RTTI_PROPERTY( m_desc );
	RTTI_PROPERTY( m_data );
RTTI_END_TYPE()

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( EditorGraphObjectResolver, tools )
	RTTI_PARENT_TYPE( tools::IObjectResolver );
RTTI_END_TYPE()

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( IGraphObjectConstructor, tools )
RTTI_END_TYPE()

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( BaseGraphObjectConstructor, tools )
	RTTI_PARENT_TYPE( IGraphObjectConstructor );
RTTI_END_TYPE()

RTTI_BEGIN_TYPE_IN_NAMESPACE( GraphConnectionObjectConstructor, tools )
	RTTI_PARENT_TYPE( BaseGraphObjectConstructor );
RTTI_END_TYPE()

RTTI_BEGIN_TYPE_IN_NAMESPACE( GraphNodeObjectConstructor, tools )
	RTTI_PARENT_TYPE( BaseGraphObjectConstructor );
RTTI_END_TYPE()

namespace
{
	const CName c_nodeKind = RED_NAME_CONSTEXPR( "NodeDescription" );
	const CName c_connectionKind = RED_NAME_CONSTEXPR( "ConnectionDescription" );
}

namespace tools
{
	NodeCreationInfo::NodeCreationInfo()
		: m_data( String::EMPTY() )
		, m_initialPosition( 0, 0 )
	{}

	NodeCreationInfo::NodeCreationInfo( const String& type, const Point initialPosition )
		: m_data( type )
		, m_initialPosition( initialPosition )
	{
	}

	ConnectionCreationInfo::ConnectionCreationInfo()
		: m_sourceNodeId( 0 )
		, m_sourceSocketId( 0 )
		, m_destinationNodeId( 0 )
		, m_destinationSocketId( 0 )
		, m_isInactive( false )
		, m_isDisabled( false )
	{}

	ConnectionCreationInfo::ConnectionCreationInfo( GraphId sourceNodeId, GraphSocketId sourceSocketId, GraphId destNodeId, GraphSocketId destSocketId, const Bool isDisabled )
		: m_sourceNodeId( sourceNodeId )
		, m_sourceSocketId( sourceSocketId )
		, m_destinationNodeId( destNodeId )
		, m_destinationSocketId( destSocketId )
		, m_isInactive( false )
		, m_isDisabled( isDisabled )
	{}

	//////////////////////////////////////////////////////////////////////////

	EditorObjectID ConnectionDataHolder::GetEditorObjectID() const
	{
		return m_desc->GetEditorObjectID();
	}

	namespace helper
	{
		Bool IsConnectionDataType( const CName& contentTypeName )
		{
			return contentTypeName == ConnectionDescription::sm_kind || contentTypeName == SocketBinding::sm_kind;
		}

		void SortDescriptors( red::DynArray< IObjectPersistentCollection::ObjectInfo >& objects )
		{
			std::sort( objects.Begin(), objects.End(),
			[]( const IObjectPersistentCollection::ObjectInfo& lhs, const IObjectPersistentCollection::ObjectInfo& rhs )
		{
			const Bool isLhsConnection = helper::IsConnectionDataType( lhs.m_id.GetKind() );
			const Bool isRhsConnection = helper::IsConnectionDataType( rhs.m_id.GetKind() );

			if ( isLhsConnection == isRhsConnection )
				return lhs.m_id < rhs.m_id;
			else if ( isLhsConnection && !isRhsConnection )
				return false; // move connection to the right
			else // if ( !isLhsConnection && isRhsConnection )
				return true; // leave it as it is
		}
		);
		}
	}

	//////////////////////////////////////////////////////////////////////////

	GraphConnectionObjectConstructor::GraphConnectionObjectConstructor( IEditorGraphEx* editor )
		: BaseGraphObjectConstructor( editor )
	{
	}

	const Bool GraphConnectionObjectConstructor::CanCreateObject( const ObjectCreationInfo& info, String* outReason ) const
	{
		ConnectionCreationInfo data;
		if( GetTypeObject<ConnectionCreationInfo>()->WriteValue( IRTTIContext::GetDefault(), &data, rtti::AccessPath(), *info.m_arguments ) )
			return true;

		return false;
	}

	const Bool GraphConnectionObjectConstructor::CreateObject( const ObjectCreationInfo& info, THandle< IBackendData >& outObject ) const
	{
		if ( !CanCreateObject( info ) )
			return false;

		ConnectionCreationInfo creationInfo;
		GetTypeObject<ConnectionCreationInfo>()->WriteValue( IRTTIContext::GetDefault(), &creationInfo, rtti::AccessPath(), *info.m_arguments );

		if ( !m_graphDescriptorEditor->CanCreateConnection( creationInfo ) )
			return false;

		GraphId connectionId = m_graphDescriptorEditor->CreateConnection( creationInfo );
		if ( connectionId == InvalidGraphId )
			return false;

		outObject = Cast< IBackendData >( ResolveObject( EditorObjectID::Build( connectionId, ConnectionDescription::sm_kind ) ) );
		return outObject != nullptr;
	}

	const Bool GraphConnectionObjectConstructor::CanDeleteObject( const EditorObjectID& id, Bool isCut ) const
	{
		return SupportsContentType( id.GetKind() );
	}

	const Bool GraphConnectionObjectConstructor::DeleteObject( const EditorObjectID& id, Bool isCut ) const
	{
		if ( !CanDeleteObject( id, isCut ) )
			return false;

		return m_graphDescriptorEditor->RemoveConnection( id.GetInternalID() );
	}

	const Bool GraphConnectionObjectConstructor::CanRestoreObject( const IObjectPersistentCollection::ObjectInfo& info ) const
	{
		return SupportsContentType( info.m_id.GetKind() );
	}

	const Bool GraphConnectionObjectConstructor::RestoreObject( const GraphObjectPlacementInfo& placementInfo ) const
	{
		if ( !placementInfo.m_info.m_object->IsA< ConnectionDataHolder >() )
			return false;

		THandle< ConnectionDataHolder > holder = Cast< ConnectionDataHolder >( placementInfo.m_info.m_object );
		if ( !m_graphDescriptorEditor->RestoreConnection( holder->m_desc, holder->m_data ) )
			return false;

		placementInfo.m_outObjects.Insert( placementInfo.m_info.m_object->GetEditorObjectID(), Cast<IBackendData >( placementInfo.m_info.m_object ) );
		return true;
	}

	const Bool GraphConnectionObjectConstructor::CanPlaceObject( const IObjectPersistentCollection::ObjectInfo& info ) const
	{
		return SupportsContentType( info.m_id.GetKind() );
	}

	const Bool GraphConnectionObjectConstructor::PlaceObject( const GraphObjectPlacementInfo& placementInfo, Bool isInPlace ) const
	{
		if ( !placementInfo.m_info.m_object->IsA< ConnectionDataHolder >() )
			return false;

		THandle<ConnectionDataHolder> connectionHolder = Cast< ConnectionDataHolder >(placementInfo.m_info.m_object);
		THandle< ConnectionDescription > desc = connectionHolder->m_desc;
		
		ConnectionCreationInfo info{ desc->m_sourceNode, desc->m_sourceSocketIndex, desc->m_destinationNode, desc->m_destinationSocketIndex };

		auto it = std::find_if( placementInfo.m_outObjects.Begin(), placementInfo.m_outObjects.End(), [info]( const red::Map< tools::EditorObjectID, THandle< IBackendData > >::ElementType& element )
		{
			return element.Key().GetInternalID() == info.m_sourceNodeId;
		} );
		if ( it != placementInfo.m_outObjects.End() )
		{
			info.m_sourceNodeId = it.Value()->GetEditorObjectID().GetInternalID();
		}
		else if ( !ResolveObject( EditorObjectID::Build( info.m_sourceNodeId, GraphNodeObjectConstructor::sm_type ) ) )
		{
			RED_LOG_ERROR( "GraphConnectionObjectConstructor: source node hasn't been previously restored." );
			return false;
		}

		it = std::find_if( placementInfo.m_outObjects.Begin(), placementInfo.m_outObjects.End(), [info]( const red::Map< tools::EditorObjectID, THandle< IBackendData > >::ElementType& element )
		{
			return element.Key().GetInternalID() == info.m_destinationNodeId;
		} );
		if ( it != placementInfo.m_outObjects.End() )
		{
			info.m_destinationNodeId = it.Value()->GetEditorObjectID().GetInternalID();
		}
		else if ( !ResolveObject( EditorObjectID::Build( info.m_destinationNodeId, GraphNodeObjectConstructor::sm_type ) ) )
		{
			RED_LOG_ERROR( "GraphConnectionObjectConstructor: destination node hasn't been previously restored." );
			return false;
		}

		// todo: consider moving this to basic OnPlaceConnection
		GraphId connectionId = m_graphDescriptorEditor->CreateConnection( info );
		if ( connectionId != InvalidGraphId )
		{
			m_graphDescriptorEditor->PlaceConnection( connectionId, desc, connectionHolder->m_data );
			THandle< IBackendData > newConnection = m_graphDescriptorEditor->GetConnectionDescription( connectionId );
			placementInfo.m_outObjects.Insert( desc->GetEditorObjectID(), newConnection );
			return true;
		}

		return false;
	}

	const Bool GraphConnectionObjectConstructor::SupportsContentType( const CName& contentTypeName ) const
	{
		return helper::IsConnectionDataType( contentTypeName );
	}

	const Bool GraphConnectionObjectConstructor::CanResolveObject( const EditorObjectID& id ) const
	{
		return SupportsContentType( id.GetKind() );
	}

	THandle< ISerializable > GraphConnectionObjectConstructor::ResolveObject( const EditorObjectID& id, const IObjectResolver::ResolvingParams& params ) const
	{
		THandle< ISerializable > connectionDescriptor = BaseGraphObjectConstructor::ResolveObject( id, params );
		THandle< ISerializable > connectionData = m_graphDescriptorEditor->GetConnectionData( id.GetInternalID() );

		return CreateHandle< ConnectionDataHolder >( Cast< ConnectionDescription >( connectionDescriptor ), connectionData );
	}

	//////////////////////////////////////////////////////////////////////////

	CName GraphNodeObjectConstructor::sm_type = RED_NAME_CONSTEXPR( "NodeDescription" );

	GraphNodeObjectConstructor::GraphNodeObjectConstructor( IEditorGraphEx* editor )
		: BaseGraphObjectConstructor( editor )
	{
	}

	const Bool GraphNodeObjectConstructor::CanCreateObject( const ObjectCreationInfo& info, String* outReason ) const
	{
		NodeCreationInfo data;
		if( GetTypeObject<NodeCreationInfo>()->WriteValue( IRTTIContext::GetDefault(), &data, rtti::AccessPath(), *info.m_arguments ) )
			return true;

		return false;
	}

	const Bool GraphNodeObjectConstructor::CreateObject( const ObjectCreationInfo& info, THandle< IBackendData >& outObject ) const
	{
		if ( !CanCreateObject( info ) )
			return false;

		// create node
		NodeCreationInfo data;
		GetTypeObject<NodeCreationInfo>()->WriteValue( IRTTIContext::GetDefault(), &data, rtti::AccessPath(), *info.m_arguments );

		GraphId id = m_graphDescriptorEditor->CreateNode( nullptr, data.m_data, data.m_initialPosition );
		outObject = Cast< IBackendData >( m_graphDescriptorEditor->GetItemDescriptorData( id ) );

		return true;
	}

	const Bool GraphNodeObjectConstructor::CanDeleteObject( const EditorObjectID& id, Bool isCut ) const
	{
		return SupportsContentType( id.GetKind() ) && m_graphDescriptorEditor->CanRemoveNode( id.GetInternalID(), isCut );
	}

	const Bool GraphNodeObjectConstructor::DeleteObject( const EditorObjectID& id, Bool isCut ) const
	{
		if ( !CanDeleteObject( id, isCut ) )
			return false;

		return m_graphDescriptorEditor->RemoveNode( id.GetInternalID() );
	}

	const Bool GraphNodeObjectConstructor::CanRestoreObject( const IObjectPersistentCollection::ObjectInfo& info ) const
	{
		return SupportsContentType( info.m_id.GetKind() );
	}

	const Bool GraphNodeObjectConstructor::RestoreObject( const GraphObjectPlacementInfo& placementInfo ) const
	{
		if ( !CanRestoreObject( placementInfo.m_info ) )
			return false;

		if ( !m_graphDescriptorEditor->RestoreNode( Cast< NodeDescription >( placementInfo.m_info.m_object ) ) )
			return false;

		placementInfo.m_outObjects.Insert( placementInfo.m_info.m_id, Cast< IBackendData >( placementInfo.m_info.m_object ) );
		return true;
	}

	const Bool GraphNodeObjectConstructor::CanPlaceObject( const IObjectPersistentCollection::ObjectInfo& info ) const
	{
		return SupportsContentType( info.m_id.GetKind() );
	}

	const Bool GraphNodeObjectConstructor::PlaceObject( const GraphObjectPlacementInfo& placementInfo, Bool isInPlace ) const
	{
		THandle< NodeDescription > originalNode = Cast< NodeDescription >( placementInfo.m_info.m_object );

		red::DynArray< Uint8 > dataBuffer{ red::PoolBackend() };
		CMemoryFileWriter writer( dataBuffer );
		serialization::SavingContext savingContext( originalNode );
		if ( !serialization::SaveToMemory( savingContext, writer ) )
		{
			return false;
		}

		serialization::LoadingContext loadingContext;
		serialization::LoadingResult loadingResult;
		if ( !serialization::LoadFromMemory( dataBuffer.Data(), dataBuffer.DataSize(), loadingContext, loadingResult ) )
		{
			return false;
		}

		if ( loadingResult.m_loadedRootObjects.Empty() )
		{
			return false;
		}

		THandle< NodeDescription > newNode = Cast< NodeDescription >( loadingResult.m_loadedRootObjects[0] );
		newNode->m_position = originalNode->m_position - placementInfo.m_placementOffset;

		if ( !m_graphDescriptorEditor->PlaceNode( newNode, isInPlace ) )
			return false;

		placementInfo.m_outObjects.Insert( placementInfo.m_info.m_id, newNode );

		return true;
	}

	const Bool GraphNodeObjectConstructor::SupportsContentType( const CName& contentTypeName ) const
	{
		return contentTypeName == sm_type;
	}

	const Bool GraphNodeObjectConstructor::CanResolveObject( const EditorObjectID& id ) const
	{
		return SupportsContentType( id.GetKind() );
	}

	THandle< ISerializable > GraphNodeObjectConstructor::GetNodeDesc( const IObjectPersistentCollection::ObjectInfo& info ) const
	{
		if ( info.m_id.GetKind() == sm_type )
			return info.m_object;

		return nullptr;
	}

	//////////////////////////////////////////////////////////////////////////

	BaseGraphObjectConstructor::BaseGraphObjectConstructor( IEditorGraphEx* editor )
		: m_graphDescriptorEditor( editor )
	{
	}

	THandle< ISerializable > BaseGraphObjectConstructor::ResolveObject( const EditorObjectID& id, const IObjectResolver::ResolvingParams& params ) const
	{
		return m_graphDescriptorEditor->GetItemDescriptorData( id.GetInternalID() );
	}

	const Bool BaseGraphObjectConstructor::CanResolveProxy(  const THandle< ISerializable >& object ) const
	{
		return object && object->IsA< GraphBackendData >();
	}

	red::SharedPtr< IEditableProxy > BaseGraphObjectConstructor::ResolveProxy( const THandle< ISerializable >& object ) const
	{
		return GraphDataEditableProxy::Create( Cast< GraphBackendData >( object ) );
	}

	//////////////////////////////////////////////////////////////////////////

	EditorGraphObjectResolver::EditorGraphObjectResolver( IEditorGraphEx* graphDescriptorEditor )
		: m_graphDescriptorEditor( graphDescriptorEditor )
	{
		graphDescriptorEditor->ProvideConstructors( m_constructors );
	}

	void EditorGraphObjectResolver::SupplementAdditionalObjectIDPaths( red::DynArray< EditorObjectIDPath >& outPaths ) const
	{
		red::HashSet< EditorObjectIDPath > connectionsSet;
		red::DynArray< EditorObjectIDPath > nodesEditorPaths;

		for ( const EditorObjectIDPath& pathId : outPaths )
		{
			const GraphId id = pathId.GetFirstChildID().GetInternalID();
			THandle< ISerializable > item = m_graphDescriptorEditor->GetItemDescriptorData( id );
			EditorObjectID editorId = item->GetEditorObjectID();

			if ( editorId.GetKind() == c_connectionKind )
			{
				// resolving object because some items might be FallbackDataHolders etc. and not "native" NodeDescriptions, item obtained above is the native one
				// maybe we could iterate over m_constructors and move implementation to connection constructor
				const THandle< ISerializable > resolvedItem = ResolveObject( pathId );
				const EditorObjectID resolvedItemEditorId = resolvedItem->GetEditorObjectID();
				connectionsSet.Insert( EditorObjectIDPath( resolvedItemEditorId ) );
			}
			else if ( editorId.GetKind() == c_nodeKind )
			{
				// resolving object because some items might be FallbackDataHolders etc. and not "native" NodeDescriptions, item obtained above is the native one
				// maybe we could iterate over m_constructors and move implementation to node constructor
				const THandle< ISerializable > resolvedItem = ResolveObject( pathId );
				const EditorObjectID resolvedItemEditorId = resolvedItem->GetEditorObjectID();

				red::DynArray< GraphId > connectionsIds;
				m_graphDescriptorEditor->GetNodeConnections( id, connectionsIds );

				for ( GraphId connectionId : connectionsIds )
					connectionsSet.Insert( EditorObjectIDPath( EditorObjectID::Build( connectionId, c_connectionKind ) ) );

				nodesEditorPaths.PushBack( EditorObjectIDPath( resolvedItemEditorId ) );
			}
		}

		red::DynArray< EditorObjectIDPath > connectionsEditorPaths;
		connectionsSet.GetElements( connectionsEditorPaths );
		nodesEditorPaths.PushBack( connectionsEditorPaths );

		red::alg::PushBackUnique( outPaths, nodesEditorPaths.Begin(), nodesEditorPaths.End() );
	}

	Bool EditorGraphObjectResolver::IsResolvable( const EditorObjectIDPath& path, Bool forEditing ) const
	{
		if ( path.GetDepth() != 1 )
		{
			return false;
		}

		const EditorObjectID& id = path.GetFinalChildID();
		return SupportsContentType( id.GetKind() );
	}

	Bool EditorGraphObjectResolver::SupportsContentType( const CName& contentTypeName ) const
	{
		for ( const auto& ptr : m_constructors )
		{
			if ( ptr->SupportsContentType( contentTypeName ) )
				return true;
		}

		return false;
	}

	red::SharedPtr< IEditableProxy > EditorGraphObjectResolver::ResolveProxy( const EditorObjectIDPath& path, const ResolvingParams& params ) const
	{
		const auto object = ResolveObject( path, params );
		for ( const auto& ptr : m_constructors )
		{
			if ( ptr->CanResolveProxy( object ) )
				return ptr->ResolveProxy( object );
		}

		return red::SharedPtr< IEditableProxy >();
	}

	THandle< ISerializable > EditorGraphObjectResolver::ResolveObject( const EditorObjectIDPath& path, const ResolvingParams& params ) const
	{
		if ( path.GetDepth() != 1 )
		{
			return nullptr;
		}

		const EditorObjectID& id = path.GetFinalChildID();
		for ( const auto& ptr : m_constructors )
		{
			if ( ptr->CanResolveObject( id ) )
				return ptr->ResolveObject( id, params );
		}

		return nullptr;
	}

	Bool EditorGraphObjectResolver::CanCreateObject( const ObjectCreationInfo& info, String* outReason ) const
	{
		// Make sure we have a constructor that can handle the data type
		for ( const auto& ptr : m_constructors )
		{
			if ( ptr->CanCreateObject( info ) )
				return true;
		}

		return false;
	}
	
	Bool EditorGraphObjectResolver::CanDeleteObjects( const red::ArraySpan< const EditorObjectIDPath >& paths, Bool isCut, String* outFailReason ) const
	{
		for ( Uint32 i = paths.Size(); i-- > 0; )
		{
			if ( paths[i].GetDepth() != 1 )
			{
				return false;
			}

			const EditorObjectID& id = paths[i].GetFinalChildID();
			Bool constructorFound = false;
			for ( const auto& ptr : m_constructors )
			{
				if ( ptr->CanDeleteObject( id, isCut ) )
				{
					constructorFound = true;
					break;
				}
			}

			if ( !constructorFound )
			{
				if ( outFailReason )
				{
					*outFailReason = "No valid object constructor found";
				}

				return false;
			}
		}

		return true;
	}

	red::SharedPtr< IObjectPreview > EditorGraphObjectResolver::CreateObjectPreview( const ObjectCreationInfo& initInfo )
	{
		return red::SharedPtr< IObjectPreview >();
	}

	red::SharedPtr< IObjectPersistentCollection > EditorGraphObjectResolver::CaptureObjects( const red::ArraySpan< const EditorObjectIDPath >& paths, const CapturingParams& params )
	{
		IObjectResolver::ResolvingParams objectResolvingParams;
		objectResolvingParams.m_resolveAsClone = params.m_captureAsClone;
		objectResolvingParams.m_resolveAsCut = params.m_captureAsCut;

		red::DynArray< THandle< ISerializable > > objectsToCapture{ red::PoolBackend() };
		for ( const EditorObjectIDPath& path : paths )
		{
			objectsToCapture.PushBack( ResolveObject( path, objectResolvingParams ) );
		}

		return ObjectSerializablePersistentCollection::CaptureObjects( objectsToCapture );
	}

	red::SharedPtr< IObjectPersistentCollection > EditorGraphObjectResolver::Action_CreateObjects( const red::ArraySpan< const ObjectCreationInfo > creationInfos, const CreateObjectsOutParams& out )
	{
		if ( m_graphDescriptorEditor->IsReadOnly() )
			return false;

		red::DynArray< THandle< ISerializable > > objectsCaptureList{ red::PoolBackend() };

		for ( const ObjectCreationInfo& info : creationInfos )
		{
			THandle< IBackendData > newObject;
			for ( const auto& ptr : m_constructors )
			{
				if ( ptr->CanCreateObject( info ) )
				{
					if ( ptr->CreateObject( info, newObject ) )
					{
						break;
					}
				}
			}

			if ( !newObject )
			{
				RED_LOG_ERROR( "EditorGraphObjectResolver: No constructor serviced the new object request" );
				return red::SharedPtr< IObjectPersistentCollection >();
			}

			objectsCaptureList.PushBack( newObject );
			out.actualCreatedPaths.EmplaceBack( newObject->GetEditorObjectID() );
		}

		out.selectAll = true;

		return ObjectSerializablePersistentCollection::CaptureObjects( objectsCaptureList );
	}

	Bool EditorGraphObjectResolver::DeleteObjectsOfKind( const red::ArraySpan< const EditorObjectIDPath >& paths, const CName kind )
	{
		for ( const EditorObjectIDPath& path : paths )
		{
			if ( path.GetDepth() != 1 )
			{
				return false;
			}

			const EditorObjectID& id = path.GetFinalChildID();

			for ( const auto& ptr : m_constructors )
			{
				if ( ptr->SupportsContentType( kind ) && ptr->CanDeleteObject( id, false ) )
				{
					if ( !ptr->DeleteObject( id, false ) )
					{
						RED_LOG_ERROR( "EditorGraphObjectResolver: Object for deletion not found. Make sure global ID is valid." );
						return false;
					}
				}
			}
		}

		return true;
	}

	namespace
	{
		Bool ExtendConnections( GraphNodeId currNode,
			red::Map< GraphId, red::DynArray< ConnectionCreationInfo > >& connectionsToCreate,
			red::DynArray< ConnectionCreationInfo >& sourceConnections,
			red::Set< GraphNodeId >& visitedNodes )
		{
			auto connectionExits = []( const ConnectionCreationInfo& toCheck, const red::DynArray< ConnectionCreationInfo >& connections )
			{
				const auto it = std::find_if( connections.Begin(), connections.End(),
					[toCheck]( const ConnectionCreationInfo& connection )
				{
					return connection.m_sourceNodeId == toCheck.m_sourceNodeId && connection.m_sourceSocketId == toCheck.m_sourceSocketId &&
						connection.m_destinationNodeId == toCheck.m_destinationNodeId && connection.m_destinationSocketId == toCheck.m_destinationSocketId;
				} );

				return it != connections.End();
			};

			if( visitedNodes.Exist( currNode ) )
			{
				return false;
			}
			visitedNodes.Insert( currNode );

			Bool extensionDone = false;
			red::DynArray< ConnectionCreationInfo > tempNewConnections;
			tempNewConnections.Reserve( sourceConnections.Size() );
			red::DynArray< GraphNodeId > nextNodes;
			nextNodes.Reserve( sourceConnections.Size() );
			for( const ConnectionCreationInfo& connection : sourceConnections )
			{
				if( red::DynArray< ConnectionCreationInfo >* otherConnections = connectionsToCreate.FindPtr( connection.m_destinationNodeId ) )
				{					
					for( const ConnectionCreationInfo& otherConnection : *otherConnections )
					{
						ConnectionCreationInfo newConnection = connection;
						newConnection.m_destinationNodeId = otherConnection.m_destinationNodeId;
						newConnection.m_destinationSocketId = otherConnection.m_destinationSocketId;
						newConnection.m_isDisabled |= otherConnection.m_isDisabled;
						newConnection.m_isInactive |= otherConnection.m_isInactive;

						// Make sure we are not adding a duplicate
						if( !connectionExits( newConnection, tempNewConnections ) )
						{
							if( connectionExits( newConnection, sourceConnections ) )	// Nodes already connected, no need to extend
							{
								// Make sure this connection wasn't added
								if( !connectionExits( connection, tempNewConnections ) )
								{
									tempNewConnections.EmplaceBack( connection );
								}
							}
							else
							{
								extensionDone = true;

								tempNewConnections.EmplaceBack( newConnection );
								if( nextNodes.Empty() || nextNodes.Back() != connection.m_destinationNodeId )
								{
									nextNodes.EmplaceBack( connection.m_destinationNodeId );
								}
							}
						}
					}
				}
				else
				{
					// Not connected to a node that is going to be deleted, keep connection as is
					tempNewConnections.EmplaceBack( connection );
				}
			}

			sourceConnections = std::move( tempNewConnections );
			for( const GraphNodeId graphId : nextNodes )
			{
				extensionDone |= ExtendConnections( graphId, connectionsToCreate, sourceConnections, visitedNodes );
			}

			visitedNodes.Remove( currNode );
			return extensionDone;
		}

		void RemoveIntermediateNodes_ExtractConnectionsToCreate( IEditorGraphEx& graphEditor, const red::ArraySpan< const EditorObjectIDPath > paths, red::DynArray< ConnectionCreationInfo >& outConnections )
		{	
			auto isNodeBeingDeleted = [&paths]( GraphNodeId nodeId )
			{
				const auto it = std::find_if( paths.begin(), paths.end(), [&nodeId]( const EditorObjectIDPath& id )
				{
					if( id.GetDepth() == 1 && id.GetFinalChildID().GetKind() == c_nodeKind )
					{
						const GraphId deletedNodeId = id.GetFinalChildID().GetInternalID();
						return nodeId == deletedNodeId;
					}
					return false;
				} );
				return it != paths.end();
			};

			red::Map< GraphId, red::DynArray< ConnectionCreationInfo > > connectionsToCreate;
			connectionsToCreate.Reserve( paths.Size() );
			for( const EditorObjectIDPath& id : paths )
			{
				if( id.GetDepth() == 1 && id.GetFinalChildID().GetKind() == c_nodeKind )
				{
					const GraphId nodeId = id.GetFinalChildID().GetInternalID();
			
					red::DynArray< ConnectionCreationInfo > outConnectionInfos;
					if( graphEditor.CanRemoveIntermediateNode( nodeId, outConnectionInfos ) )
					{
						connectionsToCreate[nodeId] = std::move( outConnectionInfos );
					}
				}
			}

			// If we are removing multiple nodes, we need to correct the connections we are trying to create.
			// We extend all connections until we cannot extend them more.
			// A -> B -> C -> D -> E
			// If we remove B, C and D:
			//		- 1. Extend B;AC to B;AD + extend C;BD to C;BE
			//		- 2. Extend B;AD to B;AE
			//		- Done, connections for C and D will be discarded latter, they come from deleted nodes.
			Bool extensionOngoing = true;
			while( extensionOngoing )
			{
				extensionOngoing = false;

				for( auto it : connectionsToCreate )
				{
					const GraphId currDeletedNodeId = it.Key();
					red::DynArray< ConnectionCreationInfo >& connections = it.Value();

					red::Set< GraphNodeId > visitedNodes;
					visitedNodes.Reserve( connectionsToCreate.Size() );
					extensionOngoing |= ExtendConnections( currDeletedNodeId, connectionsToCreate, connections, visitedNodes );
				}
			}

			for( const red::DynArray< ConnectionCreationInfo >& connectionInfos : connectionsToCreate.Values() )
			{
				outConnections.Reserve( outConnections.Size() + connectionInfos.Size() );

				for( const ConnectionCreationInfo& connection : connectionInfos )
				{
					if( !isNodeBeingDeleted( connection.m_sourceNodeId ) && !isNodeBeingDeleted( connection.m_destinationNodeId ) )
					{
						// Make sure that we are still able to create this connection, merging connections may have yield to some invalid ones.
						// If we remove B, we would try to connect AC, but they are already connected.
						// 
						//     A -> B -> C
						//      \_______/
						// 
						if( graphEditor.CanCreateConnection( connection ) )
						{
							// Make sure that we won't try to create the same connection twice.
							// If we remove B and D, both would try to restore connection between AC, we only need one.
							// 
							//     A -> B -> C
							//      \___D___/
							//
							const auto it = std::find_if( outConnections.Begin(), outConnections.End(),
								[connection]( const ConnectionCreationInfo& otherConnection )
							{
								const Bool sameSource = ( connection.m_sourceNodeId == otherConnection.m_sourceNodeId ) && ( connection.m_sourceSocketId == otherConnection.m_sourceSocketId );
								const Bool sameDestination = ( connection.m_destinationNodeId == otherConnection.m_destinationNodeId ) && ( connection.m_destinationSocketId == otherConnection.m_destinationSocketId );
								return sameSource && sameDestination;
							} );

							if( it == outConnections.End() )
							{
								outConnections.EmplaceBack( connection );
							}
						}
					}
				}
			}
		}
	}

	namespace
	{
		static red::DynArray< GraphId > ExtractNodeIds( const red::ArraySpan< const EditorObjectIDPath >& paths )
		{
			red::DynArray< GraphId > nodesToDelete;
			nodesToDelete.Reserve( paths.Size() );

			for ( const EditorObjectIDPath& id : paths )
			{
				if ( id.GetDepth() == 1 && id.GetFinalChildID().GetKind() == RED_NAME_CONSTEXPR_NOREG( "NodeDescription" ) )
				{
					nodesToDelete.PushBack( id.GetFinalChildID().GetInternalID() );
				}
			}

			return nodesToDelete;
		}
	}

	red::UniquePtr< IEditAction > EditorGraphObjectResolver::CreateDeleteAction( ObjectHandler& objectHandler, const red::ArraySpan< const EditorObjectIDPath >& paths, const Bool isShiftModifier ) const
	{
		red::DynArray< EditorObjectIDPath > allPaths{ paths };
		SupplementAdditionalObjectIDPaths( allPaths );
		red::UniquePtr< IEditAction > deleteAction = IObjectResolver::CreateDeleteAction( objectHandler, allPaths, isShiftModifier );

		if( m_graphDescriptorEditor->HasRemoveIntermediateNodeSupport() )
		{
			// atm we can either add a deletion marker node or recreate connections
			// if would be nice to recreate connections after creating deletion marker node
			// but this requires some more work 
			const auto nodesToDelete = ExtractNodeIds( paths );
			if ( m_graphDescriptorEditor->IsPostDeleteActionNeeded( nodesToDelete ) )
			{
				auto postDeleteAction = m_graphDescriptorEditor->GetPostDeleteAction( objectHandler, nodesToDelete );
				RED_FATAL_ASSERT( postDeleteAction );

				red::UniquePtr< EditGroupAction > groupAction = red::CreateUniquePtr< EditGroupAction >( "Delete objects" );
				groupAction->AddAction( std::move( deleteAction ) );
				groupAction->AddAction( std::move( postDeleteAction ) );
				deleteAction = std::move( groupAction );
			}
			else
			{
				red::DynArray< ConnectionCreationInfo > connectionsToCreate;
				RemoveIntermediateNodes_ExtractConnectionsToCreate( *m_graphDescriptorEditor, allPaths, connectionsToCreate );
				if( !connectionsToCreate.Empty() )
				{
					red::UniquePtr< EditGroupAction > groupAction = red::CreateUniquePtr< EditGroupAction >( "Delete objects" );

					// 1. Delete all current nodes and connections
					groupAction->AddAction( std::move( deleteAction ) );

					// 2. Create connections
					{
						red::DynArray< ObjectCreationInfo > objectCreationInfos;
						objectCreationInfos.Reserve( connectionsToCreate.Size() );
						for( const ConnectionCreationInfo& connectionInfo : connectionsToCreate )
						{
							rtti::ValueHolderPtr arguments;
							GetTypeObject( connectionInfo )->ReadValue( IRTTIContext::GetDefault(), &connectionInfo, rtti::AccessPath{}, arguments );
							objectCreationInfos.EmplaceBack( std::move( arguments ) );
						}

						groupAction->AddAction( red::CreateUniquePtr< Action_CreateObject >( objectHandler, std::move( objectCreationInfos ), "Create object" ) );
					}

					deleteAction = std::move( groupAction );
				}
			}
		}

		return deleteAction;
	}

	Bool EditorGraphObjectResolver::Action_DeleteObjects( DeleteContext& deleteContext )
	{
		if ( m_graphDescriptorEditor->IsReadOnly() )
			return false;

		// we don't know in what order objects come, but we want to delete connections first and then nodes
		if ( !DeleteObjectsOfKind( deleteContext.pathsToDelete, ConnectionDescription::sm_kind ) )
			return false;

		if ( !DeleteObjectsOfKind( deleteContext.pathsToDelete, GraphNodeObjectConstructor::sm_type ) )
			return false;

		return true;
	}

	Bool UnpackObjects( const red::SharedPtr< IObjectPersistentCollection >& objects, red::DynArray< IObjectPersistentCollection::ObjectInfo >& unpackedSerializables )
	{
		if ( !objects )
		{
			RED_LOG_ERROR( "EditorGraphObjectResolver: No content to unpack objects from" );
			return false;
		}

		// unpack objects from storage
		
		const auto* serializedStorage = static_cast< ObjectSerializablePersistentCollection* >( objects.Get() );
		if ( !serializedStorage->UnpackObjects( ClassID< IBackendData >(), unpackedSerializables ) )
		{
			RED_LOG_ERROR( "EditorGraphObjectResolver: There are no objects that can be added" );
			return false;
		}

		return true;
	}

	Bool EditorGraphObjectResolver::Action_RestoreObjects( const red::SharedPtr< IObjectPersistentCollection >& objects, red::DynArray< EditorObjectIDPath >& outActualCreatedPaths )
	{
		if ( m_graphDescriptorEditor->IsReadOnly() )
			return false;

		red::DynArray< IObjectPersistentCollection::ObjectInfo > unpackedSerializables{ red::PoolBackend() };
		if ( !UnpackObjects( objects, unpackedSerializables ) )
			return false;

		// nothing to restore
		if ( unpackedSerializables.Empty() )
			return true;

		helper::SortDescriptors( unpackedSerializables );

		// restore nodes back in the prefabs there were coming from
		red::Map< EditorObjectID, THandle< IBackendData > > placedObjects;
		for ( const auto& obj : unpackedSerializables )
		{
			for ( const auto& ptr : m_constructors )
			{
				if ( ptr->CanRestoreObject( obj ) )
				{
					GraphObjectPlacementInfo placementInfo( obj, placedObjects );

					ptr->RestoreObject( placementInfo );
					break;
				}
			}
		}

		// placing the nodes may have changed their IDs, output the true ones
		for ( const auto& object : placedObjects )
			outActualCreatedPaths.PushBack( EditorObjectIDPath( object.Value()->GetEditorObjectID() ) );

		// all nodes must be restored for this operation to be valid
		return placedObjects.Size() == unpackedSerializables.Size();
	}

	void EditorGraphObjectResolver::RemoveInvalidObjects( red::DynArray< IObjectPersistentCollection::ObjectInfo >& objects )
	{
		// make sure connections are placed after nodes
		helper::SortDescriptors( objects );

		red::HashSet< GraphId > placedNodes{ red::PoolBackend() };

		auto it = objects.Begin();
		// we don't want to copy nodes which cannot be removed
		while ( it != objects.End() )
		{
			const IObjectPersistentCollection::ObjectInfo& info = *it;

			if ( info.m_id.GetKind() != GraphNodeObjectConstructor::sm_type )
				break;

			// TODO: constructor thingy shouldn't have GetNodeDesc method, this thing should be untangled
			THandle< NodeDescription > nodeDesc;
			for ( const auto& ptr : m_constructors )
			{
				nodeDesc = Cast< NodeDescription >( ptr->GetNodeDesc( info ) );
				if ( nodeDesc )
				{
					if ( !nodeDesc->CanBeRemoved( false ) )
						it = objects.Remove( it ).Iterator();
					else
					{
						placedNodes.Insert( info.m_id.GetInternalID() );
						++it;
					}
					break;
				}
			}

			if ( !nodeDesc )
			{
				++it;
			}
		}

		// also connections without both ends placed in this operation shouldn't be copied
		while ( it != objects.End() )
		{
			const IObjectPersistentCollection::ObjectInfo& info = *it;

			if (  info.m_id.GetKind() != ConnectionDescription::sm_kind )
				break;

			THandle< ConnectionDescription > desc = Cast< ConnectionDataHolder >( info.m_object )->m_desc;
			if ( !placedNodes.Exist( desc->m_sourceNode ) || !placedNodes.Exist( desc->m_destinationNode ) )
				it = objects.Remove( it ).Iterator();
			else
				++it;
		}
	}

	Bool EditorGraphObjectResolver::Action_PlaceObjects( const red::SharedPtr< IObjectPersistentCollection >& objects, const ObjectPlacementInfo& info, red::DynArray< EditorObjectIDPath >& outActualCreatedPaths )
	{
		if ( m_graphDescriptorEditor->IsReadOnly() )
			return false;

		red::DynArray< IObjectPersistentCollection::ObjectInfo > unpackedSerializables{ red::PoolBackend() };
		if ( !UnpackObjects( objects, unpackedSerializables ) )
			return false;

		// nothing to restore
		if ( unpackedSerializables.Empty() )
			return true;

		Point capturedCenter;
		Bool connectionOnly = unpackedSerializables.Size() == 1 && unpackedSerializables.Front().m_id.GetKind() == ConnectionDescription::sm_kind;
		Point capturedTopLeft( std::numeric_limits<Int32>::max(), std::numeric_limits<Int32>::max() );

		if ( !connectionOnly )
		{
			// calculate the arithmetic center of all placed objects
			for ( const auto& obj : unpackedSerializables )
			{
				THandle< NodeDescription > nodeDesc;
				for ( const auto& ptr : m_constructors )
				{
					nodeDesc = Cast< NodeDescription >( ptr->GetNodeDesc( obj ) );
					if ( nodeDesc )
					{
						Point nodePosition;
						if ( m_graphDescriptorEditor->ExtractNodePosition( nodeDesc, nodePosition ) )
						{
							capturedTopLeft.x = math::Min( capturedTopLeft.x, nodePosition.x );
							capturedTopLeft.y = math::Min( capturedTopLeft.y, nodePosition.y );
						}
						break;
					}
				}
			}

			RemoveInvalidObjects( unpackedSerializables );
		}
		else
		{
			capturedTopLeft = Point( 0, 0 );
		}

		// restore nodes back
		red::Map< EditorObjectID, THandle< IBackendData > > placedObjects;
		for ( const auto& obj : unpackedSerializables )
		{
			for ( const auto& ptr : m_constructors )
			{
				if ( ptr->CanPlaceObject( obj ) )
				{
					GraphObjectPlacementInfo placementInfo( obj, placedObjects );

					if ( info.m_useAbsoluteTransform )
					{
						const Vector3& position = info.m_transform.GetPosition3();
						placementInfo.m_placementOffset = capturedTopLeft - Point( (Int32)position.X, (Int32)position.Y );
					}

					ptr->PlaceObject( placementInfo, !info.m_useAbsoluteTransform );
					break;
				}
			}
		}

		// placing the nodes may have changed their IDs, output the true ones
		for ( const auto& object : placedObjects )
			outActualCreatedPaths.PushBack( EditorObjectIDPath( object.Value()->GetEditorObjectID() ) );

		// all nodes must be restored for this operation to be valid
		// - in some cases more objects can be created automatically (like connections based on component binding)
		if ( placedObjects.Size() >= unpackedSerializables.Size() )
		{
			m_graphDescriptorEditor->OnPostPlace();
			return true;
		}

		return false;
	}

	red::SharedPtr< IObjectPersistentCollection > EditorGraphObjectResolver::Action_ReplaceObjects( const red::ArraySpan< const EditorObjectIDPath >& objectPathsToReplace, const red::SharedPtr< IObjectPersistentCollection >& objectTemplate, red::DynArray< EditorObjectIDPath >& outActualCreatedPaths )
	{
		return red::SharedPtr< IObjectPersistentCollection >();
	}
}
