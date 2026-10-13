/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "previewEditModeAdapter.h"

#include "../../../common/renderDraw/include/fragmentBuilder.h"
#include "../../../common/world/include/worldAreaShapeNode.h"
#include "../../backendEngine/include/previewEditMode.h"
#include "../../backendEngine/include/resolvableSerializableEditorView.h"
#include "genericPreviewPanelSharedData.h"
#include "objectSerializablePersistentCollection.h"
#include "previewPanel.h"
#include "selectionHandler.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( PreviewObject, tools );
	RTTI_PARENT_TYPE( IHelperObjectSubobject );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( PreviewObjectHolder, tools );
	RTTI_PARENT_TYPE( ISerializable );
	RTTI_PROPERTY( m_editorID );
	RTTI_PROPERTY( m_data );
RTTI_END_TYPE();

namespace tools
{
	PreviewEditModeAdapter::PreviewEditModeAdapter( const tools::EditableProxyPtr& proxy )
		: IHelperObject( proxy )
	{
	}

	PreviewEditModeAdapter::~PreviewEditModeAdapter()
	{
		if ( m_activeCommand )
		{
			const red::DynArray< PreviewPanel* >& panels = GetPreviewPanelSharedData()->GetPreviewPanels();
			for ( PreviewPanel* panel : panels )
			{
				if ( panel->IsCommandActive( m_activeCommand.Get() ) )
				{
					panel->CancelCommand();
				}
			}
		}
	}

	const String& PreviewEditModeAdapter::GetName() const
	{
		const static String name( "GeometryShapeEditMode" );
		return name;
	}

	void PreviewEditModeAdapter::OnRender( const CRenderCamera& camera, rend::IDebugDrawer& debugDrawer ) const
	{
		const EditModeViewport viewport( camera, debugDrawer );
		rend::FragmentBuilder builder( debugDrawer );
		builder.EnableOverlay( true );

		if ( const EditableProxyPtr proxy = GetProxy() )
		{
			if ( const THandle< world::AreaShapeNode > areaShapeNode = Cast< world::AreaShapeNode >( proxy->GetSerializable() ) )
			{
				if ( const AreaShape* areaShape = areaShapeNode->GetAreaShape() )
				{
					const Matrix transform = proxy->GetWorldTransform().ToMatrix().SetScale33( proxy->GetScale() );

					builder.BindTransform( transform );
					builder.EnableTransparency( true );
					builder.EnableOverlay( false );
					builder.SetColor( 255, 0, 0, 255 );
					builder.AddSolidBox( Vector3(), 30 );
				}
			}
		}
	}

	red::DynArray< EditorObjectID > PreviewEditModeAdapter::GetSelection() const
	{
		red::DynArray< EditorObjectID > ids;
		for ( const auto& object : m_previewObjects )
		{
			if ( object->IsSelected() )
			{
				ids.PushBack( object->GetEditorObjectID() );
			}
		}

		return ids;
	}

	void PreviewEditModeAdapter::SelectObjects( const red::DynArray< EditorObjectID >& objects )
	{
		SelectionStatePtr newSelection = red::CreateSharedPtr<SelectionState>();
		for ( const auto& object : objects )
		{
			newSelection->Add( ResolveProxy( EditorObjectIDPath( object ) ) );
		}
		( (GenericPreviewPanelSharedData*) GetPreviewPanelSharedData() )->GetSelectionHandler().Action_ChangeSelection( newSelection );
	}

	void PreviewEditModeAdapter::SetCommand( const red::SharedPtr< IPreviewCommand >& command )
	{
		const red::DynArray< PreviewPanel* >& panels = GetPreviewPanelSharedData()->GetPreviewPanels();
		for ( PreviewPanel* panel : panels )
		{
			panel->SetActiveCommand( std::move( command ) );
		}

		m_activeCommand = command;
	}

	EditorObjectID PreviewEditModeAdapter::GetInsertLocation() const
	{
		return EditorObjectID::Build( 0, CName::NONE() );
	}

	void PreviewEditModeAdapter::QueryQuickPutObjectInfo( QuickPutObjectMode& outMode, ObjectCreationInfo& outCreationInfo ) const
	{
		if ( !GetInsertLocation().IsValid() )
		{
			outMode = QuickPutObjectMode::None;
			return;
		}

		outMode = QuickPutObjectMode::WithDepthmap;
		outCreationInfo = ObjectCreationInfo( "point" );
	}

