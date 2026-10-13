/* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.*/

#include "build.h"
#include "physicsObjectResolver.h"
#include "../../backendMesh/include/physicsEditorScene.h"
#include "../../backendMesh/include/physicsBase.h"
#include "physicsObjectConstructor.h"

#include "../../backendEngine/include/backendEnginePublic.h"
#include "../../backendEngine/include/editableProxy.h"
#include "../../backendEngine/include/objectHandler.h"
#include "../../backendEngine/include/selectionIterator.h"
#include "../../backendEngine/include/selectionHandler.h"
#include "../../backendEngine/include/objectSerializablePersistentCollection.h"

#include "../../../common/interop/include/interopPublic.h"
#include "../../../common/interop/include/interopUtilsManual.h"
#include "../../../common/engine/include/renderFrame.h"
#include "../../../common/renderDraw/include/fragmentBuilder.h"

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( PhysicsObjectResolver, tools );
RTTI_PARENT_TYPE( IObjectResolver );
RTTI_END_TYPE();

namespace tools
{
	PhysicsObjectResolver::PhysicsObjectResolver( physics::PhysicsEditorScene* physicsEditorInfo, SelectionHandler* selectionHandler, CName objectType )
		: m_physicsEditorScene( physicsEditorInfo )
		, m_selectionHandler( selectionHandler )
		, m_objectType( objectType )
	{
		// create content constructors
		red::DynArray< const rtti::ClassType* > constructorClasses;
		GetRttiSystem().EnumClasses( ClassID< IPhysicsObjectConstructor >(), constructorClasses );

		// create content constructors
		for ( const auto* constructorClass : constructorClasses )
		{
			auto* constructor = constructorClass->CreateObject< IPhysicsObjectConstructor >();
			if ( nullptr != constructor )
				m_constructors.PushBack( constructor );
		}
	}


	PhysicsObjectResolver::~PhysicsObjectResolver()
	{
	}


	red::SharedPtr< IObjectPersistentCollection > tools::PhysicsObjectResolver::Action_CreateObjects( const red::ArraySpan< const ObjectCreationInfo > creationInfos, const CreateObjectsOutParams& out )
	{
		// find constructor that will accept the data and use it to create the object
		red::DynArray <THandle< physics::PhysicsBase > > newPhysicsObjects;
		auto physicsEditorScene = GetPhysicsEditorScene();
		if ( !physicsEditorScene )
		{
			RED_LOG_ERROR( "Core: PhysicsEditorScene no longer available!" );
			return red::SharedPtr< IObjectPersistentCollection >();
		}


		for ( const ObjectCreationInfo& info : creationInfos )
		{
			for ( const auto& ptr : m_constructors )
			{
				if ( ptr->CanCreateObject( info ) )
				{
					red::DynArray< EditorObjectID > selectedIds;
					auto selection = m_selectionHandler->GetSelection();

					for ( auto it = selection->Iterate(); it; ++it )
					{
						auto proxyId = it->GetEditorObjectID();
						selectedIds.PushBack( proxyId );
					}
					if ( ptr->CreateObject( info, newPhysicsObjects, physicsEditorScene, selectedIds ) )
					{
						break;
					}
				}
			}

			// unable to create the physics object
			if ( !newPhysicsObjects.Size() )
			{
				RED_LOG_ERROR( "Core: No constructor serviced the new physics object request" );
				return red::SharedPtr< IObjectPersistentCollection >();
			}

			{
				red::DynArray< EditorObjectID > createdIDs;
				physicsEditorScene->PlacePhysicsObjects( newPhysicsObjects, createdIDs );
				for ( const EditorObjectID& id : createdIDs )
				{
					out.actualCreatedPaths.PushBack( EditorObjectIDPath( id ) );
				}
			}
		}

		out.selectAll = true;

		red::DynArray< THandle< ISerializable > > physicsObjectCaptureList;
		for ( auto& physOb : newPhysicsObjects )
		{
			physicsObjectCaptureList.PushBack( physOb );
		}
		return ObjectSerializablePersistentCollection::CaptureObjects( physicsObjectCaptureList );
	}

