
#include "build.h"
#include "bugFlagsProxy.h"

#include "objectHandler.h"
#include "objectPersistentCollection.h"

#include "../../common/redReflection/include/editableView.h"

namespace tools
{
	BugFlagEditableProxy::BugFlagEditableProxy( const THandle< BugFlag >& bugFlag )
		: m_weakBugFlag( bugFlag )
		, m_editorId( bugFlag->GetEditorObjectID() )
	{
	}

	Bool BugFlagEditableProxy::IsReadOnly() const
	{
		if ( THandle< BugFlag > bugFlag = RestoreBugFlag() )
		{
			return bugFlag->IsReadOnly();
		}

		return true;
	}

	tools::EditorObjectID BugFlagEditableProxy::GetEditorObjectID() const
	{
		return m_editorId;
	}

	const tools::ViewClass BugFlagEditableProxy::GetViewClass() const
	{
		if ( THandle< BugFlag > bugFlag = RestoreBugFlag() )
		{
			return bugFlag->GetClass()->GetName();
		}

		return CName::NONE();
	}

	tools::EditableViewPtr BugFlagEditableProxy::CreateView( const ViewClass viewClass ) const
	{
		if ( THandle< BugFlag > bugFlag = RestoreBugFlag() )
		{
			return bugFlag->CreateView();
		}

		return nullptr;
	}

	void BugFlagEditableProxy::ToggleSelectionVisualization( const Bool isSelected, const red::SharedPtr<SelectionState> & selection ) const
	{
		if ( THandle< BugFlag > bugFlag = RestoreBugFlag() )
		{
			bugFlag->SetSelected( isSelected );
			bugFlag->SetHovered( false );
		}
	}

	void BugFlagEditableProxy::ToggleSelectionOutline( Bool enableOutline ) const
	{
		if ( THandle< BugFlag > bugFlag = RestoreBugFlag() )
		{
			bugFlag->SetHovered( enableOutline );
		}
	}

	Transform BugFlagEditableProxy::GetWorldTransform() const
	{
		if ( THandle< BugFlag > bugFlag = RestoreBugFlag() )
		{
			return Transform( bugFlag->GetPosition() );
		}

		return Transform();
	}

	Bool BugFlagEditableProxy::SetWorldTransform( const Transform& newTransform, Bool preview /*= false */ )
	{
		if ( THandle< BugFlag > bugFlag = RestoreBugFlag() )
		{
			bugFlag->SetPosition( newTransform.GetPosition3() );
		}

		return true;
	}

	Bool BugFlagEditableProxy::RestoreObject() const
	{
		return RestoreBugFlag() != nullptr;
	}

	THandle<ISerializable> BugFlagEditableProxy::GetSerializable() const
	{
		return RestoreBugFlag();
	}

	THandle< tools::BugFlag > BugFlagEditableProxy::RestoreBugFlag() const
	{
		return m_weakBugFlag.ToHandle();
	}

	//////////////////////////////////////////////////////////////////////////

	BugFlagsResolver::BugFlagsResolver( BugFlagsSystem* bugFlagsSystem )
		: m_system( bugFlagsSystem )
	{
	}

	Bool BugFlagsResolver::IsResolvable( const EditorObjectIDPath& path, Bool forEditing /*= true */ ) const
	{
		if ( path.GetDepth() != 1 )
		{
			return false;
		}

		return m_system->HasFlag( path.GetFirstChildID() );
	}

	Bool BugFlagsResolver::SupportsContentType( const CName& contentTypeName ) const
	{
		return contentTypeName == BugFlag::sm_kind;
	}

	tools::EditableProxyPtr BugFlagsResolver::ResolveProxy( const EditorObjectIDPath& path, const ResolvingParams& params /*= ResolvingParams() */ ) const
	{
		THandle< BugFlag > bugFlag = Cast< BugFlag >( ResolveObject( path, params ) );
		if ( bugFlag )
		{
			return red::CreateSharedPtr< BugFlagEditableProxy >( bugFlag->HandleFromThis<BugFlag>() );
		}

		return EditableProxyPtr();
	}

	THandle<ISerializable> BugFlagsResolver::ResolveObject( const EditorObjectIDPath& path, const ResolvingParams& params /*= ResolvingParams() */ ) const
	{
		const EditorObjectID& id = path.GetFirstChildID();
		return m_system->GetFlag( id );
	}

	Bool BugFlagsResolver::CanCreateObject( const ObjectCreationInfo& info, String* outReason /*= nullptr */ ) const
	{
		return false;
	}

	Bool BugFlagsResolver::CanDeleteObjects( const red::ArraySpan<const EditorObjectIDPath>& paths, Bool isCut, String* outFailReason /*= nullptr */ ) const
	{
		return false;
	}

	red::SharedPtr<tools::IObjectPreview> BugFlagsResolver::CreateObjectPreview( const ObjectCreationInfo& initInfo )
	{
		return red::SharedPtr<tools::IObjectPreview>();
	}

	red::SharedPtr<tools::IObjectPersistentCollection> BugFlagsResolver::CaptureObjects( const red::ArraySpan<const EditorObjectIDPath>& paths, const CapturingParams& params /*= CapturingParams() */ )
	{
		return red::SharedPtr<tools::IObjectPersistentCollection>();
	}

	red::SharedPtr<tools::IObjectPersistentCollection> BugFlagsResolver::Action_CreateObjects( const red::ArraySpan<const ObjectCreationInfo> creationInfos, const CreateObjectsOutParams& out )
	{
		return red::SharedPtr<tools::IObjectPersistentCollection>();
	}

	Bool BugFlagsResolver::Action_DeleteObjects( DeleteContext& deleteContext )
	{
		return false;
	}

	Bool BugFlagsResolver::Action_RestoreObjects( const red::SharedPtr<IObjectPersistentCollection>& objects, red::DynArray<EditorObjectIDPath>& outActualCreatedPaths )
	{
		return false;
	}

	Bool BugFlagsResolver::Action_PlaceObjects( const red::SharedPtr<IObjectPersistentCollection>& objects, const ObjectPlacementInfo& info, red::DynArray<EditorObjectIDPath>& outActualCreatedPaths )
	{
		return false;
	}

	red::SharedPtr<tools::IObjectPersistentCollection> BugFlagsResolver::Action_ReplaceObjects( const red::ArraySpan<const EditorObjectIDPath>& objectPathsToReplace, const red::SharedPtr<IObjectPersistentCollection>& objectTemplate, red::DynArray<EditorObjectIDPath>& outActualCreatedPaths )
	{
		return red::SharedPtr<tools::IObjectPersistentCollection>();
	}
}