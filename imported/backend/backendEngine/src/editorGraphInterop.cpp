/**
* Copyright (c) 2016-2020 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "editorGraphInterop.h"

#include "../../../common/interop/include/interopUtilsRTTI.h"
#include "../../../common/redReflection/include/serializableCollector.h"
#include "../../../common/redReflection/include/serializableMap.h"
#include "objectHandler.h"
#include "graphEditorNode.h"
#include "treeInterop.h"
#include "resourceBackend.h"
#include "objectClipboard.h"
#include "objectSerializablePersistentCollection.h"
#include "editorGraphObjectTree.h"
#include "editorGraphVariableTree.h"
#include "graphDescriptor.h"
#include "resourceEditor.h"
#include "selectionIterator.h"
#include "../../common/redReflection/include/resourceDepot.h"
#include "editorGraphSelectionHandler.h"
#include "../../common/generalEditorBackend/include/graphObjectTreeItemData.h"
#include "../../common/redReflection/include/resource.h"

namespace Config
{
	TConfigVar< Bool > cvEditorConfigSaveTime( "Editor/NewGraph", "Enabled", true );
}

namespace tools
{
	namespace
	{
		const CName c_nodeKind = RED_NAME_CONSTEXPR( "NodeDescription" );
		const CName c_connectionKind = RED_NAME_CONSTEXPR( "ConnectionDescription" );
	}

	//----

	typedef red::Map< GraphSubGraphId, IEditorGraphPtr > SubGraphs;

	EditorGraphInteropCreationInfo::EditorGraphInteropCreationInfo( const ResourceEditorPtr& editor, const IEditorGraphPtr& editorGraph, ObjectCollection* selectionCollection /*= nullptr*/, ObjectCollection* variableTreeSelectionCollection /*= nullptr */ ) : m_editor( editor )
		, m_editorGraph( editorGraph )
		, m_selectionCollection( selectionCollection )
		, m_variableTreeSelectionCollection( variableTreeSelectionCollection )
	{
	}

	GraphInteropCreationInfo::GraphInteropCreationInfo( IEditActionHistory* editHistory, EditorGraphInterop* graphEditorInterop, IEditorGraphPtr editorGraph, ObjectCollection* selectionObject )
		: m_editorGraphInterop( graphEditorInterop )
		, m_editHistory( editHistory )
		, m_editorGraph( editorGraph )
		, m_selectionCollection( selectionObject )
	{
	}

	class GraphInteropObject : public interop::utils::Object
	{
	public:
		GraphInteropObject( const AnsiChar* typeName, GraphInterop* EditorGraphInterop )
			: interop::utils::Object( typeName )
			, m_graphInterop( EditorGraphInterop )
		{
		}

		EditorGraphInterop* GetGraphInteropEditor() { return m_graphInterop->m_graphEditor; }
		IEditorGraphEx* GetEditorGraph() { return m_graphInterop->m_graph.Get(); }

		void EnableNotifiers( Bool enable ) { m_graphInterop->m_enabledNotifiers = enable; }
		Bool IsNotifiersEnabled() const { return m_graphInterop->m_enabledNotifiers; }

		interop::ObjectPtr SetDataCollection( const THandle< ISerializable >& data ) { return m_graphInterop->SetDataCollection( data ); }
		ObjectCollection* GetSelectedDataObject() { return m_graphInterop->m_selectionDataObjects; }
		ObjectCollection* GetSelectedDescriptorObject() { return m_graphInterop->m_selectionDescriptorObjects.Get(); }
		red::HashMap< SerializableID, GraphId >& GetSelectedObjects() { return m_graphInterop->m_selectionMapping; }

		const THandle<ISerializable> GetUserContext() const { return m_graphInterop->m_userContext; }

		Bool HasGraphEditor( const IEditorGraphPtr& editor ) { return GetGraphInteropEditor()->HasGraphEditor( editor ); }
		void RemoveEditorGraph( GraphSubGraphId subGraphId ) { return GetGraphInteropEditor()->RemoveEditorGraph( subGraphId ); }
		void AddGraphEditor( const IEditorGraphPtr& editor ) { return GetGraphInteropEditor()->AddEditorGraph( editor ); }

	protected:
		GraphInterop* m_graphInterop;
	};

	namespace
	{
		template< typename T >
		String ArrayToString( const red::DynArray< T >& array )
		{
			if ( array.Empty() )
				return String::EMPTY();

			red::StringBuilder<String> arrayString;
			for ( const T& element : array )
				arrayString.Appendf( "%s ", ToStringDirect<T>( element ).AsChar() );

			return arrayString.ToString();
		}

		class SelectionInterop : public GraphInteropObject
		{
		private:
			class Action_MoveSelection : public IEditAction
			{
			public:
				Action_MoveSelection( GraphInteropObject* interopObject, const red::HashSet<GraphId>& nodesIds, const Int32 xOffset, const Int32 yOffset )
					: m_editorGraph( interopObject->GetEditorGraph() )
					, m_editorGraphInterop( interopObject->GetGraphInteropEditor() )
					, m_nodesIds( nodesIds )
					, m_offset( xOffset, yOffset )
				{
				}

				virtual String GetDescription() const override
				{
					return "Move selected blocks by the specified offset";
				}

				virtual Bool Do() override
				{
					for ( GraphId id : m_nodesIds )
					{
						if ( m_editorGraph->MoveNode( id, m_offset ) == Fail )
						{
							return false;
						}
						m_editorGraphInterop->OnInvalidateNodeDescriptionProperty( m_editorGraph->GetSubGraphId(), id, "position" );
					}

					return true;
				}

				virtual Bool Undo() override
				{
					for ( GraphId id : m_nodesIds )
					{
						if ( m_editorGraph->MoveNode( id, -m_offset ) == Fail )
						{
							return false;
						}
						m_editorGraphInterop->OnInvalidateNodeDescriptionProperty( m_editorGraph->GetSubGraphId(), id, "position" );
					}

					return true;
				}

			private:
				IEditorGraphEx*			m_editorGraph;
				EditorGraphInterop*		m_editorGraphInterop;
				red::HashSet<GraphId>	m_nodesIds;
				Point					m_offset;
			};

		public:
			void DeleteSelection( const red::HashSet< GraphId >& selection )
			{
				red::HashSet< EditorObjectIDPath > connectionsSet;
				red::DynArray< EditorObjectIDPath > nodesEditorPaths;

				for ( GraphId id : selection )
				{
					THandle< ISerializable > item = GetEditorGraph()->GetItemDescriptorData( id );
					EditorObjectID editorId = item->GetEditorObjectID();

					if ( editorId.GetKind() == c_connectionKind )
					{
						connectionsSet.Insert( EditorObjectIDPath( editorId ) );
					}
					else if ( editorId.GetKind() == c_nodeKind )
					{
						red::DynArray< GraphId > connectionsIds;
						GetEditorGraph()->GetNodeConnections( id, connectionsIds );

						for ( GraphId connectionId : connectionsIds )
							connectionsSet.Insert( EditorObjectIDPath( EditorObjectID::Build( connectionId, c_connectionKind ) ) );

						nodesEditorPaths.PushBack( EditorObjectIDPath( editorId ) );
					}
				}

				red::DynArray< EditorObjectIDPath > connectionsEditorPaths;
				connectionsSet.GetElements( connectionsEditorPaths );
				nodesEditorPaths.PushBack( connectionsEditorPaths );

				m_graphInterop->GetHandler()->DeleteObjects( nodesEditorPaths );
			}

			SelectionInterop( GraphInterop* graphInterop )
				: GraphInteropObject( "Selection", graphInterop )
			{
				const red::HashSet< GraphId >& selection = graphInterop->GetCachedSelection();
				RegisterFunctor<void, Int32, Int32>( "move", [ this, selection ]( ::interop::IContext& ctx, Int32 x, Int32 y )
				{
					GetGraphInteropEditor()->GetEditHistory()->Execute( red::CreateUniquePtr< Action_MoveSelection >( this, selection, x, y ) );
				} );

				RegisterFunctor<void>( "delete", [ this, selection ]( ::interop::IContext& ctx )
				{
					DeleteSelection( selection );
				} );
			}
		};

		class NodesInterop : public GraphInteropObject
		{
		public:
			NodesInterop( GraphInterop* graphInterop )
				: GraphInteropObject( "Nodes", graphInterop )
			{
				RegisterValueRO< red::DynArray< GraphNodeId > >( "nodesIDs", [ this ]()
				{
					red::DynArray< GraphNodeId > nodes{ red::PoolFrame() };
					GetEditorGraph()->GetNodes( nodes );
					return nodes;
				} );

				RegisterFunctor<Bool, GraphNodeId>( "canRemoveNode", [ this ]( ::interop::IContext& ctx, GraphNodeId nodeId )
				{
					return GetEditorGraph()->CanRemoveNode( nodeId, false );
				} );

				RegisterMap( "nodeDescription", [ this ]( interop::IContext& ctx, const String& nodeIdStr ) -> interop::ObjectPtr
				{
					GraphNodeId nodeId;
					if ( !FromString( nodeIdStr, nodeId ) )
					{
						ctx.ThrowException( "Invalid element id - should be Uint64" );
					}

					// TODO: Store the last Get result
					if ( GetEditorGraph()->GetNodeDescription( nodeId, m_nodeDescription ) )
					{
						return SetDataCollection( m_nodeDescription );
					}

					ctx.ThrowException( "Failed to create NodeDescription object" );
					return nullptr;
				} );

				RegisterMap( "nodeData", [ this ]( interop::IContext& ctx, const String& nodeId )
				{
					GraphNodeId id;
					if ( !FromString( nodeId, id ) )
					{
						ctx.ThrowException( "Invalid element id - should be Uint64" );
					}

					// TODO: Store the last Get result
					return SetDataCollection( GetEditorGraph()->GetItemDescriptorData( id ) );
				} );

				RegisterFunctor< GraphSubGraphId, GraphNodeId >( "getNodeSubGraphId", [ this ]( ::interop::IContext& ctx, GraphNodeId nodeId )
				{
					IEditorGraphPtr editorGraph = GetEditorGraph()->GetNodeSubGraph( nodeId );
					if ( !editorGraph )
						return InvalidSubGraphId;

					if ( !HasGraphEditor( editorGraph ) )
						AddGraphEditor( editorGraph );

					return editorGraph->GetSubGraphId();
				} );

				RegisterFunctor< red::DynArray< SearchProcessOutput >, String, Uint32 >( "filter", [ this ]( ::interop::IContext& ctx, const String& str, Uint32 type )
				{
					red::DynArray< SearchProcessOutput > outArray{ red::PoolFrame() };

					GetEditorGraph()->FilterNodes( outArray, str, type );

					return outArray;
				} );

				RegisterFunctor< red::DynArray< SearchProcessOutput >, String, SearchOptions >( "search", [ this ]( interop::IContext& ctx, const String& queryString, const SearchOptions& options )
				{
					red::DynArray< SearchProcessOutput > outArray;
					GetEditorGraph()->SearchNodes( outArray, queryString, options );

					std::stable_sort( outArray.Begin(), outArray.End(), []( const SearchProcessOutput& lhs, const SearchProcessOutput& rhs )
					{
						return lhs.m_label < rhs.m_label;
					} );

					return outArray;
				} );

				RegisterFunctor<Bool, GraphId>( "refresh", [ this ]( ::interop::IContext& ctx, GraphId nodeId )
				{
					return GetEditorGraph()->RefreshNode( nodeId );
				} );

				RegisterFunctor< void, GraphId >( "validate", [ this ]( ::interop::IContext& ctx, GraphId nodeId )
				{
					GetEditorGraph()->ValidateNode( nodeId );
				} );

				RegisterFunctor<void, red::DynArray< GraphId >>( "select", [ this, graphInterop ]( ::interop::IContext& ctx, const red::DynArray< GraphId >& ids )
				{
					red::DynArray< EditorObjectID > editorIDs{ red::PoolFrame() };

					for ( GraphId id : ids )
					{
						THandle< ISerializable > descriptorData = GetEditorGraph()->GetItemDescriptorData( id );
						if ( descriptorData )
						{
							editorIDs.PushBack( descriptorData->GetEditorObjectID() );
						}
					}

					graphInterop->GetHandler()->SetSelectionFromObjectIDs( editorIDs );
				} );

				if ( !graphInterop->GetCachedSelection().Empty() )
					RegisterObject( "selection", interop::ObjectPtr( RED_NEW( SelectionInterop )( graphInterop ) ) );

				RegisterFunctor<void>( "refreshLayout", [ this ]( interop::IContext& ctx ) { GetEditorGraph()->RefreshLayout(); } );

				RegisterFunctor<void, GraphId, String>( "dropData", [ this ]( interop::IContext& ctx, const GraphId nodeId, const String& data )
				{
					GetEditorGraph()->DropData( nodeId, data );
				} );

				///////////////////////////////////////////////////////////////////////
				// Node's sockets sub interface

				RegisterMap( "nodeSockets", [ this ]( interop::IContext& ctx, const String& nodeId )
				{
					GraphNodeId id;
					if ( !FromString( nodeId, id ) )
					{
						ctx.ThrowException( "Invalid element id - should be Uint64" );
					}

					auto getter = [ this, id ]( interop::IContext& ctx, red::DynArray< GraphSocketId >& outValue )
					{
						red::DynArray< GraphSocketId > sockets{ red::PoolFrame() };
						GetEditorGraph()->GetNodeSockets( id, outValue );
					};

					interop::utils::ValueObject<red::DynArray< GraphSocketId >>* socketsString = RED_NEW( interop::utils::ValueObject<red::DynArray< GraphSocketId >> )( getter, interop::utils::ValueObject<red::DynArray< GraphSocketId >>::TSetter() );
					return interop::ObjectPtr( socketsString );
				} );

				RegisterMap( "socketDescription", [ this ]( interop::IContext& ctx, const String nodeStr )
				{
					GraphConnectionId nodeId;
					if ( FromString( nodeStr, nodeId ) )
					{
						if ( nodeId == InvalidGraphId )
						{
							ctx.ThrowException( "Invalid ConnectionId" );
						}

						return interop::ObjectPtr( RED_NEW( interop::utils::MapObject )(
							[ this, nodeId ]( interop::IContext& ctx, const String& elementId ) -> interop::ObjectPtr
						{
							GraphSocketId socketId;
							if ( !FromString( elementId, socketId ) )
							{
								ctx.ThrowException( "Invalid element id - should be Uint64" );
							}

							// TODO: Store the last Get result
							if ( m_socketDescription = GetEditorGraph()->GetSocketDescription( nodeId, socketId ) )
							{
								return interop::utils::CreateRTTIAccessFunc( ctx, m_socketDescription.Get(), [ this, nodeId, socketId ]( const rtti::AccessPath& path, const red::String& )
								{
									THandle< NodeDescription > nodeDescription;
									GetEditorGraph()->GetNodeDescription( nodeId, nodeDescription );
									if ( THandle< IGraphNodeDescriptor > graphNodeDescriptor = Cast< IGraphNodeDescriptor >( nodeDescription ) )
									{
										graphNodeDescriptor->OnSocketDataChanged( socketId, path );
									}
								} );
							}

							ctx.ThrowException( "Failed to create SocketDescription object" );
							return nullptr;
						} ) );
					}
					else
					{
						return interop::ObjectPtr( RED_NEW( interop::utils::MapObject )(
							[ this, nodeStr ]( interop::IContext& ctx, const String& elementId ) -> interop::ObjectPtr
						{
							GraphSocketId socketId;
							if ( !FromString( elementId, socketId ) )
							{
								ctx.ThrowException( "Invalid element id - should be Uint64" );
							}

							// TODO: Store the last Get result
							if ( m_socketDescription = GetEditorGraph()->GetSocketDescription( nodeStr, socketId ) )
							{
								return interop::utils::CreateRTTIAccessFunc( ctx, m_socketDescription.Get() );
							}

							ctx.ThrowException( "Failed to create SocketDescription object" );
							return nullptr;
						} ) );
					}
				} );

				RegisterMap( "socketData", [ this ]( interop::IContext& ctx, const String& nodeIdStr )
				{
					GraphSocketId nodeId;
					if ( !FromString( nodeIdStr, nodeId ) )
					{
						ctx.ThrowException( "Invalid element id - should be Uint64" );
					}

					return interop::ObjectPtr( RED_NEW( interop::utils::MapObject )(
						[ this, nodeId ]( interop::IContext& ctx, const String& elementId ) -> interop::ObjectPtr
					{
						GraphSocketId socketId;
						if ( !FromString( elementId, socketId ) )
						{
							ctx.ThrowException( "Invalid element id - should be Uint64" );
						}

						return SetDataCollection( GetEditorGraph()->GetNodeSocketData( nodeId, socketId ) );
					} ) );
				} );

				RegisterFunctor<void, GraphId, GraphSocketId>( "copySocketsConnections",
					[ this ]( interop::IContext& ctx, GraphId sourceNodeId, GraphSocketId sourceSocketId )
				{
					red::DynArray< THandle< SocketBinding > > socketBindings{ red::PoolFrame() };
					GetEditorGraph()->CopySocketsConnections( sourceNodeId, sourceSocketId, socketBindings );

					red::DynArray< THandle< ISerializable > > serializables{ red::PoolFrame() };
					for ( const auto& socketBinding : socketBindings )
						serializables.PushBack( Cast< ISerializable >( socketBinding ) );

					red::SharedPtr< IObjectPersistentCollection > state = ObjectSerializablePersistentCollection::CaptureObjects( serializables );

					IObjectClipboard* clipboard = GetGraphInteropEditor()->GetBackend()->GetClipboard();
					clipboard->StoreData( state );
				}
				);

				RegisterFunctor<void, GraphId, GraphSocketId>( "pasteSocketsConnections",
					[ this ]( interop::IContext& ctx, GraphId destinationNodeId, GraphSocketId destinationSocketId )
				{
					IObjectClipboard* clipboard = GetGraphInteropEditor()->GetBackend()->GetClipboard();
					red::SharedPtr< IObjectPersistentCollection > state = clipboard->GetData();

					if ( !state )
					{
						ctx.ThrowException( "GraphInterop: No compatible data in the clipboard" );
						return; // no objects in clipboard
					}

					// unpack objects in the clipboard for inspection
					red::DynArray< IObjectPersistentCollection::ObjectInfo > objectInfos{ red::PoolFrame() };
					state->UnpackObjects( ClassID<ISerializable>(), objectInfos );
					if ( objectInfos.Empty() )
					{
						ctx.ThrowException( "GraphInterop: Clipboard is not empty but there are no objects" );
						return;
					}

					red::DynArray< ObjectCreationInfo > initInfos{ red::PoolFrame() };
					for ( auto& objectInfo : objectInfos )
					{
						if ( objectInfo.m_id.GetKind() == SocketBinding::sm_kind )
						{
							THandle< SocketBinding > socketBinding = Cast< SocketBinding >( objectInfo.m_object );

							ConnectionCreationInfo connectionCreationInfo{ destinationNodeId, destinationSocketId, socketBinding->m_nodeId, socketBinding->m_socketId };

							rtti::ValueHolderPtr value;
							GetTypeObject< ConnectionCreationInfo >()->ReadValue( IRTTIContext::GetDefault(), &connectionCreationInfo, rtti::AccessPath(), value );

							initInfos.PushBack( ObjectCreationInfo( value ) );
						}
					}

					if ( initInfos.Empty() )
					{
						ctx.ThrowException( "GraphInterop: No socket bindings present in the clipboard" );
						return;
					}

					m_graphInterop->GetHandler()->CreateObjects( initInfos );
				}
				);
			}

		private:
			THandle< NodeDescription > m_nodeDescription;
			THandle< SocketDescription > m_socketDescription;
		};

		class ConnectionsInterop : public GraphInteropObject
		{
		public:
			ConnectionsInterop( GraphInterop* graphInterop )
				: GraphInteropObject( "Connections", graphInterop )
			{
				///////////////////////////////////////////////////////////////////////
				// Node's connections sub interface

				RegisterMap( "nodeConnections", [ this ]( interop::IContext& ctx, const String& nodeId )
				{
					GraphNodeId id;
					if ( !FromString( nodeId, id ) )
					{
						ctx.ThrowException( "Invalid element id - should be Uint64" );
					}

					auto getter = [ this, id ]( interop::IContext& ctx, red::DynArray< GraphId > & outValue )
					{
						red::DynArray< GraphId > connections{ red::PoolFrame() };
						GetEditorGraph()->GetNodeConnections( id, connections );
						outValue = connections;
					};

					interop::utils::ValueObject<red::DynArray< GraphId >>* connectionsString = RED_NEW( interop::utils::ValueObject<red::DynArray< GraphId >> )( getter, interop::utils::ValueObject<red::DynArray< GraphId >>::TSetter() );
					return interop::ObjectPtr( connectionsString );
				} );

				RegisterFunctor<Bool, GraphNodeId, GraphSocketId, GraphNodeId, GraphSocketId>( "canCreateConnection", [ this ]( ::interop::IContext& ctx, GraphNodeId outputNodeId, GraphSocketId outputsocketId, GraphNodeId inputNodeId, GraphSocketId inputsocketId )
				{
					ConnectionCreationInfo info( outputNodeId, outputsocketId, inputNodeId, inputsocketId );
					return GetEditorGraph()->CanCreateConnection( info );
				} );

				RegisterFunctor<red::DynArray< GraphSocketId >, GraphNodeId, GraphSocketId, String>( "getEligibleSockets", [ this ]( ::interop::IContext& ctx, GraphNodeId outputNodeId, GraphSocketId outputSocketId, const String& nodeClass )
				{
					red::DynArray< GraphSocketId > sockets{ red::PoolFrame() };
					GetEditorGraph()->GetEligibleSockets( outputNodeId, outputSocketId, nodeClass, sockets );

					return sockets;
				} );

				RegisterFunctor<interop::GraphConnectionCreationData, ConnectionCreationInfo>( "getConnectionCreationData", [ this ]( ::interop::IContext& ctx, ConnectionCreationInfo info )
				{
					return GetEditorGraph()->GetConnectionCreationData( info );
				} );

				RegisterFunctor<void, GraphConnectionId, Bool>( "disableConnection", [ this ]( ::interop::IContext& ctx, GraphConnectionId connectionId, Bool value )
				{
					if ( !GetEditorGraph()->SetConnectionDisabled( connectionId, value ) )
						ctx.ThrowException( "Couldn't %hs a connection (Reason: %hs)", value ? "enable" : "disable", GetEditorGraph()->GetLastError().AsChar() );
				} );

				RegisterMap( "connectionDescription", [ this ]( interop::IContext& ctx, const String connectiondId ) -> interop::ObjectPtr
				{
					GraphConnectionId connectionId;
					if ( !FromString( connectiondId, connectionId ) )
					{
						ctx.ThrowException( "Invalid element id - should be Uint64" );
					}

					if ( connectionId == InvalidGraphId )
					{
						ctx.ThrowException( "Invalid ConnectionId" );
					}

					// TODO: Store the last Get result
					if ( m_connectionDescription = GetEditorGraph()->GetConnectionDescription( connectionId ) )
					{
						return interop::utils::CreateRTTIAccessFunc( ctx, m_connectionDescription.Get(), m_connectionDescription->GetClass(), false );
					}

					ctx.ThrowException( "Failed to create ConnectionDescription object" );
					return interop::ObjectPtr();
				} );

				RegisterMap( "connectionData", [ this ]( interop::IContext& ctx, const String& connectionIdStr )
				{
					GraphConnectionId connectionId;
					if ( !FromString( connectionIdStr, connectionId ) )
					{
						ctx.ThrowException( "Invalid element id - should be Uint64" );
					}

					// TODO: Store the last Get result
					return SetDataCollection( GetEditorGraph()->GetConnectionData( connectionId ) );
				} );
			}
		private:
			THandle< ConnectionDescription > m_connectionDescription;
		};

		class VariablesInterop : public GraphInteropObject
		{
		public:
			VariablesInterop( GraphInterop* graphInterop )
				: GraphInteropObject( "Variables", graphInterop )
			{
				RegisterValueRO<red::DynArray< GraphVariableId >>( "variables", [ this ]()
				{
					red::DynArray< GraphVariableId > variables{ red::PoolFrame() };
					GetEditorGraph()->GetVariables( variables );

					return variables;
				} );

				RegisterFunctor<GraphVariableId, String>( "createVariable", [ this ]( ::interop::IContext& ctx, const String& type )
				{
					return GetEditorGraph()->CreateVariable( type );
				} );

				RegisterFunctor<void, GraphVariableId>( "removeVariable", [ this ]( ::interop::IContext& ctx, GraphVariableId variableId )
				{
					GetEditorGraph()->RemoveVariable( variableId );
				} );

				RegisterMap( "variableData", [ this ]( interop::IContext& ctx, const String& variableIdStr )
				{
					GraphVariableId variableId;
					if ( !FromString( variableIdStr, variableId ) )
					{
						ctx.ThrowException( "Invalid element id - should be Uint64" );
					}

					// TODO: Store the last Get result
					return SetDataCollection( GetEditorGraph()->GetConnectionData( variableId ) );
				} );
			}
		};

		class ComponentsInterop : public GraphInteropObject
		{
		public:
			ComponentsInterop( GraphInterop* graphInterop )
				: GraphInteropObject( "Components", graphInterop )
			{
				RegisterFunctor<GraphComponentId, GraphId, String>( "createNodeComponent", [ this ]( interop::IContext& ctx, GraphId nodeId, const String& type )
				{
					return GetEditorGraph()->CreateNodeComponent( nodeId, type );
				} );

				RegisterFunctor<GraphComponentId, GraphId, GraphSocketId, String>( "createSocketComponent", [ this ]( interop::IContext& ctx, GraphId nodeId, GraphSocketId socketId, const String& type )
				{
					return GetEditorGraph()->CreateSocketComponent( nodeId, socketId, type );
				} );

				RegisterFunctor<GraphComponentId, GraphId, String>( "createConnectionComponent", [ this ]( interop::IContext& ctx, GraphId connectionId, const String& type )
				{
					return GetEditorGraph()->CreateConnectionComponent( connectionId, type );
				} );

				RegisterMap( "nodeComponents", [ this ]( interop::IContext& ctx, const String& nodeId )
				{
					GraphNodeId id;
					if ( !FromString( nodeId, id ) )
					{
						ctx.ThrowException( "Invalid element id - should be Uint64" );
					}

					auto getter = [ this, id ]( interop::IContext& ctx, red::DynArray< GraphComponentId >& outValue )
					{
						GetEditorGraph()->GetNodeComponents( id, outValue );
					};

					interop::utils::ValueObject< red::DynArray< GraphComponentId > >* components = RED_NEW( interop::utils::ValueObject<red::DynArray< GraphComponentId > > )( getter, interop::utils::ValueObject<red::DynArray< GraphComponentId > >::TSetter() );
					return interop::ObjectPtr( components );
				} );

				RegisterMap( "socketsComponents", [ this ]( interop::IContext& ctx, const String& nodeIdStr )
				{
					GraphId nodeId;
					if ( !FromString( nodeIdStr, nodeId ) )
					{
						ctx.ThrowException( "Invalid element id - should be Uint64" );
					}

					return interop::ObjectPtr( RED_NEW( interop::utils::MapObject )(
						[ this, nodeId ]( interop::IContext& ctx, const String& socketIdStr ) -> interop::ObjectPtr
					{
						GraphSocketId socketId;
						if ( !FromString( socketIdStr, socketId ) )
						{
							ctx.ThrowException( "Invalid element id - should be Uint64" );
						}

						auto getter = [ this, nodeId, socketId ]( interop::IContext& ctx, red::DynArray< GraphComponentId >& outValue )
						{
							GetEditorGraph()->GetSocketComponents( nodeId, socketId, outValue );
						};

						interop::utils::ValueObject< red::DynArray< GraphComponentId > >* components = RED_NEW( interop::utils::ValueObject< red::DynArray< GraphComponentId > > )( getter, interop::utils::ValueObject< red::DynArray< GraphComponentId > >::TSetter() );
						return interop::ObjectPtr( components );
					} ) );
				} );

				RegisterMap( "connectionsComponents", [ this ]( interop::IContext& ctx, const String& connectionIdStr )
				{
					GraphId connectionId;
					if ( !FromString( connectionIdStr, connectionId ) )
					{
						ctx.ThrowException( "Invalid element id - should be Uint64" );
					}

					auto getter = [ this, connectionId ]( interop::IContext& ctx, red::DynArray< GraphComponentId >& outValue )
					{
						GetEditorGraph()->GetConnectionComponents( connectionId, outValue );
					};

					interop::utils::ValueObject< red::DynArray< GraphComponentId > >* components = RED_NEW( interop::utils::ValueObject< red::DynArray< GraphComponentId > > )( getter, interop::utils::ValueObject< red::DynArray< GraphComponentId > >::TSetter() );
					return interop::ObjectPtr( components );
				} );

				RegisterMap( "nodeComponent", [ this ]( interop::IContext& ctx, const String nodeIdStr )
				{
					GraphId nodeId;
					if ( !FromString( nodeIdStr, nodeId ) )
					{
						ctx.ThrowException( "Invalid element id - should be Uint64" );
					}

					if ( nodeId == InvalidGraphId )
					{
						ctx.ThrowException( "Invalid ConnectionId" );
					}

					return interop::ObjectPtr( RED_NEW( interop::utils::MapObject )(
						[ this, nodeId ]( interop::IContext& ctx, const String& componentIdStr ) -> interop::ObjectPtr
					{
						GraphComponentId componentId;
						if ( !FromString( componentIdStr, componentId ) )
						{
							ctx.ThrowException( "Invalid element id - should be Uint64" );
						}

						// TODO: Store the last Get result
						m_component = GetEditorGraph()->GetNodeComponent( nodeId, componentId );
						if ( m_component )
						{
							return interop::utils::CreateRTTIAccessFunc( ctx, &m_component );
						}

						ctx.ThrowException( "Failed to create SocketDescription object" );
						return nullptr;
					} ) );
				} );

				RegisterMap( "socketComponent", [ this ]( interop::IContext& ctx, const String& nodeIdStr )
				{
					GraphId nodeId;
					if ( !FromString( nodeIdStr, nodeId ) )
					{
						ctx.ThrowException( "Invalid element id - should be Uint64" );
					}

					return interop::ObjectPtr( RED_NEW( interop::utils::MapObject )(
						[ this, nodeId ]( interop::IContext& ctx, const String& socketIdStr ) -> interop::ObjectPtr
					{
						GraphSocketId socketId;
						if ( !FromString( socketIdStr, socketId ) )
						{
							ctx.ThrowException( "Invalid element id - should be Uint64" );
						}

						return interop::ObjectPtr( RED_NEW( interop::utils::MapObject )(
							[ this, nodeId, socketId ]( interop::IContext& ctx, const String& componentIdStr ) -> interop::ObjectPtr
						{
							GraphComponentId componentId;
							if ( !FromString( componentIdStr, componentId ) )
							{
								ctx.ThrowException( "Invalid element id - should be Uint64" );
							}

							m_component = GetEditorGraph()->GetSocketComponent( nodeId, socketId, componentId );
							if ( m_component )
							{
								return interop::utils::CreateRTTIAccessFunc( ctx, &m_component );
							}

							ctx.ThrowException( "Failed to find SocketComponent object" );
							return nullptr;
						} ) );
					} ) );
				} );

				RegisterMap( "connectionComponent", [ this ]( interop::IContext& ctx, const String connectionIdStr )
				{
					GraphConnectionId connectionId;
					if ( !FromString( connectionIdStr, connectionId ) )
					{
						ctx.ThrowException( "Invalid element id - should be Uint64" );
					}

					if ( connectionId == InvalidGraphId )
					{
						ctx.ThrowException( "Invalid ConnectionId" );
					}

					return interop::ObjectPtr( RED_NEW( interop::utils::MapObject )(
						[ this, connectionId ]( interop::IContext& ctx, const String& componentIdStr ) -> interop::ObjectPtr
					{
						GraphComponentId componentId;
						if ( !FromString( componentIdStr, componentId ) )
						{
							ctx.ThrowException( "Invalid element id - should be Uint64" );
						}

						// TODO: Store the last Get result
						m_component = GetEditorGraph()->GetConnectionComponent( connectionId, componentId );
						if ( m_component )
						{
							return interop::utils::CreateRTTIAccessFunc( ctx, &m_component );
						}

						ctx.ThrowException( "Failed to create ConnectionComponent object" );
						return nullptr;
					} ) );
				} );
			}

		private:
			THandle< GraphComponentDescription > m_component;
		};
	}

	class ValidationResultsInterop : public interop::utils::Object
	{
	public:
		ValidationResultsInterop( GraphInterop* graphInterop )
			: interop::utils::Object( "ValidationResults" )
		{
			const red::SharedPtr< IGraphValidatorOutput >& validatorOutput = graphInterop->GetLastValidatorOutput();

			RegisterResizableArray( "elements", [ validatorOutput ]( interop::IContext& ctx ) { return validatorOutput ? validatorOutput->GetEntries().Size() : 0; },
				[ validatorOutput ]( interop::IContext& ctx, Uint32 index ) { return interop::utils::CreateRTTIAccessFunc( ctx, &validatorOutput->GetEntries()[ index ] ); } );

			RegisterFunctor<void, Uint32>( "fix", [ graphInterop, validatorOutput ]( interop::IContext& ctx, Uint32 index )
			{
				if ( index < validatorOutput->GetEntries().Size() )
				{
					if ( validatorOutput->GetEntries()[ index ].Fix() )
					{
						graphInterop->Validate();
					}
				}
			} );
		}
	};

	//////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////

	GraphSubGraphId EditorGraphInterop::m_nextId = 0;

	EditorGraphInterop::GraphInteropInstance::GraphInteropInstance( const GraphInteropCreationInfo& info )
		: graphInterop( info )
		, editor( info.m_editorGraph )
	{
	}

	EditorGraphInterop::GraphInteropInstance::~GraphInteropInstance()
	{}

	EditorGraphInterop::EditorGraphInterop( const EditorGraphInteropCreationInfo& info )
		: m_resourceEditor( info.m_editor )
		, m_sharedDataCollection( info.m_selectionCollection )
		, m_openEditors{ red::PoolBackend() }
	{
		if ( info.m_customObjectResolverProvider )
		{
			m_objectResolverProvider = info.m_customObjectResolverProvider;
		}
		else
		{
			m_objectResolverProvider = []( IEditorGraphPtr graph ) { return red::CreateUniquePtr< EditorGraphObjectResolver >( graph.Get() ); };
		}
		AddEditorGraph( info.m_editorGraph );
		m_currentlyEditedId = info.m_editorGraph->GetSubGraphId();

		if ( info.m_editorGraph->IsSupportVariables() )
		{
			m_variableTree = red::CreateSharedPtr<VariableTree>( &info.m_editor->GetAccessHistory(), info.m_variableTreeSelectionCollection, info.m_editorGraph.Get() );
			m_variableTreeInterop = red::CreateSharedPtr<TreeContainerInterop>( m_variableTree.Get(), info.m_variableTreeSelectionCollection );
		}
	}

	EditorGraphInterop::~EditorGraphInterop() = default;

	IEditActionHistory* EditorGraphInterop::GetEditHistory() const
	{
		IResourceEditor* editor = GetEditor();
		return editor ? &editor->GetAccessHistory() : nullptr;
	}

	ResourceBackend* EditorGraphInterop::GetBackend() const
	{
		IResourceEditor* editor = GetEditor();
		return editor ? editor->GetBackend() : nullptr;
	}

	IResourceEditor* EditorGraphInterop::GetEditor() const
	{
		ResourceEditorPtr editor = m_resourceEditor.Lock();
		return editor.Get();
	}

	void EditorGraphInterop::SetupInterop( EditorGraphInterop* graphInterop, ::interop::utils::Object& op )
	{
		// graph accessors
		op.RegisterMap( "graphs", [ graphInterop ]( ::interop::IContext& ctx, const String& graphIdStr ) -> interop::ObjectPtr
		{
			GraphSubGraphId graphId;
			if ( !FromString( graphIdStr, graphId ) )
				ctx.ThrowException( "Invalid graph id - should be Uint32" );

			if ( graphId == InvalidSubGraphId )
				ctx.ThrowException( "InvalidSubGraphId" );

			auto it = graphInterop->m_openEditors.Find( graphId );
			if ( it != graphInterop->m_openEditors.End() )
			{
				return interop::ObjectPtr( it.Value()->graphInterop.CreateInterop() );
			}

			ctx.ThrowException( "Unknown graph '%hs'", graphIdStr.AsChar() );
			return interop::ObjectPtr();
		} );

		if ( graphInterop->m_variableTreeInterop )
		{
			op.RegisterObject( "variableTree", interop::ObjectPtr( graphInterop->m_variableTreeInterop ) );
		}

		op.RegisterValue< GraphSubGraphId >( "currentGraphId", &graphInterop->m_currentlyEditedId );
		op.RegisterFunctor< GraphSubGraphId >( "getRootGraphId", [ graphInterop ]( interop::IContext& ctx )
		{
			return graphInterop->m_openEditors.Begin().Key();
		} );

		op.RegisterFunctor< red::DynArray< GraphSubGraphId >, red::DynArray< GraphId > >( "openPath", [ graphInterop ]( interop::IContext& ctx, const red::DynArray< GraphId >& nodeIds )
		{
			red::DynArray< GraphSubGraphId > subgraphsPath{ red::PoolFrame() };
			subgraphsPath.Reserve( nodeIds.Size() );

			IEditorGraphPtr editorGraph = graphInterop->m_openEditors.Begin().Value()->editor;
			for ( GraphId id : nodeIds )
			{
				editorGraph = editorGraph->GetNodeSubGraph( id );
				if ( !editorGraph )
				{
					// TODO: clean-up opened graphs
					ctx.ThrowException( "EditorGraphInterop: provided path is not resolvable from this root resource" );
					return red::DynArray< GraphSubGraphId >();
				}

				if ( !graphInterop->HasGraphEditor( editorGraph ) )
					graphInterop->AddEditorGraph( editorGraph );

				subgraphsPath.PushBack( editorGraph->GetSubGraphId() );
			}

			return subgraphsPath;
		} );

		op.RegisterFunctor< red::DynArray< String > >( "getAvailableLayers", [ graphInterop ]( ::interop::IContext& ctx )
		{
			red::DynArray< String > layers;
			graphInterop->GetAvailableLayers( layers );
			return layers;
		} );

		op.RegisterFunctor<void, String>( "toggleLayer", [ graphInterop ]( ::interop::IContext& ctx, const String& layer )
		{
			graphInterop->ToggleLayer( layer );
		} );

		op.RegisterNotifier( "_onGraphIdChanged", graphInterop->m_onGraphIdChanged );
		op.RegisterNotifier( "_onLayersChanged", graphInterop->m_onLayersChanged );
	}

	void EditorGraphInterop::GetAvailableLayers( red::DynArray< String >& availableLayers ) const
	{
	}

	red::UniquePtr< IObjectResolver > EditorGraphInterop::ProvideObjectResolver( IEditorGraphPtr graph ) const
	{
		return m_objectResolverProvider( graph );
	}

	GraphSubGraphId EditorGraphInterop::AllocateSubgraphId()
	{
		return m_nextId++;
	}

	void EditorGraphInterop::ToggleLayer( const String& layer )
	{
		if ( m_activeLayers.Exist( layer ) )
		{
			m_activeLayers.RemoveReorder( layer );
		}
		else
		{
			m_activeLayers.PushBack( layer );
		}

		OnLayersChanged();
	}

	void EditorGraphInterop::ActivateLayer( const String& layer )
	{
		if ( !m_activeLayers.Exist( layer ) )
		{
			m_activeLayers.PushBack( layer );
			OnLayersChanged();
		}
	}

	void EditorGraphInterop::DeactivateLayer( const String& layer )
	{
		if ( m_activeLayers.Exist( layer ) )
		{
			m_activeLayers.RemoveReorder( layer );
			OnLayersChanged();
		}
	}

	void EditorGraphInterop::OnLayersChanged()
	{
		m_onLayersChanged.Notify( ArrayToString( m_activeLayers ) );
	}

	void EditorGraphInterop::AddEditorGraph( const IEditorGraphPtr& editor )
	{
		editor->AttachListener( this );
		editor->SetSubGraphId( AllocateSubgraphId() );
		editor->Open();

		GraphInteropCreationInfo info
		{
			GetEditHistory(),
			this,
			editor,
			m_sharedDataCollection,
		};

		GraphInteropInstancePtr instance( RED_NEW( GraphInteropInstance )( info ) );
		instance->graphInterop.SetUserContext( m_userContext );
		m_openEditors.Insert( editor->GetSubGraphId(), instance );
	}

	void EditorGraphInterop::RemoveEditorGraph( GraphSubGraphId subGraphId )
	{
		auto it = m_openEditors.Find( subGraphId );
		if ( it != m_openEditors.End() )
		{
			GraphInteropInstancePtr& instance = it.Value();
			IEditorGraphPtr& editor = instance->editor;

			editor->DetachListener( this );
			m_openEditors.Remove( it );

			return;
		}
	}

	Bool EditorGraphInterop::HasGraphEditor( const IEditorGraphPtr& editor )
	{
		for ( const auto& instance : m_openEditors )
		{
			if ( instance.Value()->editor == editor )
				return true;
		}

		return false;
	}
	
	GraphInterop* EditorGraphInterop::GetGraphInterop( GraphSubGraphId subGraphId )
	{
		auto it = m_openEditors.Find( subGraphId );
		if ( it != m_openEditors.End() )
		{
			return &it.Value()->graphInterop;
		}

		return nullptr;
	}

	IEditorGraphPtr EditorGraphInterop::GetEditor( GraphSubGraphId subGraphId )
	{
		auto it = m_openEditors.Find( subGraphId );
		if ( it != m_openEditors.End() )
		{
			return it.Value()->editor;
		}

		return nullptr;
	}

	ObjectHandler* EditorGraphInterop::GetCurrentHandler()
	{
		GraphInterop* graphInterop = GetGraphInterop( m_currentlyEditedId );
		if ( graphInterop )
		{
			return graphInterop->GetHandler();
		}

		return nullptr;
	}

	void EditorGraphInterop::OnCreateNode( GraphSubGraphId subGraphId, GraphNodeId nodeId )
	{		
		if ( GraphInterop* interop = GetGraphInterop( subGraphId ) )
		{
			interop->OnCreateNode( nodeId );
		}
	}

	void EditorGraphInterop::OnRecreateNode( GraphSubGraphId subgraphId, GraphNodeId nodeId )
	{
		if ( GraphInterop* interop = GetGraphInterop( subgraphId ) )
		{
			interop->OnRecreateNode( nodeId );
		}
	}

	void EditorGraphInterop::OnRemoveNode( GraphSubGraphId subGraphId, GraphNodeId nodeId )
	{
		if ( GraphInterop* interop = GetGraphInterop( subGraphId ) )
		{
			interop->OnRemoveNode( nodeId );
		}
	}

	void EditorGraphInterop::OnInvalidateNodeDescription( GraphSubGraphId subGraphId, GraphNodeId nodeId )
	{
		if ( GraphInterop* interop = GetGraphInterop( subGraphId ) )
		{
			interop->OnInvalidateNodeDescription( nodeId );
		}
	}
	
	void EditorGraphInterop::OnInvalidateNodeDescriptionProperty( GraphSubGraphId subGraphId, GraphNodeId nodeId, const red::String& propertyName )
	{
		if ( GraphInterop* interop = GetGraphInterop( subGraphId ) )
		{
			interop->OnInvalidateNodeDescriptionProperty( nodeId, propertyName );
		}
	}
	
	void EditorGraphInterop::OnInvalidateNodeData( GraphSubGraphId subGraphId, GraphNodeId nodeId )
	{
		if ( GraphInterop* interop = GetGraphInterop( subGraphId ) )
		{
			interop->OnInvalidateNodeData( nodeId );
		}
	}

	void EditorGraphInterop::OnInvalidateNodeComponent( GraphSubGraphId subgraphId, GraphNodeId nodeId, GraphComponentId index )
	{
		if ( GraphInterop* interop = GetGraphInterop( subgraphId ) )
		{
			interop->OnInvalidateNodeComponent( nodeId, index );
		}
	}

	void EditorGraphInterop::OnCreateConnection( GraphSubGraphId subGraphId, GraphConnectionId connectionId )
	{
		if ( GraphInterop* interop = GetGraphInterop( subGraphId ) )
		{
			interop->OnCreateConnection( connectionId );
		}
	}

	void EditorGraphInterop::OnRemoveConnection( GraphSubGraphId subGraphId, GraphConnectionId connectionId )
	{
		if ( GraphInterop* interop = GetGraphInterop( subGraphId ) )
		{
			interop->OnRemoveConnection( connectionId );
		}
	}

	void EditorGraphInterop::OnInvalidateConnectionDescription( GraphSubGraphId subGraphId, GraphConnectionId connectionId )
	{
		if ( GraphInterop* interop = GetGraphInterop( subGraphId ) )
		{
			interop->OnInvalidateConnectionDescription( connectionId );
		}
	}

	void EditorGraphInterop::OnInvalidateConnectionDescriptionProperty( GraphSubGraphId subGraphId, GraphConnectionId connectionId, const String& propertyName )
	{
		if ( GraphInterop* interop = GetGraphInterop( subGraphId ) )
		{
			interop->OnInvalidateConnectionDescriptionProperty( connectionId, propertyName );
		}
	}

	void EditorGraphInterop::OnInvalidateConnectionData( GraphSubGraphId subGraphId, GraphConnectionId connectionId )
	{
		if ( GraphInterop* interop = GetGraphInterop( subGraphId ) )
		{
			interop->OnInvalidateConnectionData( connectionId );
		}
	}

	void EditorGraphInterop::OnInvalidateConnectionComponent( GraphSubGraphId subgraphId, GraphConnectionId connectionId, GraphComponentId componentId )
	{
		if ( GraphInterop* interop = GetGraphInterop( subgraphId ) )
		{
			interop->OnInvalidateConnectionComponent( connectionId, componentId );
		}
	}

	void EditorGraphInterop::OnNodeSocketsChanged( GraphSubGraphId subGraphId, GraphNodeId nodeId )
	{
		if ( GraphInterop* interop = GetGraphInterop( subGraphId ) )
		{
			interop->OnNodeSocketsChanged( nodeId );
		}
	}

	void EditorGraphInterop::OnInvalidateSocketDescription( GraphSubGraphId subGraphId, GraphNodeId nodeId, GraphSocketId socketId )
	{
		if ( GraphInterop* interop = GetGraphInterop( subGraphId ) )
		{
			interop->OnInvalidateSocketDescription( nodeId, socketId );
		}
	}

	void EditorGraphInterop::OnInvalidateSocketData( GraphSubGraphId subGraphId, GraphNodeId nodeId, GraphSocketId socketId )
	{
		if ( GraphInterop* interop = GetGraphInterop( subGraphId ) )
		{
			interop->OnInvalidateSocketData( nodeId, socketId );
		}
	}

	void EditorGraphInterop::OnVariableCreated( GraphSubGraphId subGraphId, GraphVariableId variableId )
	{
		if ( GraphInterop* interop = GetGraphInterop( subGraphId ) )
		{
			interop->OnVariableCreated( variableId );
		}
	}

	void EditorGraphInterop::OnVariableRemoved( GraphSubGraphId subGraphId, GraphVariableId variableId )
	{
		if ( GraphInterop* interop = GetGraphInterop( subGraphId ) )
		{
			interop->OnVariableRemoved( variableId );
		}
	}

	void EditorGraphInterop::OnGraphReloaded( GraphSubGraphId ownerSubGraphId, GraphSubGraphId reloadedSubGraphId, GraphId nodeId )
	{
		if ( GraphInterop* interop = GetGraphInterop( reloadedSubGraphId ) )
		{
			interop->OnReload( ownerSubGraphId, nodeId );
		}
	}

	void EditorGraphInterop::OnGraphIdChanged( GraphSubGraphId oldId, GraphSubGraphId newId )
	{
		for ( auto pair : m_openEditors )
		{
			GraphSubGraphId subgraphId = pair.Key();
			GraphInteropInstancePtr instance = pair.Value();

			GraphSubGraphId currentId = instance->editor->GetSubGraphId();

			if ( subgraphId != currentId && oldId == subgraphId && currentId == newId )
			{
				m_openEditors.Remove( subgraphId );
				Bool result = m_openEditors.Insert( newId, instance ).IsSuccessful();
				RED_ASSERT( result, "Graphs id has changed but it's already being used in another editor" );

				m_onGraphIdChanged.Notify( interop::Uint64Pair( oldId, newId ) );
			}

			if ( m_currentlyEditedId == oldId )
				m_currentlyEditedId = newId;
		}
	}

	void EditorGraphInterop::OnCloseSubgraph( GraphSubGraphId subgraphId )
	{
		RemoveEditorGraph( subgraphId );
	}

	void EditorGraphInterop::OnSelectionChanged( GraphSubGraphId subgraphId, const red::DynArray< GraphId >& ids )
	{
		IEditorGraphPtr graphEditor = GetEditor( subgraphId );
		red::DynArray< EditorObjectID > editorIDs{ red::PoolFrame() };

		for ( const GraphId id : ids )
		{
			THandle< ISerializable > descriptorData = graphEditor->GetItemDescriptorData( id );
			if ( descriptorData )
			{
				editorIDs.PushBack( descriptorData->GetEditorObjectID() );
			}
		}

		GraphInterop* graphInterop = GetGraphInterop( subgraphId );
		ObjectHandler* objectHandler = graphInterop->GetHandler();
		// HACK: We need the cast to make the hack local to EditorGraphSelectionHandler. If we remove the hack, we can remove the cast.
		if( !static_cast< EditorGraphSelectionHandler* >( objectHandler->GetSelectionHandler() )->HACK_IsCurrentlySelecting() )
		{
			objectHandler->SetSelectionFromObjectIDs( editorIDs );
		}
	}

	void EditorGraphInterop::OnEnableConnection(GraphSubGraphId subgraphId, GraphConnectionId connectionId)
	{
		if ( GraphInterop* interop = GetGraphInterop( subgraphId ) )
		{
			interop->SetSelectedObjects();
		}
	}

	void EditorGraphInterop::OnDisableConnection(GraphSubGraphId subgraphId, GraphConnectionId connectionId)
	{
		if ( GraphInterop* interop = GetGraphInterop( subgraphId ) )
		{
			interop->SetSelectedObjects();
		}
	}

	void EditorGraphInterop::SetUserContext(THandle<ISerializable> userContext)
	{
		m_userContext = userContext;
		for ( auto& item : m_openEditors )
		{
			GraphInteropInstancePtr subgraphEditor = item.Value();
			subgraphEditor->graphInterop.SetUserContext( userContext );
		}

		if ( m_variableTree )
			m_variableTree->SetUserContext( userContext );
	}

	//////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////

	GraphInterop::GraphInterop( const GraphInteropCreationInfo& info )
		: m_graphEditor( info.m_editorGraphInterop )
		, m_graph( info.m_editorGraph )
		, m_editHistory( info.m_editHistory )
		, m_enabledNotifiers( true )
		, m_dataCollection( red::CreateUniquePtr< ObjectCollection >( *info.m_editHistory ) )
		, m_selectionDataObjects( info.m_selectionCollection )
		, m_selectionDescriptorObjects( red::CreateUniquePtr< ObjectCollection >( *info.m_editHistory ) )
		, m_selectionMapping{ red::PoolBackend() }
		, m_cachedSelection{ red::PoolBackend() }
	{
		m_serializableNotifier.SetCallback( [this]( const SerializableID& id, const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& targetValue )
			{ OnNotifyPropertyChange( id, path ); } );

		auto selectionCallback = [ this ]( const SelectionStatePtr& selection )
		{
			for ( SelectionIterator iterator = selection->Iterate(); iterator; ++iterator )
			{
				GraphId selectedId = iterator->GetEditorObjectID().GetInternalID();

				if ( !m_cachedSelection.Exist( selectedId ) )
					m_selectionChangeInfo.m_selected.PushBack( selectedId );
				else
					m_cachedSelection.Remove( selectedId );
			}

			for ( GraphId id : m_cachedSelection )
			{
				Int32 index = m_selectionChangeInfo.m_selected.GetIndex( id );
				if ( index == red::INVALID_INDEX )
					m_selectionChangeInfo.m_deselected.PushBack( id );
				else
					m_selectionChangeInfo.m_selected.RemoveAt( index );
			}

			m_cachedSelection.Clear();
			for ( SelectionIterator iterator = selection->Iterate(); iterator; ++iterator )
			{
				m_cachedSelection.Insert( iterator->GetEditorObjectID().GetInternalID() );
			}

			SetSelectedObjects();

			if ( m_selectionChangeInfo.HasPending() )
				Send( m_editorNotifer, "SelectionChanged" );
		};

		auto selectionUpdatedCallback = [ this ]()
		{
			m_selectionDataObjects->UpdateGlobalSelection();
		};

		m_selectionHandler = red::CreateUniquePtr< EditorGraphSelectionHandler >( *m_editHistory, selectionCallback, selectionUpdatedCallback );
		m_handler = red::CreateUniquePtr< ObjectHandler >( *m_editHistory, m_graphEditor->GetBackend()->GetClipboard(), m_selectionHandler.Get() );
		m_objectResolver = m_graphEditor->ProvideObjectResolver( m_graph );
		m_handler->AttachObjectResolver( m_objectResolver.Get() );

		m_classTree = m_graph->GetClassTree();
		if ( m_classTree )
			m_classTreeContainer = red::CreateSharedPtr<TreeContainerInterop>( m_classTree.Get(), m_selectionDataObjects );

		m_importTree = m_graph->GetImportTree();
		if ( m_importTree )
			m_importTreeContainer = red::CreateSharedPtr<TreeContainerInterop>( m_importTree.Get(), m_selectionDataObjects );
	}

	GraphInterop::~GraphInterop()
	{
		m_handler->DetachObjectResolver( m_objectResolver.Get() );
	}

	void GraphInterop::SetSelectedObjects()
	{
		m_selectionMapping.Clear();

		red::DynArray< GraphId > selectedIds{ red::PoolFrame() };
		m_cachedSelection.GetElements( selectedIds );
		m_graph->Select( selectedIds );

		if ( !m_cachedSelection.Empty() )
		{
			red::DynArray< THandle< ISerializable > > selectedObjects{ red::PoolFrame() };
			red::DynArray< THandle< ISerializable > > selectedObjectsDescriptors{ red::PoolFrame() };
			Bool readonlyObjects = m_graph->IsReadOnly();

			for ( GraphId id : m_cachedSelection )
			{
				THandle< ISerializable > data = m_graph->GetItemData( id );
				if ( data )
				{
					SerializableCollector collector;
					collector.Run( data );

					red::DynArray< SerializableID > serializableIDs{ red::PoolFrame() };
					collector.GetCollectedIDs( serializableIDs );

					for ( SerializableID serializableID : serializableIDs )
						m_selectionMapping.Insert( serializableID, id );

					selectedObjects.PushBack( data );
					readonlyObjects |= m_graph->IsItemDataReadOnly( id );
				}

				THandle< ISerializable > descriptorData = m_graph->GetItemDescriptorData( id );
				if ( descriptorData )
				{
					selectedObjectsDescriptors.PushBack( descriptorData );
				}
			}

			m_selectionDataObjects->SetObjects( selectedObjects, m_userContext, readonlyObjects );
			m_selectionDescriptorObjects->SetObjects( selectedObjectsDescriptors );
		}
		else
		{
			ClearSelection();
		}
	}

	const GraphObjectTree* GraphInterop::GetClassTree() const
	{
		return m_classTree.Get();
	}

	GraphObjectTree* GraphInterop::GetClassTree()
	{
		return m_classTree.Get();
	}

	void GraphInterop::ClearSelection()
	{
		m_selectionDataObjects->SetObject( m_graph->GetData() );
		m_selectionDescriptorObjects->Reset();
	}

	Bool GraphInterop::RemoveItem( const GraphId id )
	{
		THandle< ISerializable > item = m_graph->GetItemDescriptorData( id );

		if ( !item )
			return false;

		red::DynArray< EditorObjectIDPath > paths;
		paths.PushBack( EditorObjectIDPath( item->GetEditorObjectID() ) );

		return GetHandler()->DeleteObjects( paths );
	}

	void GraphInterop::CreateConnection( const ConnectionCreationInfo& info )
	{
		rtti::ValuePtr value;
		GetTypeObject< ConnectionCreationInfo >()->ReadValue( IRTTIContext::GetDefault(), &info, rtti::AccessPath(), value );

		ObjectCreationInfo sourceInfo{ value };
		GetHandler()->CreateObject( sourceInfo );
	}

	void GraphInterop::CreateNode( const NodeCreationInfo& info )
	{
		rtti::ValuePtr value;
		GetTypeObject< NodeCreationInfo >()->ReadValue( IRTTIContext::GetDefault(), &info, rtti::AccessPath(), value );

		ObjectCreationInfo sourceInfo{ value };
		GetHandler()->CreateObject( sourceInfo );
	}

	void GraphInterop::SetupInterop( GraphInterop* graphInterop, ::interop::utils::Object& op )
	{
		// Initialize interfaces object
		graphInterop->m_nodes = interop::ObjectPtr( RED_NEW( NodesInterop )( graphInterop ) );
		graphInterop->m_connections = interop::ObjectPtr( RED_NEW( ConnectionsInterop )( graphInterop ) );
		graphInterop->m_variables = interop::ObjectPtr( RED_NEW( VariablesInterop )( graphInterop ) );
		graphInterop->m_components = interop::ObjectPtr( RED_NEW( ComponentsInterop )( graphInterop ) );
		graphInterop->m_validationResults = interop::ObjectPtr( RED_NEW( ValidationResultsInterop )( graphInterop ) );

		op.RegisterObject( "nodes", graphInterop->m_nodes );
		op.RegisterObject( "connections", graphInterop->m_connections );
		op.RegisterObject( "variables", graphInterop->m_variables );
		op.RegisterObject( "components", graphInterop->m_components );
		op.RegisterObject( "validationResults", graphInterop->m_validationResults );

		op.RegisterObject( "dataSelection", graphInterop->m_selectionDataObjects->GetRootInterop() );
		op.RegisterObject( "descriptorSelection", graphInterop->m_selectionDescriptorObjects->GetRootInterop() );

		op.RegisterObject( "classTree", [graphInterop]( interop::IContext& ctx )
		{
			return interop::ObjectPtr( graphInterop->m_classTreeContainer );
		} );

		op.RegisterObject( "importTree", [graphInterop]( interop::IContext& ctx )
		{
			return interop::ObjectPtr( graphInterop->m_importTreeContainer );
		} );

		op.RegisterObject( "objects", graphInterop->m_handler->CreateInterop() );

		op.RegisterFunctor<void>( "initGraph", [graphInterop]( interop::IContext& ctx )
		{
			graphInterop->ClearSelection();
		} );

		op.RegisterFunctor<ObjectTreeStructure>( "getClassTreeStructure", [graphInterop]( interop::IContext& ctx )
		{
			ObjectTreeStructure structure;
			graphInterop->m_classTree->GetStructure( structure );

			return structure;
		} );

		op.RegisterFunctor<ObjectTreeStructure>( "getImportTreeStructure", [graphInterop]( interop::IContext& ctx )
		{
			ObjectTreeStructure structure;
			graphInterop->m_importTree->GetStructure( structure );

			return structure;
		} );

		op.RegisterFunctor< interop::SelectionChangeInfo >( "getSelectionChangeInfo", [graphInterop]( interop::IContext& ctx )
		{
			interop::SelectionChangeInfo info;
			info.m_selected = std::move( graphInterop->m_selectionChangeInfo.m_selected );
			info.m_deselected = std::move( graphInterop->m_selectionChangeInfo.m_deselected );

			return info;
		} );

		op.RegisterFunctor< red::DynArray< GraphValidatorOutputEntry > >( "validate", [graphInterop]( interop::IContext& ctx )
		{
			graphInterop->Validate();

			if ( graphInterop->m_lastValidatorOutput )
			{
				const red::DynArray< GraphValidatorOutputEntry >& entries = graphInterop->m_lastValidatorOutput->GetEntries();

				Uint32 warnings = 0;
				Uint32 errors = 0;

				for ( const GraphValidatorOutputEntry& entry : entries )
				{
					switch ( entry.m_type )
					{
					case GraphValidatorOutputEntryType::Warning:
						++warnings;
						break;

					case GraphValidatorOutputEntryType::Error:
						++errors;
						break;
					}
				}

				if ( errors > 0 )
				{
					RED_REPORT_ERROR( RED_LOCATION( graphInterop->m_editHistory->GetResourceEditor()->GetResource()->GetPath() ), "Graph % Error count: %", RED_NAME_CONSTEXPR( "Validation" ), errors );
				}

				if ( warnings > 0 )
				{
					RED_REPORT_WARNING( RED_LOCATION( graphInterop->m_editHistory->GetResourceEditor()->GetResource()->GetPath() ), "Graph % Warning count: %", RED_NAME_CONSTEXPR( "Validation" ), warnings );
				}

				return entries;
			}

			return red::DynArray< GraphValidatorOutputEntry >();
		} );

		op.RegisterFunctor<void, Int32, Int32, String>( "dropData", [graphInterop]( interop::IContext& ctx, const Int32 x, const Int32 y, const String& data )
		{
			if ( graphInterop->m_graph->CanCreateItemFromData( data ) )
			{
				rtti::ValueHolderPtr point = rtti::ValueHolder::CreateStructure();
				point->SetStructElement( RED_NAME_CONSTEXPR( "x" ), rtti::ValueHolder::CreateSingle( ToStringDirect( x ).AsChar() ) );
				point->SetStructElement( RED_NAME_CONSTEXPR( "y" ), rtti::ValueHolder::CreateSingle( ToStringDirect( y ).AsChar() ) );

				NodeCreationInfo nodeCreationInfo;
				nodeCreationInfo.m_data = data;
				nodeCreationInfo.m_initialPosition = Point( x, y );

				rtti::ValueHolderPtr value;
				GetTypeObject< NodeCreationInfo >()->ReadValue( IRTTIContext::GetDefault(), &nodeCreationInfo, rtti::AccessPath(), value );

				ObjectCreationInfo info( value );
				if ( !graphInterop->GetHandler()->CreateObject( info ) )
					ctx.ThrowException( "Couldn't create item with parameters: %hs", data.AsChar() );
			}
			else
			{
				graphInterop->m_graph->DropData( Point( x, y ), data );
			}
		} );

		op.RegisterFunctor<void, String>( "createItem", [graphInterop]( ::interop::IContext& ctx, const String& data )
		{
			ObjectCreationInfo info( data );
			if ( !graphInterop->GetHandler()->CreateObject( info ) )
				ctx.ThrowException( "Couldn't create item with parameters: %hs", data.AsChar() );
		} );

		op.RegisterFunctor<void, GraphId>( "removeItem", [graphInterop]( ::interop::IContext& ctx, GraphId id )
		{
			if ( !graphInterop->RemoveItem( id ) )
			{
				ctx.ThrowException( "Couldn't remove item with id: %d", id );
			}
		} );

		op.RegisterFunctor<void, String, ConnectionCreationInfo, Uint32, Uint32 >( "createIntermediateBlock",
			[graphInterop]( interop::IContext& ctx, const String& blockTypeName, const ConnectionCreationInfo& info, const Uint32 sourceSocketId, const Uint32 destSocketId )
		{
			if ( !graphInterop->m_graph->CanCreateIntermediateNode( info, blockTypeName ) )
				return;

			graphInterop->m_editHistory->StartTransaction( "Create Intermediate Node" );

			// transaction
			{
				red::DynArray< GraphConnectionId > existingConnections;
				Bool areConnectionsReadonly = false;
				graphInterop->m_graph->GetNodeConnections( info.m_sourceNodeId, existingConnections );
				for ( const GraphConnectionId& connectionId : existingConnections )
				{
					THandle< ConnectionDescription > connectionDesc = graphInterop->m_graph->GetConnectionDescription( connectionId );
					if ( connectionDesc->m_sourceNode == info.m_sourceNodeId && connectionDesc->m_sourceSocketIndex == info.m_sourceSocketId
						&& connectionDesc->m_destinationNode == info.m_destinationNodeId && connectionDesc->m_destinationSocketIndex == info.m_destinationSocketId )
					{
						areConnectionsReadonly = connectionDesc->m_isInactive;
						graphInterop->RemoveItem( connectionId );
						break;
					}
				}

				Point sourcePosition;
				graphInterop->m_graph->GetNodePosition( info.m_sourceNodeId, sourcePosition );
				Point destinationPosition;
				graphInterop->m_graph->GetNodePosition( info.m_destinationNodeId, destinationPosition );

				graphInterop->CreateNode( NodeCreationInfo{ blockTypeName, ( sourcePosition + destinationPosition ) / 2 } );

				// TODO: this is a hack
				GraphId intermediateNodeId = graphInterop->m_graph->GetLastId();
				RED_ASSERT( intermediateNodeId != InvalidGraphId, "Can't determine the newly created node's id" );

				auto firstConnection = ConnectionCreationInfo{ info.m_sourceNodeId, info.m_sourceSocketId, intermediateNodeId, sourceSocketId };
				auto secondConnection = ConnectionCreationInfo{ intermediateNodeId, destSocketId, info.m_destinationNodeId, info.m_destinationSocketId };

				firstConnection.m_isInactive = areConnectionsReadonly;
				secondConnection.m_isInactive = areConnectionsReadonly;
				graphInterop->CreateConnection( firstConnection );
				graphInterop->CreateConnection( secondConnection );
			}

			graphInterop->m_editHistory->EndTransaction();
		} );
		
		op.RegisterFunctor<void, String>( "importGraph", [graphInterop]( ::interop::IContext& ctx, const String& importPath )
		{
			NodeCreationInfo importInfo;
			if( GetTypeObject<NodeCreationInfo>()->WriteValue( IRTTIContext::GetDefault(), &importInfo, rtti::AccessPath(), *rtti::ValueHolder::Parse( importPath.AsChar() ) ) )
			{
				const String& importPath = importInfo.m_data;
				const res::ResourcePath resourcePath = res::ResourcePath::Build( importPath.AsChar() );

				if ( !GetResourceDepot()->ResourceExists( resourcePath ) )
				{
					ctx.ThrowException( "Import file %hs does not exist!", importPath.AsChar() );
					return;
				}

				if ( !graphInterop->ImportGraph( importPath, importInfo.m_initialPosition ) )
				{
					ctx.ThrowException( "Couldn't import graph with parameters: %hs", importPath.AsChar() );
				}
			}
		} );

		op.RegisterFunctor< void, GraphId >( "moveItem", [graphInterop]( ::interop::IContext& ctx, GraphId id )
		{
			THandle< ISerializable > item = graphInterop->m_graph->GetItemDescriptorData( id );
			if ( !item )
			{
				ctx.ThrowException( "Couldn't detach item with id: %d, reason: no item with this id", id );
				return;
			}

			red::DynArray< EditorObjectIDPath > paths;
			paths.PushBack( EditorObjectIDPath( item->GetEditorObjectID() ) );

			graphInterop->GetHandler()->MoveObjects( paths );
		} );

		op.RegisterValue<Bool>( "isReadOnly",
			/*GET*/	[graphInterop]( interop::IContext& ctx, Bool& outValue )
		{
			outValue = graphInterop->m_graph->IsReadOnly();
		},
			/*SET*/	[graphInterop]( interop::IContext& ctx, const Bool& newValue )
		{
			graphInterop->m_graph->SetReadOnly( newValue );
		});

		op.RegisterValueRO<String>( "ownerEditor", [graphInterop]()
		{
			return graphInterop->m_graph->GetOwnerEditorName();
		} );

		op.RegisterValueRO<String>( "name", graphInterop->m_graph->GetName() );
		op.RegisterValueRO<Bool>( "isTopDownTree", graphInterop->m_graph->IsTopDownTree() );
		op.RegisterValueRO<Bool>( "hasClipboardSupport", graphInterop->m_graph->HasClipboardSupport() );
		op.RegisterValueRO<Bool>( "hasValidationSupport", graphInterop->m_graph->HasValidationSupport() );
		op.RegisterValueRO<Bool>( "hasCloningSupport", graphInterop->m_graph->HasCloningSupport() );

		op.RegisterNotifier( "_editorNotifer", graphInterop->m_editorNotifer );
		op.RegisterNotifier( "_nodesNotifier", graphInterop->m_nodesNotifier );
		op.RegisterNotifier( "_connectionsNotifiers", graphInterop->m_connectionsNotifier );
		op.RegisterNotifier( "_variablesNotifiers", graphInterop->m_variableNotifier );
		op.RegisterNotifier( "_validationNotifier", graphInterop->m_validationNotifier );
	}

	void GraphInterop::OnNotifyPropertyChange( const SerializableID& id, const rtti::AccessPath& path )
	{
		auto itr = m_selectionMapping.Find( id );
		if ( itr != m_selectionMapping.End() )
		{
			GraphId graphId = itr.Value();
			m_graph->OnDataItemChanged( graphId, path );
			//m_selectionDataObjects->Refresh();

			if ( path.ToString() == "name" )
			{
				Validate();
			}

			THandle< ISerializable > serializable;
			if ( GSerializableMap->FindSerializable( id, serializable ) )
			{
				red::SharedPtr< IEditableView > view = serializable->CreateView();
				rtti::ValuePtr value;
				view->GetValue( IRTTIContext::GetDefault(), path, value );

				if ( value && value->IsHandle() )
				{
					SerializableID childId( FromStringDirect<Uint64>( value->GetHandleValue() ) );
					m_selectionMapping.Insert( childId, graphId );
				}
			}
		}
	}

	interop::ObjectPtr GraphInterop::SetDataCollection( const THandle< ISerializable >& data )
	{
		if ( data == nullptr )
			return nullptr;

		red::DynArray< THandle< ISerializable > > tmp{ red::PoolFrame() };
		tmp.PushBack( data );

		m_dataCollection->SetObjects( tmp, m_userContext );
		return m_dataCollection->GetRootInterop();
	}