	Bool tools::PhysicsObjectResolver::Action_DeleteObjects( DeleteContext& context )
	{
		auto physicsEditorScene = GetPhysicsEditorScene();
		if( physicsEditorScene )
		{
			for ( const EditorObjectIDPath& path : context.pathsToDelete )
			{
				RED_FATAL_ASSERT( path.GetDepth() == 1, "This object resolver expects single ID in the path" );
				const EditorObjectID id = path.GetFinalChildID();

				physicsEditorScene->RemovePhysicsObject( id );
			}
			physicsEditorScene->NotifyOnRemoved();
			return true;
		}
		return false;
	}

	Bool tools::PhysicsObjectResolver::Action_PlaceObjects(const red::SharedPtr< IObjectPersistentCollection >& objects, const ObjectPlacementInfo& info, red::DynArray< EditorObjectIDPath >& outActualCreatedPaths)
	{
		// no objects or not valid objects
		if ( !objects )
		{
			RED_LOG_ERROR( "Core: No content to unpack objects from" );
			return false;
		}

		// unpack objects from storage
		red::DynArray< IObjectPersistentCollection::ObjectInfo > unpackedSerializables;

		const auto* serializedStorage = static_cast< ObjectSerializablePersistentCollection* >( objects.Get() );
		serializedStorage->UnpackObjects( ClassID< physics::PhysicsBase >(), unpackedSerializables );

		// get physics objects
		red::DynArray< physics::PhysicsBasePtr > physObjects;
		for ( const auto objectInfo : unpackedSerializables )
		{
			auto physObjectsPtr = Cast< physics::PhysicsBase >( objectInfo.m_object );
			RED_ASSERT( physObjectsPtr, "Something that is not a physics object unpacked from serialized container" );
			if ( physObjectsPtr )
			{
				physObjects.PushBack( physObjectsPtr );
			}
		}

		// nothing to place
		if ( physObjects.Empty() )
		{
			RED_LOG_ERROR( "Core: There are no physics objects that can be added" );
			return false;
		}
		auto physicsEditorScene = GetPhysicsEditorScene();
		if( physicsEditorScene )
		{
			red::DynArray< EditorObjectID > createdIDs;
			physicsEditorScene->PlacePhysicsObjects( physObjects, createdIDs );
			for ( const EditorObjectID& id : createdIDs )
			{
				outActualCreatedPaths.PushBack( EditorObjectIDPath( id ) );
			}

			return true;
		}
		return false;
	}

	red::SharedPtr< IObjectPersistentCollection > PhysicsObjectResolver::Action_ReplaceObjects(const red::ArraySpan< const EditorObjectIDPath >& objectPathsToReplace, const red::SharedPtr< IObjectPersistentCollection >& objectTemplate, red::DynArray< EditorObjectIDPath >& outActualCreatedPaths)
	{
		return nullptr;
	}

	Bool tools::PhysicsObjectResolver::Action_RestoreObjects(const red::SharedPtr< IObjectPersistentCollection >& objects, red::DynArray< EditorObjectIDPath >& outActualCreatedPaths)
	{
		// no objects or not valid objects
		if ( !objects )
		{
			RED_LOG_ERROR( "Core: No content to unpack objects from" );
			return false;
		}

		// unpack objects from storage
		red::DynArray< IObjectPersistentCollection::ObjectInfo > unpackedSerializables;

		const auto* serializedStorage = static_cast< ObjectSerializablePersistentCollection* >( objects.Get() );
		serializedStorage->UnpackObjects( ClassID< physics::PhysicsBase >(), unpackedSerializables );

		// get physics objects
		red::DynArray< physics::PhysicsBasePtr > physObjects;
		for ( const auto objectInfo : unpackedSerializables )
		{
			auto physObjectsPtr = Cast< physics::PhysicsBase >( objectInfo.m_object );
			RED_ASSERT( physObjectsPtr, "Something that is not a physics object unpacked from serialized container" );
			if ( physObjectsPtr )
			{
				physObjects.PushBack( physObjectsPtr );
			}
		}

		// nothing to restore
		if ( physObjects.Empty() )
		{
			RED_LOG_ERROR( "Core: There are no physics objects that can be added" );
			return false;
		}

		auto physicsEditorScene = GetPhysicsEditorScene();
		if( physicsEditorScene )
		{
			red::DynArray< EditorObjectID > createdIDs;
			physicsEditorScene->PlacePhysicsObjects( physObjects, createdIDs );
			for ( const EditorObjectID& id : createdIDs )
			{
				outActualCreatedPaths.PushBack( EditorObjectIDPath( id ) );
			}

			return true;
		}
		return false;
	}