	Bool PreviewEditModeAdapter::CalculateObjectPlacementLocation( const Vector3& rayOrigin, const Vector3& rayDirection, Vector3& outLocation ) const
	{
		return false;
	}

	//////////////////////////////////////////////////////////////////////////

	Bool PreviewEditModeAdapter::IsExclusive() const
	{
		return true;
	}


	Bool PreviewEditModeAdapter::IsResolvable( const EditorObjectIDPath& path, Bool forEditing ) const
	{
		if ( path == GetParentObjectPath() )
		{
			return true;
		}

		const EditorObjectID& id = path.GetFinalChildID();
		return SupportsContentType( id.GetKind() );
	}

	Bool PreviewEditModeAdapter::SupportsContentType( const CName& contentTypeName ) const
	{
		return false;
	}


	Bool PreviewEditModeAdapter::CanCreateObject( const ObjectCreationInfo& info, String* outReason ) const
	{
		return false;
	}

	Bool PreviewEditModeAdapter::CanDeleteObjects( const red::ArraySpan< const EditorObjectIDPath >& paths, Bool isCut, String* outFailReason ) const
	{
		if ( paths.Empty() )
		{
			return false;
		}

		for ( const EditorObjectIDPath& path : paths )
		{
			if ( path == GetParentObjectPath() )
			{
				continue;
			}

			if ( !SupportsContentType( path.GetFinalChildID().GetKind() ) )
			{
				return false;
			}
		}

		return true;
	}

	red::SharedPtr< IEditableProxy > PreviewEditModeAdapter::ResolveProxy( const EditorObjectIDPath& path, const ResolvingParams& params ) const
	{
		if ( path == GetParentObjectPath() )
		{
			return GetProxy();
		}

		THandle< IHelperObjectSubobject > object = Cast< IHelperObjectSubobject >( ResolveObject( path, params ) );
		if ( !object )
		{
			return red::SharedPtr< IEditableProxy >();
		}

		return object->CreateEditableProxy();
	}

	THandle< ISerializable > PreviewEditModeAdapter::ResolveObject( const EditorObjectIDPath& path, const ResolvingParams& params ) const
	{
		const EditorObjectID id = path.GetFinalChildID();

		for ( const PreviewObjectPtr& object : m_previewObjects )
		{
			if ( object->GetEditorObjectID() == id )
			{
				if ( CanBeResolved( id ) )
				{
					return object;
				}

				break;
			}
		}

		return THandle< ISerializable >();
	}

	red::SharedPtr< IObjectPreview > PreviewEditModeAdapter::CreateObjectPreview( const ObjectCreationInfo& initInfo )
	{
		if ( !CanCreateObject( initInfo ) )
		{
			return red::SharedPtr< IObjectPreview >();
		}

		EditorObjectID id = GetInsertLocation();
		if ( !id.IsValid() )
		{
			return red::SharedPtr< IObjectPreview >();
		}

		return red::CreateSharedPtr< GenericEditModeObjectPreview >();
	}
	
	Bool PreviewEditModeAdapter::CanBeResolved( const EditorObjectID& id ) const
	{
		return true;
	}

	void PreviewEditModeAdapter::CaptureDeleteData( const EditorObjectID& id, red::DynArray< PreviewObjectHolderPtr >& deleteData ) const
	{
		PreviewObjectHolderPtr dataHolder = CreateHandle< PreviewObjectHolder >();
		dataHolder->m_editorID = id;

		deleteData.PushBack( dataHolder );
	}

	red::SharedPtr< IObjectPersistentCollection > PreviewEditModeAdapter::CaptureObjects( const red::ArraySpan< const EditorObjectIDPath >& paths, const CapturingParams& params )
	{
		red::DynArray< PreviewObjectHolderPtr > objectsToCapture;

		for ( const EditorObjectIDPath& path : paths )
		{
			if ( path == GetParentObjectPath() )
			{
				continue;
			}

			RED_FATAL_ASSERT( path.GetDepth() == 1, "This object resolver expects single ID in the path" );
			const EditorObjectID id = path.GetFinalChildID();

			RED_FATAL_ASSERT( SupportsContentType( id.GetKind() ), "Trying to capture unsupported editor object" );
	
			if ( !params.m_captureAsClone )
			{
				CaptureDeleteData( id, objectsToCapture );
			}
		}

		red::DynArray< THandle< ISerializable > > objects;
		for ( const PreviewObjectHolderPtr& holder : objectsToCapture )
		{
			objects.PushBack( holder );
		}

		return ObjectSerializablePersistentCollection::CaptureObjects( objects );
	}

