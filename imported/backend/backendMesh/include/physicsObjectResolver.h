/* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.*/
#pragma once

#include "../../backendEngine/include/objectResolver.h"
#include "../../backendEngine/include/selectionFwd.h"
#include "../../../common/redMemory/include/weakPtr.h"

namespace physics
{
	class PhysicsEditorScene;
	class PhysicsBase;
	using PhysicsBasePtr = THandle< PhysicsBase >;
}

namespace tools
{
	class IPhysicsObjectConstructor;

	class BACKEND_MESH_API PhysicsObjectResolver : public IObjectResolver
	{
		RTTI_DECLARE_TYPE( PhysicsObjectResolver );
	public:
		PhysicsObjectResolver( physics::PhysicsEditorScene* physicsEditorInfo, SelectionHandler* selectionHandler, CName objectType );
		virtual ~PhysicsObjectResolver();

		virtual physics::PhysicsBasePtr FindPhysicsObjectByGlobalID( const EditorObjectID& globalId ) const;
		virtual Bool IsResolvable( const EditorObjectIDPath& path, Bool forEditing ) const override final;
		virtual Bool SupportsContentType( const CName& contentTypeName ) const override final;
		virtual Bool CanCreateObject( const ObjectCreationInfo& info, String* outReason = nullptr ) const override final;
		virtual Bool CanDeleteObjects( const red::ArraySpan< const EditorObjectIDPath >& paths, Bool isCut, String* outFailReason = nullptr ) const override final;
		virtual red::SharedPtr< IEditableProxy > ResolveProxy( const EditorObjectIDPath& path, const ResolvingParams& params = ResolvingParams() ) const override final;
		virtual THandle< ISerializable > ResolveObject( const EditorObjectIDPath& path, const ResolvingParams& params = ResolvingParams() ) const override final;
		virtual red::SharedPtr< IObjectPreview > CreateObjectPreview( const ObjectCreationInfo& initInfo ) override final;
		virtual red::SharedPtr< IObjectPersistentCollection > CaptureObjects( const red::ArraySpan< const EditorObjectIDPath >& paths, const CapturingParams& params = CapturingParams() ) override final;
		virtual red::SharedPtr< IObjectPersistentCollection > Action_CreateObjects( const red::ArraySpan< const ObjectCreationInfo > creationInfos, const CreateObjectsOutParams& out ) override final;
		virtual Bool Action_DeleteObjects( DeleteContext& deleteContext ) override final;
		virtual Bool Action_RestoreObjects( const red::SharedPtr< IObjectPersistentCollection >& objects, red::DynArray< EditorObjectIDPath >& outActualCreatedPaths ) override final;
		virtual Bool Action_PlaceObjects( const red::SharedPtr< IObjectPersistentCollection >& objects, const ObjectPlacementInfo& info, red::DynArray< EditorObjectIDPath >& outActualCreatedPaths ) override final;
		virtual red::SharedPtr< IObjectPersistentCollection > Action_ReplaceObjects( const red::ArraySpan< const EditorObjectIDPath >& objectPathsToReplace, const red::SharedPtr< IObjectPersistentCollection >& objectTemplate, red::DynArray< EditorObjectIDPath >& outActualCreatedPaths ) override final;

		red::SharedPtr< IEditableProxy > ResolveProxyParent(const EditorObjectIDPath& path) const;

		Bool Action_SnapObjects( const red::DynArray< EditorObjectID >& objectsID, Bool alternativeMode );
		Bool Action_SelectParent( const red::DynArray< EditorObjectID >& ids );
		Bool Action_SelectObject( const red::DynArray< EditorObjectID >& ids );
		Bool Action_RecomputeCOM( const red::DynArray< EditorObjectID >& ids );
		Bool Action_GenerateDefaultDestructionHierarchy( red::DynArray<Uint32>& oldHierarchy );
		Bool Action_SetDestructionHierarchy(const red::DynArray<Uint32>& hierarchy );

		physics::PhysicsEditorScene* GetPhysicsEditorScene() const;
	private:

		physics::PhysicsEditorScene* m_physicsEditorScene;
		SelectionHandler* m_selectionHandler;

		typedef red::DynArray< IPhysicsObjectConstructor*> TPhysicsObjectConstructor;
		TPhysicsObjectConstructor m_constructors;
		CName m_objectType;
	};

}