	Bool PhysicsObjectResolver::Action_SnapObjects(const red::DynArray< EditorObjectID >& objectsID, Bool alternativeMode )
	{
		auto physicsEditorScene = GetPhysicsEditorScene();
		if( physicsEditorScene )
		{
			for( EditorObjectID id : objectsID )
			{
				physicsEditorScene->SnapPhysicsObject( id, alternativeMode );
			}
			return true;
		}
		return false;
	}

	Bool PhysicsObjectResolver::Action_SelectParent( const red::DynArray< EditorObjectID >& ids )
	{
		const Uint32 size = ids.Size();
		SelectionStatePtr state( RED_NEW( SelectionState )() );
		for( Uint32 i = 0; i < size; ++i )
		{
			state->Add( ResolveProxyParent( EditorObjectIDPath( ids[i] ) ) );
		}

		return m_selectionHandler->Action_ChangeSelection( state );
	}

	Bool PhysicsObjectResolver::Action_SelectObject( const red::DynArray< EditorObjectID >& ids )
	{
		const Uint32 size = ids.Size();
		SelectionStatePtr state( RED_NEW( SelectionState )( ) );
		for( Uint32 i = 0; i < size; ++i )
		{
			state->Add( ResolveProxy(EditorObjectIDPath( ids[ i ] ) ) );
		}
		return m_selectionHandler->Action_ChangeSelection( state );
	}

	Bool PhysicsObjectResolver::Action_RecomputeCOM( const red::DynArray< EditorObjectID >& ids )
	{
		auto physicsEditorScene = GetPhysicsEditorScene();
		if( physicsEditorScene )
		{
			physicsEditorScene->RecomputeCOM( ids );
			return true;
		}
		return false;
	}

	Bool PhysicsObjectResolver::Action_GenerateDefaultDestructionHierarchy(red::DynArray<Uint32>& hierarchy)
	{
		auto physicsEditorScene = GetPhysicsEditorScene();
		if( physicsEditorScene )
		{
			hierarchy = physicsEditorScene->GenerateDefaultDestructionHierarchy();
			return true;
		}
		return false;
	}

	Bool PhysicsObjectResolver::Action_SetDestructionHierarchy(const red::DynArray<Uint32>& hierarchy)
	{
		auto physicsEditorScene = GetPhysicsEditorScene();
		if( physicsEditorScene )
		{
			physicsEditorScene->SetDestructionHierarchy(hierarchy);
			return true;
		}
		return false;
	}

	physics::PhysicsEditorScene* PhysicsObjectResolver::GetPhysicsEditorScene() const
	{
		return m_physicsEditorScene;
	}

	Bool tools::PhysicsObjectResolver::CanCreateObject( const ObjectCreationInfo& info, String* outReason ) const
	{
		// Make sure we have a constructor that can handle the data type
		for ( const auto& ptr : m_constructors )
			if ( ptr->CanCreateObject( info ) )
				return true;

		return false;
	}

	Bool PhysicsObjectResolver::CanDeleteObjects( const red::ArraySpan< const EditorObjectIDPath >& paths, Bool isCut, String* outFailReason ) const
	{
		return true;
	}

	red::SharedPtr< IObjectPersistentCollection > tools::PhysicsObjectResolver::CaptureObjects( const red::ArraySpan< const EditorObjectIDPath >& paths, const CapturingParams& params )
	{
		red::DynArray< THandle< ISerializable > > physicsObjects = {red::PoolBackend() };
		for ( const EditorObjectIDPath& path : paths )
		{
			RED_FATAL_ASSERT( path.GetDepth() == 1, "This object resolver expects single ID in the path" );
			const EditorObjectID id = path.GetFinalChildID();

			const auto ptr = FindPhysicsObjectByGlobalID( id );
			RED_ASSERT( ptr, "Trouble finding object with given ID, copy/paste may not work" );
			if ( ptr )
			{
				RED_FATAL_ASSERT( ptr->GetEditorObjectID() == id, "The IDs don't match" );
				physicsObjects.PushBack( ptr );
				ptr->EvaluateSerializationDependencies( physicsObjects );
			}
		}

		// no objects to capture
		if ( physicsObjects.Empty() )
			return red::SharedPtr< IObjectPersistentCollection >();

		// remove dupes
		red::alg::RemoveDuplicates( physicsObjects );

		// capture objects into the serialization based object collection
		return ObjectSerializablePersistentCollection::CaptureObjects( physicsObjects );
	}