	Bool PreviewEditModeAdapter::Action_RestoreObjects( const red::SharedPtr< IObjectPersistentCollection >& objects, red::DynArray< EditorObjectIDPath >& outActualCreatedPaths )
	{
		// unpack objects from storage
		red::DynArray< IObjectPersistentCollection::ObjectInfo > unpackedSerializables;
		const auto* serializedStorage = static_cast< ObjectSerializablePersistentCollection* >( objects.Get() );
		if ( !serializedStorage->UnpackObjects( ClassID< PreviewObjectHolder >(), unpackedSerializables ) )
		{
			RED_LOG_ERROR( "Core: Could not unpack saved data holders" );
			return false;
		}

		// get point data
		red::DynArray< PreviewObjectHolderPtr > dataHolders;
		for ( const auto objectInfo : unpackedSerializables )
		{
			auto pointData = Cast< PreviewObjectHolder >( objectInfo.m_object );
			RED_ASSERT( pointData, "Something that is not a area shape point was unpacked from serialized container" );
			if ( pointData )
			{
				dataHolders.PushBack( pointData );
			}
		}

		RestoreObjects( dataHolders );

		// placing the nodes may have changed their IDs, output the true ones
		for ( const auto& object : dataHolders )
		{
			outActualCreatedPaths.PushBack( EditorObjectIDPath( object->GetEditorObjectID() ) );
		}

		return true;
	}

	red::SharedPtr< IObjectPersistentCollection > PreviewEditModeAdapter::Action_CreateObjects( const red::ArraySpan< const ObjectCreationInfo > creationInfos, const CreateObjectsOutParams& out )
	{
		if ( creationInfos.Size() != 1 )
		{
			return false;
		}

		const ObjectCreationInfo& info = creationInfos.Front();

		Vector3 localSpacePosition = GetInverseTransform().TransformPoint( info.m_transform.GetPosition3() );
		localSpacePosition /= GetScale();

		// create persistent object
		PreviewObjectHolderPtr pointData = CreateHandle< PreviewObjectHolder >();

		red::DynArray< THandle< ISerializable > > objectsToCapture;
		objectsToCapture.PushBack( pointData );

		// capture objects into the serialization based object collection
		return ObjectSerializablePersistentCollection::CaptureObjects( objectsToCapture );

		return nullptr;
	}

	Bool PreviewEditModeAdapter::Action_DeleteObjects( DeleteContext& deleteContext )
	{
		red::HashSet< EditorObjectID > objectsToRemove;
		for ( const EditorObjectIDPath& path : deleteContext.pathsToDelete )
		{
			if ( path == GetParentObjectPath() )
			{
				deleteContext.outObjectsToSelect.PushBack( path );
				continue;
			}

			const EditorObjectID id = path.GetFinalChildID();
			objectsToRemove.Insert( id );
		}

		if ( objectsToRemove.Empty() )
		{
			// nothing to do
			return false;
		}

		red::DynArray< EditorObjectID > elements;
		objectsToRemove.GetElements( elements );
		RemoveObjects( elements );

		return true;
	}

	Bool PreviewEditModeAdapter::Action_PlaceObjects( const red::SharedPtr< IObjectPersistentCollection >& objects, const ObjectPlacementInfo& info, red::DynArray< EditorObjectIDPath >& outActualCreatedPaths )
	{
		// required for paste

		return false;
	}

	red::SharedPtr< IObjectPersistentCollection > PreviewEditModeAdapter::Action_ReplaceObjects( const red::ArraySpan< const EditorObjectIDPath >& objectPathsToReplace, const red::SharedPtr< IObjectPersistentCollection >& objectTemplate, red::DynArray< EditorObjectIDPath >& outActualCreatedPaths )
	{
		return false;
	}

