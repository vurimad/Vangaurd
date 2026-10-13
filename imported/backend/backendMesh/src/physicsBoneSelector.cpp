/* Copyright (c) 2017-2018 CD Projekt Red. All Rights Reserved.*/

#include "build.h"
#include "physicsBoneSelector.h"
#include "../../backendMesh/include/physicsEditorScene.h"
#include "../../backendMesh/include/physicsMesh.h"
#include "../../backendMesh/include/physicsBase.h"

#include "../../backendEngine/include/editableCollectionInterop.h"
#include "../../../common/redReflection/include/editableView.h"
#include "../../backendMesh/include/physicsActor.h"


RTTI_BEGIN_TYPE_IN_NAMESPACE( EditControlPhysicsBoneNameSelector, tools, prv );
RTTI_PARENT_TYPE( tools::IEditableCustomProperty );
RTTI_END_TYPE();


namespace tools
{
	namespace prv
	{
		const Bool EditControlPhysicsBoneNameSelector::HandlesCustomType( const CName customTypeName, const EditableCollectionRootItem* /*root*/ ) const
		{
			return customTypeName == RED_NAME_CONSTEXPR_NOREG( "PhysicsBoneNameSelector" );
		}

		CName EditControlPhysicsBoneNameSelector::GetControlType() const
		{
			return RED_NAME( red::String::Printf( "%hs:Restricted", ::GetTypeName<CName>().AsChar() ) );
		}

		Bool EditControlPhysicsBoneNameSelector::HasOptions() const
		{
			return true;
		}

		const red::DynArray<red::String>& EditControlPhysicsBoneNameSelector::GetOptions() const
		{
			if( m_options.Empty() )
			{
				RefreshOptions();
			}

			return m_options;
		}

		void EditControlPhysicsBoneNameSelector::RefreshOptions() const
		{
			m_options.Clear();

			red::DynArray< THandle< physics::PhysicsBase > > physObjects;

			EditableCollectionRootItem* root = GetRoot();
			if( const THandle<physics::PhysicsEditorScene> userContext = Cast< physics::PhysicsEditorScene >( root->GetNegotiatedView()->GetUserContext() ) )
			{
				physObjects = userContext->GetPhysicsObjectsOnScene(  );
			}

			if( !physObjects.Empty() )
			{
				for( THandle< physics::PhysicsBase > physObj : physObjects )
				{
					if( physObj->GetBaseType() == physics::EPBT_Mesh )
					{
						physics::PhysicsMeshPtr meshPtr = Cast< physics::PhysicsMesh >( physObj );
						physics::PhysicsBaseID meshId = meshPtr->GetPhysicsObjectID();
						red::DynArray< CName > meshBones = meshPtr->GetBoneNames();
						for( Int32 index : meshBones.Indices() )
						{
							CName boneName = meshBones[ index ];
							String idStr = String::Printf( "%s, Bone: %s", meshPtr->GetMeshName().AsChar(), boneName.AsChar() );
							m_options.PushBack( idStr );
							m_values.Insert( idStr, { meshId, boneName, index } );
						}
					}
				}					
			}
		}

		Bool EditControlPhysicsBoneNameSelector::SetValue( IRTTIContext& ctx, const rtti::AccessPath& path, const red::SharedPtr<rtti::ValueHolder>& newValue ) const
		{
			red::SharedPtr<rtti::ValueHolder> newStructureValue = newValue->CreateStructure();
			auto newMaping = m_values[ newValue->GetSingleValue() ];
			String idStr = String::Printf( "%d", newMaping.m_meshId );
			String boneStr = String::Printf( "%d", newMaping.m_boneIndex );

			newStructureValue->SetStructElement( RED_NAME_CONSTEXPR( "meshId" ), rtti::ValueHolder::CreateSingle( idStr.AsChar() ) );
			newStructureValue->SetStructElement( RED_NAME_CONSTEXPR( "boneName" ), rtti::ValueHolder::CreateSingle( newMaping.m_boneName.AsChar() ) );
			newStructureValue->SetStructElement( RED_NAME_CONSTEXPR( "boneIndex" ), rtti::ValueHolder::CreateSingle( boneStr.AsChar() ) );

			return IEditableCustomProperty::SetValue( ctx, path, newStructureValue );
		}

		Bool EditControlPhysicsBoneNameSelector::GetValue( IRTTIContext& ctx, const rtti::AccessPath& path, red::SharedPtr<rtti::ValueHolder>& outValue ) const
		{
			red::SharedPtr<rtti::ValueHolder> newStructureValue;
			GetRoot()->ReadValue( ctx, path, newStructureValue );

			physics::PhysicsMeshBoneMapping mapping;
			GetTypeObject<physics::PhysicsMeshBoneMapping>()->WriteValue( ctx, &mapping, rtti::AccessPath(), *newStructureValue );
			
			if( m_options.Empty() )
			{
				RefreshOptions();
			}

			for ( auto key : m_values.GetKeys() )
			{
				physics::PhysicsMeshBoneMapping& mappingToCompare = m_values[ key ];
				if( mappingToCompare.m_meshId == mapping.m_meshId && mappingToCompare.m_boneIndex == mapping.m_boneIndex )
				{
					outValue = newStructureValue->CreateSingle( key.AsChar() );
					return true;
				}
			}
			
			return IEditableCustomProperty::GetValue( ctx, path, outValue );
		}

		void EditControlPhysicsBoneNameSelector::Initialize( const CName customPropertyType, EditableCollection* collection, prv::EditableCollectionRootItem* root, const rtti::AccessPath& pathSoFar, const rtti::IType* type, const Bool isInlined, const Bool isReadOnly )
		{
			IEditableCustomProperty::Initialize( customPropertyType, collection, root, pathSoFar, type, isInlined, isReadOnly );
		}
	}
		
}