// 	void GraphInterop::OnChangeGraphId( GraphSubGraphId newId )
// 	{
// 		Send( m_editorNotifer, "IdChanged", newId );
// 	}

	void GraphInterop::OnReload( GraphSubGraphId subgraphId, GraphId nodeId )
	{
		Send( m_editorNotifer, "Reload", subgraphId, nodeId );
	}

	void GraphInterop::OnCreateNode( GraphNodeId nodeId )
	{
		 Send( m_nodesNotifier, "CreatedNode", nodeId );
		 Validate();
	}

	void GraphInterop::OnRecreateNode( GraphNodeId nodeId )
	{
		Send( m_nodesNotifier, "RemovedNode", nodeId );
		Send( m_nodesNotifier, "CreatedNode", nodeId );
	}

	void GraphInterop::OnRemoveNode( GraphNodeId nodeId )
	{
		Send( m_nodesNotifier, "RemovedNode", nodeId );
		Validate();
	}

	void GraphInterop::OnInvalidateNodeDescription( GraphNodeId nodeId )
	{
		Send( m_nodesNotifier, "InvalidateNodeDescription", nodeId );
	}

	void GraphInterop::OnInvalidateNodeDescriptionProperty( GraphNodeId nodeId, const red::String& propertyName )
	{
		Send( m_nodesNotifier, "InvalidateNodeDescriptionProperty", nodeId, propertyName );
	}

	void GraphInterop::OnInvalidateNodeData( GraphNodeId nodeId )
	{
		Send( m_nodesNotifier, "InvalidateNodeData", nodeId );
	}

	void GraphInterop::OnInvalidateNodeComponent( GraphNodeId nodeId, GraphComponentId index )
	{
		Send( m_nodesNotifier, "InvalidateNodeComponent", nodeId, index );
	}

	void GraphInterop::OnInvalidateSocketComponent( GraphNodeId nodeId, GraphSocketId socketId, GraphComponentId index )
	{
		Send( m_nodesNotifier, "InvalidateSocketComponent", nodeId, socketId, index );
	}

	void GraphInterop::OnInvalidateConnectionComponent( GraphConnectionId connectionId, GraphComponentId index )
	{
		Send( m_connectionsNotifier, "InvalidateConnectionComponent", connectionId, index );
	}

	void GraphInterop::OnNodeSocketsChanged( GraphNodeId nodeId )
	{
		Send( m_nodesNotifier, "SocketsChanged", nodeId );
	}

	void GraphInterop::OnInvalidateSocketDescription( GraphNodeId nodeId, GraphSocketId socketId )
	{
		Send( m_nodesNotifier, "InvalidateSocketDescription", nodeId, socketId );
	}

	void GraphInterop::OnInvalidateSocketData( GraphNodeId nodeId, GraphSocketId socketId )
	{
		Send( m_nodesNotifier, "InvalidateSocketData", nodeId, socketId );
	}
	
	void GraphInterop::OnCreateConnection( GraphConnectionId connectionId )
	{
		Send( m_connectionsNotifier, "ConnectionCreated", connectionId );
	}

	void GraphInterop::OnRemoveConnection( GraphConnectionId connectionId )
	{
		Send( m_connectionsNotifier, "ConnectionRemoved", connectionId );
	}

	void GraphInterop::OnInvalidateConnectionDescription( GraphConnectionId connectionId )
	{
		Send( m_connectionsNotifier, "InvalidateConnectionDescription", connectionId );
	}

	void GraphInterop::OnInvalidateConnectionDescriptionProperty( GraphConnectionId connectionId, const String& propertyName )
	{
		Send( m_connectionsNotifier, "InvalidateConnectionDescriptionProperty", connectionId, propertyName );
	}

	void GraphInterop::OnInvalidateConnectionData( GraphConnectionId connectionId )
	{
		Send( m_connectionsNotifier, "InvalidateConnectionData", connectionId );
	}

	void GraphInterop::OnVariableCreated( GraphVariableId variableID )
	{
		Send( m_variableNotifier, "VariableCreated", variableID );
	}

	void GraphInterop::OnVariableRemoved( GraphVariableId variableID )
	{
		Send( m_variableNotifier, "VariableRemoved", variableID );
	}

	void GraphInterop::OnClassTreeChanged()
	{
		Send( m_editorNotifer, "ClassTreeChanged" );
		Validate();
	}

	void GraphInterop::OnImportTreeChanged()
	{
		Send( m_editorNotifer, "ImportTreeChanged" );
		Validate();
	}

	void GraphInterop::SetUserContext(THandle<ISerializable> userContext)
	{
		m_userContext = userContext;
	}

	void GraphInterop::Send( const interop::InteropNotifier& notifier, const AnsiChar* command )
	{
		if ( m_enabledNotifiers == false )
			return;

		notifier.Notify(String(command));
	}

	void GraphInterop::Send( const interop::InteropNotifier& notifier, const AnsiChar* command, GraphId firstId, GraphId secondId )
	{			
		if ( m_enabledNotifiers == false )
			return;

		String id1String, id2String;
		ToString( id1String, firstId );
		ToString( id2String, secondId );

		String commandStr( command );
		commandStr += " ";
		commandStr += id1String;
		commandStr += " ";
		commandStr += id2String;

		notifier.Notify( commandStr );
	}

	void GraphInterop::Send( const interop::InteropNotifier& notifier, const AnsiChar* command, GraphId firstId, GraphId secondId, GraphId thirdId )
	{			
		if ( m_enabledNotifiers == false )
			return;

		String id1String, id2String, id3String;
		ToString( id1String, firstId );
		ToString( id2String, secondId );
		ToString( id3String, thirdId );

		String commandStr( command );
		commandStr += " ";
		commandStr += id1String;
		commandStr += " ";
		commandStr += id2String;
		commandStr += " ";
		commandStr += id3String;

		notifier.Notify( commandStr );
	}

	void GraphInterop::Send( const interop::InteropNotifier& notifier, const AnsiChar* command, GraphNodeId id )
	{	
		if ( m_enabledNotifiers == false )
			return;

		String idString;
		ToString( idString, id );

		notifier.Notify( String( command ) + " " + idString );
	}

	void GraphInterop::Send( const interop::InteropNotifier& notifier, const AnsiChar* command, GraphNodeId id, const String& additionalParameters )
	{	
		if ( m_enabledNotifiers == false )
			return;

		String idString;
		ToString( idString, id );

		notifier.Notify( String( command ) + " " + idString + " " + additionalParameters );
	}

	void GraphInterop::Validate()
	{
		m_lastValidatorOutput.Reset( m_graph->Validate() );
		// Encoding a boolean into the number to avoid creating logic specifically asking for that information on the frontend side
		Int32 issuesCountEncoded = m_lastValidatorOutput ? m_lastValidatorOutput->GetEntries().Size() : 0;
		if ( m_lastValidatorOutput->GetState() == GraphValidationState::Errors )
			issuesCountEncoded *= -1;

		m_validationNotifier.Notify( issuesCountEncoded );
	}

	Bool GraphInterop::ImportGraph( const String& importPath, const Point& position )
	{
		res::ResourcePath path = res::ResourcePath::Build( importPath.AsChar() );
		if ( path.IsValid() )
		{
			auto graphImporter = m_graph->GetGraphImporter();

			if ( graphImporter )
			{
				if ( graphImporter->CopyGraphDescriptor( path, position ) )
				{
					return m_handler->PasteObjects();
				}
			}
		}

		return false;
	}
}