	void PreviewEditModeAdapter::RegisterShortcuts( ShortcutManager* manager, PreviewPanel* panel )
	{
	}

	void PreviewEditModeAdapter::RestoreObjects( const red::DynArray< PreviewObjectHolderPtr >& objects )
	{
	}
	
	void PreviewEditModeAdapter::RemoveObjects( const red::DynArray< EditorObjectID >& objects )
	{
	}

	///
	///
	///

	PreviewObjectHolder::PreviewObjectHolder() = default;

	PreviewObjectHolder::~PreviewObjectHolder() = default;

	EditorObjectID PreviewObjectHolder::GetEditorObjectID() const
	{
		return m_editorID;
	}

	///
	///
	///

	PreviewObjectProxy::PreviewObjectProxy( const THandle< PreviewObject >& object, const ObjectHandler& objectHandler )
		: m_objectWeakRef( object )
		, m_objectHandler( objectHandler )
		, m_editorObjectID( object->GetEditorObjectID() )
	{
	}

	PreviewObjectPtr PreviewObjectProxy::ResolveObject() const
	{
		PreviewObjectPtr object = m_objectWeakRef.ToHandle();
		if ( !object )
		{
			// weak handle invalid - resolve object
			object = Cast< PreviewObject >( m_objectHandler.ResolveObject( m_editorObjectID ) );
			m_objectWeakRef = object;
		}

		return object;
	}

	Bool PreviewObjectProxy::IsReadOnly() const
	{
		return false;
	}

	EditorObjectID PreviewObjectProxy::GetEditorObjectID() const
	{
		return m_editorObjectID;
	}

	EditorObjectIDPath PreviewObjectProxy::GetEditorObjectIDPath() const
	{
		return IEditableProxy::GetEditorObjectIDPath();
	}

	THandle< ISerializable > PreviewObjectProxy::GetSerializable() const
	{
		return ResolveObject();
	}

	bool PreviewObjectProxy::RestoreObject() const
	{
		ResolveObject();
		return true;
	}

	const ViewClass PreviewObjectProxy::GetViewClass() const
	{
		auto object = ResolveObject();
		if ( !object )
		{
			return CName::NONE();
		}

		return object->GetClass()->GetName();
	}

	red::SharedPtr<IEditableView> PreviewObjectProxy::CreateView( const ViewClass viewClass ) const
	{
		auto object = ResolveObject();
		if ( !object )
		{
			return red::SharedPtr< IEditableView >();
		}

		return red::CreateSharedPtr< ResolvableSerializableEditorView >( m_objectHandler, GetEditorObjectIDPath(), nullptr, IsReadOnly(), object->GetClass() );
	}

	void PreviewObjectProxy::ToggleSelectionVisualization( const Bool isSelected, const red::SharedPtr< tools::SelectionState > & selection ) const
	{
		auto object = ResolveObject();
		if ( object )
		{
			object->SetSelected( isSelected );
			object->SetHovered( false );
		}
	}

	void PreviewObjectProxy::ToggleSelectionOutline( Bool isSelected ) const
	{
		auto object = ResolveObject();
		if ( object )
		{
			object->SetHovered( isSelected );
		}
	}

	Transform PreviewObjectProxy::GetWorldTransform() const
	{
		auto object = ResolveObject();
		if ( !object )
		{
			return Transform::IDENTITY();
		}

		return object->GetWorldTransform();
	}

	Bool PreviewObjectProxy::SetWorldTransform( const Transform& localToWorld, Bool preview )
	{
		auto object = ResolveObject();
		if ( !object )
		{
			return false;
		}

		if ( object->SetWorldTransform( localToWorld, preview ) )
		{
			return true;
		}

		return false;
	}

	Bool PreviewObjectProxy::SupportsGizmoTransformation() const
	{
		if ( ResolveObject() )
		{
			return true;
		}

		return false;
	}

	red::SharedPtr< IEditableProxy > PreviewObjectProxy::GetParentEditableProxy() const
	{
		auto object = ResolveObject();
		if ( object )
		{
			return object->GetParentEditableProxy();
		}

		return nullptr;
	}

	//////////////////////////////////////////////////////////////////////////

