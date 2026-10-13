/**
* Copyright (c) 2015-2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "editableProxy.h"
#include "gizmoTypes.h"

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( IEditableProxy, tools );
RTTI_END_TYPE();

namespace tools
{

	IEditableProxy::IEditableProxy()
	{
	}

	IEditableProxy::~IEditableProxy()
	{
	}
	
	Bool IEditableProxy::IsInstance() const
	{
		return false;
	}

	world::NodeScalingMode IEditableProxy::GetScalingMode() const
	{
		return world::NodeScalingMode::Uniform;
	}

	Bool IEditableProxy::SupportsGizmoTransformation() const
	{
		return true;
	}

	Bool IEditableProxy::GetSpaceOverride( GizmoSpaceType& space ) const
	{
		return false;
	}

	EditorObjectIDPath IEditableProxy::GetEditorObjectIDPath() const
	{
		return EditorObjectIDPath( GetEditorObjectID() );
	}

	EditorObjectID IEditableProxy::GetInstanceEditorObjectID() const
	{
		return GetEditorObjectID();
	}

	Box IEditableProxy::GetWorldBoundingBox() const
	{
		const Float defaultSize = 1.0f;
		return Box( GetWorldTransform().GetPosition(), defaultSize );
	}

	Vector3 IEditableProxy::GetScale() const
	{
		return Vector3::ONES();
	}

	Bool IEditableProxy::CanOverrideGizmoType() const
	{
		return false;
	}

	GizmoType IEditableProxy::GetGizmoType() const
	{
		return GizmoType::Translation;
	}

	Bool IEditableProxy::SetScale( const Vector3& newScale )
	{
		RED_FATAL_ASSERT( GetScalingMode() != world::NodeScalingMode::None, "This editable proxy type does not support scaling" );
		return true;
	}

	Transform IEditableProxy::GetPivotTransform() const
	{
		return Transform::IDENTITY();
	}

	Bool IEditableProxy::SetPivotTransform(const Transform& newTransform)
	{
		return false;
	}

	void IEditableProxy::ToggleEditingState( const Bool isEdited )
	{
	}

	void IEditableProxy::ToggleSelectionOutline( Bool enableOutline ) const
	{
	}

	red::SharedPtr< IEditableProxy > IEditableProxy::GetParentEditableProxy() const
	{
		return nullptr;
	}

} // tools