/**
* Copyright (c) 2015-2019 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "editableFwd.h"
#include "backendMemory.h"

#include "../../../common/redReflection/include/editorObjectIdPath.h"
#include "../../../common/world/include/worldNodeTypes.h"

namespace tools
{
	class SelectionState;
	enum class GizmoType : Uint8;
	enum class GizmoSpaceType : Uint8;

	/// Proxy for editable object
	/// This represents a little bit more knowledge about what we are editing, especially the GlobalID of the object
	/// From the editable proxy we can create a IEditableView to access the underlying properties of the object
	/// The key element of the proxy is that we can negotiate a view class that is compatible with multiple editable proxies at the same time (this is useful in case of editing multiple objects at the same time)
	class BACKEND_ENGINE_API IEditableProxy
	{
		RTTI_DECLARE_POLYMORPHIC_TYPE( IEditableProxy );
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		IEditableProxy();
		virtual ~IEditableProxy();

		/// Is this object read only ?
		/// This is a global state of the object
		virtual Bool IsReadOnly() const = 0;
		
		// Returns true if this is a proxy for an instance editing (not the actual object)
		virtual Bool IsInstance() const;

		// Get scaling mode for this object
		virtual world::NodeScalingMode GetScalingMode() const;
		virtual Bool SupportsGizmoTransformation() const;
		virtual Bool GetSpaceOverride( GizmoSpaceType& space ) const;

		//-----

		/// Get Global ID of the object, unique in the CURRENT OBJECT RESOLVER
		/// NOTE: GetContext.Resolve(GetEditorObjectID()) should yield the same proxy
		virtual EditorObjectID GetEditorObjectID() const = 0;

		/// Get Global ID Path of the object, unique in the whole editor
		/// NOTE: GetContext.Resolve(GetEditorObjectIDPath()) should yield the same proxy
		virtual EditorObjectIDPath GetEditorObjectIDPath() const;

		// By default same than GetEditorObjectID. However in some case Instance object get different Id for selection than edition. 
		virtual EditorObjectID GetInstanceEditorObjectID() const; 
		
		/// Get the master view class of the object
		/// This is the view class that represents the object in the full way
		/// Since the views usually map RTTI classes this is the name of the class of the object
		virtual const ViewClass GetViewClass() const = 0;

		/// Create object "view" of a given class via which it's possible to modify object's properties
		/// May return empty view if the given view class is not supported
		virtual EditableViewPtr CreateView( const ViewClass viewClass ) const = 0;

		//-----

		/// Change the selection visualization on/off
		virtual void ToggleSelectionVisualization( const Bool isSelected, const red::SharedPtr< SelectionState > & selection ) const = 0;

		/// Change the selection outline visualization on/off
		virtual void ToggleSelectionOutline( Bool enableOutline ) const;

		virtual const Bool IsVisible() const { return true; }
		virtual void SetVisibility( const Bool isVisible ) {}


		/// Change editing state on/off.
		/// Can be used to modify visualization or change some internal object state.
		virtual void ToggleEditingState( const Bool isEdited );

		//-----

		/// Get current object transform as viewed in the world space
		/// This allows the gizmos and object movement to be generalized
		virtual Transform GetWorldTransform() const = 0;

		/// Set desired object transform in world space
		/// This allows the gizmos and object movement to be generalized
		virtual Bool SetWorldTransform( const Transform& newTransform, Bool preview = false ) = 0;

		/// Get global bounding box
		virtual Box GetWorldBoundingBox() const;

		/// Get the current object scale (in local space)
		/// By default returns identity
		virtual Vector3 GetScale() const;

		/// Tell if this proxy can override gizmo type
		/// By default returns false;
		virtual Bool CanOverrideGizmoType() const;

		/// Get the gizmo type that will be used for this proxy
		/// By default returns GizmoType::Translation
		virtual GizmoType GetGizmoType() const;

		/// Set desired object scale in local space
		virtual Bool SetScale( const Vector3& newScale );

		/// Get the current object pivot transform (in local space)
		/// By default returns identity
		virtual Transform GetPivotTransform() const;

		/// Set desired object pivot transform in local space
		virtual Bool SetPivotTransform( const Transform& newTransform );

		//-----

		/// Restore actual object in the selection proxy
		/// This is needed if a deep undo is done and some object may have been deleted in the mean time
		/// This MUST return false if the restoration of original object has failed
		virtual Bool RestoreObject() const = 0;

		/// Get pointer to the edited object itself
		/// NOTE: any actions on those 
		virtual THandle< ISerializable > GetSerializable() const = 0;

		virtual void CacheTransformationPivot( const Vector3& pivot ) {}
	
		virtual red::SharedPtr< IEditableProxy > GetParentEditableProxy() const;
	};

	/// Editable object handle
	typedef red::SharedPtr< IEditableProxy >	EditableProxyPtr;

} // tools