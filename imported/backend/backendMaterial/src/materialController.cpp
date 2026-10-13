/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "../include/materialController.h"

#include "../../../common/resourceMaterial/include/material.h"
#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/resourceMaterial/include/materialTemplate.h"
#include "../../../common/resourceMaterial/include/materialInstance.h"
#include "../../../common/resourceMaterial/include/materialLayerLibrary.h"
#include "../../../common/resourceMaterial/include/material.h"
#include "../../../common/resourceMaterial/include/materialParameter.h"
#include "../../../common/redReflection/include/resourcePath.h" 

#include "../../../common/renderBackend/include/materialLibrary.h"
#include "../../../common/renderBackend/include/materialMetaDefinitionLoader.h"
#include "../../../backend/backendEngine/include/treeInterop.h"
#include "../../../common/resourceMaterial/include/materialMetaDefinition.h"

namespace tools
{
	const String spherePath("engine\\meshes\\editor\\sphere.w2mesh");
	const String cubePath( "engine\\meshes\\editor\\cube.mesh" );
	const String planePath("engine\\meshes\\editor\\plane1m.mesh");
	const String cylinderPath( "engine\\meshes\\editor\\cylinder.w2mesh" );
	const String refspherePath( "engine\\meshes\\editor\\reference_sphere.w2mesh" );
	const String cubeoldPath("engine\\meshes\\editor\\box.w2mesh");
	

	MaterialControllerPtr tools::MaterialControllerFactory::CreateMaterialController( THandle< CResource > resource, tools::ResourceBackend* resourceBackend, IEditActionHistory* actionHistory, ObjectCollection* sharedSelection )
	{
		auto classType = resource->GetClass();
		if ( classType->IsA( ClassID< CMaterialTemplate >() ) )
		{
			return red::CreateSharedPtr< TemplateMaterialController >( resource, resourceBackend );
		}
		else if ( classType->IsA( ClassID< CMaterialInstance >() ) )
		{
			return red::CreateSharedPtr< TemplateMaterialController >( resource, resourceBackend );
		}
		else if ( classType->IsA( ClassID< CMaterialLayerLibrary >() ) )
		{
			return red::CreateSharedPtr< LayerLibraryMaterialController >( resource, resourceBackend, actionHistory, sharedSelection );
		}
		else
		{
			return nullptr;
		}
	}

	MaterialController::MaterialController( THandle< CResource >& material, ResourceBackend* resourceBackend )
		: m_material( material )
		, m_preview( nullptr )
		, m_interop( RED_NEW( MaterialInterop )( this ) )
		, m_resourceBackend( resourceBackend )
	{
		m_mesh = m_meshDataProvider.LoadDefaultMesh();
		m_interop->SetGeometryTypes( m_meshDataProvider.GetSupportedGeometryTypes() );
	}

	void MaterialController::SetupInterop( MaterialController* object, ::interop::utils::Object& op )
	{
		op.RegisterObject( "geometryController", object->m_interop->CreateInterop() );
	}

	void MaterialController::SetPreview( MaterialPreviewPanelPtr& preview )
	{
		m_preview = preview;
		UpdateRenderer( Cast< IMaterial >( GetMaterial() ) );
	}

	Bool MaterialController::ChangeGeometry( const String& geometryName )
	{
		THandle<CMesh> newMesh = m_meshDataProvider.LoadMesh( geometryName );
		if ( newMesh )
		{
			m_mesh = newMesh;
			UpdateRenderer( Cast< IMaterial >( GetMaterial() ) );
			return true;
		}
		return false;
	}

	void MaterialController::UpdateRenderer( const THandle< IMaterial >& material )
	{
		if ( m_preview )
		{
			m_preview->SetupRendering( m_mesh, material );
		}
	}

	void MaterialController::OnNotifyPropertyChange( const SerializableID& id, const rtti::AccessPath& path )
	{
		if ( id == GetMaterial()->GetID() )
		{
			THandle< IMaterial > material = Cast< IMaterial >( GetMaterial() );
			if ( THandle< CMaterialTemplate > materialTemplate = Cast< CMaterialTemplate >( material ) )
			{
				if ( path.ToString() == "name" )
				{
					UpdateMaterialMeta( materialTemplate );
				}
			}

			UpdateRenderer( material );
		}
	}

	Bool MaterialController::UpdateMaterialMeta( const THandle< CMaterialTemplate >& materialHandle )
	{
		if ( !GetMaterialLibrary() || !materialHandle )
		{
			return false;
		}

		const CName name = materialHandle->GetName();
		MaterialMetaDefinition metaDefinition( name );
		CMaterialMetaDefinitionLoader::Logger log( name );
		CMaterialMetaDefinitionLoader loader( false, GetMaterialLibrary(), log );
		if ( loader.Load( name, metaDefinition ) != CMaterialMetaDefinitionLoader::LoaderStatus::Ok )
		{
			return false;
		}
		
		Bool modified = materialHandle->UpdateParametersList( metaDefinition );
		modified = materialHandle->UpdateTechniques( metaDefinition ) || modified;
		modified = modified || materialHandle->GetPriority() != metaDefinition.m_materialPriority;
		modified = modified || materialHandle->GetMaterialType() != metaDefinition.m_materialType;

		materialHandle->SetPriority( metaDefinition.m_materialPriority );
		materialHandle->SetMaterialType( metaDefinition.m_materialType );
		if ( !GetMaterialLibrary()->UpdateTechniquesForMaterial( materialHandle->GetName(), metaDefinition.m_techniques ) )
		{
			return false;
		}
		if ( modified )
		{
			materialHandle->MarkModified();
		}
#ifndef NO_RUNTIME_MATERIAL_COMPILATION
		materialHandle->ForceRecompilation();
#endif // NO_RUNTIME_MATERIAL_COMPILATION

		return true;
	}