	PreviewObject::PreviewObject() = default;

	PreviewObject::PreviewObject( PreviewEditModeAdapter* mode, EditorObjectID editorId )
		: m_editorId( editorId )
		, m_parentObject( mode )
	{
	}

	PreviewObject::~PreviewObject() = default;

	PreviewEditModeAdapter* PreviewObject::GetEditMode() const
	{
		return m_parentObject;
	}

	EditableProxyPtr PreviewObject::CreateEditableProxy() const
	{
		return EditableProxyPtr( RED_NEW( PreviewObjectProxy )( HandleFromThis< PreviewObject >(), m_parentObject->GetObjectHandler() ) );
	}

	EditorObjectID PreviewObject::GetEditorObjectID() const
	{
		return m_editorId;
	}

	tools::EditorObjectIDPath PreviewObject::Selectable_CreateEditorId() const
	{
		return tools::EditorObjectIDPath( GetEditorObjectID() );
	}

	EditableProxyPtr PreviewObject::GetParentEditableProxy() const
	{
		return m_parentObject->GetProxy();
	}

	Transform PreviewObject::GetWorldTransform() const
	{
		const Transform& parentToWorld = m_parentObject->GetTransform();
		const Vector3& scale = m_parentObject->GetScale();
		const Vector3 transformedPoint = parentToWorld.TransformPoint( GetPosition() * scale );

		return Transform( transformedPoint, parentToWorld.GetOrientation() );
	}

	Bool PreviewObject::SetWorldTransform( const Transform& localToWorld, Bool preview )
	{
		const Transform worldToParent = m_parentObject->GetInverseTransform();
		const Vector3& scale = m_parentObject->GetScale();
		const Vector3 transformedPoint = worldToParent.TransformPoint( localToWorld.GetPosition3() );

		return Move( transformedPoint / scale ) ;
	}

	Color PreviewObject::GetColor( Bool overlay ) const
	{
		Color color = GetEditMode()->GetColor( IHelperObject::ColorMode::Normal );
		if ( IsSelected() )
		{
			color = GetEditMode()->GetColor( IHelperObject::ColorMode::Selected );
		}
		else if ( IsHovered() )
		{
			color = GetEditMode()->GetColor( IHelperObject::ColorMode::Hover );
		}

		if ( overlay )
		{
			color.A /= 2u;
		}
		return color;
	}

	Vector3 PreviewObject::GetPosition() const
	{
		return Vector3();
	}

	Bool PreviewObject::Move( const Vector3& postion )
	{
		return false;
	}

	Bool PreviewObject::IsHovered() const
	{
		return m_hovered;
	}

	Bool PreviewObject::IsSelected() const
	{
		return m_selected;
	}

	void PreviewObject::SetHovered( Bool hovered )
	{ 
		m_hovered = hovered;
	}

	void PreviewObject::SetSelected( Bool selected )
	{ 
		m_selected = selected;
	}

	void PreviewObject::OnRender( const EditModeViewport& viewport, rend::FragmentBuilder& builder, Bool overlay ) const
	{
	}

	///
	///
	///

	GenericEditModeObjectPreview::GenericEditModeObjectPreview() = default;
	GenericEditModeObjectPreview::~GenericEditModeObjectPreview() = default;

	void GenericEditModeObjectPreview::OnDestroy()
	{
		// Do nothing
	}

	void GenericEditModeObjectPreview::OnMove( const Transform& newTransform )
	{
		m_worldSpaceTransform = newTransform;
	}

	void GenericEditModeObjectPreview::OnRender( const CRenderCamera& camera, rend::IDebugDrawer& dd )
	{
		const EditModeViewport viewport( camera, dd );
		rend::FragmentBuilder builder( dd );
		builder.SetColor( Color::Color( 30, 240, 30, 160 ) );
		builder.EnableOverlay( true );
		builder.EnableTransparency( true );

		// draw point preview
		const Vector3& newPointPosition = m_worldSpaceTransform.GetPosition3();

		const Float screenSpaceScale = viewport.CalcScreenSpaceScale( newPointPosition );
		const Float controlPointSize = math::Max( 0.0f, screenSpaceScale * 0.05f );
		builder.AddSolidBox( Box( newPointPosition, controlPointSize ) );
	}
}
