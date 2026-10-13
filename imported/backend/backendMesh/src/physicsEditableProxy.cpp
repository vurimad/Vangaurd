/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "physicsEditableProxy.h"
#include "physicsEditorScene.h"
#include "../../../common/redReflection/include/editableView.h"
#include "../../backendEngine/include/selectionState.h"

namespace physics
{
	PhysicsEditableProxy::PhysicsEditableProxy( const PhysicsBasePtr& physicsBase )
		: m_physicsBase( physicsBase )
		, m_globalId( physicsBase->GetEditorObjectID() )
	{

	}

	Bool PhysicsEditableProxy::EnsureExistance() const
	{
		return m_physicsBase != nullptr;
	}

	red::SharedPtr<tools::IEditableView> PhysicsEditableProxy::CreateView( const tools::ViewClass viewClass ) const
	{
		if( !EnsureExistance() )
			return red::SharedPtr<tools::IEditableView>();

		return m_physicsBase->CreateView( nullptr, IsReadOnly(), viewClass, HandleFromPtr( m_physicsBase->GetEditorScene() ) );
	}

	const tools::ViewClass PhysicsEditableProxy::GetViewClass() const
	{
		if( !EnsureExistance() )
			return CName::NONE();

		return m_physicsBase->GetClass()->GetName();
	}

	tools::EditorObjectID PhysicsEditableProxy::GetEditorObjectID() const
	{
		return m_globalId;
	}

	Bool PhysicsEditableProxy::IsReadOnly() const
	{
		return false;
	}

	world::NodeScalingMode PhysicsEditableProxy::GetScalingMode() const
	{
		if( EnsureExistance() )
		{
			if( m_physicsBase->GetBaseType() == EPBT_Shape )
				return world::NodeScalingMode::All;
		}

		return world::NodeScalingMode::None;
	}

	Bool PhysicsEditableProxy::SupportsGizmoTransformation() const
	{
		if ( EnsureExistance() )
		{
			return true;
		}
		return false;
	}

	void PhysicsEditableProxy::ToggleSelectionVisualization( const Bool isSelected, const red::SharedPtr< tools::SelectionState > & selection ) const
	{
		if( EnsureExistance() )
		{
			m_physicsBase->ToggleSelectionVisualization( isSelected );
			if( isSelected && selection->Size() == 1)
			{
				m_physicsBase->NotifySelectionHandlers();
			}
		}
	}

	Transform PhysicsEditableProxy::GetWorldTransform() const
	{
		if( EnsureExistance() )
			return m_physicsBase->GetLocalToWorld().ToXform();

		return Transform::IDENTITY();
	}

	Bool PhysicsEditableProxy::SetWorldTransform( const Transform& newTransform, Bool preview )
	{
		if( EnsureExistance() )
		{
			m_physicsBase->SetLocalToWorld( newTransform.ToMatrix() );
			return true;
		}

		return false;
	}

	Bool PhysicsEditableProxy::SetScale( const Vector3& newScale )
	{
		if( EnsureExistance() )
		{
			return m_physicsBase->SetScale( newScale );
		}

		return false;	
	}

	Vector3 PhysicsEditableProxy::GetScale() const
	{
		if( EnsureExistance() )
		{
			return m_physicsBase->GetScale();
		}

		return Vector3::ONES();
	}

	Bool PhysicsEditableProxy::RestoreObject() const
	{
		return EnsureExistance();
	}

	THandle< ISerializable > PhysicsEditableProxy::GetSerializable() const
	{
		if( !EnsureExistance() )
			return nullptr;

		return m_physicsBase;
	}

}