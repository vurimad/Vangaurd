/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#pragma once

// this shouldn't be here in the final version, but for now it allows to refresh proxy when autohide distance changes
// - need to wait for the proxy creation pipeline updates with the proper solution
#include "../../../common/redReflection/include/serializableValueNotifier.h"
#include "../../../common/redReflection/include/resourceMonitor.h"
#include "../../backendEngine/include/resourceEditor.h"
#include "../../backendEngine/include/houdiniEngineUtils.h"

#include "lodGenerationUtils.h"

class CMesh;

namespace physics
{
	class PhysicsEditorScene;
}

namespace interop
{
	class MeshResourceEditor;
}

namespace tools
{
	class MeshPreviewPanel;
	class ChunksListInterop;
	class MaterialsListInterop;
	class AppearancesListInterop;
	class StructureTree;
	class TreeContainerInterop;
	class MeshReport;
	class MeshPanelSharedData;
	class GenericPreviewPanelSharedData;

	/// Editor for mesh files
	class BACKEND_MESH_API MeshResourceEditor : public IResourceEditor
	{
        RED_BASE_CLASS(IResourceEditor);
		RTTI_DECLARE_TYPE( MeshResourceEditor );
		DECLARE_INTEROP_DERIVED( MeshResourceEditor, IResourceEditor );

		friend class StructureTree;

	public:
		MeshResourceEditor();
		virtual ~MeshResourceEditor();

		/// Get mesh being edited
		const THandle< CMesh > GetMesh() const;

		tools::MeshPanelSharedData* GetMeshPanelSharedData() const;

		/// IResourceEditor interface
		virtual Bool Initialize( const THandle< CResource >& resource ) override;
		virtual void InitializePreviewPanelSharedData();

		virtual const Bool SupportsResourceClass( const rtti::ClassType* resourceClass ) const override;
		virtual const THandle< CResource > GetResource() const override;
		virtual void OnPreviewPanelRequest(const String& type, tools::RenderingWindowPtr renderingWindowPtr, tools::PreviewPanelPtr& outPanel) override;

		virtual Bool HasReloadSupport() const override { return true; }
		virtual Bool OnOpenResourceReloaded( const THandle<CResource>& newResource ) override final;
		virtual Bool OnResourcePreSave( const THandle<CResource>& resource ) override final;
		virtual Bool Save( red::DynArray< res::ResourcePath >& successfullySaved = red::DynArray< res::ResourcePath >(),
			red::DynArray< res::ResourcePath >& failedToSave = red::DynArray< res::ResourcePath >() ) override;

		/// refresh the structure tree
		void RefreshStructure();
		virtual void RefreshStructureTree();
		void RefreshChunkList( const Int32 forcedLOD );
		Bool RemoveMaterial( Uint32 removedMaterialIndices ) const;

		Bool GenerateLODsHE( const mesh::LODGenerationUtils::GenerationInfo& generationInfo, houdini_legacy::HoudiniEngineSession* session, const CName& appearanceName );

	protected:
		// the mesh being edited
		THandle< CMesh > m_resource;

		// shared data
		red::SharedPtr<GenericPreviewPanelSharedData> m_previewSharedData;

	private:
		// create master mesh preview
		void OnMeshChanged();
		void OnMaterialsChanged( Bool visual );
		// called when any properties of any appearance change
		void OnAppearancesChanged();
		// current selection of mesh appearance changed
		void OnSelectedAppearanceChanged( CName newMeshAppearance );
		/// Called when bones list is changed (i.e. some bones were removed).
		void OnBonesChanged();
		
		void OnNotifyPropertyChange( const SerializableID& id, const rtti::AccessPath& path );
		virtual void OnActionExecute(const IEditAction& action) override final;

		// Garment related functions.
		void CalculateGarmentMeshStreams( const String& bodyTypeAbbr );
		void ImportGarmentMeshStreams( const String& garmentPath );
		void CloseHoudiniSesssion();
		Bool BakeAO( Float maxDistance, Float spread, Float softness, Uint8 procLods );
		void ExportMesh( const String& filePath );
		void InitializeDependenciesMonitors();
		void AppendDependencyMonitor( const res::ResourcePath& dependencyPath );

		void SetChunksCascadeFlags();

		Bool RemoveLOD( Int32 lod );
		Bool RemoveLOD0();

		red::SharedPtr< MeshPreviewPanel > m_previewPanel;
		
		red::SharedPtr< ChunksListInterop > m_chunksList;
		red::SharedPtr< MaterialsListInterop > m_materialsList;
		red::SharedPtr< AppearancesListInterop > m_apperancesList;

		red::SharedPtr< StructureTree > m_structureTree;
		red::SharedPtr< TreeContainerInterop > m_structureTreeInterop;

		red::UniquePtr< MeshReport > m_report;
		interop::InteropNotifier m_reportChanged;

		res::Monitor m_resMonitor;
		SerializableValueNotifier m_serializableNotifier;
		
		red::HashMap< res::ResourcePath, red::UniquePtr< res::Monitor > > m_dependenciesMonitor{ red::PoolBackend() };
	};

} // tools
