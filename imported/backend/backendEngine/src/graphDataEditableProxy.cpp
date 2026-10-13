/*
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "build.h"

#include "graphDataEditableProxy.h"
#include "../../../common/redReflection/include/backendData.h"
#include "../../../common/redReflection/include/editableView.h"

namespace tools
{

	GraphDataEditableProxy::GraphDataEditableProxy( const THandle< GraphBackendData >& data )
		: m_globalId( data->GetEditorObjectID() )
		, m_data( data )
	{
	}

	Bool GraphDataEditableProxy::IsReadOnly() const
	{
		return false;
	}

	world::NodeScalingMode GraphDataEditableProxy::GetScalingMode() const
	{
		return world::NodeScalingMode::None;
	}

	Bool GraphDataEditableProxy::SupportsGizmoTransformation() const
	{
		return false;
	}

	EditorObjectID GraphDataEditableProxy::GetEditorObjectID() const
	{
		return m_globalId;
	}

	const tools::ViewClass GraphDataEditableProxy::GetViewClass() const
	{
		if ( !EnsureExistance() )
			return CName::NONE();

		return m_data->GetClass()->GetName();
	}

	red::SharedPtr<tools::IEditableView> GraphDataEditableProxy::CreateView( const tools::ViewClass viewClass ) const
	{
		if ( !EnsureExistance() )
			return red::SharedPtr<tools::IEditableView>();

		return m_data->CreateView( nullptr, IsReadOnly(), viewClass );
	}

	void GraphDataEditableProxy::ToggleSelectionVisualization( const Bool isSelected, const red::SharedPtr< SelectionState > & selection ) const
	{
		/*m_data->ChangeSelection( isSelected );*/
	}

	Transform GraphDataEditableProxy::GetWorldTransform() const
	{
		return Transform::IDENTITY();
	}

	Bool GraphDataEditableProxy::SetWorldTransform( const Transform& newTransform, Bool preview )
	{
		return false;
	}

	Bool GraphDataEditableProxy::RestoreObject() const
	{
		return EnsureExistance();
	}

	THandle< ISerializable > GraphDataEditableProxy::GetSerializable() const
	{
		if ( !EnsureExistance() )
			return nullptr;

		return m_data;
	}

	Bool GraphDataEditableProxy::EnsureExistance() const
	{
		return m_data != nullptr;
	}

	GraphDataEditableProxyPtr GraphDataEditableProxy::Create( const THandle< GraphBackendData >& data )
	{
		return red::CreateSharedPtr< GraphDataEditableProxy >( data );
	}

} // tools
