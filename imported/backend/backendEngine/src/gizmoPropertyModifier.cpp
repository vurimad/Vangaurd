/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "gizmoPropertyModifier.h"
#include "objectPersistentCollection.h"
#include "objectHandler.h"
#include "gizmoTypes.h"
#include "bugFlagsService.h"
#include "../../../common/redReflection/include/serializableMap.h"
#include "propertyPathHelpers.h"

namespace tools
{
	const CName GizmoModifierEditableProxy::s_objectKind = RED_NAME_CONSTEXPR( "GizmoModifierEditableProxy" );

	const Bool GizmoModifierCustomEditableProperty::HandlesCustomType( const CName customTypeName, const prv::EditableCollectionRootItem* root ) const
	{
		return customTypeName == RED_NAME_CONSTEXPR_NOREG( "gizmoModifierCustomEditableProperty" );
	}

	CName GizmoModifierCustomEditableProperty::GetControlType() const
	{
		return RED_NAME( String::Printf( "%hs:Gizmo", GetType()->GetName().AsChar() ).AsChar() );
	}

	Bool GizmoModifierCustomEditableProperty::IsCompound() const
	{
		return true;
	}

	Bool GizmoModifierCustomEditableProperty::HasUnlistedChildren() const
	{
		return true;
	}

	interop::ObjectPtr GizmoModifierCustomEditableProperty::GetUnlistedChild( interop::IContext& ctx, const String& name ) const
	{
		if ( name == "select" )
			return GetSelectFunctor( ctx );

		return IEditableCustomProperty::GetUnlistedChild( ctx, name );
	}

	interop::ObjectPtr GizmoModifierCustomEditableProperty::GetSelectFunctor( interop::IContext& ctx ) const
	{
		return interop::ObjectPtr( nullptr );
	}

	//---------------------------------------------------------------------

	GizmoModifier_ObjectResolver::GizmoModifier_ObjectResolver( SerializableHandle userContext )
		: m_userContext{ userContext }
	{}

	Bool GizmoModifier_ObjectResolver::IsResolvable( const EditorObjectIDPath& path, Bool forEditing ) const
	{
		if ( path.GetDepth() == 1 )
		{
			return SupportsContentType( path.GetFinalChildID().GetKind() );
		}

		return false;
	}

	Bool GizmoModifier_ObjectResolver::SupportsContentType( const CName& contentTypeName ) const
	{
		return contentTypeName.AsStringView().StartsWith( GizmoModifierEditableProxy::s_objectKind.AsChar() );
	}

	red::SharedPtr< GizmoModifierEditableProxy > GizmoModifier_ObjectResolver::CreateProxy( SerializableHandle object, SerializableHandle userContext, const rtti::AccessPath& path, const String& gizmoType ) const
	{
		return nullptr;
	}

	EditableProxyPtr GizmoModifier_ObjectResolver::ResolveProxy( const EditorObjectIDPath& path, const ResolvingParams& params ) const
	{
		if ( IsResolvable( path ) )
		{
			const SerializableID id{ path.GetFinalChildID().GetInternalID() };
			const String kindStr( path.GetFinalChildID().GetKind().AsChar() );

			const red::DynArray< String > parts = kindStr.Split( ":" );
			RED_ASSERT( parts.Size() == 4, "Wrong params count" );

			SerializableHandle object = nullptr;
			SerializableHandle userContext = nullptr;

			if ( GSerializableMap->FindSerializable( id, object ) )
			{
				GSerializableMap->FindSerializable( SerializableID( FromStringDirect< Uint64 >( parts[ 3 ] ) ), userContext );
				return CreateProxy( object, userContext, rtti::AccessPath( parts[ 1 ].AsChar() ), parts[ 2 ] );
			}
		}

		return nullptr;
	}

	SerializableHandle GizmoModifier_ObjectResolver::ResolveObject( const EditorObjectIDPath& path, const ResolvingParams& params ) const
	{
		return nullptr;
	}

	Bool GizmoModifier_ObjectResolver::CanCreateObject( const ObjectCreationInfo& info, String* outReason ) const
	{
		return false;
	}