	tools::MaterialInteropPtr& MaterialController::GetInterop()
	{
		return m_interop;
	}

	Bool TemplateMaterialController::CheckNotification( const SerializableID& id )
	{
		return id == GetMaterial()->GetID();
	}

	void TemplateMaterialController::SetupInterop( TemplateMaterialController* object, ::interop::utils::Object& op )
	{
	}

	LayerLibraryMaterialController::LayerLibraryMaterialController( THandle< CResource >& material, ResourceBackend* resourceBackend, IEditActionHistory* actionHistory, ObjectCollection* sharedSelection )
		: MaterialController( material, resourceBackend )
		, m_layersTree()
	{
		auto pathView = material->GetPath().ToStringView();
		auto pos = pathView.FindReverse( "\\" );
		pathView.TrimBack( pathView.Length() - pos );
		m_layersTree = red::CreateSharedPtr< MaterialLayersTree >( this, m_resourceBackend, pathView.ToString(), actionHistory );
		m_interopTree = red::CreateSharedPtr< TreeContainerInterop >( m_layersTree.Get(), sharedSelection );
	}

	LayerLibraryMaterialController::~LayerLibraryMaterialController()
	{
	}

	void LayerLibraryMaterialController::OnLayerSelected( red::SharedPtr< MaterialLayerDesc >& layerDesc )
	{
		m_activeLayer = layerDesc;
		UpdateRenderer( m_activeLayer->GetResource() );
	}

	Bool LayerLibraryMaterialController::CheckNotification( const SerializableID& id )
	{
		return id == GetMaterial()->GetID() ||
			id == m_activeLayer->GetResource()->GetID();
	}

	void LayerLibraryMaterialController::OnNotifyPropertyChange( const SerializableID& id, const rtti::AccessPath& path )
	{
		if ( m_activeLayer )
		{
			if ( id == m_activeLayer->GetResource()->GetID() )
			{
				m_activeLayer->GetResource()->MarkModified();
			}
			UpdateRenderer( m_activeLayer->GetResource() );
		}
	}

	Bool LayerLibraryMaterialController::Save()
	{
		return m_layersTree->Save();
	}

	Bool LayerLibraryMaterialController::ChangeGeometry( const String& geometryName )
	{
		THandle<CMesh> newMesh = m_meshDataProvider.LoadMesh( geometryName );
		if ( newMesh )
		{
			m_mesh = newMesh;
			if ( m_activeLayer )
			{
				UpdateRenderer( m_activeLayer->GetResource() );
			}
			return true;
		}
		return false;
	}

	void LayerLibraryMaterialController::SetupInterop( LayerLibraryMaterialController* ptr, ::interop::utils::Object& op )
	{
		op.RegisterObject( "interopLayerTree", interop::ObjectPtr( ptr->m_interopTree ) );
		op.RegisterObject( "layerTree" , ptr->m_layersTree->CreateInterop());

		op.RegisterValue< String >( "gamePath",
			[ptr]( interop::IContext& ctx, String& outVal )
			{
				outVal = ptr->GetGamePath();
			},
			[ptr]( interop::IContext& ctx, const String& val )
			{
				ptr->SetGamePath( val );
			},
			&ptr->m_gamePathChanged
		);

		// export .json		
		op.RegisterFunctor<Bool, String>("OnExportJSON", [ptr](::interop::IContext& ctx, const red::String& absPathStr)
		{
			const red::AbsolutePath absPath = red::AbsolutePath::CreateFilePath( absPathStr );
			return ptr->OnExportJSON( absPath );
		}
		);
	}

	Bool LayerLibraryMaterialController::OnExportJSON(const red::AbsolutePath& absPath)
	{
		// export .json
		red::AbsolutePath outputPath = red::paths::SetExtension( absPath, "json" );

		// NOTE: This is quite a hack as I'm not sure what m_gamePath is
		// The assumption is that it's the base path to the game depot but it could be something
		// completely different. This code needs a good cleanup sometime to sort these things out
		// TODO: Fix these files
		red::AbsolutePath matlibAbsolutePath = red::AbsolutePath::CreateDirPath( m_gamePath );
		matlibAbsolutePath.AppendFilePath( m_material->GetPath().ToStringView() );

		return m_layersTree->GetDataProvider()->UpdateJson( outputPath, matlibAbsolutePath );
	}

	MeshDataProvider::MeshDataProvider()
	{
		AddMesh( "sphere", spherePath );
		AddMesh( "cube", cubePath );
		AddMesh( "plane", planePath);
		AddMesh( "cylinder", cylinderPath );
		AddMesh( "ref sphere", refspherePath );
		AddMesh( "cube old", cubeoldPath);

	}

	void MeshDataProvider::AddMesh( const String& name, const String& path )
	{
		m_geometryMap[name] = TResRef<CMesh>( res::ResourcePath::Build( path ) );
		m_supportedGeometryTypes.PushBack( name );
	}

	THandle< CMesh > MeshDataProvider::LoadMesh( const String& name )
	{
		auto iter = m_geometryMap.Find( name );
		if ( iter != m_geometryMap.End() )
		{
			return iter.Value().Get();
		}
		return nullptr;
	}

	THandle< CMesh > MeshDataProvider::LoadDefaultMesh()
	{
		return LoadMesh( m_supportedGeometryTypes[0] );
	}

	void MeshDataProvider::Reset()
	{
		m_geometryMap.Clear();
		m_supportedGeometryTypes.Clear();
	}

	const red::DynArray<String>& MeshDataProvider::GetSupportedGeometryTypes()
	{
		return m_supportedGeometryTypes;
	}

} // namespace tools
