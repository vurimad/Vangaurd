/* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.*/

#include "build.h"
#include "physicsBase.h"
#include "physicsEditableProxy.h"
#include "physicsEditorScene.h"
#include "meshStructureNodes.h"

#include "../../../common/renderData/include/renderVertices.h"
#include "../../../common/renderData/include/renderer.h"
#include "../../../common/renderData/include/renderResource.h"
#include "../../../common/interop/include/interopUtilsRTTI.h"
#include "../../../common/redFileSystem/include/file.h"
#include "../../../common/redReflection/include/editableView.h"
#include "../../../common/renderDraw/include/fragmentBuilder.h"

RTTI_BEGIN_ENUM_IN_NAMESPACE( EPhysicsBaseType, physics )
RTTI_ENUM_OPTION_ALIAS( "Base", EPBT_Base );
RTTI_ENUM_OPTION_ALIAS( "Actor", EPBT_Actor );
RTTI_ENUM_OPTION_ALIAS( "Shape", EPBT_Shape );
RTTI_ENUM_OPTION_ALIAS( "Joint", EPBT_Joint );
RTTI_ENUM_OPTION_ALIAS( "Mesh", EPBT_Mesh );
RTTI_ENUM_OPTION_ALIAS( "Max", EPBT_Max );
RTTI_END_ENUM();

RTTI_BEGIN_TYPE_IN_NAMESPACE( PhysicsBase, physics );
RTTI_PARENT_TYPE( ISerializable );
RTTI_PROPERTY_CATEGORY( "1. Base properties" );
RTTI_PROPERTY( m_baseType );
RTTI_PROPERTY( m_localToWorld ).customEditor("transformMatrix").editable().tooltip( "Physical object transform, relative to the origin." );
RTTI_PROPERTY( m_physicsBaseId );
RTTI_PROPERTY( m_flags );
RTTI_END_TYPE();

namespace physics
{
	PhysicsBase::PhysicsBase( PhysicsBaseID id,PhysicsEditorScene* editorParent /*= nullptr */ )
		: m_baseType( EPBT_Base )
		, m_physicsBaseId( id )
		, m_localToWorld( Matrix::IDENTITY() )
		, m_editorScene( editorParent )
		, m_flags( EPBF_WasModified | EPBF_IsVisible )
		, m_physicsRenderHelper( nullptr )
		, m_alpha( 255 )
		, m_bounds( Box::EMPTY() )
	{}

	PhysicsBase::PhysicsBase( PhysicsBaseID id, EPhysicsBaseType type,PhysicsEditorScene* editorParent )
		: m_baseType( type )
		, m_physicsBaseId( id )
		, m_localToWorld( Matrix::IDENTITY() )
		, m_editorScene( editorParent )
		, m_flags( EPBF_WasModified | EPBF_IsVisible )
		, m_physicsRenderHelper( nullptr )
		, m_alpha( 255 )
		, m_bounds( Box::EMPTY() )
	{}

	PhysicsBase::~PhysicsBase()
	{

	}

	void PhysicsBase::OnUpdate()
	{
		if( IsFlagSet( EPBF_WasModified ) )
		{
			SetFlag( EPBF_RequestRenderingRebuild, true );

			SetFlag( EPBF_WasModified, false );
		}
	}

	void PhysicsBase::OnRender( rend::IDebugDrawer& dd, PhysicsRendererOptionsPtr options )
	{
		if( IsFlagSet( EPBF_IsVisible ) )
		{
			if( m_physicsRenderHelper )
				m_physicsRenderHelper->Render( dd, GetSelectableObject() );

			if( options->m_bounds )
			{
				rend::FragmentBuilder builder( dd );
				builder.EnableTransparency( true );
				builder.EnableOverlay( true );
				builder.SetColor( Color::CYAN( ) );
				builder.SetLocalTransform( Matrix::IDENTITY() );
				builder.AddWireBox( GetBoundsWorld() );
			}
		}
	}

	void PhysicsBase::OnPostLoad( const PostLoadContext& context )
	{
		TBaseClass::OnPostLoad( context );

		SetFlag( EPBF_WasModified, true );
		SetFlag( EPBF_IsVisible, true );
		SetFlag( EPBF_IsSelected, false );
	}

	void PhysicsBase::OnRestore(PhysicsEditorScene* scene )
	{
		if( m_editorScene != scene  )
		{
			m_editorScene = scene;
		}
		SetFlag( EPBF_WasModified, true );
		SetFlag( EPBF_IsVisible, true );
	}

	void PhysicsBase::OnRemove()
	{
		m_physicsRenderHelper = nullptr;
	}

	void PhysicsBase::PrepareForRendering( PhysicsRendererOptionsPtr options )
	{}

	void PhysicsBase::SetupInterop( PhysicsBase* ptr, ::interop::utils::Object& op )
	{
		op.RegisterObject( "data", [ ptr ] ( ::interop::IContext& ctx )
		{
			return interop::utils::CreateRTTIAccessFunc( ctx, ptr, ptr->GetClass(), false );
		} );
	}

	void PhysicsBase::SetLocalToWorld( const Matrix& localToWorld )
	{
		m_localToWorld = localToWorld;
		if( m_physicsRenderHelper )
		{
			m_physicsRenderHelper->SetLocalToWorld( m_localToWorld );
		}

		NotifyTransformChanged();
		NotifyPropertyChanged( rtti::AccessPath()[ "localToWorld" ] );
	}

