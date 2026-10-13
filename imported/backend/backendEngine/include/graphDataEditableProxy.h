/*
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "build.h"

#include "editableProxy.h"
#include "editorGraphTypes.h"

namespace tools 
{
	class GraphDataEditableProxy;
	using GraphDataEditableProxyPtr = red::SharedPtr< GraphDataEditableProxy >;

	class BACKEND_ENGINE_API GraphDataEditableProxy : public IEditableProxy
	{
	public:
		GraphDataEditableProxy( const THandle< GraphBackendData >& data );
		virtual Bool IsReadOnly() const override final;
		virtual world::NodeScalingMode GetScalingMode() const override final;
		virtual Bool SupportsGizmoTransformation() const override final;
		virtual tools::EditorObjectID GetEditorObjectID() const override final;
		virtual const tools::ViewClass GetViewClass() const override final;
		virtual red::SharedPtr<tools::IEditableView> CreateView( const tools::ViewClass viewClass ) const override final;
		virtual void ToggleSelectionVisualization( const Bool isSelected, const red::SharedPtr< SelectionState > & selection ) const override final;
		virtual Transform GetWorldTransform() const override final;
		virtual Bool SetWorldTransform( const Transform& newTransform, Bool preview = false ) override final;
		virtual Bool RestoreObject() const override final;
		virtual THandle< ISerializable > GetSerializable() const override final;

		// create editable proxy for backend data
		static GraphDataEditableProxyPtr Create( const THandle< GraphBackendData >& data );

	private:
		Bool EnsureExistance() const;

		tools::EditorObjectID m_globalId;
		mutable THandle< GraphBackendData > m_data;
	};

} // tools