	namespace helper
	{
		class TemporaryPhysicsPreviewObject : public IObjectPreview
		{
		public:
			TemporaryPhysicsPreviewObject()
				: m_placementPos( 0,0,0 )
				, m_placementPosValid( false )
			{}

			virtual void OnDestroy() override
			{}

			virtual void OnMove( const Transform& newTransform ) override
			{
				m_placementPos = newTransform.GetPosition3();
				m_placementPosValid = true;
			}

			virtual void OnRender( const CRenderCamera& camera, rend::IDebugDrawer& dd ) override
			{
				if ( m_placementPosValid )
				{
					rend::FragmentBuilder builder( dd );
					builder.SetColor( Color::YELLOW() );
					builder.EnableOverlay( true );
					builder.AddWireBox( Box(m_placementPos, 1.0f) );
				}
			}

		private:
			Vector3	m_placementPos;
			Bool	m_placementPosValid;
		};
	}

	red::SharedPtr< IObjectPreview > tools::PhysicsObjectResolver::CreateObjectPreview(const ObjectCreationInfo& initInfo)
	{
		// is it even possible to create the object ?
		if ( !CanCreateObject( initInfo ) )
			return red::SharedPtr< IObjectPreview >();

		red::SharedPtr< IObjectPreview > ret( RED_NEW( helper::TemporaryPhysicsPreviewObject )() );
		return ret;
	}

	physics::PhysicsBasePtr PhysicsObjectResolver::FindPhysicsObjectByGlobalID(const EditorObjectID& globalId) const
	{
		auto physicsEditorScene = GetPhysicsEditorScene();
		if( physicsEditorScene )
		{
			return physicsEditorScene->FindByGlobalId( globalId );
		}
		return nullptr;
	}

	THandle< ISerializable > tools::PhysicsObjectResolver::ResolveObject( const EditorObjectIDPath& path, const ResolvingParams& params ) const
	{
		auto physicsEditorScene = GetPhysicsEditorScene();
		if( physicsEditorScene )
		{
			if ( path.GetDepth() == 1 )
			{
				return physicsEditorScene->FindByGlobalId( path.GetFinalChildID() );
			}
		}
		return nullptr;
	}

	red::SharedPtr< IEditableProxy > tools::PhysicsObjectResolver::ResolveProxy( const EditorObjectIDPath& path, const ResolvingParams& params ) const
	{
		if ( path.GetDepth() == 1 )
		{
			const physics::PhysicsBasePtr physicsObjectInfo = FindPhysicsObjectByGlobalID( path.GetFinalChildID() );
			if ( physicsObjectInfo )
				return physicsObjectInfo->CreateEditableProxy();
		}

		return red::SharedPtr< IEditableProxy >();
	}

	red::SharedPtr< tools::IEditableProxy > PhysicsObjectResolver::ResolveProxyParent( const EditorObjectIDPath& path ) const
	{
		const physics::PhysicsBasePtr physicsObjectInfo = FindPhysicsObjectByGlobalID( path.GetFinalChildID() );
		if( physicsObjectInfo )
			return physicsObjectInfo->CreateEditableProxy( true );

		return red::SharedPtr< IEditableProxy >();
	}

	Bool tools::PhysicsObjectResolver::IsResolvable( const EditorObjectIDPath& path, Bool forEditing ) const
	{
		 if ( path.GetDepth() != 1 )
		 {
			 return false;
		 }

		 const EditorObjectID& id = path.GetFinalChildID();
		 return SupportsContentType( id.GetKind() );
	}


	Bool PhysicsObjectResolver::SupportsContentType( const CName& contentTypeName ) const
	{
		return contentTypeName == m_objectType;
	}

}