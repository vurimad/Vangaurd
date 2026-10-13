/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "materialPreviewPanel.h"
#include "materialInterop.h"
#include "materialLayersTree.h"

class CMesh;

namespace tools
{
	class TreeContainerInterop;

	class MeshDataProvider
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
	public:
		MeshDataProvider();
		~MeshDataProvider() {}
		void AddMesh( const String& name, const String& path );
		THandle< CMesh > LoadMesh( const String& name );
		THandle< CMesh > LoadDefaultMesh();
		void Reset();
		const red::DynArray<String>& GetSupportedGeometryTypes();
	private:
		red::HashMap< String, TResRef<CMesh> > m_geometryMap;
		red::DynArray< String > m_supportedGeometryTypes;
	};

	class MaterialController
	{
		DECLARE_INTEROP( MaterialController );
		RED_USE_MEMORY_POOL( red::PoolBackend );
	public:
		MaterialController( THandle< CResource >& material, ResourceBackend* resourceBackend );
		virtual ~MaterialController() {}

		
		const THandle< CResource > GetMaterial() const { return m_material; }
		void SetPreview( MaterialPreviewPanelPtr& preview );
		virtual Bool ChangeGeometry( const String& geometryName );
		
		void UpdateRenderer( const THandle< IMaterial >& material );
		static Bool UpdateMaterialMeta( const THandle< CMaterialTemplate >& material );

		virtual Bool Save() { return true; };
		virtual Bool CheckNotification( const SerializableID& id ) = 0;
		virtual void OnNotifyPropertyChange( const SerializableID& id, const rtti::AccessPath& path );
		virtual MaterialInteropPtr& GetInterop();		

	protected:
		const THandle< CResource > m_material;
		THandle<CMesh> m_mesh;
		MeshDataProvider m_meshDataProvider;


		MaterialPreviewPanelPtr m_preview;
		MaterialInteropPtr m_interop;
		ResourceBackend* m_resourceBackend;
	};

	typedef red::SharedPtr<MaterialController> MaterialControllerPtr;

	class TemplateMaterialController : public MaterialController
	{
		DECLARE_INTEROP_DERIVED( TemplateMaterialController, MaterialController );
	public:
		TemplateMaterialController( THandle< CResource >& material, ResourceBackend* resourceBackend ) : MaterialController( material, resourceBackend ) {}
		virtual ~TemplateMaterialController() {}

		virtual Bool CheckNotification( const SerializableID& id ) override;
	};

	class LayerLibraryMaterialController : public MaterialController
	{
		DECLARE_INTEROP_DERIVED( LayerLibraryMaterialController, MaterialController );
	public:
		LayerLibraryMaterialController( THandle< CResource >& material, ResourceBackend* resourceBackend, IEditActionHistory* actionHistory, ObjectCollection* sharedSelection );
		virtual ~LayerLibraryMaterialController();

		void OnLayerSelected( red::SharedPtr< MaterialLayerDesc >& layerDesc );
		Bool OnExportJSON( const red::AbsolutePath& absPathStr );

		virtual Bool CheckNotification( const SerializableID& id ) override;
		virtual void OnNotifyPropertyChange( const SerializableID& id, const rtti::AccessPath& path );
		virtual Bool Save() override;		
		virtual Bool ChangeGeometry( const String& geometryName );

		red::SharedPtr< MaterialLayersTree > m_layersTree;
		red::SharedPtr< TreeContainerInterop > m_interopTree;
		red::SharedPtr< MaterialLayerDesc > m_activeLayer;

		const String& GetGamePath() const { return m_gamePath; }
		void SetGamePath( const String& path ) { m_gamePath = path; }

		String m_gamePath;
		interop::InteropNotifier m_gamePathChanged;
	};

	class MaterialControllerFactory
	{
	public:
		MaterialControllerPtr CreateMaterialController( THandle< CResource > resource, ResourceBackend* resourceBackend, IEditActionHistory* actionHistory, ObjectCollection* sharedSelection );
	};

} // namespace tools