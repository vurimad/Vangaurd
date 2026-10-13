/**
* Copyright (c) 2016-2020 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "editActionHistory.h"
#include "editorGraphBase.h"
#include "editorGraphObjectResolver.h"
#include "editAction.h"
#include "../include/graphDescriptor.h"
#include "resourceEditor.h"
#include "../../common/redReflection/include/serializableMap.h"
#include "../../common/redReflection/include/editableView.h"

namespace tools
{

	class Action_MoveNode : public IEditAction
	{
	public:
		Action_MoveNode( EditorGraphBase* graphDescriptorEditor, const GraphId& nodeId, const Point& newPosition )
			: m_editorGraph( graphDescriptorEditor )
			, m_nodeId( nodeId )
			, m_newPosition( newPosition )
			, m_oldPosition( 0, 0 )
		{
			m_editorGraph->GetNodePosition( nodeId, m_oldPosition );
		}

		virtual String GetDescription() const override
		{
			return "Move nodes to a specific point";
		}

		virtual Bool Do() override
		{
			return MoveTo( m_newPosition );
		}

		virtual Bool Undo() override
		{
			return MoveTo( m_oldPosition );
		}

	private:
		Bool MoveTo( const Point& position )
		{
			Point currentPosition;
			if ( !m_editorGraph->GetNodePosition( m_nodeId, currentPosition )
				|| m_editorGraph->MoveNode( m_nodeId, position - currentPosition ) == Fail )
			{
				return false;
			}

			m_editorGraph->OnInvalidateNodeDescriptionProperty( m_editorGraph->GetSubGraphId(), m_nodeId, "position" );
			return true;
		}

		EditorGraphBase* m_editorGraph;
		GraphId	m_nodeId;
		Point m_newPosition;
		Point m_oldPosition;
	};


	EditorGraphBase::EditorGraphBase( const ResourceEditorPtr& resourceEditor )
		: m_resourceEditor( resourceEditor )
		, m_graphListner{ red::PoolBackend() }
	{
		m_errorBuffer.Reserve( 128 );
		m_onError = [this]( const String& errorMessage ) { SetError( errorMessage ); };
	}

	void EditorGraphBase::AttachListener( IEditorGraphListener* listener )
	{
		RED_ASSERT( !m_graphListner.Exist( listener ), "Listener already registered" );

		if ( listener && !m_graphListner.Exist( listener ) )
		{			
			m_graphListner.PushBack( listener );
		}
	}

	void EditorGraphBase::DetachListener( IEditorGraphListener* listener )
	{
		m_graphListner.Remove( listener );
	}

	void EditorGraphBase::SetError( const String& errorMessage )
	{
		m_errorBuffer[0] = 0;
		red::Strcat( m_errorBuffer.AsChar(), errorMessage.AsChar(), m_errorBuffer.Capacity() );
	}

	const String& EditorGraphBase::GetLastError()
	{
		return m_errorBuffer;
	}

	Bool EditorGraphBase::CanCreateItemFromData( const String& data ) const
	{
		return false;
	}

	void EditorGraphBase::MarkModified()
	{
	}

	IGraphValidatorOutput* EditorGraphBase::Validate() const
	{
		return nullptr;
	}

	void EditorGraphBase::Open()
	{
	}

	void EditorGraphBase::OnPostPlace()
	{
	}

	ISerializable* EditorGraphBase::GetData() const
	{
		return nullptr;
	}

	void EditorGraphBase::SearchNodes( red::DynArray<SearchProcessOutput>& outNodes, const String& queryString, const SearchOptions& options )
	{
	}

	tools::GraphId EditorGraphBase::GetLastId() const
	{
		return InvalidGraphId;
	}

	void EditorGraphBase::CopySocketsConnections( GraphId sourceNodeId, GraphSocketId sourceSocketId, red::DynArray< THandle< SocketBinding > >& outSocketBindings )
	{
	}

	void EditorGraphBase::OnCreateNode( GraphSubGraphId subgraphId, GraphNodeId nodeId )
	{
		Forward( m_graphListner, [ subgraphId, nodeId ] ( IEditorGraphListener* l ) { l->OnCreateNode( subgraphId, nodeId ); } );
	}

	void EditorGraphBase::OnRecreateNode( GraphSubGraphId subgraphId, GraphNodeId nodeId )
	{
		Forward( m_graphListner, [ subgraphId, nodeId ]( IEditorGraphListener* l ) { l->OnRecreateNode( subgraphId, nodeId ); } );
	}

	void EditorGraphBase::OnRemoveNode( GraphSubGraphId subgraphId, GraphNodeId nodeId )
	{
		Forward( m_graphListner, [ subgraphId, nodeId ] ( IEditorGraphListener* l ) { l->OnRemoveNode( subgraphId, nodeId ); } );
	}

	void EditorGraphBase::OnInvalidateNodeDescription( GraphSubGraphId subgraphId, GraphNodeId nodeId )
	{
		Forward( m_graphListner, [ subgraphId, nodeId ] ( IEditorGraphListener* l ) { l->OnInvalidateNodeDescription( subgraphId, nodeId ); } );
	}

	void EditorGraphBase::OnInvalidateNodeDescriptionProperty( GraphSubGraphId subgraphId, GraphNodeId nodeId, const red::String& propertyName )
	{
		Forward( m_graphListner, [ subgraphId, nodeId, propertyName ] ( IEditorGraphListener* l ) { l->OnInvalidateNodeDescriptionProperty( subgraphId, nodeId, propertyName ); } );
	}

	void EditorGraphBase::OnInvalidateNodeData( GraphSubGraphId subgraphId, GraphNodeId nodeId )
	{
		Forward( m_graphListner, [ subgraphId, nodeId ] ( IEditorGraphListener* l ) { l->OnInvalidateNodeData( subgraphId, nodeId ); } );
	}

	void EditorGraphBase::OnInvalidateNodeComponent( GraphSubGraphId subgraphId, GraphNodeId nodeId, GraphComponentId index )
	{
		Forward( m_graphListner, [ subgraphId, nodeId, index ]( IEditorGraphListener* l ) { l->OnInvalidateNodeComponent( subgraphId, nodeId, index ); } );
	}

	void EditorGraphBase::OnInvalidateSocketComponent( GraphSubGraphId subgraphId, GraphNodeId nodeId, GraphSocketId socketId, GraphComponentId index )
	{
		Forward( m_graphListner, [ subgraphId, nodeId, socketId, index ]( IEditorGraphListener* l ) { l->OnInvalidateSocketComponent( subgraphId, nodeId, socketId, index ); } );
	}

	void EditorGraphBase::OnInvalidateConnectionComponent( GraphSubGraphId subgraphId, GraphConnectionId connectionId, GraphComponentId index )
	{
		Forward( m_graphListner, [ subgraphId, connectionId, index ]( IEditorGraphListener* l ) { l->OnInvalidateConnectionComponent( subgraphId, connectionId, index ); } );
	}

	void EditorGraphBase::OnCreateConnection( GraphSubGraphId subgraphId, GraphConnectionId connectionId )
	{
		Forward( m_graphListner, [ subgraphId, connectionId ] ( IEditorGraphListener* l ) { l->OnCreateConnection( subgraphId, connectionId ); } );
	}

	void EditorGraphBase::OnRemoveConnection( GraphSubGraphId subgraphId, GraphConnectionId connectionId )
	{
		Forward( m_graphListner, [ subgraphId, connectionId ] ( IEditorGraphListener* l ) { l->OnRemoveConnection( subgraphId, connectionId ); } );
	}
	
	void EditorGraphBase::OnEnableConnection( GraphSubGraphId subgraphId, GraphConnectionId connectionId )
	{
		Forward( m_graphListner, [ subgraphId, connectionId ] ( IEditorGraphListener* l ) { l->OnEnableConnection( subgraphId, connectionId ); } );
	}
	
	void EditorGraphBase::OnDisableConnection( GraphSubGraphId subgraphId, GraphConnectionId connectionId )
	{
		Forward( m_graphListner, [ subgraphId, connectionId ] ( IEditorGraphListener* l ) { l->OnDisableConnection( subgraphId, connectionId ); } );
	}

	void EditorGraphBase::OnInvalidateConnectionDescription( GraphSubGraphId subgraphId, GraphConnectionId connectionId )
	{
		Forward( m_graphListner, [ subgraphId, connectionId ] ( IEditorGraphListener* l ) { l->OnInvalidateConnectionDescription( subgraphId, connectionId ); } );
	}

	void EditorGraphBase::OnInvalidateConnectionDescriptionProperty( GraphSubGraphId subgraphId, GraphConnectionId connectionId, const String& propertyName )
	{
		Forward( m_graphListner, [ subgraphId, connectionId, propertyName ] ( IEditorGraphListener* l ) { l->OnInvalidateConnectionDescriptionProperty( subgraphId, connectionId, propertyName ); } );
	}

	void EditorGraphBase::OnInvalidateConnectionData( GraphSubGraphId subgraphId, GraphConnectionId connectionId )
	{
		Forward( m_graphListner, [ subgraphId, connectionId ] ( IEditorGraphListener* l ) { l->OnInvalidateConnectionData( subgraphId, connectionId ); } );
	}

	void EditorGraphBase::OnNodeSocketsChanged( GraphSubGraphId subgraphId, GraphNodeId nodeId )
	{
		Forward( m_graphListner, [ subgraphId, nodeId ] ( IEditorGraphListener* l ) { l->OnNodeSocketsChanged( subgraphId, nodeId ); } );
	}

	void EditorGraphBase::OnInvalidateSocketDescription( GraphSubGraphId subgraphId, GraphNodeId nodeId, GraphSocketId socketId )
	{
		Forward( m_graphListner, [ subgraphId, nodeId, socketId ] ( IEditorGraphListener* l ) { l->OnInvalidateSocketData( subgraphId, nodeId, socketId ); } );
	}

	void EditorGraphBase::OnInvalidateSocketData( GraphSubGraphId subgraphId, GraphNodeId nodeId, GraphSocketId socketId )
	{
		Forward( m_graphListner, [ subgraphId, nodeId, socketId ] ( IEditorGraphListener* l ) { l->OnInvalidateSocketDescription( subgraphId, nodeId, socketId ); } );
	}

	void EditorGraphBase::OnVariableCreated( GraphSubGraphId subgraphId, GraphVariableId variableId )
	{
		Forward( m_graphListner, [ subgraphId, variableId ] ( IEditorGraphListener* l ) { l->OnVariableCreated( subgraphId, variableId ); } );
	}

	void EditorGraphBase::OnVariableRemoved( GraphSubGraphId subgraphId, GraphVariableId variableId )
	{
		Forward( m_graphListner, [ subgraphId, variableId ] ( IEditorGraphListener* l ) { l->OnVariableRemoved( subgraphId, variableId ); } );
	}

	void EditorGraphBase::OnGraphReloaded( GraphSubGraphId ownerSubGraphId, GraphSubGraphId reloadedSubGraphId, GraphId nodeId )
	{
		Forward( m_graphListner, [ ownerSubGraphId, reloadedSubGraphId, nodeId ]( IEditorGraphListener* l ) { l->OnGraphReloaded( ownerSubGraphId, reloadedSubGraphId, nodeId ); } );
	}

	void EditorGraphBase::OnGraphIdChanged( GraphSubGraphId oldId, GraphSubGraphId newId )
	{
		Forward( m_graphListner, [ oldId, newId ]( IEditorGraphListener* l ) { l->OnGraphIdChanged( oldId, newId ); } );
	}

	void EditorGraphBase::OnCloseSubgraph( GraphSubGraphId subgraphId )
	{
		Forward( m_graphListner, [ subgraphId ] ( IEditorGraphListener* l ) { l->OnCloseSubgraph( subgraphId ); } );
	}

	void EditorGraphBase::OnExternalGraphOpened( const THandle<CResource>& resource )
	{
		Forward( m_graphListner, [ resource ] ( IEditorGraphListener* l ) { l->OnExternalGraphOpened( resource ); } );
	}

	void EditorGraphBase::OnSelectionChanged( GraphSubGraphId subgraphId, const red::DynArray< GraphId >& ids )
	{
		Forward( m_graphListner, [ subgraphId, &ids ] ( IEditorGraphListener* l ) { l->OnSelectionChanged( subgraphId, ids ); } );
	}

	void EditorGraphBase::RefreshLayout()
	{
		const static Int32 defaultExtent = 10000;

		red::DynArray< GraphId > nodes;
		GetNodes( nodes );

		red::DynArray< Point > positions;
		Point min( std::numeric_limits<Int32>::max(), std::numeric_limits<Int32>::max() );
		Point max( std::numeric_limits<Int32>::min(), std::numeric_limits<Int32>::min() );

		for ( GraphId nodeId : nodes )
		{
			Point blockPosition( std::numeric_limits<Int32>::max(), std::numeric_limits<Int32>::max() );
			if ( GetNodePosition( nodeId, blockPosition ) )
			{
				if ( blockPosition.x < min.x )
				{
					min.x = blockPosition.x;
				}

				if ( blockPosition.y < min.y )
				{
					min.y = blockPosition.y;
				}

				if ( blockPosition.x > max.x )
				{
					max.x = blockPosition.x;
				}

				if ( blockPosition.y > max.y )
				{
					max.y = blockPosition.y;
				}
			}
			positions.PushBack( blockPosition );
		}

		Int64 xExtents = Int64( max.x ) - Int64( min.x );
		Int64 yExtents = Int64( max.y ) - Int64( min.y );
		if ( xExtents > defaultExtent || yExtents > defaultExtent )
		{
			Point center( 0, 0 );
			Int32 defaultLimit = defaultExtent / 2;

			if ( min.x > -defaultLimit )
			{
				center.x += min.x + defaultLimit;
			}
			else if ( max.x < defaultLimit )
			{
				center.x -= defaultLimit - max.x;
			}

			if ( min.y > -defaultLimit )
			{
				center.y += min.y + defaultLimit;
			}
			else if ( max.y < defaultLimit )
			{
				center.y -= defaultLimit - max.y;
			}

			red::UniquePtr< EditGroupAction > groupAction;
			for ( Uint32 i = 0; i < nodes.Size(); ++i )
			{
				Point diff = positions[i] - center;
				if ( Abs( diff.x ) > defaultLimit || Abs( diff.y ) > defaultLimit )
				{
					if ( !groupAction )
					{
						groupAction = red::CreateUniquePtr< EditGroupAction >( "RefreshLayout" );
					}

					Vector2 dir( Float( diff.x ), Float( diff.y ) );
					dir.Normalize();
					dir *= (Float)defaultLimit;
					
					Point updatedPosition = center + Point( Int32( dir.X ), Int32( dir.Y ) );
					groupAction->AddAction( red::CreateUniquePtr< Action_MoveNode >( this, nodes[i], updatedPosition ) );
				}
			}

			IResourceEditor* editor = GetEditor();
			if ( editor && groupAction )
			{
				editor->GetAccessHistory().Execute( std::move( groupAction ) );
			}
		}
	}

	Bool EditorGraphBase::RefreshNode( GraphId nodeId )
	{
		return true;
	}

	void EditorGraphBase::ValidateNode( GraphId nodeId )
	{

	}

	MoveStatus EditorGraphBase::MoveNode( const GraphId& nodeId, const Point& moveOffset )
	{
		THandle< ISerializable > itemDescriptor = GetItemDescriptorData( nodeId );
		if ( !itemDescriptor )
			return Fail;

		THandle< NodeDescription > nodeDescriptor = Cast< NodeDescription >( itemDescriptor );
		if ( !nodeDescriptor )
			return Continue;

		nodeDescriptor->m_position += moveOffset;
		nodeDescriptor->NotifyPropertyChanged( rtti::AccessPath( "position" ) );

		MarkModified();

		return Success;
	}

	Bool EditorGraphBase::ExtractNodePosition( const THandle<GraphBackendData>& node, Point& outPosition ) const
	{
		if ( node->IsA<NodeDescription>() )
		{
			outPosition = Cast<NodeDescription>( node )->m_position;
			return true;
		}
		return false;
	}

	void EditorGraphBase::Select( const red::DynArray< GraphId >& selectionIds )
	{
		OnSelectionChanged( GetSubGraphId(), selectionIds );
	}

	const String& EditorGraphBase::GetName() const
	{
		return String::EMPTY();
	}

	const String EditorGraphBase::GetOwnerEditorName() const
	{
		// it's easier to identify editors by their main resource path, than the name
		const IResourceEditor* editor = GetEditor();
		return editor ? editor->GetPath().ToString() : String::EMPTY();
	}

	Bool EditorGraphBase::IsTopDownTree() const
	{
		return false;
	}

	Bool EditorGraphBase::HasClipboardSupport() const
	{
		return false;
	}

	Bool EditorGraphBase::HasValidationSupport() const
	{
		return false;
	}

	Bool EditorGraphBase::HasRemoveIntermediateNodeSupport() const
	{
		return false;
	}

	Bool EditorGraphBase::IsPostDeleteActionNeeded( const red::DynArray< GraphId >& nodesToDelete ) const
	{
		return false;
	}

	red::UniquePtr< IEditAction > EditorGraphBase::GetPostDeleteAction( ObjectHandler& objectHandler, const red::DynArray< GraphId >& nodesToDelete )
	{
		return nullptr;
	}

	Bool EditorGraphBase::IsReadOnly() const
	{
		return m_isReadonly;
	}

	void EditorGraphBase::SetReadOnly( const Bool isReadonly )
	{
		m_isReadonly = isReadonly;
	}

	void EditorGraphBase::ProvideConstructors( red::DynArray<IGraphObjectConstructor *>& outConstructors )
	{
		// create content constructors
		outConstructors.PushBack( RED_NEW( GraphConnectionObjectConstructor )( this ) );
		outConstructors.PushBack( RED_NEW( GraphNodeObjectConstructor )( this ) );
	}

	red::UniquePtr< IGraphImporter > EditorGraphBase::GetGraphImporter() const
	{
		return nullptr;
	}

	void InnerHandleToString( red::StringBuilder<String>& builder, const String& idString )
	{
		SerializableID id( FromStringDirect<Uint64>( idString ) );
		THandle< ISerializable > innerData;
		if ( GSerializableMap->FindSerializable( id, innerData ) )
		{
			builder.Append( innerData->GetFriendlyName().AsChar() );
			builder.Append( '\n' );
			DataToString( builder, innerData );
		}
	}

	void DataToString( red::StringBuilder<String>& builder, const THandle< ISerializable >& data )
	{
		if ( !data )
			return;

		EditableViewPtr view = data->CreateView();

		rtti::EditableProperties properties;
		view->GetRootProperties( properties );
		for ( const rtti::ClassEditablePropertyInfo property : properties.Get() )
		{
			rtti::ValuePtr value;
			if( !view->GetValue( IRTTIContext::GetDefault(), rtti::AccessPath()[property.m_name], value ) )
			{
				continue;
			}

			builder.Append( property.m_name.AsChar() );
			builder.Append( '=' );

			if ( value->IsHandle() )
			{
				InnerHandleToString( builder, value->GetHandleValue() );
			}
			else if ( value->IsArray() && value->GetNumElements() > 0 && value->GetArrayElement( 0 )->IsHandle() )
			{
				for ( Uint32 index = 0; index < value->GetNumElements(); ++index )
				{
					const rtti::ValuePtr innerValue = value->GetArrayElement( index );
					InnerHandleToString( builder, innerValue->GetHandleValue() );
				}
			}
			else
			{
				builder.Append( value->ToString() );
				builder.Append( '\n' );
			}
		}
	}

}