	Bool PhysicsBase::SetScale( const Vector3& newScale )
	{
		return false;
	}

	Vector3 PhysicsBase::GetScale() const
	{
		return Vector3::ONES();
	}

	void PhysicsBase::SetVisible(Bool decision)
	{
		SetFlag( EPBF_IsVisible, decision );
	}

	void PhysicsBase::NotifyTransformChanged()
	{
		for( PhysicsBaseWeakPtr weakPtr : m_transformChangeListeners )
		{
			auto sharedPtr = weakPtr.ToHandle();
			if( sharedPtr )
			{
				sharedPtr->OnNotifyTransformChanged( GetPhysicsObjectID() );
			}
		}
	}

	void PhysicsBase::ToggleSelectionVisualization( const Bool drawAsSelected )
	{
		if( IsFlagSet( EPBF_SelectionVisualizationEnabled ) != drawAsSelected )
		{
			// toggle flag
			SetFlag( EPBF_SelectionVisualizationEnabled, drawAsSelected );
			if( m_physicsRenderHelper )
			{
				m_physicsRenderHelper->SetSelected( drawAsSelected );
			}
			if( !drawAsSelected )
			{
				SetFlag( EPBF_ScheduleWakeUp, true );
			}
			SetFlag( EPBF_IsSelected, drawAsSelected );
		}
	}

	void PhysicsBase::NotifySelectionHandlers()
	{
		for( const red::WeakPtr<tools::IMeshPhysicsStructureNode>& handlerWeak : m_notificationHandlers )
		{
			if( red::SharedPtr<tools::IMeshPhysicsStructureNode> handler = handlerWeak.Lock() )
			{
				handler->OnPhysicsObjectSelected( HandleFromThis<PhysicsBase>() );
			}
		}
	}

	red::SharedPtr< tools::IEditableProxy > PhysicsBase::CreateEditableProxy( Bool getParentProxy ) const
	{
		red::SharedPtr< tools::IEditableProxy > ret;
		ret.Reset( RED_NEW( PhysicsEditableProxy )( HandleFromThis< PhysicsBase >() ) );
		return ret;
	}

	red::SharedPtr< tools::IEditableView > PhysicsBase::CreateView( const red::SharedPtr< tools::IEditableView >& parentView /*= nullptr*/, const Bool isReadOnly /*= false*/, const CName viewClass /*= CName::NONE()*/, THandle< ISerializable > userContext /*= nullptr */ ) const
	{
		if( userContext == nullptr )
		{
			userContext = HandleFromPtr( GetEditorScene() );
		}
		return ISerializable::CreateView( parentView, isReadOnly, viewClass, userContext );
	}

	tools::EditorObjectIDPath PhysicsBase::Selectable_CreateEditorId() const
	{
		return tools::EditorObjectIDPath( GetEditorObjectID() );
	}

	tools::EditorObjectID PhysicsBase::GetEditorObjectID() const
	{
		if( auto editorScene = GetEditorScene() )
		{
			return editorScene->GetEditorIdFromPhysicsBaseId( GetPhysicsObjectID() );
		}
		return tools::EditorObjectID();
	}

	Bool PhysicsBase::IsFlagSet( PhysicsBaseFlag flag ) const
	{
		return ( m_flags & flag ) > 0;
	}

	void PhysicsBase::SetFlag( PhysicsBaseFlag flag, Bool decision )
	{
		if( decision )
		{
			m_flags |= flag;
		}
		else
		{
			m_flags &= ( 0xFFFFFFFF ^ flag );
		}
	}

	void PhysicsBase::RegisterListener( PhysicsBaseWeakPtr listener )
	{
		if( !listener.Expired() )
		{
			red::alg::PushBackUnique( m_transformChangeListeners, listener );
		}
	}

	void PhysicsBase::UnregisterListener( PhysicsBaseWeakPtr listener )
	{
		if( !listener.Expired() )
		{
			m_transformChangeListeners.Remove( listener );
		}
	}

	void PhysicsBase::OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& value )
	{
		ISerializable::OnPropertyPostChange( path, oldValue, value );
		SetFlag( EPBF_WasModified, true );

		for( const red::WeakPtr<tools::IMeshPhysicsStructureNode>& handlerWeak : m_notificationHandlers )
		{
			if( red::SharedPtr<tools::IMeshPhysicsStructureNode> handler = handlerWeak.Lock() )
			{
				handler->OnPhysicsObjectPropertyChanged( HandleFromThis<PhysicsBase>(), path );
			}
		}
	}

	physics::PhysicsEditorScene* PhysicsBase::GetEditorScene() const
	{
		return m_editorScene;
	}

	String PhysicsBase::GetName() const
{
		return "Base";
	}

	void PhysicsBase::AttachNotificationHandler( const red::SharedPtr<tools::IMeshPhysicsStructureNode>& handlerToAdd )
	{
		for( const red::WeakPtr<tools::IMeshPhysicsStructureNode>& handlerWeak : m_notificationHandlers )
		{
			if( red::SharedPtr<tools::IMeshPhysicsStructureNode> handler = handlerWeak.Lock() )
			{
				if( handler == handlerToAdd )
					return;
			}
		}
		m_notificationHandlers.PushBack( handlerToAdd );
	}

	void PhysicsBase::EvaluateSerializationDependencies( red::DynArray< THandle< ISerializable > >& dependenciesArray )
	{

	}

}

