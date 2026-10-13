/**
* Copyright (c) 2007-2020 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "fbxCommon.h"
#include "fbxsdk/scene/animation/fbxanimcurve.h"
#include "../../../common/core/include/stringConversion.h"
#include "../../../common/redContainers/include/string/stringView.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#if !defined( VANGUARD_ASSET_HOST )
#include "../../../backend/backendEngine/include/exporter.h"
#include "../../../common/redMath/include/random.h"
#include "../../../common/resource/include/xmlNewWriter.h"
#include "../../../common/resourceMaterial/include/bitmapTexture.h"
#include "../../../common/resourceMaterial/include/material.h"
#include "../../../common/resourceMaterial/include/materialInstance.h"
#include "../../../common/resourceMaterial/include/materialParameterInstance.h"
#include "../../../common/resourceMaterial/include/textureArray.h"
#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/resourceMesh/include/meshDataViewRO.h"
#endif

//-----------------------------------------------------------------------------

FBXImportScene::FBXImportScene()
	: m_manager(NULL)
	, m_scene(NULL)
{
}

FBXImportScene::~FBXImportScene()
{
}

FBXImportScene* FBXImportScene::Load( const String& filename )
{
	FbxManager* manager = FbxManager::Create();
	if ( !manager )
	{
		RED_LOG_ERROR( "FBXImportScene: Unable to create the FBX SDK manager" );
		return nullptr;
	}

	// Print version
	RED_LOG_SPAM( "FBXImportScene: Autodesk FBX SDK version %hs", manager->GetVersion() );

	//Create an IOSettings object. This object holds all import/export settings.
	FbxIOSettings* ios = FbxIOSettings::Create( manager, IOSROOT );
	manager->SetIOSettings(ios);

	// Get the file version number generate by the FBX SDK.
	int lFileMajor, lFileMinor, lFileRevision;
	int lSDKMajor,  lSDKMinor,  lSDKRevision;
	FbxManager::GetFileFormatVersion(lSDKMajor, lSDKMinor, lSDKRevision);

	// Create an importer.
	FbxImporter* lImporter = FbxImporter::Create(manager,"");

	// Initialize the importer by providing a filename.
	const bool lImportStatus = lImporter->Initialize( filename.AsChar(), -1, manager->GetIOSettings());
	lImporter->GetFileVersion(lFileMajor, lFileMinor, lFileRevision);

	if ( !lImportStatus )
	{
		RED_LOG_ERROR( "FBXImportScene: Call to FbxImporter::Initialize() failed." );
		RED_LOG_ERROR( "FBXImportScene: Error: %s", lImporter->GetStatus().GetErrorString() );
		return nullptr;
	}

	RED_LOG_SPAM( "FBXImportScene: FBX file format version for this FBX SDK is %d.%d.%d", lSDKMajor, lSDKMinor, lSDKRevision);

	if (lImporter->IsFBX())
	{
		RED_LOG_SPAM( "FBXImportScene: File Version Major: %d Minor: %d Revision: %d", lFileMajor, lFileMinor, lFileRevision );

		// Set the import states. By default, the import states are always set to 
		// true. The code below shows how to change these states.
		manager->GetIOSettings()->SetBoolProp(IMP_FBX_MATERIAL,        true);
		manager->GetIOSettings()->SetBoolProp(IMP_FBX_TEXTURE,         true);
		manager->GetIOSettings()->SetBoolProp(IMP_FBX_LINK,            true);
		manager->GetIOSettings()->SetBoolProp(IMP_FBX_SHAPE,           true);
		manager->GetIOSettings()->SetBoolProp(IMP_FBX_GOBO,            true);
		manager->GetIOSettings()->SetBoolProp(IMP_FBX_ANIMATION,       true);
		manager->GetIOSettings()->SetBoolProp(IMP_FBX_GLOBAL_SETTINGS, true);
	}

	// Import the scene.
	FbxScene* pScene = FbxScene::Create( manager, "ImportScene" );
	if ( !lImporter->Import(pScene) )
	{
		lImporter->Destroy();
		pScene->Destroy();
		return NULL;
	}

	// Convert axes
	FbxAxisSystem exportAxes( FbxAxisSystem::eZAxis, FbxAxisSystem::eParityEven, FbxAxisSystem::eRightHanded );
	exportAxes.ConvertScene( pScene );
	//FbxAxisSystem::max.ConvertScene( pScene );

	// Scene units are meteres
	FbxSystemUnit::m.ConvertScene( pScene );

	// Return imported scene
	lImporter->Destroy();

	// Return imported scene
	FBXImportScene* retScene = RED_NEW( FBXImportScene )();
	retScene->m_manager = manager;
	retScene->m_scene = pScene;
	return retScene;
}


#if !defined( VANGUARD_ASSET_HOST )
FBXExportScene::FBXExportScene( const String& saveFilePath )
	: m_scene(NULL)
	, m_manager(NULL)
{
	// Create SDK objects
	m_manager = FbxManager::Create();
	if( !m_manager )
	{
		RED_LOG_ERROR( "FBXExportScene: Error: Unable to create FBX Manager!" );
		return;
	}

	String version = String::Printf( "%hs", m_manager->GetVersion() );
	// Print version
	RED_LOG_SPAM( "FBXExportScene: Autodesk FBX SDK version %hs", m_manager->GetVersion() );

	//Create an IOSettings object. This object holds all import/export settings.
	FbxIOSettings* ios = FbxIOSettings::Create( m_manager, IOSROOT );
	m_manager->SetIOSettings(ios);

	// Create an FBX scene. This object holds most objects imported/exported from/to files.
	String fileName = ::red::paths::ExtractFileName( saveFilePath );
	m_scene = FbxScene::Create( m_manager, fileName.AsChar() );
	if ( !m_scene )
	{
		RED_LOG_ERROR( "FBXExportScene: FBX: Unable to create FBX scene!" );
		m_manager->Destroy();
		return;
	}

	// Scene export up axis (Z)
	//FbxAxisSystem exportAxes( FbxAxisSystem::eZAxis, FbxAxisSystem::eParityOdd, FbxAxisSystem::eRightHanded );
	//m_scene->GetGlobalSettings().SetAxisSystem( exportAxes );

	// Scene units are meters
	//m_scene->GetGlobalSettings().SetSystemUnit( FbxSystemUnit( 100.0, 1.0 ) );
	m_scene->GetGlobalSettings().SetSystemUnit( FbxSystemUnit::m );
	m_scene->GetGlobalSettings().SetAxisSystem( FbxAxisSystem( FbxAxisSystem::eZAxis, FbxAxisSystem::eParityEven, FbxAxisSystem::eRightHanded ) );

}

FBXExportScene::~FBXExportScene()
{
	if ( NULL != m_scene )
	{
		m_scene->Destroy();
		m_scene = NULL;
	}

	if ( NULL != m_manager )
	{
		m_manager->Destroy();
		m_manager = NULL;
	}
}

Bool FBXExportScene::Save( const String& filePath )
{
	Bool exportStatus = false;

	// Now create a bind pose with the link list
	if (m_clusteredFbxNodes.GetCount())
	{
		// A pose must be named. Arbitrarily use the name of the patch node.
		FbxPose* lPose = FbxPose::Create( m_scene, "BasePose" );
		lPose->SetIsBindPose(true);

		const Uint32 numBones = m_clusteredFbxNodes.GetCount();
		for (Uint32 i=0; i<numBones; i++)
		{
			FbxNode*  lKFbxNode   = m_clusteredFbxNodes.GetAt(i);
			FbxMatrix lBindMatrix = lKFbxNode->EvaluateGlobalTransform();
			lPose->Add(lKFbxNode, lBindMatrix);
		}

		// Add the pose to the scene
		m_scene->AddPose(lPose);
	}

	// Store rest pose ( the same as bind pose )
	{
		FbxPose* lPose = FbxPose::Create( m_scene, "RestPose" );
		lPose->SetIsBindPose(false);

		// Process all nodes
		for ( Uint32 i=0; i<m_nodes.Size(); ++i )
		{
			FbxNode* node = m_nodes[i];
			FbxMatrix lBindMatrix = node->EvaluateGlobalTransform();
			lPose->Add( node, lBindMatrix );
		}

		// Add the pose to the scene
		m_scene->AddPose(lPose);
	}

	// Save scene
	{
		// Create an exporter
		FbxExporter* lExporter = FbxExporter::Create( m_manager, "");

		// Use native format
		const Int32 pFileFormat = m_manager->GetIOPluginRegistry()->GetNativeWriterFormat();

		// Set the export states. By default, the export states are always set to 
		// true except for the option eEXPORT_TEXTURE_AS_EMBEDDED. The code below 
		// shows how to change these states.
		m_manager->GetIOSettings()->SetBoolProp(EXP_FBX_MATERIAL,        true);
		m_manager->GetIOSettings()->SetBoolProp(EXP_FBX_TEXTURE,         true);
		m_manager->GetIOSettings()->SetBoolProp(EXP_FBX_EMBEDDED,        true);
		m_manager->GetIOSettings()->SetBoolProp(EXP_FBX_SHAPE,           true);
		m_manager->GetIOSettings()->SetBoolProp(EXP_FBX_GOBO,            true);
		m_manager->GetIOSettings()->SetBoolProp(EXP_FBX_ANIMATION,       true);
		m_manager->GetIOSettings()->SetBoolProp(EXP_FBX_GLOBAL_SETTINGS, true);

		// Initialize the exporter by providing a filename.
		if ( !lExporter->Initialize( filePath.AsChar(), pFileFormat, m_manager->GetIOSettings()) )
		{
			RED_LOG_ERROR( "FBXExportScene: FBX: Call to FbxExporter::Initialize() failed." );
			RED_LOG_ERROR( "FBXExportScene: FBX: Error returned: %hs", lExporter->GetStatus().GetErrorString() );
			exportStatus = false;
		}
		else
		{
			// Info
			int lMajor, lMinor, lRevision;
			FbxManager::GetFileFormatVersion(lMajor, lMinor, lRevision);
			RED_LOG_SPAM( "FBXExportScene: FBX file format version %d.%d.%d", lMajor, lMinor, lRevision);

			// Export the scene.
			if ( !lExporter->Export( m_scene ) )
			{
				RED_LOG_ERROR( "FBXExportScene: FBX: error exporting scene: %hs", lExporter->GetStatus().GetErrorString() );
				exportStatus = false;
			}
			else
			{
				exportStatus = true;
			}
		}

		// Destroy the exporter.
		lExporter->Destroy();
	}

	if ( exportStatus )
	{
		const auto xmlPath = red::utils::ReplaceExtension( red::AbsolutePath::CreateFilePath( filePath.AsChar() ), "xml" );

		if ( !m_xmlBuffer.Get()->SaveToFile( xmlPath ) )
		{
			RED_LOG_ERROR( "FBXExportScene: Failed to save content manifest file '%hs' for file '%hs'", xmlPath.AsChar(), filePath.AsChar() );
		}
	}

	// Return status
	return exportStatus;
}

Uint32 FBXExportScene::AppendPhongMaterial( FbxNode* lNode, const CName& name, const FbxDouble3& diffuse )
{
	FbxSurfacePhong* newMaterial = FbxSurfacePhong::Create( m_scene, name.AsChar() );
	newMaterial->ShadingModel.Set("Phong");
	newMaterial->Diffuse.Set( diffuse );
	return lNode->AddMaterial( newMaterial );
}

static void AddNodeRecursively(FbxArray<FbxNode*>& pNodeArray, FbxNode* pNode)
{
	if (pNode)
	{
		AddNodeRecursively(pNodeArray, pNode->GetParent());

		if (pNodeArray.Find(pNode) == -1)
		{
			// Node not in the list, add it
			pNodeArray.Add(pNode);
		}
	}
}

void FBXExportScene::ExportXMLMeshData( xml::Writer& writer, const mesh::DataViewRO& view )
{
	xml::WriteBlock dataNode{ writer, "mesh_data" };
	writer.Attrf( "autohideDistance", "%.2f", view.GetAutoHideDistance() );

	const Uint32 numLodLevels = view.GetNumLODs();

	{
		xml::WriteBlock lodsNode{ writer, "LODs" };

		for ( Uint32 i = 0; i < numLodLevels; ++i )
		{
			const float lodDistance = view.GetLODDistance( i );

			xml::WriteBlock infoNode{ writer, "LOD_info" };
			writer.Attrf( "distance", "%.2f", lodDistance );
		}
	}
}

void FBXExportScene::ExportXMLMaterialData( xml::Writer& writer, const CMesh* srcModel, const IMaterial* material, Uint32 materialID, const FbxSurfacePhong* newMaterial )
{
	const auto alreadySaved = m_xmlSavedMaterials.Exist( materialID );
	if ( alreadySaved )
	{
		return;
	}

	xml::WriteBlock materialNode{ writer, "material" };
	const Bool localInstance = material->GetParent() == srcModel;
	writer.Attr( "name", newMaterial->GetName() );
	writer.Attr( "local", ::ToStringDirect( localInstance ).AsChar() );

	if ( localInstance )
	{
		const auto* baseMaterial = material->GetBaseMaterial();
		if ( baseMaterial && baseMaterial->HasValidPath() )
		{
			const auto base = baseMaterial->GetPath().ToString();
			writer.Attr( "base", base.AsChar() );
		}

		if ( const auto* materialInstance = Cast< CMaterialInstance >( material ) )
		{
			for ( const auto& param : materialInstance->GetParameters() )
			{
				xml::WriteBlock parameterNode{ writer, "param" };
				writer.Attr( "name", param.GetName().AsChar() );

				String val;
				if ( param.GetType()->ToString( param.GetData(), val ) ) // FIXME ToString deprecated
				{
					writer.Attr( "type", param.GetType()->GetName().AsChar() );
					writer.Attr( "value", val.AsChar() );
				}
				else
				{
					// hmmmm...
					RED_LOG_ERROR( "FBXExportScene: Parameter %hs of type %hs is not supported!", param.GetName().AsChar(), param.GetType()->GetName().AsChar() );
				}
			}
		}
	}
	else if ( material->HasValidPath() )
	{
		const auto path = material->GetPath().ToString();
		writer.Attr( "base", path.AsChar() );
	}

	m_xmlSavedMaterials.PushBack( materialID );
}

void FBXExportScene::ExportMesh( const Matrix& modelLocalToWorld, Bool exportSkinning, const CMesh* srcModel, const red::String& name  )
{
	m_nodes.Clear();
	m_nodeMap.Clear();
	m_clusteredFbxNodes.Clear();
	m_xmlBuffer = red::UniquePtr< CBasicTextBuilder, red::PoolBackend >::Create();
	m_xmlSavedMaterials.Clear();

	const mesh::DataViewRO view( *srcModel );

	m_xmlBuffer->Write( "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" );
	auto writer = xml::Writer::Create( *m_xmlBuffer );
	xml::WriteBlock meshNode{ *writer, "mesh" };
	ExportXMLMeshData( *writer, view );

	// Create LOD buckets
	red::DynArray< Uint8 > lodMasks;
	const Uint32 numChunks = view.GetNumChunks();
	const Uint32 numLods = view.GetNumLODs();
	for (Uint32 chunkIndex = 0; chunkIndex < numChunks; ++chunkIndex)
	{
		const auto chunkLodMask = view.GetChunkLodMask(chunkIndex);
		red::alg::PushBackUnique(lodMasks, chunkLodMask);
	}

	// Export all LODs
	xml::WriteBlock materialsNode{ *writer, "materials" };
	for (const auto& lodMask : lodMasks)
	{
		// Mesh name
		String meshName = name;
		if (meshName.Empty())
		{
			meshName = red::paths::ExtractFileStem( srcModel->GetPath().ToStringView() );
		}

		// Prefix Node name with lod index if there are LODs for this mesh
		String nodeName = meshName;
		for (Uint32 i = 0; i < numLods; ++i)
		{
			const auto testMask = 1 << i;
			if (testMask & lodMask)
			{
				String lodSuffix = String::Printf(("_lod%d"), i);
				nodeName += lodSuffix;
			}
		}

		// Create new mesh for each chunk
		FbxMesh* newMesh = FbxMesh::Create(m_manager, meshName.AsChar() );

		// Count total vertices in the mesh ( in all chunks )
		Uint32 totalVertexCount = 0;
		Uint32 totalTriangleCount = 0;
		for (Uint32 i = 0; i < numChunks; ++i)
		{
			if (lodMask & view.GetChunkLodMask(i))
			{
				totalVertexCount += view.GetChunkVertexCount(i);
				totalTriangleCount  += view.GetChunkIndexCount(i) / 3;
			}
		}

		// Allocate vertices ( in FBX they are called Control Points )
		newMesh->InitControlPoints( totalVertexCount );

		// Create the default layer
		if ( !newMesh->GetLayer(0) )
		{
			newMesh->CreateLayer();
		}

		// Allocate normals
		FbxGeometryElementNormal* lNormalElement = newMesh->CreateElementNormal();
		lNormalElement->SetMappingMode(FbxGeometryElement::eByControlPoint);
		lNormalElement->SetReferenceMode(FbxGeometryElement::eDirect);
		newMesh->GetLayer(0)->SetNormals(lNormalElement);

		// Create UV for Diffuse channel
		FbxGeometryElementUV* lUVDiffuseElement = newMesh->CreateElementUV( "DiffuseUV" );
		lUVDiffuseElement->SetMappingMode(FbxGeometryElement::eByControlPoint);
		lUVDiffuseElement->SetReferenceMode(FbxGeometryElement::eDirect);
		newMesh->GetLayer(0)->SetUVs(lUVDiffuseElement);

		// Create second UV channel
		FbxGeometryElementUV* lUVSecondDiffuseElement = newMesh->CreateElementUV("SecondUV");
		lUVSecondDiffuseElement->SetMappingMode(FbxGeometryElement::eByControlPoint);
		lUVSecondDiffuseElement->SetReferenceMode(FbxGeometryElement::eDirect);
		newMesh->GetLayer(0)->SetUVs(lUVSecondDiffuseElement, FbxLayerElement::eTextureReflection);

		// Set material mapping.
		FbxGeometryElementMaterial* lMaterialElement = newMesh->CreateElementMaterial();
		lMaterialElement->SetMappingMode(FbxGeometryElement::eByPolygon);
		lMaterialElement->SetReferenceMode(FbxGeometryElement::eIndexToDirect);
		newMesh->GetLayer(0)->SetMaterials(lMaterialElement);

		FbxGeometryElementVertexColor* lVecrtexColorElement = newMesh->CreateElementVertexColor();
		lVecrtexColorElement->SetMappingMode(FbxGeometryElement::eByControlPoint);
		lVecrtexColorElement->SetReferenceMode(FbxGeometryElement::eDirect);
		newMesh->GetLayer(0)->SetVertexColors(lVecrtexColorElement);

		// Create mesh node
		FbxNode* lNode = FbxNode::Create( m_manager, nodeName.AsChar() );
		lNode->SetNodeAttribute( newMesh );
		lNode->SetShadingMode( FbxNode::eTextureShading );

		// Clusters for each bone
		red::DynArray< FbxCluster* > boneClusters;
		red::DynArray< Matrix > skinMatrices;
		red::DynArray< Int32 > globalBonesMap;
		if (exportSkinning)
		{
			const Uint32 numBones = view.GetNumBones();
			boneClusters.Resize(numBones);
			skinMatrices.Resize(numBones);
			globalBonesMap.Resize(numBones);
			for (Uint32 i=0; i<numBones; ++i)
			{
				// empty initial cluster
				boneClusters[i] = NULL;

				// Create skeleton
				FbxSkeleton* skeletonAttr = FbxSkeleton::Create( m_manager, "Skeleton" );
				skeletonAttr->SetSkeletonType( FbxSkeleton::eRoot );
				skeletonAttr->Size.Set( 0.01 );

				// Find bone by name
				Int32 globalBoneIndex = -1;
				const String boneName = view.GetBoneName(i).AsChar();
				if ( m_nodeMap.Find( boneName, globalBoneIndex ) )
				{
					globalBonesMap[i] = globalBoneIndex;
				}
				else
				{
					// Create node
					FbxNode* newNode = FbxNode::Create( m_manager, boneName.AsChar() );
					m_scene->GetRootNode()->AddChild( newNode );
					newNode->SetNodeAttribute(skeletonAttr);
					newNode->Visibility.Set( 0.0 );
					newNode->VisibilityInheritance.Set( false );

					// Export transform
					{
						Matrix rigMatrix = view.GetBoneMatrix(i).FullInverted();
						Vector4 pos = rigMatrix.GetTranslation();
						math::Quaternion q = rigMatrix.ToQuat();
						const FbxAMatrix rotM( FbxVector4(0,0,0,0), FbxQuaternion( q.i, q.j, q.k, q.r ), FbxVector4(1,1,1,1) );

						newNode->LclTranslation.Set( FbxDouble3( pos.X, pos.Y, pos.Z ) );
						newNode->LclRotation.Set( rotM.GetR() );
						newNode->RotationOrder = eEulerXYZ; // always XYZ in SDK 2013.1!!!
					}

					m_nodeMap.Insert( boneName, m_nodes.Size() );
					globalBonesMap[i] = m_nodes.Size();
					m_nodes.PushBack( newNode );
				}
			}
		}

		// Mesh mapped materials
		red::HashMap< CName, Int32 > meshMappedMaterials;

		// Output vertices
		Uint32 firstChunkVertex = 0;
		FbxVector4* lControlPoints = newMesh->GetControlPoints();

		// Export mesh chunks
		for ( Uint32 i=0; i<numChunks; ++i )
		{
			if (0 == (lodMask & view.GetChunkLodMask(i)))
				continue;

			// Chunk material name
			CName materialName = RED_NAME_CONSTEXPR("Default");
			if (view.GetChunkMaterialCount(i) > 0)
				materialName = view.GetChunkMaterialName(i, 0);

			// Map chunk material to node material
			Int32 mappedMaterialIndex = -1;
			if (!meshMappedMaterials.Find(materialName, mappedMaterialIndex))
			{
				// Create new material
				mappedMaterialIndex = AppendPhongMaterial( lNode, RED_NAME( materialName ), FbxDouble3( math::DefaultRandom().Get<Float>(), math::DefaultRandom().Get<Float>(), math::DefaultRandom().Get<Float>() ) );
				meshMappedMaterials.Set(materialName, mappedMaterialIndex );
				FbxSurfacePhong* newMaterial = (FbxSurfacePhong*)lNode->GetMaterial( mappedMaterialIndex );

				// Extract material textures
				// TODO: this will not work any more with layered stuff, rewrite
				//ExtractTextures( newMaterial, material.Get(), bMaterialNameSet );
				//ExportXMLMaterialData( srcModel, material.Get(), chunk->m_materialID[0], newMaterial );
			}

			// Export vertices and normals
			const Uint32 numVertices = view.GetChunkVertexCount(i);
			const mesh::StreamViewRO<Vector3> srcPosition(view.GetChunkStreamData(i, MST_Position_3F));
			const mesh::StreamViewRO<Vector3> srcNormal(view.GetChunkStreamData(i, MST_Normal_3F));
			const mesh::StreamViewRO<Color> srcColor(view.GetChunkStreamData(i, MST_Color_U32));
			const mesh::StreamViewRO<Vector2> srcUV(view.GetChunkStreamData(i, MST_TexCoord0_2F));
			const mesh::StreamViewRO<Vector2> srcUV2(view.GetChunkStreamData(i, MST_TexCoord1_2F));
			const mesh::StreamViewRO<Color> srcSkinIndices(view.GetChunkStreamData(i, MST_SkinningIndices_4U8));
			const mesh::StreamViewRO<Vector4> srcSkinWeights(view.GetChunkStreamData(i, MST_SkinningWeights_4F));
			for ( Uint32 k=0; k<numVertices; ++k )
			{
				// Default crap
				const Uint32 controlPointIndex = firstChunkVertex+k;
				Vector4 vertexPosition(srcPosition[k]);
				Vector4 vertexNormal(srcNormal[k]);

				// Skinning
				if ( exportSkinning && srcSkinWeights.IsValid() && srcSkinIndices.IsValid() )
				{
					Matrix skinMatrix = Matrix::ZEROS();
					Float weightSum = 0.0f;

					for ( Uint32 z=0; z<4; ++z )
					{
						const Float w = srcSkinWeights[k][z];
						if ( w > 0.0f )
						{
							// find global bone for mesh bone
							const Uint32 mehsBoneIndex = srcSkinIndices[k].RGBA[z];

							// get global bone index
							const Int32 globalBoneIndex = globalBonesMap[mehsBoneIndex];
							if (globalBoneIndex == -1)
							{
								continue;
							}

							// Create cluster on first use of bone
							FbxCluster* cluster = boneClusters[ mehsBoneIndex ];
							if ( !cluster )
							{
								FbxNode* globalBone = m_nodes[globalBoneIndex];

								// Create cluster
								cluster = FbxCluster::Create( m_manager, "" );
								cluster->SetLink( globalBone );
								cluster->SetLinkMode( FbxCluster::eAdditive );
								cluster->SetAssociateModel( lNode );
								boneClusters[ mehsBoneIndex ] = cluster;

								// Register in model
								const FbxAMatrix lXMatrix = lNode->EvaluateGlobalTransform();
								cluster->SetTransformMatrix(lXMatrix);
								const FbxAMatrix lnXMatrix = globalBone->EvaluateGlobalTransform();
								cluster->SetTransformLinkMatrix(lnXMatrix);
							}

							// Add point influence
							cluster->AddControlPointIndex( controlPointIndex, w );
						}
					}
				}

				// Export vertex data
				lControlPoints[ controlPointIndex ] = FbxVector4( vertexPosition.X, vertexPosition.Y, vertexPosition.Z, 1.0f );
				lNormalElement->GetDirectArray().Add( FbxVector4( vertexNormal.X, vertexNormal.Y, vertexNormal.Z, 0.0f ) );

				if (srcUV.IsValid())
					lUVDiffuseElement->GetDirectArray().Add( FbxVector2( srcUV[k].X, srcUV[k].Y) );

				if (srcUV2.IsValid())
					lUVSecondDiffuseElement->GetDirectArray().Add( FbxVector2(srcUV2[k].X, 1.0f - srcUV2[k].Y) );

				if (srcColor.IsValid())
				{
					UINT32 color = srcColor[k].ToUint32();
					Uint8 r = (Uint8)color;
					Uint8 g = (Uint8)(color >> 8);
					Uint8 b = (Uint8)(color >> 16);
					Uint8 a = (Uint8)(color >> 24);
					lVecrtexColorElement->GetDirectArray().Add(FbxColor((Float)r / 255.f,
						(Float)g / 255.f, (Float)b / 255.f, (Float)a / 255.f));
				}
			}

			// Export polygons
			const Uint32 numTriangles = view.GetChunkIndexCount(i) / 3;
			const mesh::StreamViewRO<Uint16> srcIndices(view.GetChunkStreamData(i, MST_Index_U16));
			for ( Uint32 k=0; k<numTriangles; ++k )
			{
				newMesh->BeginPolygon( mappedMaterialIndex );
				newMesh->AddPolygon((Uint32)srcIndices[k * 3 + 2] + firstChunkVertex);
				newMesh->AddPolygon((Uint32)srcIndices[k * 3 + 1] + firstChunkVertex);
				newMesh->AddPolygon((Uint32)srcIndices[k * 3 + 0] + firstChunkVertex);
				newMesh->EndPolygon();
			}

			// Advance in-chunk vertex pointer
			firstChunkVertex += numVertices;
		}

		// Add mesh node to scene
		m_scene->GetRootNode()->AddChild( lNode );

		// Update skinning
		if (exportSkinning)
		{
			// Create skin modifier
			FbxGeometry* lPatchAttribute = (FbxGeometry*) lNode->GetNodeAttribute();
			FbxSkin* lSkin = FbxSkin::Create( m_manager, "" );
			for ( Uint32 i=0; i<boneClusters.Size(); ++i )
			{
				FbxCluster* cluster = boneClusters[i];
				if ( cluster != NULL )
				{
					lSkin->AddCluster(cluster);
				}
			}

			// Add skin
			lPatchAttribute->AddDeformer(lSkin);

			// Now list the all the link involve in the patch deformation
			if (lNode && lNode->GetNodeAttribute())
			{
				Uint32 lSkinCount=0;
				Uint32 lClusterCount=0;
				switch (lNode->GetNodeAttribute()->GetAttributeType())
				{
				case FbxNodeAttribute::eMesh:
				case FbxNodeAttribute::eNurbs:
				case FbxNodeAttribute::ePatch:
					{
						lSkinCount = ((FbxGeometry*)lNode->GetNodeAttribute())->GetDeformerCount(FbxDeformer::eSkin);
						for( Uint32 i=0; i<lSkinCount; ++i )
						{
							FbxSkin *lSkin=(FbxSkin*)((FbxGeometry*)lNode->GetNodeAttribute())->GetDeformer(i, FbxDeformer::eSkin);
							lClusterCount += lSkin->GetClusterCount();
						}
						break;
					}
				}

				if (lClusterCount)
				{
					// Again, go through all the skins get each cluster link and add them
					for (Uint32 i=0; i<lSkinCount; ++i)
					{
						FbxSkin *lSkin=(FbxSkin*)((FbxGeometry*)lNode->GetNodeAttribute())->GetDeformer(i, FbxDeformer::eSkin);
						lClusterCount = lSkin->GetClusterCount();
						for (Uint32 j=0; j<lClusterCount; ++j)
						{
							FbxNode* lClusterNode = lSkin->GetCluster(j)->GetLink();
							AddNodeRecursively(m_clusteredFbxNodes, lClusterNode);
						}
					}

					// Add the patch to the pose
					m_clusteredFbxNodes.Add(lNode);
				}
			}
		}
	}
}

Bool FBXExportScene::ExtractTexture( const IMaterial* srcMaterial, const String& paramName, ITexture*& outTexture )
{
	const CMaterialInstance* matInstance = Cast< CMaterialInstance >( srcMaterial );
	while ( matInstance )
	{
		for ( const MaterialParameterInstance& param : matInstance->GetParameters() )
		{
			const String x( param.GetName().AsChar() );
			if ( x.EqualsNC( paramName ) || paramName.Empty() )
			{
				TResRef< ITexture > texRef = 0;
				matInstance->ReadParameter( param.GetName(), texRef );
				if ( texRef )
				{
					const THandle< ITexture > texHandle = texRef.Get();
					if ( texHandle  && ( texHandle->IsA< CBitmapTexture >() || texHandle->IsA< CTextureArray >() ) )
					{
						outTexture = texHandle.Get();
						return true;
					}
				}
			}
		}
		matInstance = Cast< CMaterialInstance >( matInstance->GetBaseMaterial() );
	}

	// Not read
	return false;
}

Bool FBXExportScene::ExtractTextureData( const CBitmapTexture* bitmapTexture, String& outFileName )
{
	if ( !bitmapTexture->GetPath().IsValid() )
		return false;

	const red::StringView depotPath = bitmapTexture->GetPath().ToStringView();
	red::AbsolutePath savePath = red::paths::GetTempDirectory().AddDirPath( depotPath ).AppendFilePath( ".tga" );

	IExporter* exporter = IExporter::FindExporter( ClassID< CBitmapTexture >(), "tga" );

	if ( !exporter )
	{
		RED_LOG_SPAM( "No valid exporter for extension 'tga' for %hs", bitmapTexture->GetFriendlyName().AsChar() );
		return false;
	}

	IExporter::ExportOptions exportOptions;
	exportOptions.m_resource = HandleFromPtr( bitmapTexture );
	exportOptions.m_saveFileFormat = CFileFormat( "tga", String::EMPTY() );
	exportOptions.m_saveFilePath = savePath.AsString();

	RED_LOG_SPAM( "Exporting resource %hs as %hs."
		, exportOptions.m_resource->GetFriendlyName().AsChar()
		, exportOptions.m_saveFilePath.AsChar() );

	exporter->DoExport( exportOptions );
	outFileName = savePath.AsString();
	return true;
}

void FBXExportScene::ExtractTextures( FbxSurfacePhong* newMaterial, const IMaterial* srcMaterial, const Bool bMaterialNameSet )
{
	Bool materialNameSet = bMaterialNameSet;

	// Diffuse
	{
		// Get the source texture
		ITexture* diffuse = nullptr;
		if ( !ExtractTexture( srcMaterial, ("DiffuseMap"), diffuse ))
		{
			if ( !ExtractTexture( srcMaterial, ("Diffuse"), diffuse ) )
			{
				if ( !ExtractTexture( srcMaterial, ("DiffuseArray"), diffuse ) )
				{
					if ( !ExtractTexture( srcMaterial, ("tex_diffuse"), diffuse ) )
					{
						// extract any texture
						ExtractTexture( srcMaterial, String::EMPTY(), diffuse );
					}
				}
			}
		}

		// we're not gonna create local textures here as they're created in another step - we'll just use a path
		// for now I'll use the editor's bitmap exporter
		// Create local texture
		if ( NULL != diffuse )
		{
			// Change material name (will help with reimport)
			if (!materialNameSet)
			{
				const String diffuseFilePath( red::paths::ExtractFileStem( diffuse->GetPath().ToStringView() ) );
				newMaterial->SetName( diffuseFilePath.AsChar() );
				materialNameSet = true;
			}

			// Already exported ?
			FbxFileTexture* lTexture = NULL;
			if (m_textures.Find(diffuse->GetPath(), lTexture))
			{
				newMaterial->Diffuse.ConnectSrcObject(lTexture);
			}
			else if ( diffuse->IsA< CBitmapTexture >() )
			{
				// Create temporary file name
				String textureFilePath;
				if ( ExtractTextureData( Cast< CBitmapTexture >( diffuse ), textureFilePath ) )
				{
					lTexture = FbxFileTexture::Create( m_scene, "Diffuse Texture" );

					// Setup texture
					lTexture->SetFileName( textureFilePath.AsChar() );
					lTexture->SetTextureUse( FbxTexture::eStandard);
					lTexture->SetMappingType( FbxTexture::eUV );
					lTexture->SetMaterialUse( FbxFileTexture::eModelMaterial );
					lTexture->SetSwapUV(false);
					lTexture->SetTranslation(0.0, 0.0);
					lTexture->SetScale(1.0, 1.0);
					lTexture->SetRotation(0.0, 0.0);
					lTexture->UVSet.Set( "DiffuseUV");
					m_scene->AddTexture( lTexture );

					m_textures.Set(diffuse->GetPath(), lTexture);

					// Connect to material
					newMaterial->Diffuse.ConnectSrcObject(lTexture);
				}
			}
		}
	}

	// Normal
	{
		// Get the source texture
		ITexture* normal = nullptr;
		if ( !ExtractTexture( srcMaterial, ("NormalMap"), normal ))
		{
			if ( !ExtractTexture( srcMaterial, ("Normal"), normal ) )
			{
				if ( !ExtractTexture( srcMaterial, ("NormalArray"), normal ) )
				{
					if ( !ExtractTexture( srcMaterial, ("tex_normal"), normal ) )
					{
						normal = nullptr;
					}
				}
			}
		}

		// Create local texture
		if ( nullptr != normal )
		{
			// Change material name (will help with reimport)
			if (!materialNameSet)
			{
				const String fileName( red::paths::ExtractFileStem( normal->GetPath().ToStringView() ) );
				newMaterial->SetName( fileName.AsChar() );
				materialNameSet = true;
			}

			FbxFileTexture* lTexture = nullptr;
			if (m_textures.Find(normal->GetPath(), lTexture))
			{
				newMaterial->NormalMap.ConnectSrcObject(lTexture);
			}
			else if ( normal->IsA< CBitmapTexture >() )
			{
				// Create temporary file name
				String textureFilePath;
				if ( ExtractTextureData( Cast< CBitmapTexture >( normal ), textureFilePath ) )
				{
					FbxFileTexture* lTexture = FbxFileTexture::Create( m_scene, "Normal Texture" );

					// Setup texture
					lTexture->SetFileName( textureFilePath.AsChar() );
					lTexture->SetTextureUse( FbxTexture::eBumpNormalMap);
					lTexture->SetMappingType( FbxTexture::eUV );
					lTexture->SetMaterialUse( FbxFileTexture::eModelMaterial );
					lTexture->SetSwapUV(false);
					lTexture->SetTranslation(0.0, 0.0);
					lTexture->SetScale(1.0, 1.0);
					lTexture->SetRotation(0.0, 0.0);
					lTexture->UVSet.Set( "DiffuseUV");
					m_scene->AddTexture( lTexture );

					m_textures.Set(normal->GetPath(), lTexture);

					// Connect to material
					newMaterial->NormalMap.ConnectSrcObject(lTexture);
				}
			}
		}
	}
}
#endif