	Bool GizmoModifier_ObjectResolver::CanDeleteObjects( const red::ArraySpan<const EditorObjectIDPath>& paths, Bool isCut, String* outFailReason ) const
	{
		return false;
	}

	red::SharedPtr<IObjectPreview> GizmoModifier_ObjectResolver::CreateObjectPreview( const ObjectCreationInfo& initInfo )
	{
		return nullptr;
	}

	red::SharedPtr< IObjectPersistentCollection > GizmoModifier_ObjectResolver::CaptureObjects( const red::ArraySpan< const EditorObjectIDPath >& paths, const CapturingParams& params )
	{
		return nullptr;
	}

	red::SharedPtr<IObjectPersistentCollection> GizmoModifier_ObjectResolver::Action_CreateObjects( const red::ArraySpan<const ObjectCreationInfo> creationInfos, const CreateObjectsOutParams& out )
	{
		return nullptr;
	}

	Bool GizmoModifier_ObjectResolver::Action_DeleteObjects( DeleteContext& deleteContext )
	{
		return false;
	}

	Bool GizmoModifier_ObjectResolver::Action_RestoreObjects( const red::SharedPtr<IObjectPersistentCollection>& objects, red::DynArray<EditorObjectIDPath>& outActualCreatedPaths )
	{
		return false;
	}

	Bool GizmoModifier_ObjectResolver::Action_PlaceObjects( const red::SharedPtr<IObjectPersistentCollection>& objects, const ObjectPlacementInfo& info, red::DynArray<EditorObjectIDPath>& outActualCreatedPaths )
	{
		return false;
	}

	red::SharedPtr<IObjectPersistentCollection> GizmoModifier_ObjectResolver::Action_ReplaceObjects( const red::ArraySpan<const EditorObjectIDPath>& objectPathsToReplace, const red::SharedPtr<IObjectPersistentCollection>& objectTemplate, red::DynArray<EditorObjectIDPath>& outActualCreatedPaths )
	{
		return nullptr;
	}

	Transform GizmoModifierEditableProxy::GetWorldTransform() const
	{
		Transform outTransform;

		if ( SerializableHandle object = GetSerializable() )
		{
			red::SharedPtr< IEditableView > editableView = object->CreateView();

			rtti::ValueHolderPtr outValue = nullptr;
			editableView->GetValue( IRTTIContext::GetDefault(), m_path, outValue );

			if ( m_propType == Vector3::GetTypeName() )
			{
				Vector3 outVec3;
				GetTypeObject< Vector3 >()->WriteValue( IRTTIContext::GetDefault(), &outVec3, rtti::AccessPath(), *outValue );

				Transform transform = outVec3;
				outTransform = transform * GetOffset();
			}
			else if ( m_propType == Vector4::GetTypeName() )
			{
				Vector4 outVec4;
				GetTypeObject< Vector4 >()->WriteValue( IRTTIContext::GetDefault(), &outVec4, rtti::AccessPath(), *outValue );

				Transform transform = outVec4;
				outTransform = transform * GetOffset();
			}
			else if ( m_propType == Quaternion::GetTypeName() )
			{
				Quaternion outQuaternion;
				GetTypeObject< Quaternion >()->WriteValue( IRTTIContext::GetDefault(), &outQuaternion, rtti::AccessPath(), *outValue );

				Transform transform = outQuaternion;
				outTransform = transform * GetOffset();
			}
			else if( m_propType == Transform::GetTypeName() )
			{
				Transform transform;
				GetTypeObject( transform )->WriteValue( IRTTIContext::GetDefault(), &transform, rtti::AccessPath(), *outValue );
				outTransform = transform * GetOffset();
			}
		}

		return outTransform;
	}

	Bool GizmoModifierEditableProxy::CanOverrideGizmoType() const
	{
		return m_propType != Transform::GetTypeName();	// If we are editing a transform, we can use default gizmo
	}

	GizmoType GizmoModifierEditableProxy::GetGizmoType() const
	{
		if ( m_gizmoType == "translation" )
			return GizmoType::Translation;
		else if ( m_gizmoType == "rotation" )
			return GizmoType::Rotation;

		return TBaseClass::GetGizmoType();
	}

