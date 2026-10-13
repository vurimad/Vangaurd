#pragma once

#include "editableProxy.h"
#include "objectResolver.h"

#include "../../../common/renderData/include/renderSelectable.h"

namespace tools
{
	class BugFlagsSystem;
	class BugFlag;

	class BACKEND_ENGINE_API BugFlagEditableProxy : public IEditableProxy
	{
	public:
		BugFlagEditableProxy( const THandle< BugFlag >& bugFlag );

		virtual Bool IsReadOnly() const override;
		virtual tools::EditorObjectID GetEditorObjectID() const override;
		virtual const tools::ViewClass GetViewClass() const override;
		virtual tools::EditableViewPtr CreateView( const ViewClass viewClass ) const override;
		virtual void ToggleSelectionVisualization( const Bool isSelected, const red::SharedPtr<SelectionState> & selection ) const override;
		virtual void ToggleSelectionOutline( Bool enableOutline ) const override;
		virtual Transform GetWorldTransform() const override;
		virtual Bool SetWorldTransform( const Transform& newTransform, Bool preview /*= false */ ) override;
		virtual Bool RestoreObject() const override;
		virtual THandle<ISerializable> GetSerializable() const override;

	private:
		THandle< tools::BugFlag > RestoreBugFlag() const;

		WeakHandle< BugFlag > m_weakBugFlag;
		EditorObjectID m_editorId;
	};

	//////////////////////////////////////////////////////////////////////////

	class BACKEND_ENGINE_API BugFlagsResolver : public IObjectResolver
	{
	public:
		BugFlagsResolver( BugFlagsSystem* bugFlagsSystem );

		virtual Bool BugFlagsResolver::IsResolvable( const EditorObjectIDPath& path, Bool forEditing /*= true */ ) const override;
		virtual Bool BugFlagsResolver::SupportsContentType( const CName& contentTypeName ) const override;
		virtual tools::EditableProxyPtr BugFlagsResolver::ResolveProxy( const EditorObjectIDPath& path, const ResolvingParams& params /*= ResolvingParams() */ ) const override;
		virtual THandle<ISerializable> BugFlagsResolver::ResolveObject( const EditorObjectIDPath& path, const ResolvingParams& params /*= ResolvingParams() */ ) const override;
		virtual Bool BugFlagsResolver::CanCreateObject( const ObjectCreationInfo& info, String* outReason /*= nullptr */ ) const override;
		virtual Bool BugFlagsResolver::CanDeleteObjects( const red::ArraySpan<const EditorObjectIDPath>& paths, Bool isCut, String* outFailReason /*= nullptr */ ) const override;
		virtual red::SharedPtr<tools::IObjectPreview> BugFlagsResolver::CreateObjectPreview( const ObjectCreationInfo& initInfo ) override;
		virtual red::SharedPtr<tools::IObjectPersistentCollection> BugFlagsResolver::CaptureObjects( const red::ArraySpan<const EditorObjectIDPath>& paths, const CapturingParams& params /*= CapturingParams() */ ) override;
		virtual red::SharedPtr<tools::IObjectPersistentCollection> BugFlagsResolver::Action_CreateObjects( const red::ArraySpan<const ObjectCreationInfo> creationInfos, const CreateObjectsOutParams& out ) override;
		virtual Bool BugFlagsResolver::Action_DeleteObjects( DeleteContext& deleteContext ) override;
		virtual Bool BugFlagsResolver::Action_RestoreObjects( const red::SharedPtr<IObjectPersistentCollection>& objects, red::DynArray<EditorObjectIDPath>& outActualCreatedPaths ) override;
		virtual Bool BugFlagsResolver::Action_PlaceObjects( const red::SharedPtr<IObjectPersistentCollection>& objects, const ObjectPlacementInfo& info, red::DynArray<EditorObjectIDPath>& outActualCreatedPaths ) override;
		virtual red::SharedPtr<tools::IObjectPersistentCollection> BugFlagsResolver::Action_ReplaceObjects( const red::ArraySpan<const EditorObjectIDPath>& objectPathsToReplace, const red::SharedPtr<IObjectPersistentCollection>& objectTemplate, red::DynArray<EditorObjectIDPath>& outActualCreatedPaths ) override;

	private:
		BugFlagsSystem* m_system;
	};
}