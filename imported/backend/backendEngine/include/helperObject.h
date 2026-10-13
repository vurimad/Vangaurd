/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "previewFwd.h"
#include "editableProxy.h"
#include "objectResolver.h"
#include "backendMemory.h"

#include "../../../common/renderData/include/renderSelectable.h"
#include "../../../common/interop/include/interopBase.h"
#include "../../../common/interop/include/interopUtilsManual.h"

namespace tools
{
	class IHelperObject;

	typedef red::UniquePtr< IHelperObject >	HelperObjectPtr;

	/// Helper object interface
	/// Must implement IObjectResolver in order to manage internal "subobjects"
	class BACKEND_ENGINE_API IHelperObject : public IObjectResolver, public red::NonCopyable
	{
		RTTI_DECLARE_TYPE( IHelperObject )

	public:

		enum class ColorMode
		{
			Normal,
			Hover,
			Selected,
		};

		enum class QuickPutObjectMode
		{
			None,				// quick put object mode is not supported
			WithDepthmap,		// quick put object mode is supported and requires depth map
			WithoutDepthmap,	// quick put object mode is supported and does not require depth map
		};

		explicit IHelperObject( const EditableProxyPtr& parentObjectProxy );
		virtual ~IHelperObject() { }

		virtual void BindSharedData( PreviewPanelSharedData* sharedData );

		// Get helper object overall color
		virtual Color GetColor( ColorMode mode ) const;

		// get parent (edited object)
		const THandle< ISerializable >& GetParentObject() const { return m_parentObject; }
		const EditorObjectIDPath& GetParentObjectPath() const { return m_parentPath; }

		// get edited object ID
		const EditorObjectID& GetParentEditorObjectID() const;

		// get preview panel shared state for which the helper object is used
		PreviewPanelSharedData* GetPreviewPanelSharedData() const { return m_previewPanelSharedData; }
	
		// Get "helper object to world" transform
		const Transform& GetTransform() const { return m_worldTransform; }
		const Transform GetInverseTransform() const { return m_worldTransform.GetInverse(); }

		// Get underlying object scaling. This is supported only by some helper objects.
		const Vector3& GetScale() const { return m_scale; }

		// helper object rendering
		virtual void OnRender( const CRenderCamera& camera, rend::IDebugDrawer& debugDrawer ) const = 0;

		// get helper object type name
		virtual const String& GetName() const;

		virtual Bool RegisterInteropObject( ::interop::utils::Object& op );

		// return currently supported put object mode and creation info for it (can depend on selection)
		virtual void QueryQuickPutObjectInfo( QuickPutObjectMode& outMode, ObjectCreationInfo& outCreationInfo ) const = 0;

		// find optimal object placement location when depth buffer is not supported
		// NOTE: this can be useful for finding nearest position on a spline, etc.
		virtual Bool CalculateObjectPlacementLocation( const Vector3& rayOrigin, const Vector3& rayDirection, Vector3& outLocation ) const = 0;
		
		const EditableProxyPtr GetProxy() const { return m_proxy; }

		virtual void RegisterShortcuts( ShortcutManager* manager, PreviewPanel* panel ) {}
		virtual void UnregisterShortcuts( ShortcutManager* manager ) {}

		// Called from WorldEditorState_HelperObjects when the editor state
		// gets pushed/poped from the editor state stack.
		virtual void OnAttachedToPreviewPanel( PreviewPanel* panel ) {}
		virtual void OnDetachedFromPreviewPanel( PreviewPanel* panel ) {}

		virtual void OnActionExecuted() {}

	private:
		// NOTE: we could use just EditableProxy here but we won't be able to resolve it in helper object edit mode
		Transform m_worldTransform;
		Vector3 m_scale;

		EditorObjectIDPath m_parentPath;

		// parent object (e.g. Node or Component) - used for modification notifying
		THandle< ISerializable > m_parentObject;
		EditableProxyPtr m_proxy;

		// parent preview panel shared data
		PreviewPanelSharedData* m_previewPanelSharedData;
	};


	// Base class for helper subobjects (single edge, point, etc.) within the whole helper object
	class BACKEND_ENGINE_API IHelperObjectSubobject
		: public ISerializable
		, public IRenderSelectable
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
		RTTI_DECLARE_POLYMORPHIC_TYPE( IHelperObjectSubobject );

	public:
		virtual ~IHelperObjectSubobject() { }

		virtual EditableProxyPtr CreateEditableProxy() const = 0;
	};

} // tools