	red::SharedPtr< IEditableView > GizmoModifierEditableProxy::CreateView( const ViewClass viewClass ) const
	{
		if ( auto object = GetSerializable() )
		{
			return object->CreateView( nullptr, IsReadOnly(), GetViewClass(), m_userContext );
		}

		return nullptr;
	}

	const ViewClass GizmoModifierEditableProxy::GetViewClass() const
	{
		if ( auto event = GetSerializable() )
		{
			return event->GetClass()->GetName();
		}

		return CName::NONE();
	}

	EditorObjectID GizmoModifierEditableProxy::GetEditorObjectID() const
	{
		return GetSerializable()->GetEditorObjectID();
	}

	SerializableHandle GizmoModifierEditableProxy::GetSerializable() const
	{
		return m_object.ToHandle();
	}

	Bool GizmoModifierEditableProxy::SetWorldTransform( const Transform& localToWorld, Bool preview )
	{
		if ( SerializableHandle object = GetSerializable() )
		{
			red::SharedPtr< IEditableView > editableView = object->CreateView();

			rtti::ValueHolderPtr oldValue = nullptr;
			editableView->GetValue( IRTTIContext::GetDefault(), m_path, oldValue );

			rtti::ValueHolderPtr newValue = nullptr;

			const Transform newTransform = localToWorld * GetOffset().Invert();

			if ( m_propType == Vector3::GetTypeName() )
			{
				Vector3 newPosition = newTransform.GetPosition3();
				GetTypeObject< Vector3 >()->ReadValue( IRTTIContext::GetDefault(), &newPosition, rtti::AccessPath(), newValue );
			}
			else if ( m_propType == Vector4::GetTypeName() )
			{
				Vector4 newPosition = newTransform.GetPosition();
				GetTypeObject< Vector4 >()->ReadValue( IRTTIContext::GetDefault(), &newPosition, rtti::AccessPath(), newValue );
			}
			else if ( m_propType == Quaternion::GetTypeName() )
			{
				Quaternion newOrientation = newTransform.GetOrientation();
				GetTypeObject< Quaternion >()->ReadValue( IRTTIContext::GetDefault(), &newOrientation, rtti::AccessPath(), newValue );
			}
			else if( m_propType == Transform::GetTypeName() )
			{
				GetTypeObject( newTransform )->ReadValue( IRTTIContext::GetDefault(), &newTransform, rtti::AccessPath(), newValue );
			}

			if ( editableView->SetValue( IRTTIContext::GetDefault(), m_path, oldValue, newValue ) )
			{
				object->NotifyPropertyChanged( m_path );
			}

			return true;
		}

		return false;
	}

	Bool GizmoModifierEditableProxy::RestoreObject() const
	{
		return true;
	}

	world::NodeScalingMode GizmoModifierEditableProxy::GetScalingMode() const
	{
		return world::NodeScalingMode::None;
	}

	Bool GizmoModifierEditableProxy::IsReadOnly() const
	{
		return false;
	}

	GizmoModifierEditableProxy::GizmoModifierEditableProxy( SerializableHandle object, SerializableHandle userContext, const rtti::AccessPath& path, const String& gizmoType )
		: m_object( object )
		, m_userContext( userContext )
		, m_path( path )
		, m_gizmoType( gizmoType )
	{
		propertyPathHelpers::ExtractProperty( *object, m_path, [this]( const void* obj, const rtti::IType* propertyType, const rtti::Property* property )
		{
			m_propType = propertyType->GetName();
		} );
	}

	Transform GizmoModifierEditableProxy::GetOffset() const
	{
		return Transform();
	}

	void GizmoModifierEditableProxy::ToggleSelectionVisualization( const Bool isSelected, const red::SharedPtr< tools::SelectionState > & selection ) const
	{
	}
} // tools

RTTI_BEGIN_TYPE_IN_NAMESPACE( GizmoModifierCustomEditableProperty, tools );
	RTTI_PARENT_TYPE( IEditableCustomProperty );
RTTI_END_TYPE();

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( GizmoModifier_ObjectResolver, tools );
	RTTI_PARENT_TYPE( IObjectResolver );
RTTI_END_TYPE();

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( GizmoModifierEditableProxy, tools );
	RTTI_PARENT_TYPE( IEditableProxy );
RTTI_END_TYPE();
