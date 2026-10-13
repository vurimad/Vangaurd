/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../backend/backendEngine/include/helperObject.h"
#include "../../backend/backendEngine/include/previewPanelSharedData.h"
#include "../../backend/backendEngine/include/objectHandler.h"

namespace rend
{
	class FragmentBuilder;
}

namespace tools
{
	class IPreviewCommand;

	class PreviewObject;
	class PreviewObjectHolder;
	class PreviewPanelSharedData;

	using PreviewObjectPtr = THandle< PreviewObject >;
	using PreviewObjectHolderPtr = THandle< PreviewObjectHolder >;

	class BACKEND_ENGINE_API PreviewEditModeAdapter : public IHelperObject
	{
	public:
		PreviewEditModeAdapter( const tools::EditableProxyPtr& proxy );
		virtual ~PreviewEditModeAdapter() override;

		// IHelperObject interface
		virtual const String& GetName() const override;
		virtual void OnRender( const CRenderCamera& camera, rend::IDebugDrawer& debugDrawer ) const override;
		virtual void QueryQuickPutObjectInfo( QuickPutObjectMode& outMode, ObjectCreationInfo& outCreationInfo ) const override;
		virtual Bool CalculateObjectPlacementLocation( const Vector3& rayOrigin, const Vector3& rayDirection, Vector3& outLocation ) const override;

		// IObjectResolver interface
		virtual Bool IsExclusive() const override final;
		virtual Bool IsResolvable( const EditorObjectIDPath& path, Bool forEditing ) const override final;
		virtual Bool SupportsContentType( const CName& contentTypeName ) const override;
		virtual Bool CanCreateObject( const ObjectCreationInfo& info, String* outReason = nullptr ) const override final;
		virtual Bool CanDeleteObjects( const red::ArraySpan< const EditorObjectIDPath >& paths, Bool isCut, String* outFailReason = nullptr ) const override;
		virtual red::SharedPtr< IEditableProxy > ResolveProxy( const EditorObjectIDPath& path, const ResolvingParams& params = ResolvingParams() ) const override final;
		virtual THandle< ISerializable > ResolveObject( const EditorObjectIDPath& path, const ResolvingParams& param = ResolvingParams() ) const override final;
		virtual red::SharedPtr< IObjectPreview > CreateObjectPreview( const ObjectCreationInfo& initInfo ) override;
		virtual red::SharedPtr< IObjectPersistentCollection > CaptureObjects( const red::ArraySpan< const EditorObjectIDPath >& paths, const CapturingParams& params = CapturingParams() ) override final;
		virtual red::SharedPtr< IObjectPersistentCollection > Action_CreateObjects( const red::ArraySpan< const ObjectCreationInfo > creationInfos, const CreateObjectsOutParams& out ) override final;
		virtual Bool Action_DeleteObjects( DeleteContext& deleteContext ) override final;
		virtual Bool Action_RestoreObjects( const red::SharedPtr< IObjectPersistentCollection >& objects, red::DynArray< EditorObjectIDPath >& outActualCreatedPaths ) override final;
		virtual Bool Action_PlaceObjects( const red::SharedPtr< IObjectPersistentCollection >& objects, const ObjectPlacementInfo& info, red::DynArray< EditorObjectIDPath >& outActualCreatedPaths ) override final;
		virtual red::SharedPtr< IObjectPersistentCollection > Action_ReplaceObjects( const red::ArraySpan< const EditorObjectIDPath >& objectPathsToReplace, const red::SharedPtr< IObjectPersistentCollection >& objectTemplate, red::DynArray< EditorObjectIDPath >& outActualCreatedPaths ) override final;
		virtual void RegisterShortcuts( ShortcutManager* manager, PreviewPanel* panel ) override;

		red::DynArray< EditorObjectID > GetSelection() const;
		void SelectObjects( const red::DynArray< EditorObjectID >& objects );

	protected:
		virtual EditorObjectID GetInsertLocation() const;
		virtual Bool CanBeResolved( const EditorObjectID& id ) const;

		virtual void CaptureDeleteData( const EditorObjectID& id, red::DynArray< PreviewObjectHolderPtr >& deleteData ) const;
		virtual void RestoreObjects( const red::DynArray< PreviewObjectHolderPtr >& objects );
		virtual void RemoveObjects( const red::DynArray< EditorObjectID >& objects );


