/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "physicsBase.h"

#include "../../backendEngine/include/editableProxy.h"

namespace physics
{
	class PhysicsEditableProxy : public tools::IEditableProxy
	{
	public:
		PhysicsEditableProxy( const PhysicsBasePtr& physicsBase );

		virtual Bool IsReadOnly() const override final;

		virtual world::NodeScalingMode GetScalingMode() const override final;
		virtual Bool SupportsGizmoTransformation() const override final;

		virtual tools::EditorObjectID GetEditorObjectID() const override final;

		virtual const tools::ViewClass GetViewClass() const override final;

		virtual red::SharedPtr<tools::IEditableView> CreateView( const tools::ViewClass viewClass ) const override final;

		virtual void ToggleSelectionVisualization( const Bool isSelected, const red::SharedPtr< tools::SelectionState > & selection ) const override final;

		virtual Transform GetWorldTransform() const override final;

		virtual Bool SetWorldTransform( const Transform& newTransform, Bool preview = false ) override final;
		virtual Bool SetScale( const Vector3& newScale ) override final;
		virtual Vector3 GetScale() const override final;

		virtual Bool RestoreObject() const override final;

		virtual THandle< ISerializable > GetSerializable() const override final;

	private:
		Bool EnsureExistance() const;

	private:
		tools::EditorObjectID				m_globalId;
		mutable PhysicsBasePtr				m_physicsBase;

	};
}