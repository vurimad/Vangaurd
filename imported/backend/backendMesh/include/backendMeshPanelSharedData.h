/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../../common/redMemory/include/weakPtr.h"
#include "../../../common/redReflection/include/resourceMonitor.h"

#include "../../backendEngine/include/resourceEditor.h"
#include "../../backendEngine/include/genericPreviewPanelSharedData.h"
#include "../../backendEngine/include/objectCollection.h"

class CMesh;

namespace mesh
{
	class MeshParameter;
}

namespace physics
{
	class PhysicsEditorScene;
	class PhysicsDecorationEditorVisualization;
	class PhysicsEditorOptions;
	class PhysicsMesh;

	struct PhysicsShapeRendererOptions;
	class PhysicsSimulationOptions;
	using PhysicsSimOptionsPtr = THandle< PhysicsSimulationOptions >;
	using PhysicsRendererOptionsPtr = THandle< PhysicsShapeRendererOptions >;
}

namespace tools
{
    struct ClothEditorState;
	class LODGenerationInterop;
	class PhysicsObjectResolver;

	struct MeshProxyUpdateInfo
	{
		CName m_meshAppearance;
		Int32 m_forcedLodLevel;

		Bool m_needsRecreation = false;
	};

	class BACKEND_MESH_API MeshPanelSharedData : public GenericPreviewPanelSharedData
	{
		RED_BASE_CLASS( GenericPreviewPanelSharedData );
		DECLARE_INTEROP_DERIVED( MeshPanelSharedData, GenericPreviewPanelSharedData );

	public:
		MeshPanelSharedData( IResourceEditor& editor );
		virtual ~MeshPanelSharedData();

		virtual void NotifyPanelCreated( PreviewPanel* panel ) override final;
		virtual void NotifyPanelDestroyed( PreviewPanel* panel ) override final;
		void NotifyLODsChange();

		void LoadEditorOptionsFromFile();

		void CreatePhysicsData();
		void PlaceMesh( const THandle< CMesh >& mesh );
		void SetupRendering( THandle< CMesh > mesh, CName meshAppearance, Int32 forcedLodLevel );

		void Save();
		void RegisterShortcuts( ShortcutManager& shortcutManager, CName context );

		physics::PhysicsEditorScene* GetMeshEditorPhysicsScene();

		Bool SnapToParent( const red::DynArray< EditorObjectID >& objectIds );
		Bool SnapToParentAlternativeMode( const red::DynArray< EditorObjectID >& objectIds );
		Bool SelectParent( const red::DynArray< EditorObjectID >& objectIds );
		Bool RecomputeCOM( const red::DynArray< EditorObjectID >& objectIds );
		Bool DeleteObjects( const red::DynArray< EditorObjectIDPath >& objectIdPaths );
		Bool GenerateDefaultDestructionHierarchy();

		void SaveInitialVehiclePoses();
		void SaveFinalVehiclePoses();
		void SwitchToFinalVehiclePoses();
		void SwitchToInitialVehiclePoses();

		void StartSimulation();
		void StopSimulation();

		CName GetDefaultEditModeName() const;

		const physics::PhysicsRendererOptionsPtr& GetRenderOptions() const;	
		const physics::PhysicsSimOptionsPtr& GetPhysicsSimulationOptions() const;

		red::BlobSpan GetMaxDistanceChannelSpan( Uint16 chunk ) const;
		red::BlobSpan GetMaxDistanceExtChannelSpan( Uint16 chunk ) const;
		red::BlobSpan GetBackstopDistanceChannelSpan( Uint16 chunk ) const;
		red::BlobSpan GetBackstopRadiusChannelSpan( Uint16 chunk ) const;
		red::BlobSpan GetSelfCollisionChannelSpan( Uint16 chunk ) const;
		red::BlobView GetClothResource( Uint16 chunk ) const;
		Uint32 GetNumClothLODs() const;
        void RefreshCompiledClothData();
		void RemoveClothParam(THandle<mesh::MeshParameter> param);

		res::ResourcePath GetMeshPath() const;

		MeshProxyUpdateInfo m_proxyUpdateInfo;

	protected:
		virtual void OnUpdateSharedState( const UpdateContext& ctx ) override;
		void OnResourceMonitorMessage( const res::ResourcePath& path, const CName& message, const res::MonitorUserDataPtr& userData );

		void Internal_SetClothState( const tools::ClothEditorState& state );
		void Internal_CompileClothData( Bool logSuccess = true );
		void Internal_ImportCollisionCapsules( red::AbsolutePath absolutePath );
		void Internal_ClearCollisionCapsules();
		void Internal_ValidateClothData();

		THandle< physics::PhysicsEditorScene >	m_physicsData;
		red::UniquePtr< PhysicsObjectResolver >	m_objectResolverPhysicsData;

		red::UniquePtr< LODGenerationInterop > m_lodGenerationInterop;

		THandle< physics::PhysicsEditorOptions > m_options;

		red::UniquePtr< ObjectCollection > m_renderOptionsView;
		res::Monitor m_monitor;

		CName m_previousEditMode; // edit mode to go back after simulation

        struct clothImpl;
        red::UniquePtr< clothImpl > clothPimpl;

	private:
		interop::InteropNotifier m_onLODsChangedNotifier;
	};

} // tools