/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "../include/addColorSetToProxyMeshCommand.h"

#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/resourceMesh/include/meshParamGarmentSupport.h"
#include "../../../common/redReflection/include/cloningUtils.h"
#include "../../../common/resource/include/resourceSaver.h"
#include "../../../common/resourceMaterial/include/bitmapTexture.h"

#include "../../backendMesh/include/meshBackendUtils.h"
#include "../../backendMesh/include/meshDataView.h"
#include "../../backendREA/include/reFileHelpers.h"
#include "../../backendEngine/include/editableCollectionCustomPropertyEditor.h"

namespace tools
{

	Bool AddColorSetToProxyMeshOptions::NeedOptions() const
	{
		return true;
	}

	//-------------------------------------------------------------------------------------------------------------------------

	Bool AddColorSetToProxyMesh::SupportsResource( const rtti::ClassType* resourceClass )
	{
		return resourceClass == ClassID< CMesh >();
	}

	CName AddColorSetToProxyMesh::GetName() const
	{
		return RED_NAME_CONSTEXPR( "addColorSetToProxyMesh" );
	}

	THandle< ResourceCommandOptions > AddColorSetToProxyMesh::CreateOptions( const THandle< ResourceCommandOptions > suggestedOptions ) const
	{
// 		res::ResourcePath meshPath = GetResourcePath();
// 		String bodyTypeApprev = mesh::ExtractBodyTypeNameFromMeshPath( meshPath.ToStringView() );

		THandle< AddColorSetToProxyMeshOptions > options = CreateHandle< AddColorSetToProxyMeshOptions >( );

		return options;
	}

