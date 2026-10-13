/**
* Copyright (c) 2013-2020 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "objectHandler.h"

#include "objectClipboard.h"
#include "objectActionCreateObject.h"
#include "objectActionPasteObjects.h"
#include "objectActionReplaceObjects.h"
#include "objectActionUngroupObjects.h"
#include "objectPersistentCollection.h"

#include "selectionIterator.h"
#include "selectionHandler.h"
#include "editActionHistory.h"
#include "resourceEditor.h"

#include "../../../common/core/include/message.h"
#include "../../../common/redReflection/include/resourceCollector.h"
#include "../../../common/redReflection/include/editableView.h"
#include "../../../common/world/include/worldNode.h"

namespace tools
{
	// Special structure for overriding ObjectCreationInfo properties from arguments value.
	// Speparated to keep ObjectCreationInfo simple.
	class BACKEND_ENGINE_API ObjectCreationInfo_InitData : public ISerializable
	{
		RTTI_DECLARE_TYPE( ObjectCreationInfo_InitData );

	public:
		ObjectCreationInfo_InitData() : m_scale( Vector3::ONES() ) {}

		Vector3 m_scale;
	};
}


RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( IObjectPreview, tools );
RTTI_END_TYPE();

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( ObjectCreationInfo_InitData, tools );
	RTTI_PARENT_TYPE( ISerializable );
	RTTI_PROPERTY( m_scale );
RTTI_END_TYPE();


namespace tools
{
	//-----

	const CName ObjectCreationInfo::c_specialArgumentName = RED_NAME_CONSTEXPR( "objectCreationInfo" );

	ObjectCreationInfo::ObjectCreationInfo( const String& arguments )
		: m_arguments( rtti::ValueHolder::Parse( arguments.AsChar() ) )
		, m_clone( false )
		, m_useCustomTarget( false )
		, m_transform( Transform::IDENTITY() )
		, m_scale( Vector3::ONES() )
	{
		ProcessSpecialArguments();
	}

	ObjectCreationInfo::ObjectCreationInfo( const red::SharedPtr< rtti::ValueHolder >& arguments )
		: m_arguments( arguments )
		, m_clone( false )
		, m_useCustomTarget( false )
		, m_transform( Transform::IDENTITY() )
		, m_scale( Vector3::ONES() )
	{
		ProcessSpecialArguments();
	}

	void ObjectCreationInfo::ProcessSpecialArguments()
	{
		if ( !m_arguments->IsArray() && !m_arguments->IsStructure() )
		{
			return;
		}

		for ( Uint32 i = 0; i < m_arguments->GetNumElements(); ++i )
		{
			const auto name = m_arguments->GetStructElementName( i );
			if ( name == c_specialArgumentName )                                  // Look for special element name.
			{
				const rtti::ValuePtr value = m_arguments->GetStructElement( i );
				THandle< ObjectCreationInfo_InitData > initData = CreateHandle< ObjectCreationInfo_InitData >();
				red::SharedPtr< tools::IEditableView > view = initData->CreateView();
				if ( view )
				{
					for ( Uint32 paramIndex = 0; paramIndex < value->GetNumElements(); ++paramIndex )
					{
						const auto paramName = value->GetStructElementName( paramIndex );
						const auto paramValue = value->GetStructElement( paramIndex );

						rtti::ValuePtr oldValue;
						view->GetValue( IRTTIContext::GetDefault(), rtti::AccessPath( paramName.AsChar() ), oldValue );

						Bool result = view->SetValue( IRTTIContext::GetDefault(), rtti::AccessPath( paramName.AsChar() ), oldValue, paramValue, false );
						RED_FATAL_ASSERT( result, "Unable to override ObjectCreationInfo properties from arguments" );
					}

					m_scale = initData->m_scale;                // For now this is the only property that can be overridden this way.
				}
				break;
			}
		}
	}

	//-----

	ObjectPlacementInfo::ObjectPlacementInfo()
		: m_useAbsoluteTransform( false )
		, m_preserveWorldSpaceLocation( false )
	{}

	//-----

	ObjectHandler::ObjectHandler( IEditActionHistory& actionHistory, IObjectClipboard* clipboard, ISelectionHandler* selectionHandler )
		: m_actionHistory( &actionHistory )
		, m_selectionHandler( selectionHandler )
		, m_clipboard( clipboard )
	{
		RED_FATAL_ASSERT( selectionHandler != nullptr, "Invalid initialization parameters" );
	}

	ObjectHandler::~ObjectHandler()
	{
		RED_FATAL_ASSERT( m_resolvers.Empty(), "There are still registered object resolvers. Please tidy up your code." );
	}

	void ObjectHandler::AttachObjectResolver( IObjectResolver* resolver )
	{
		RED_FATAL_ASSERT( resolver, "Trying to register invalid object resolver" );
		RED_FATAL_ASSERT( !m_resolvers.Exist( resolver ), "Object resolver already exists" );
		m_resolvers.InsertAt( 0, resolver ); // insert the resolver in the beginning so it has the highest priority
		resolver->AttachedToHandler( this );
	}

	void ObjectHandler::DetachObjectResolver( IObjectResolver* resolver )
	{
		RED_FATAL_ASSERT( resolver, "Invalid object resolver" );

		if ( resolver )
		{
			resolver->DetachedFromHandler( this );
		}

		RED_VERIFY( m_resolvers.Remove( resolver ).IsSuccessful() );
	}

	void ObjectHandler::SetupInterop( ObjectHandler* ptr, ::interop::utils::Object& op )
	{
		const Bool hasClipboardData = ptr->m_clipboard->HasData();
		op.RegisterValueRO<Bool>( "hasClipboardData", hasClipboardData );

		op.RegisterValue< red::DynArray<EditorObjectID> >( "selection",
			[ ptr ] ( ::interop::IContext& ctx, red::DynArray<EditorObjectID>& outValue )
		{
			ptr->GetSelectedObjectIDs( outValue );
		},
			[ ptr ] ( ::interop::IContext& ctx, const red::DynArray<EditorObjectID>& newValue )
		{
			ptr->SetSelectionFromObjectIDs( newValue );
		} );

		op.RegisterValue< red::DynArray<EditorObjectIDPath> >( "selectionPaths",
			[ ptr ] ( ::interop::IContext& ctx, red::DynArray<EditorObjectIDPath>& outValue )
		{
			ptr->GetSelectedObjectIDPaths( outValue );
		},
			[ ptr ] ( ::interop::IContext& ctx, const red::DynArray<EditorObjectIDPath>& inValue )
		{
			ptr->SetSelectionFromObjectIDPaths( inValue );
		} );

		op.RegisterFunctor<Bool>( "copy", [ ptr ] ( ::interop::IContext& ctx )
		{
			return ptr->CopyObjects();
		} );

		op.RegisterFunctor< Bool >( "clone", [ptr](::interop::IContext& ctx ) {
			return ptr->CloneObjects();
		} );

		op.RegisterFunctor<Bool>( "cut", [ ptr ] ( ::interop::IContext& ctx )
		{
			return ptr->CutObjects();
		} );

		op.RegisterFunctor<Bool, red::DynArray< EditorObjectIDPath >>( "delete", [ ptr ] ( ::interop::IContext& ctx, const red::DynArray< EditorObjectIDPath >& paths )
		{
			return ptr->DeleteObjects( paths );
		} );

		op.RegisterFunctor<Bool, Bool>( "deleteSelection", [ ptr ] ( ::interop::IContext& ctx, const Bool shiftModifier )
		{
			return ptr->DeleteSelection( shiftModifier );
		} );

		op.RegisterFunctor<void>( "replace", [ ptr ] ( ::interop::IContext& ctx )
		{
			const Bool hasClipboardData = ptr->m_clipboard->HasData();
			if ( !hasClipboardData )
			{
				RED_REPORT_WARNING( RED_LOCATION( ptr->m_actionHistory->GetResourceEditor()->GetPath() ), "Nothing in clipboard to replace objects with" );
			}
			else
			{
				if ( !ptr->ReplaceSelectedObjects() )
				{
					ctx.ThrowException( "Unable to replace selection" );
				}
			}
		} );

		op.RegisterFunctor<void, EditorObjectIDPath, String>( "selectSimilar", [ ptr ] ( ::interop::IContext& ctx, const EditorObjectIDPath& path, const String& settings )
		{
			if ( !ptr->SelectSimilarObjects( path, settings ) )
			{
				ctx.ThrowException( "Unable to select similar objects" );
			}
		} );

		op.RegisterFunctor<void, String>( "selectSimilarFromSelection", [ ptr ] ( ::interop::IContext& ctx, const String& settings )
		{
			red::DynArray< EditorObjectIDPath > paths;
			ptr->GetSelectedObjectIDPaths( paths );

			if ( paths.Empty() )
			{
				ctx.ThrowException( "Empty selection" );
				return;
			}

			if ( !ptr->SelectSimilarObjects( paths.Front(), settings ) )
			{
				ctx.ThrowException( "Unable to select similar objects" );
			}
		} );

		op.RegisterFunctor<red::DynArray<String>, red::DynArray< EditorObjectIDPath >>( "usedResources", [ ptr ] ( ::interop::IContext& ctx, const red::DynArray< EditorObjectIDPath >& paths )
		{
			red::DynArray<String> resourcePaths{ red::PoolBackend() };
			ptr->CollectUsedResources( paths, resourcePaths );
			return resourcePaths;
		} );

		op.RegisterFunctor<red::DynArray<String>>( "usedResourcesForSelection", [ ptr ] ( ::interop::IContext& ctx )
		{
			red::DynArray< EditorObjectIDPath > paths;
			ptr->GetSelectedObjectIDPaths( paths );

			red::DynArray<String> resourcePaths{ red::PoolBackend() };
			ptr->CollectUsedResources( paths, resourcePaths );
			return resourcePaths;
		} );

		op.RegisterFunctor<Bool>( "paste", [ ptr ] ( ::interop::IContext& ctx )
		{
			ObjectPlacementInfo placementInfo;
			if ( !ptr->PasteObjects( placementInfo ) )
			{
				ctx.ThrowException( "Unable to paste selection" );
				return false;
			}
			return true;

		} );

		op.RegisterFunctor<Bool>( "pasteSpecial", [ ptr ] ( ::interop::IContext& ctx )
		{
			ObjectPlacementInfo placementInfo;
			placementInfo.m_preserveWorldSpaceLocation = true;
			return ptr->PasteObjects( placementInfo );
		} );

		op.RegisterFunctor<void, Vector3, Bool>( "pasteAt", [ ptr ] ( ::interop::IContext& ctx, const Vector3& initialPosition, const Bool& selectPastedObjects )
		{
			const Bool hasClipboardData = ptr->m_clipboard->HasData();
			if ( !hasClipboardData )
			{
				RED_REPORT_WARNING( RED_LOCATION( ptr->m_actionHistory->GetResourceEditor()->GetPath() ), "Nothing in clipboard to replace objects with" );
			}
			else
			{
				ObjectPlacementInfo placementInfo;
				placementInfo.m_useAbsoluteTransform = true;
				placementInfo.m_transform = Transform( initialPosition );

				if ( !ptr->PasteObjects( placementInfo, selectPastedObjects ) )
				{
					ctx.ThrowException( "Unable to paste selection" );
				}
			}
		} );

		op.RegisterFunctor<void>( "selectAll", [ ptr ] ( ::interop::IContext& ctx )
		{
			ptr->SelectAll();
			return true;
		} );
	}

	Bool ObjectHandler::CreateObject( const ObjectCreationInfo& initInfo, Bool refreshSelection )
	{
		IObjectResolver* unifiedResolver = FindObjectResolver( initInfo );

		String reason;
		if ( unifiedResolver && unifiedResolver->CanCreateObject( initInfo, &reason ) )
		{
			red::DynArray<ObjectCreationInfo> initInfos{ red::PoolBackend() };
			initInfos.PushBack( initInfo );

			return m_actionHistory->Execute( red::CreateUniquePtr< Action_CreateObject >( *this, initInfos, "Create object", refreshSelection ) );
		}

		RED_REPORT_ERROR( RED_LOCATION( m_actionHistory->GetResourceEditor()->GetPath() ), "Cannot % object: %", RED_NAME_CONSTEXPR( "Create" ), reason );

		return false;
	}

	Bool ObjectHandler::CreateObjects( const red::DynArray<ObjectCreationInfo>& initInfos )
	{
		IObjectResolver* unifiedResolver = GetUnifiedResolver( initInfos );

		if( !unifiedResolver )
		{
			return false;
		}

		String reason;
		const Bool supportsAll = std::all_of( initInfos.Begin(), initInfos.End(), [unifiedResolver, &reason ] ( const ObjectCreationInfo& info )
		{
			return unifiedResolver->CanCreateObject( info, &reason );
		} );

		if ( !supportsAll )
		{
			RED_REPORT_ERROR( RED_LOCATION( m_actionHistory->GetResourceEditor()->GetPath() ), "Cannot % objects: %", RED_NAME_CONSTEXPR( "Create" ), reason );

			return false;
		}

		return m_actionHistory->Execute( red::CreateUniquePtr< Action_CreateObject >( *this, initInfos, "Create objects" ) );
	}

	Bool ObjectHandler::CanCreateObject( const ObjectCreationInfo& initInfo, String* reason )
	{
		IObjectResolver* objectResolver = FindObjectResolver( initInfo );

		if ( objectResolver && objectResolver->CanCreateObject( initInfo, reason ) )
		{
			return true;
		}

		return false;
	}

	ObjectPreviewPtr ObjectHandler::CreateObjectPreview( const ObjectCreationInfo& initInfo )
	{
		IObjectResolver* objectResolver = FindObjectResolver( initInfo );

		if ( !objectResolver )
		{
			return nullptr;
		}

		return objectResolver->CreateObjectPreview( initInfo );
	}

	Bool ObjectHandler::CreateObjectsPreview( const red::DynArray<ObjectCreationInfo>& initInfos, red::DynArray<ObjectPreviewPtr>& outPreviews )
	{
		IObjectResolver* unifiedResolver = GetUnifiedResolver( initInfos );

		if ( !unifiedResolver )
		{
			return false;
		}

		String reason;
		const Bool supportsAll = std::all_of( initInfos.Begin(), initInfos.End(), [unifiedResolver, &reason ] ( const ObjectCreationInfo& info )
		{
			return unifiedResolver->CanCreateObject( info, &reason );
		} );

		if ( !supportsAll )
		{
			RED_REPORT_ERROR( RED_LOCATION( m_actionHistory->GetResourceEditor()->GetPath() ), "Cannot % objects: %", RED_NAME_CONSTEXPR( "Create" ), reason );

			return false;
		}

		for ( const ObjectCreationInfo& info : initInfos )
		{
			if ( auto preview = unifiedResolver->CreateObjectPreview( info ) )
			{
				outPreviews.PushBack( preview );
			}
		}

		return !outPreviews.Empty();
	}

	IObjectResolver* ObjectHandler::GetUnifiedResolver( const red::ArraySpan< const EditorObjectID > ids ) const
	{
		if ( ids.Empty() )
		{
			return nullptr;
		}

		for ( IObjectResolver* resolver : m_resolvers )
		{
			const Bool isSupportingAll = std::all_of( ids.begin(), ids.end(), [ resolver ] ( const EditorObjectID& id )
			{
				return resolver->SupportsContentType( id.GetKind() );
			} );

			if ( isSupportingAll )
			{
				return resolver;
			}

			if ( resolver->IsExclusive() )
			{
				break;
			}
		}

		return nullptr;
	}

	IObjectResolver* ObjectHandler::GetUnifiedResolver( const red::ArraySpan< const EditorObjectIDPath > paths, Bool forEditing ) const
	{
		if ( paths.Empty() )
		{
			return nullptr;
		}

		for ( IObjectResolver* resolver : m_resolvers )
		{
			const Bool isSupportingAll = std::all_of( paths.begin(), paths.end(), [ resolver, forEditing] ( const EditorObjectIDPath& path )
			{
				return resolver->IsResolvable( path, forEditing );
			} );

			if ( isSupportingAll )
			{
				return resolver;
			}

			if ( resolver->IsExclusive() )
			{
				break;
			}
		}

		return nullptr;
	}

	IObjectResolver* ObjectHandler::GetUnifiedResolver( const SelectionStatePtr& selection, Bool forEditing ) const
	{
		if ( !selection || selection->IsEmpty() )
		{
			return nullptr;
		}

		for ( IObjectResolver* resolver : m_resolvers )
		{
			Bool isSupportingAll = true;

			for ( auto iter = selection->Iterate(); iter; ++iter )
			{
				if ( !resolver->IsResolvable( iter->GetEditorObjectIDPath(), forEditing ) )
				{
					isSupportingAll = false;
					break;
				}
			}

			if ( isSupportingAll )
			{
				return resolver;
			}

			if ( resolver->IsExclusive() )
			{
				break;
			}
		}

		return nullptr;
	}

	IObjectResolver* ObjectHandler::GetUnifiedResolver( const red::ArraySpan< const ObjectCreationInfo >& creationInfos ) const
	{
		for( IObjectResolver* resolver : m_resolvers )
		{
			const Bool isSupportingAll = std::all_of( creationInfos.begin(), creationInfos.end(),
				[resolver]( const ObjectCreationInfo& creationInfo )
			{
				return resolver->CanCreateObject( creationInfo );
			} );

			if( isSupportingAll )
			{
				return resolver;
			}

			if( resolver->IsExclusive() )
			{
				break;
			}
		}

		return nullptr;
	}

	IObjectResolver* ObjectHandler::GetUnifiedResolver( const red::SharedPtr< IObjectPersistentCollection >& objects ) const
	{
		if ( !objects )
		{
			return nullptr;
		}

		// unpack objects from the storage
		red::DynArray< IObjectPersistentCollection::ObjectInfo > unpackedSerializables{ red::PoolBackend() };
		if ( !objects->UnpackObjects( ClassID< ISerializable >(), unpackedSerializables ) )
		{
			RED_LOG_ERROR( "Core: There are no objects in the persistent collection" );
			return nullptr;
		}

		for ( IObjectResolver* resolver : m_resolvers )
		{
			const Bool isSupportingAll = std::all_of( unpackedSerializables.Begin(), unpackedSerializables.End(),
				[ this, resolver ] ( const IObjectPersistentCollection::ObjectInfo& info )
			{
				return resolver->SupportsContentType( info.m_id.GetKind() );
			} );

			if ( isSupportingAll )
			{
				return resolver;
			}

			if ( resolver->IsExclusive() )
			{
				break;
			}
		}

		return nullptr;
	}

	IObjectResolver* ObjectHandler::FindObjectResolver( const EditorObjectIDPath& path, Bool forEditing ) const
	{
		return GetUnifiedResolver( red::MakeArraySpan( &path, 1 ), forEditing );
	}

	IObjectResolver* ObjectHandler::FindObjectResolver( const ObjectCreationInfo& creationInfo ) const
	{
		return GetUnifiedResolver( red::MakeArraySpan( &creationInfo, 1 ) );
	}

	IObjectResolver* ObjectHandler::InternalFindObjectResolver( const rtti::ClassType* classType ) const
	{
		for( IObjectResolver* const resolver : m_resolvers )
		{
			if( resolver->GetClass()->IsA( classType ) )
			{
				return resolver;
			}

			if( resolver->IsExclusive() )
			{
				return nullptr;
			}
		}
		return nullptr;
	}

	Bool ObjectHandler::SelectAll()
	{
		// Select all objects of the first resolver that supports select all.
		// TODO: What should we do if we have multiple object resolvers that support SelectAll? (At this moment only PrefabObjectResolver implements IObjectResolver::CollextResolvableObjects 14/Jan/2019)
		for( IObjectResolver* objectResolver : m_resolvers )
		{
			red::DynArray<EditorObjectIDPath> paths{ red::PoolBackend() };
			objectResolver->CollectResolvableObjects( paths );

			if( paths.Empty() && !objectResolver->IsExclusive() )
			{
				continue;
			}

			if( objectResolver->IsExclusive() )
			{
				break;
			}

			// build new selection set
			SelectionStatePtr newSelection = SelectionState::EmptySelection();
			for( const EditorObjectIDPath& path : paths )
			{
				const EditableProxyPtr editableProxy = objectResolver->ResolveProxy( path );
				if( editableProxy )
				{
					newSelection->Add( editableProxy );
				}
			}

			return m_selectionHandler->HandleDirectSelection( newSelection, "Select All" );
		}

		return true;
	}

	Bool ObjectHandler::DeleteObjects( const red::DynArray< EditorObjectIDPath >& paths, const Bool isShiftModifier, Bool isCut )
	{
		if ( paths.Empty() )
		{
			RED_REPORT_INFO( RED_LOCATION( m_actionHistory->GetResourceEditor()->GetPath() ), "Nothing to %", RED_NAME_CONSTEXPR( "Delete" ) );

			return false;
		}

		IObjectResolver* unifiedResolver = GetUnifiedResolver( paths );

		if ( !unifiedResolver )
		{
			RED_REPORT_INFO( RED_LOCATION( m_actionHistory->GetResourceEditor()->GetPath() ), "Cannot % objects in this mode", RED_NAME_CONSTEXPR( "Delete" ) );

			return false;
		}

		String failReason;
		if ( !unifiedResolver->CanDeleteObjects( paths, isCut, &failReason ) )
		{
			RED_REPORT_ERROR( RED_LOCATION( m_actionHistory->GetResourceEditor()->GetPath() ), "Cannot % objects. %", RED_NAME_CONSTEXPR( "Delete" ), failReason );

			return false;
		}

		red::UniquePtr< IEditAction > deleteAction = unifiedResolver->CreateDeleteAction( *this, paths, isShiftModifier );

		// if the resolver reports pre-delete action, build a group action containing it
		red::UniquePtr< IEditAction > preDeleteAction = unifiedResolver->GetPreDeleteAction( paths );
		if ( preDeleteAction )
		{
			red::UniquePtr< EditGroupAction > groupAction = red::CreateUniquePtr< EditGroupAction >( "Delete objects" );
			groupAction->AddAction( std::move( preDeleteAction ) );
			groupAction->AddAction( std::move( deleteAction ) );
			deleteAction = std::move( groupAction );
		}

		return m_actionHistory->Execute( std::move( deleteAction ) );
	}

	Bool ObjectHandler::DeleteSelection( const Bool isShiftModifier, Bool isCut )
	{
		const SelectionStatePtr& selection = m_selectionHandler->GetSelection();
		if ( !selection || selection->IsEmpty() )
		{
			RED_REPORT_INFO( RED_LOCATION( m_actionHistory->GetResourceEditor()->GetPath() ), "Nothing to %", RED_NAME_CONSTEXPR( "Delete" ) );

			return false;
		}

		red::DynArray< EditorObjectIDPath > paths{ red::PoolBackend() };
		GetSelectedObjectIDPaths( paths );
		return DeleteObjects( paths, isShiftModifier, isCut );
	}

	Bool ObjectHandler::CopyObjects( Bool isCut )
	{
		// no clipboard to service this stuff :*(
		if ( !m_clipboard )
		{
			RED_LOG_ERROR( "[%hs] isCut=%hs. No clipboard to service copy/cut.", __FUNCTION__, isCut ? "true" : "false" );
			return false;
		}

		const SelectionStatePtr& selection = m_selectionHandler->GetSelection();
		if ( !selection || selection->IsEmpty() )
		{
			RED_REPORT_INFO( RED_LOCATION( m_actionHistory->GetResourceEditor()->GetPath() ), "Nothing to %", RED_NAME_CONSTEXPR( "Copy" ) );
			RED_LOG_ERROR( "[%hs] isCut=%hs. Nothing to copy/cut (no selection || selection empty).", __FUNCTION__, isCut ? "true" : "false" );
			return false;
		}

		red::DynArray< EditorObjectIDPath > paths{ red::PoolBackend() };
		selection->ListObjects( paths );

		return CopyObjects( paths, isCut );
	}

	void ReportOperation( const CName& operation, MessageSeverity severity, const AnsiChar* details, const res::ResourcePath& primaryResourcePath, const red::DynArray< EditorObjectIDPath >& paths )
	{
		Message message( severity, std::move( RED_LOCATION( primaryResourcePath ) ) );

		message << operation;

		switch ( severity )
		{
		case MessageSeverity::Success:
			message << "successful";
			break;

		default:
			message << "failed";
		}

		message << details;

		for ( const EditorObjectIDPath& path : paths )
		{
			if( path.IsValid() )
				message << path;
		}

		IMessageReporter::ReportSafe( message );
	}

	Bool ObjectHandler::CopyObjects( const red::DynArray< EditorObjectIDPath >& paths, Bool isCut )
	{
		const res::ResourcePath& editedResource = m_actionHistory->GetResourceEditor()->GetPath();

		auto* resolver = GetUnifiedResolver( paths, /* forEditing */ false );
		if ( !resolver )
		{
			ReportOperation( RED_NAME_CONSTEXPR( "Copy" ), MessageSeverity::Error, "Try with different objects set.", editedResource, paths );
			RED_LOG_ERROR( "[%hs] isCut=%hs. Try with different objects set.", __FUNCTION__, isCut ? "true" : "false" );
			return false;
		}

		// capture the state of the given objects
		IObjectResolver::CapturingParams params;
		params.m_captureAsCut = isCut;
		auto state = resolver->CaptureObjects( paths, params );
		if ( !state )
		{
			ReportOperation( RED_NAME_CONSTEXPR( "Copy" ), MessageSeverity::Error, "Failed to capture objects.", editedResource, paths );
			RED_LOG_ERROR( "[%hs] isCut=%hs. Failed to capture objects.", __FUNCTION__, isCut ? "true" : "false" );
			return false;
		}

		// push state into clipboard
		if ( !m_clipboard->StoreData( state ) )
		{
			ReportOperation( RED_NAME_CONSTEXPR( "Copy" ), MessageSeverity::Error, "Unable to store data in the clipboard.", editedResource, paths );
			RED_LOG_ERROR( "[%hs] isCut=%hs. Unable to store data in the clipboard.", __FUNCTION__, isCut ? "true" : "false" );
			return false;
		}

		ReportOperation( RED_NAME_CONSTEXPR( "Copy" ), MessageSeverity::Success, "", editedResource, paths );
		return true;
	}

	Bool ObjectHandler::CutObjects()
	{
		// put the objects we are going to delete in the clipboard
		constexpr Bool isCut = true;
		if ( !CopyObjects( isCut ) )
		{
			RED_REPORT_ERROR( RED_LOCATION( m_actionHistory->GetResourceEditor()->GetPath() ), "Failed to % because % sub-step failed", RED_NAME_CONSTEXPR( "Cut" ), RED_NAME_CONSTEXPR( "Copy" ) );

			return false;
		}
		else
		{
			// get rid of the objects
			if ( !DeleteSelection( false, isCut ) )
			{
				RED_REPORT_ERROR( RED_LOCATION( m_actionHistory->GetResourceEditor()->GetPath() ), "Failed to % because % sub-step failed", RED_NAME_CONSTEXPR( "Cut" ), RED_NAME_CONSTEXPR( "Delete" ) );

				return false;
			}
		}

		RED_REPORT_SUCCESS( RED_LOCATION( m_actionHistory->GetResourceEditor()->GetPath() ), "% successful", RED_NAME_CONSTEXPR( "Cut" ) );
		return true;
	}

	Bool ObjectHandler::CloneObjects()
	{
		// no clipboard to service this stuff :*(
		if( !m_clipboard )
			return false;

		const SelectionStatePtr& selection = m_selectionHandler->GetSelection();
		if( !selection || selection->IsEmpty() )
		{
			RED_REPORT_INFO( RED_LOCATION( m_actionHistory->GetResourceEditor()->GetPath() ), "Nothing to %", RED_NAME_CONSTEXPR( "Clone" ) );

			return false;
		}

		red::DynArray< EditorObjectIDPath > paths{red::PoolBackend()};
		selection->ListObjects( paths );

		const res::ResourcePath& editedResource = m_actionHistory->GetResourceEditor()->GetPath();

		auto* resolver = GetUnifiedResolver( paths, /* forEditing */ false );
		if( !resolver )
		{
			ReportOperation( RED_NAME_CONSTEXPR( "Clone" ), MessageSeverity::Error, "Try with different objects set.", editedResource, paths );
			return false;
		}

		// capture the state of the given objects
		IObjectResolver::CapturingParams params;
		params.m_captureAsClone = true;
		auto state = resolver->CaptureObjects( paths, params );
		if( !state )
		{
			ReportOperation( RED_NAME_CONSTEXPR( "Clone" ), MessageSeverity::Error, "Failed to capture objects.", editedResource, paths );
			return false;
		}

		// push state into clipboard
		if( !m_clipboard->StoreData( state ) )
		{
			ReportOperation( RED_NAME_CONSTEXPR( "Clone" ), MessageSeverity::Error, "Unable to store data in the clipboard.", editedResource, paths );
			return false;
		}

		ReportOperation( RED_NAME_CONSTEXPR( "Clone" ), MessageSeverity::Success, "", editedResource, paths );
		return true;
	}

	void ObjectHandler::ClearClipboard()
	{
		if ( m_clipboard->HasData() )
		{
			m_clipboard->StoreData( red::SharedPtr< IObjectPersistentCollection >() );
		}
	}

	Bool ObjectHandler::PasteObjects()
	{
		const Bool hasClipboardData = m_clipboard->HasData();
		if ( !hasClipboardData )
		{
			return false;
		}
		else
		{
			ObjectPlacementInfo placementInfo;
			if ( !PasteObjects( placementInfo ) )
			{
				return false;
			}
		}
		return true;
	}

	Bool ObjectHandler::UngroupObjects( const red::DynArray< EditorObjectIDPath >& paths, const red::DynArray< Uint64 >& groupIds )
	{
		auto* resolver = GetUnifiedResolver( paths, true );
		if ( !resolver )
		{
			RED_REPORT_INFO( RED_LOCATION( m_actionHistory->GetResourceEditor()->GetPath() ), "Cannot % these objects, please try with a different set of objects", RED_NAME_CONSTEXPR( "Ungroup" ) );

			return false;
		}

		String reason;
		if ( !resolver->CanUngroupObjects( paths, &reason ) )
		{
			RED_REPORT_INFO( RED_LOCATION( m_actionHistory->GetResourceEditor()->GetPath() ), "Cannot % these objects, please try with a different set of objects, reason: ", RED_NAME_CONSTEXPR( "Ungroup" ), reason.AsChar() );

			return false;
		}

		auto action = red::CreateUniquePtr< Action_UngroupObjects >( *this, paths, groupIds );
		return m_actionHistory->Execute( std::move( action ) );
	}

	Bool ObjectHandler::MoveObjects( const red::DynArray<EditorObjectIDPath>& paths, const ObjectPlacementInfo& placementInfo )
	{
		auto* resolver = GetUnifiedResolver( paths, true );
		if ( !resolver )
		{
			RED_REPORT_INFO( RED_LOCATION( m_actionHistory->GetResourceEditor()->GetPath() ), "Cannot % these objects, please try with a different set of objects", RED_NAME_CONSTEXPR( "Move" ) );

			return false;
		}

		// capture the state of the given objects
		auto state = resolver->CaptureObjects( paths );
		if ( !state )
		{
			RED_REPORT_INFO( RED_LOCATION( m_actionHistory->GetResourceEditor()->GetPath() ), "Cannot % these objects, capture failed", RED_NAME_CONSTEXPR( "Move" ) );

			return false;
		}

		// TODO this will paste object into active context only
		auto action = red::CreateUniquePtr< Action_PasteObjects >( *this, state, placementInfo, true );
		return m_actionHistory->Execute( std::move( action ) );
	}

	Bool ObjectHandler::PasteObjects( const ObjectPlacementInfo& placementInfo, const Bool selectPastedObjects/* = true*/ )
	{
		// get object from clipboard
		auto state = m_clipboard->GetData();
		if ( !state )
		{
			RED_REPORT_INFO( RED_LOCATION( m_actionHistory->GetResourceEditor()->GetPath() ), "Nothing to % - clipboard is empty!", RED_NAME_CONSTEXPR( "Paste" ) );

			return true; // no objects in clipboard
		}

		// unpack objects in the clipboard for inspection
		red::DynArray< IObjectPersistentCollection::ObjectInfo > objectInfos{ red::PoolBackend() };
		state->UnpackObjects( ClassID<ISerializable>(),  objectInfos );
		if ( objectInfos.Empty() )
		{
			RED_REPORT_INFO( RED_LOCATION( m_actionHistory->GetResourceEditor()->GetPath() ), "Nothing to % - clipboard does not contain any applicable objects", RED_NAME_CONSTEXPR( "Paste" ) );

			return true;
		}

		// find appropriate object resolver to paste
		auto* resolver = GetUnifiedResolver( state );
		if( !resolver )
		{
			RED_REPORT_ERROR( RED_LOCATION( m_actionHistory->GetResourceEditor()->GetPath() ), "% failed", RED_NAME_CONSTEXPR( "Paste" ) );

			return false;
		}

		auto action = red::CreateUniquePtr< Action_PasteObjects >( *this, state, placementInfo, selectPastedObjects );
		auto* typedAction = action.Get(); // for use here

		// create the objects via the undoable action
		Bool success = m_actionHistory->Execute( std::move( action ) );

		if ( success )
		{
			RED_REPORT_SUCCESS( RED_LOCATION( m_actionHistory->GetResourceEditor()->GetPath() ), "% successful for % items", RED_NAME_CONSTEXPR( "Paste" ), objectInfos.Size() );
		}
		else
		{
			RED_REPORT_ERROR( RED_LOCATION( m_actionHistory->GetResourceEditor()->GetPath() ), "% failed", RED_NAME_CONSTEXPR( "Paste" ) );
		}

		return success;
	}

	Bool ObjectHandler::SelectSimilarObjects( const EditorObjectIDPath& objectTemplate, const String& settings )
	{
		red::DynArray< EditorObjectIDPath > selectedObjectPaths{ red::PoolBackend() };
		selectedObjectPaths.PushBack( objectTemplate );

		// get the ONE resolver that will handle the pasting
		auto* resolver = GetUnifiedResolver( selectedObjectPaths );
		if (!resolver)
		{
			RED_LOG_WARNING("ObjectHandler: Unable to find one unified resolver for objects in the clipboard");
			return false;
		}

		// ask the resolve to get list of similar objects
		red::DynArray< EditorObjectIDPath > similarObjectPaths{ red::PoolBackend() };
		resolver->FindSimilarObjects( objectTemplate, settings, similarObjectPaths );

		// anything to select ?
		if ( similarObjectPaths.Size() <= 1 )
			return true;

		// build new selection set
		SelectionStatePtr newSelection = SelectionState::EmptySelection();
		for ( const EditorObjectIDPath& path : similarObjectPaths )
		{
			const auto editableProxy = resolver->ResolveProxy( path );
			if (editableProxy)
			{
				newSelection->Add(editableProxy);
			}
		}

		// create the objects via the undo action
		return m_selectionHandler->HandleDirectSelection(newSelection, "Select similar objects");
	}

	Bool ObjectHandler::ReplaceSelectedObjects( const Bool selectPastedObjects /*= true*/ )
	{
		// get object from clipboard
		auto state = m_clipboard->GetData();
		if (!state)
		{
			RED_LOG_WARNING("Core: No compatible data in the clipboard");
			return true; // no objects in clipboard
		}

		// get the objects in the current selection
		const auto& selection = m_selectionHandler->GetSelection();
		if (!selection)
		{
			RED_LOG_WARNING("ObjectHandler: Selection is required for the replace to work");
			return true;
		}

		// get object to replace (currently selected one)
		red::DynArray< EditorObjectIDPath > selectedObjectPaths{ red::PoolBackend() };
		for ( auto it = selection->Iterate(); it; ++it )
			selectedObjectPaths.PushBack( it->GetEditorObjectIDPath() );

		// unpack objects in the clipboard for inspection
		red::DynArray< IObjectPersistentCollection::ObjectInfo > objectInfos{ red::PoolBackend() };
		state->UnpackObjects(ClassID<ISerializable>(), objectInfos);
		if (objectInfos.Empty())
		{
			RED_LOG_WARNING("ObjectHandler: Clipboard is not empty but there are no objects");
			return true;
		}

		// get the ONE resolver that will handle the pasting
		auto* resolver = GetUnifiedResolver( selectedObjectPaths );
		if ( !resolver )
		{
			RED_REPORT_ERROR( RED_LOCATION( m_actionHistory->GetResourceEditor()->GetPath() ), "Cannot % objects. Try with a different set of objects", RED_NAME_CONSTEXPR( "Replace" ) );

			return false;
		}

		red::UniquePtr< IEditAction > action = red::CreateUniquePtr< Action_ReplaceObjects >( *this, selectedObjectPaths, state, selectPastedObjects );

		// if the resolver reports pre-delete action, build a group action containing it
		red::UniquePtr< IEditAction > preDeleteAction = resolver->GetPreDeleteAction( selectedObjectPaths );
		if ( preDeleteAction )
		{
			red::UniquePtr< EditGroupAction > groupAction = red::CreateUniquePtr< EditGroupAction >( "Delete objects" );
			groupAction->AddAction( std::move( preDeleteAction ) );
			groupAction->AddAction( std::move( action ) );
			action = std::move( groupAction );
		}

		// create the objects via the undoable action
		return m_actionHistory->Execute( std::move( action ) );
	}

	void ObjectHandler::GetSelectedObjectIDs( red::DynArray< EditorObjectID >& outIDs ) const
	{
		const auto& selection = m_selectionHandler->GetSelection();
		if ( selection )
		{
			for ( auto it = selection->Iterate(); it; ++it )
			{
				outIDs.PushBack( it->GetEditorObjectID() );
			}
		}
	}

	void ObjectHandler::GetSelectedObjectIDPaths(red::DynArray< EditorObjectIDPath >& outIDPaths) const
	{
		const auto& selection = m_selectionHandler->GetSelection();
		if (selection)
		{
			for (auto it = selection->Iterate(); it; ++it)
			{
				outIDPaths.PushBack( it->GetEditorObjectIDPath() );
			}
		}
	}

	EditorObjectIDPath ObjectHandler::GetActiveObjectIDPath() const
	{
		for( IObjectResolver* resolver : m_resolvers )
		{
			const EditorObjectIDPath path = resolver->GetActiveObjectIDPath();
			if( path.IsValid() )
			{
				return path;
			}

			if( resolver->IsExclusive() )
			{
				break;
			}
		}

		return EditorObjectIDPath{};
	}

	void ObjectHandler::SetSelectionFromObjectIDs(const red::DynArray< EditorObjectID >& ids )
	{
		SelectionStatePtr newSelection = SelectionState::EmptySelection();
		for (const auto& id : ids)
		{
			auto resolvedProxy = ResolveProxy(id);
			if (resolvedProxy)
			{
				newSelection->Add(resolvedProxy);
			}
			else
			{
				RED_LOG_ERROR("Failed to resolve '%hs' to valid object", id.ToString().AsChar());
			}
		}

		m_selectionHandler->HandleDirectSelection(newSelection, "External selection");
	}

	void ObjectHandler::SetSelectionFromObjectIDPaths( const red::DynArray< EditorObjectIDPath >& paths )
	{
		SelectionStatePtr newSelection = SelectionState::EmptySelection();
		for ( const EditorObjectIDPath& path : paths )
		{
			auto resolvedProxy = ResolveProxy( path );
			if ( resolvedProxy )
			{
				newSelection->Add( resolvedProxy );
			}
			else
			{
				RED_LOG_ERROR( "Failed to resolve '%hs' to valid object", path.ToString().AsChar() );
			}
		}

		m_selectionHandler->HandleDirectSelection( newSelection, "External selection" );
	}

	void ObjectHandler::CollectUsedResources( const red::DynArray< EditorObjectIDPath >& paths, red::DynArray<String>& outPaths ) const
	{
		ResourceCollector collector;

		for ( const auto& path : paths )
		{
			THandle< ISerializable > serializable = ResolveObject( path );
			if ( serializable && serializable->IsA<world::Node>() )
				collector.Run( serializable );
		}

		collector.GetCollectedPaths( outPaths );
	}

	// ---

	THandle< ISerializable > ObjectHandler::ResolveObject( const EditorObjectID& id ) const
	{
		return ResolveObject( EditorObjectIDPath( id ) );
	}

	EditableProxyPtr ObjectHandler::ResolveProxy( const EditorObjectID& id ) const
	{
		return ResolveProxy( EditorObjectIDPath( id ) );
	}

	THandle< ISerializable > ObjectHandler::ResolveObject( const EditorObjectIDPath& path, const IObjectResolver::ResolvingParams& params ) const
	{
		for ( IObjectResolver* resolver : m_resolvers )
		{
			THandle< ISerializable > object = resolver->ResolveObject( path );
			if ( object )
			{
				return object;
			}

			if ( params.m_isForSelection && resolver->IsExclusive() )
			{
				break;
			}
		}

		return THandle< ISerializable >();
	}

	EditableProxyPtr ObjectHandler::ResolveProxy( const EditorObjectIDPath& path, const IObjectResolver::ResolvingParams& params ) const
	{
		for ( IObjectResolver* resolver : m_resolvers )
		{
			if ( const EditableProxyPtr proxy = resolver->ResolveProxy( path, params ) )
			{
				return proxy;
			}

			if ( params.m_isForSelection && resolver->IsExclusive() )
			{
				break;
			}
		}

		return EditableProxyPtr();
	}

} // tools

