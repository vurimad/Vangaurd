/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "configBackend.h"
#include "objectResolver.h"
#include "editableProxy.h"
#include "editableCollectionCustomPropertyEditor.h"


namespace interop
{
	class IContext;
}

namespace tools
{
	class EditableCollection;

	class BACKEND_ENGINE_API GizmoModifierEditableProxy : public IEditableProxy
	{
		RTTI_DECLARE_TYPE( GizmoModifierEditableProxy );
		RED_BASE_CLASS( IEditableProxy );
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		explicit GizmoModifierEditableProxy( SerializableHandle object, SerializableHandle userContext, const rtti::AccessPath& path, const String& gizmoType );

		virtual Transform GetOffset() const;
		virtual Transform GetWorldTransform() const override;

		virtual void ToggleSelectionVisualization( const Bool isSelected, const red::SharedPtr< tools::SelectionState > & selection ) const override;

		static const CName s_objectKind;

	protected:
		virtual SerializableHandle GetSerializable() const override;

	private:
		// tools::IEditableProxy
		virtual Bool IsReadOnly() const override;
		virtual world::NodeScalingMode GetScalingMode() const override;
		virtual Bool RestoreObject() const override;
		virtual Bool SetWorldTransform( const Transform& localToWorld, Bool preview = false ) override;

		virtual tools::EditorObjectID GetEditorObjectID() const override;

		virtual const tools::ViewClass GetViewClass() const override;

		virtual red::SharedPtr< tools::IEditableView > CreateView( const tools::ViewClass viewClass ) const override;

		virtual Bool CanOverrideGizmoType() const override;
		virtual tools::GizmoType GetGizmoType() const override;

	protected:
		WeakHandle< ISerializable > m_object;
		SerializableHandle m_userContext;
		const rtti::AccessPath m_path;
		Transform m_transform;
		const String m_gizmoType;
		CName m_propType;
	};

	class BACKEND_ENGINE_API GizmoModifierCustomEditableProperty : public IEditableCustomProperty
	{
		RTTI_DECLARE_TYPE( GizmoModifierCustomEditableProperty );
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		virtual const Bool HandlesCustomType( const CName customTypeName, const prv::EditableCollectionRootItem* root ) const override;
		virtual CName GetControlType() const override;
		virtual Bool IsCompound() const override final;
		virtual Bool HasUnlistedChildren() const override;
		virtual interop::ObjectPtr GetUnlistedChild( interop::IContext& ctx, const String& name ) const override;
		virtual interop::ObjectPtr GetSelectFunctor( interop::IContext& ctx ) const;
	};

	class BACKEND_ENGINE_API GizmoModifier_ObjectResolver : public IObjectResolver
	{
		RTTI_DECLARE_TYPE( GizmoModifier_ObjectResolver );
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		explicit GizmoModifier_ObjectResolver( SerializableHandle userContext = nullptr );

		virtual Bool IsResolvable( const tools::EditorObjectIDPath& path, Bool forEditing = true ) const override;
		virtual Bool SupportsContentType( const CName& contentTypeName ) const override;

		virtual red::SharedPtr< GizmoModifierEditableProxy > CreateProxy( SerializableHandle object, SerializableHandle userContext, const rtti::AccessPath& path, const String& gizmoType ) const;

	private:
		virtual tools::EditableProxyPtr ResolveProxy( const tools::EditorObjectIDPath& path, const ResolvingParams& params = ResolvingParams() ) const override;
		virtual SerializableHandle ResolveObject( const tools::EditorObjectIDPath& path, const ResolvingParams& params = ResolvingParams() ) const override;

		virtual Bool CanCreateObject( const tools::ObjectCreationInfo& info, String* outReason = nullptr ) const override;
		virtual Bool CanDeleteObjects( const red::ArraySpan<const tools::EditorObjectIDPath>& paths, Bool isCut, String* outFailReason = nullptr ) const override;

		virtual red::SharedPtr<tools::IObjectPreview> CreateObjectPreview( const tools::ObjectCreationInfo& initInfo ) override;
		virtual red::SharedPtr< tools::IObjectPersistentCollection > CaptureObjects( const red::ArraySpan< const tools::EditorObjectIDPath >& paths, const CapturingParams& params = CapturingParams() ) override;

		virtual red::SharedPtr<tools::IObjectPersistentCollection> Action_CreateObjects( const red::ArraySpan<const tools::ObjectCreationInfo> creationInfos, const CreateObjectsOutParams& out ) override;
		virtual Bool Action_DeleteObjects( DeleteContext& deleteContext ) override;
		virtual Bool Action_RestoreObjects( const red::SharedPtr<tools::IObjectPersistentCollection>& objects, red::DynArray<tools::EditorObjectIDPath>& outActualCreatedPaths ) override;
		virtual Bool Action_PlaceObjects( const red::SharedPtr<tools::IObjectPersistentCollection>& objects, const tools::ObjectPlacementInfo& info, red::DynArray<tools::EditorObjectIDPath>& outActualCreatedPaths ) override;
		virtual red::SharedPtr<tools::IObjectPersistentCollection> Action_ReplaceObjects( const red::ArraySpan<const tools::EditorObjectIDPath>& objectPathsToReplace, const red::SharedPtr<tools::IObjectPersistentCollection>& objectTemplate, red::DynArray<tools::EditorObjectIDPath>& outActualCreatedPaths ) override;

		SerializableHandle m_userContext;
	};

} // tools