		void SetCommand( const red::SharedPtr< IPreviewCommand >& command );

		red::DynArray< PreviewObjectPtr > m_previewObjects;

	private:
		red::SharedPtr< IPreviewCommand > m_activeCommand;
	};

	///
	///
	///

	// abstract area shape point data used for serialization
	class PreviewObjectHolder : public ISerializable
	{
		RTTI_DECLARE_TYPE( PreviewObjectHolder );
	public:
		PreviewObjectHolder();
		virtual ~PreviewObjectHolder() override;

		virtual EditorObjectID GetEditorObjectID() const override;

		EditorObjectID m_editorID{};
		THandle< ISerializable > m_data;
	};

	///
	///
	///

	// Base class for preview objects that are wrapped by PreviewObjectProxy
	class PreviewObject : public IHelperObjectSubobject
	{
		RTTI_DECLARE_TYPE( PreviewObject );
	public:
		PreviewObject();
		PreviewObject( PreviewEditModeAdapter* mode, EditorObjectID editorId );
		virtual ~PreviewObject() override;

		virtual Vector3 GetPosition() const;
		virtual Bool Move( const Vector3& postion );

		virtual Transform GetWorldTransform() const;
		virtual Bool SetWorldTransform( const Transform& localToWorld, Bool preview );

		Color GetColor( Bool overlay ) const;

		virtual void OnRender( const EditModeViewport& viewport, rend::FragmentBuilder& builder, Bool overlay ) const;

		Bool IsHovered() const;
		Bool IsSelected() const;
		void SetHovered( Bool hovered );
		void SetSelected( Bool selected );

		EditableProxyPtr GetParentEditableProxy() const;

		///
		/// IHelperObjectSubobject interface
		///

		virtual EditableProxyPtr CreateEditableProxy() const override;
		virtual EditorObjectIDPath Selectable_CreateEditorId() const override;

		///
		/// ISerializable interface
		///

		virtual EditorObjectID GetEditorObjectID() const override;

	protected:
		PreviewEditModeAdapter* GetEditMode() const;

	private:
		PreviewEditModeAdapter* m_parentObject{};
		EditorObjectID m_editorId{};

		Bool m_selected{ false };
		Bool m_hovered{ false };
	};

	using PreviewObjectPtr = THandle< PreviewObject >;

	///
	///
	///

	// FINAL editable proxy for Preview object
	class PreviewObjectProxy final : public IEditableProxy, public red::NonCopyable
	{
	public:
		PreviewObjectProxy( const THandle< PreviewObject >& object, const ObjectHandler& objectHandler );

		virtual Bool IsReadOnly() const override final;
		virtual EditorObjectID GetEditorObjectID() const override final;
		virtual EditorObjectIDPath GetEditorObjectIDPath() const override final;
		virtual bool RestoreObject() const override final;
		virtual const ViewClass GetViewClass() const override final;
		virtual red::SharedPtr<IEditableView> CreateView( const ViewClass viewClass ) const override final;
		virtual void ToggleSelectionVisualization( const Bool isSelected, const red::SharedPtr< tools::SelectionState > & selection ) const override final;
		virtual void ToggleSelectionOutline( Bool isSelected ) const override final;
		virtual Transform GetWorldTransform() const override final;
		virtual Bool SetWorldTransform( const Transform& localToWorld, Bool preview = false ) override final;
		virtual Bool SupportsGizmoTransformation() const override final;
		virtual THandle< ISerializable > GetSerializable() const override final;
		virtual red::SharedPtr< IEditableProxy > GetParentEditableProxy() const override final;

	private:
		// get handle to the actual helper object point
		PreviewObjectPtr ResolveObject() const;

		const ObjectHandler& m_objectHandler; // for resolving
		mutable WeakHandle< PreviewObject > m_objectWeakRef; // the point we are editing

		EditorObjectID m_editorObjectID;	// global ID
	};

	///
	///
	///

	class GenericEditModeObjectPreview: public IObjectPreview
	{
	public:
		explicit GenericEditModeObjectPreview();
		virtual ~GenericEditModeObjectPreview() override;

		virtual void OnDestroy();
		virtual void OnMove( const Transform& newTransform );
		virtual void OnRender( const CRenderCamera& camera, rend::IDebugDrawer& dd );

	private:
		Transform m_worldSpaceTransform;
	};

}