	ResourceCommandResult AddColorSetToProxyMesh::Execute( const THandle<ResourceCommandOptions>& options )
	{
		if ( !options )
		{
			return ResourceCommandResult::Error( "Core: Options are not passed" );
		}

		THandle< AddColorSetToProxyMeshOptions > colorSetOptions = Cast< AddColorSetToProxyMeshOptions >( options );
		if ( !colorSetOptions)
		{
			return ResourceCommandResult::Error( "Core: Invalid options type: %s ", options->GetFriendlyName().AsChar() );
		}

		const res::ResourcePath txtResourcePath = res::ResourcePath::Build(colorSetOptions->GetBaseColorPath());
		const res::ResourcePath normalResourcePath = res::ResourcePath::Build(colorSetOptions->GetNormalPath());
		const res::ResourcePath metalnessResourcePath = res::ResourcePath::Build(colorSetOptions->GetMetalnessPath());
		const res::ResourcePath roughnessResourcePath = res::ResourcePath::Build(colorSetOptions->GetRoughnessPath());
		const res::ResourcePath emissiveResourcePath = res::ResourcePath::Build(colorSetOptions->GetEmissivePath());

		res::LoadingOptions loadingOptions;
		loadingOptions.skipPostLoad = false;

		TResRef<ITexture> txtAlbedo = GResourceLoader->IssueLoadingRequest(Convert(txtResourcePath, loadingOptions))->WaitUntilLoaded< ITexture >();
		TResRef<ITexture> txtNormal = GResourceLoader->IssueLoadingRequest(Convert(normalResourcePath, loadingOptions))->WaitUntilLoaded< ITexture >();
		TResRef<ITexture> txtMetalness = GResourceLoader->IssueLoadingRequest(Convert(metalnessResourcePath, loadingOptions))->WaitUntilLoaded< ITexture >();
		TResRef<ITexture> txtRoughness = GResourceLoader->IssueLoadingRequest(Convert(roughnessResourcePath, loadingOptions))->WaitUntilLoaded< ITexture >();
		TResRef<ITexture> txtEmissive = GResourceLoader->IssueLoadingRequest(Convert(emissiveResourcePath, loadingOptions))->WaitUntilLoaded< ITexture >();

		String name = colorSetOptions->GetColorSetName();
		const CName appearanceCName = RED_NAME(name);
		const String bodyMaterialName = name.Append("_body", 5);
		const String hairMaterialName = name.Append("_hair", 5);

		TResRef< CMesh > meshRef( GetResourcePath() );
		THandle< CMesh > mesh = meshRef.Get();
		
		mesh::DataView meshView(*mesh.Get());

		meshView.AddAppearanceCopy(RED_NAME(colorSetOptions->GetColorSetName()), *meshView.GetDefaultAppearanceData().Get(), -1);

		THandle< IMaterial > sourceBodyMaterial = meshView.GetMaterialDataByName(RED_NAME_CONSTEXPR("Body"));
		RED_ASSERT(sourceBodyMaterial, "Body material was not found, .mesh is broken!");
		THandle< IMaterial > bodyMaterial = Cast< IMaterial >(tools::CloneObject(sourceBodyMaterial));
		bodyMaterial->SetParent(meshView.GetMesh());

		if (!colorSetOptions->GetBaseColorPath().Empty())
		{
			bodyMaterial->WriteParameter(RED_NAME_CONSTEXPR("BaseColor"), txtAlbedo);
		}
		if (!colorSetOptions->GetBaseColorPath().Empty())
		{
			bodyMaterial->WriteParameter(RED_NAME_CONSTEXPR("Normal"), txtNormal);
		}
		if (!colorSetOptions->GetBaseColorPath().Empty())
		{
			bodyMaterial->WriteParameter(RED_NAME_CONSTEXPR("Metalness"), txtMetalness);
		}
		if (!colorSetOptions->GetBaseColorPath().Empty())
		{
			bodyMaterial->WriteParameter(RED_NAME_CONSTEXPR("Roughness"), txtRoughness);
		}
		if (!colorSetOptions->GetBaseColorPath().Empty())
		{
			bodyMaterial->WriteParameter(RED_NAME_CONSTEXPR("Emissive"), txtEmissive);
		}
		meshView.AddMaterial(bodyMaterial, RED_NAME(bodyMaterialName));
		meshView.SetAppearanceChunkMaterial(meshView.GetAppearanceIndex(appearanceCName), 0, RED_NAME(bodyMaterialName));

		THandle< IMaterial > sourceHairMaterial = meshView.GetMaterialDataByName(RED_NAME_CONSTEXPR("Hair"));
		if (sourceHairMaterial)
		{
			THandle< IMaterial > hairMaterial = Cast< IMaterial >(tools::CloneObject(sourceHairMaterial));
			hairMaterial->SetParent(meshView.GetMesh());

			if (!colorSetOptions->GetBaseColorPath().Empty())
			{
				hairMaterial->WriteParameter(RED_NAME_CONSTEXPR("Albedo"), txtAlbedo);
			}
			meshView.AddMaterial(hairMaterial, RED_NAME(hairMaterialName));
			meshView.SetAppearanceChunkMaterial(meshView.GetAppearanceIndex(appearanceCName), 1, RED_NAME(hairMaterialName));
		}
		meshView.Apply();

		if (!res::SaveResource(mesh))
		{
			return ResourceCommandResult::Error(String::Printf("Unable to remove LODs because saving failed"));
		}

		return ResourceCommandResult::Success(mesh);
		return ResourceCommandResult(red::DynArray< THandle< CResource > >());
	}
}

RTTI_BEGIN_TYPE_IN_NAMESPACE( AddColorSetToProxyMesh, tools );
	RTTI_PARENT_TYPE( tools::GeneralResourceCommand );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE(AddColorSetToProxyMeshOptions, tools );
RTTI_PARENT_TYPE(ResourceCommandOptions);
	RTTI_PROPERTY(m_colorSetName).editable();
	RTTI_PROPERTY(m_baseColorPath).editable();
	RTTI_PROPERTY(m_normalPath).editable();
	RTTI_PROPERTY(m_metalnessPath).editable();
	RTTI_PROPERTY(m_roughnessPath).editable();
	RTTI_PROPERTY(m_emissivePath).editable();
RTTI_END_TYPE();
