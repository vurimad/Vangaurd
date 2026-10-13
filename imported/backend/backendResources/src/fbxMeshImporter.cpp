/**
* Copyright (c) 2007-2020 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "fbxCommon.h"
#include "fbxMeshBuilder.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/resource/include/xmlDocument.h"
#include "../../../common/resourceMaterial/include/material.h"
#include "../../../common/resourceMaterial/include/materialInstance.h"
#include "../../../common/resourceMaterial/include/texture.h"
#include "../../../common/resourceMaterial/include/textureArray.h"
#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/resourceMesh/include/meshUtils.h"
#include "../../../backend/backendEngine/include/importer.h"
#include "../../../backend/backendMesh/include/materialLayersLoader.h"
#include "../../../backend/backendMesh/include/meshDataBuilder.h"
#include "../../../backend/backendMesh/include/meshDataView.h"


class FBXMeshImportOptions : public ResourceImportOptions
{
	RTTI_DECLARE_TYPE( FBXMeshImportOptions );
	RED_BASE_CLASS( ResourceImportOptions );
public:
	FBXMeshImportOptions()
		: m_importAsSkinnedMesh( false )
		, m_generateCollisionIfMissing( false )
	{}

	virtual ~FBXMeshImportOptions() {}

	virtual Bool NeedOptions() const override { return true; }

	virtual bool ParseFromCommandLine( const red::CommandLine& stringOptions ) override
	{
		m_importAsSkinnedMesh = stringOptions.HasOption( "skinnedMesh" );
		m_generateCollisionIfMissing = stringOptions.HasOption( "generateCollisionIfMissing" );
		return true;
	}

	Bool			m_importAsSkinnedMesh;
	Bool			m_generateCollisionIfMissing;
};

RTTI_BEGIN_TYPE( FBXMeshImportOptions );
	RTTI_PARENT_TYPE( ResourceImportOptions );
	RTTI_PROPERTY( m_importAsSkinnedMesh ).editable();
	RTTI_PROPERTY( m_generateCollisionIfMissing ).editable();
RTTI_END_TYPE();


#define MAX_LOD_INDEX 6

class CFBXMeshImporter : public IImporter
{
	RTTI_DECLARE_TYPE( CFBXMeshImporter );

public:
	Bool			m_importAsSkinnedMesh;
	Bool			m_generateCollisionIfMissing;

public:
	FbxManager*		m_manager;

public:
	CFBXMeshImporter();
	virtual ImportResult DoImport( THandle< CResource > inputResource, const THandle<ResourceImportOptions>& options ) override;
	virtual THandle<ResourceImportOptions> CreateOptions( const red::AbsolutePath& sourcePath ) override;

private:
	void WalkNodes(const FbxNode* node, FBXMeshBuilder& builder);
	void CreateMeshChunks(const Uint32 numBones, const Uint32 pointCount, const FBXMeshBuilder::Triangle* triList, mesh::DataBuilder& outInfo, const Uint8 lodMask);
	void ImportXMLMaterialsData( xml::Document& doc, const xml::Document::Node* docNode, CMesh* retMesh, mesh::DataView& view );
};

RTTI_BEGIN_TYPE( CFBXMeshImporter )
	RTTI_PARENT_TYPE(IImporter)
RTTI_END_TYPE()



CFBXMeshImporter::CFBXMeshImporter()
	: m_importAsSkinnedMesh( false )
	, m_generateCollisionIfMissing( false )
{
	m_resourceClass = ClassID< CMesh >();
	m_formats.PushBack( CFileFormat( "fbx", "Autodesk FBX" ) );
}

THandle<ResourceImportOptions> CFBXMeshImporter::CreateOptions( const red::AbsolutePath& sourcePath )
{
	return CreateHandle<FBXMeshImportOptions>();
}

// TODO move out
template< typename T >
static Bool TryGetAttributeValue( const xml::Document& doc, const xml::Document::Node* node, const AnsiChar* attributeName, T& value )
{
	const auto attribute = doc.GetFirstAttribute( node, attributeName );
	if ( !attribute )
	{
		return false;
	}

	const auto attributeValue = doc.GetAttributeValue( attribute );
	if ( !attributeValue )
	{
		return false;
	}

	T tempValue;
	if ( ::FromString( attributeValue, tempValue ) )
	{
		value = tempValue;
		return true;
	}

	return false;
}

ImportResult CFBXMeshImporter::DoImport( THandle< CResource > inputResource, const THandle<ResourceImportOptions>& options )
{
	// get mesh to import into
	auto meshData = Cast< CMesh >( inputResource );
	if ( !meshData)
	{
		return ImportResult::Error( "Core: Unable to import mesh into something that is not a mesh" );
	}

	auto fbxImportOptions = Cast< FBXMeshImportOptions >( options );
	RED_FATAL_ASSERT( fbxImportOptions != nullptr );
	m_importAsSkinnedMesh = fbxImportOptions->m_importAsSkinnedMesh;
	m_generateCollisionIfMissing = fbxImportOptions->m_generateCollisionIfMissing;

	// Load scene
	const red::AbsolutePath& sourceFilePath = options->GetSourcePath();
	FBXImportScene* scene = FBXImportScene::Load( sourceFilePath.AsString() );
	if ( !scene )
	{
		return ImportResult::Error( "CFBXMeshImporter: Unable to load scene from '%hs'", sourceFilePath.AsChar() );
	}

	// Walk the input nodes
	mesh::DataBuilder initInfo;
	//initInfo.m_lodDegenerationStartDistance = -1.0f;

	// Walk the nodes - collecting meshes
	FBXMeshBuilder builder;
	FbxNode* rootNode = scene->m_scene->GetRootNode();
	WalkNodes(rootNode, builder);

	// No data imported
	if ( builder.m_lods.Empty() )
	{
		return ImportResult::Error( "CFBXMeshImporter: No geometry data in file '%hs'", sourceFilePath.AsChar() );
	}

	// Automatic LOD distances
	Float lodDistances[MAX_LOD_INDEX+1] = { 0.0f, 10.0f, 20.0f, 30.0f, 40.0f, 80.0f, 120.0f };
	Float autohideDistance = -1.0f;

	String xmlContent;
	red::AbsolutePath xmlPath = red::utils::ReplaceExtension( sourceFilePath, "xml" );
	red::LoadFileToString( xmlPath, xmlContent );

	red::UniquePtr< xml::Document > doc( xml::Document::Create( xmlContent.AsChar() ) );
	Uint32 lodCounter = 0;
	Bool xmlWellFormed = true;

	const auto meshNode = doc->GetRootNode();
	if ( meshNode && !red::Strcmp( doc->GetNodeName( meshNode ), "mesh" ) )
	{
		const auto dataNode = doc->GetFirstNode( meshNode, "mesh_data" );
		if ( dataNode )
		{
			TryGetAttributeValue( *doc, dataNode, "autohideDistance", autohideDistance );

			const auto lodNode = doc->GetFirstNode( dataNode, "LODs" );
			if ( lodNode )
			{
				for ( xml::NodeIterator infoIterator( *doc, lodNode, "LOD_info" ); infoIterator; ++infoIterator )
				{
					if ( lodCounter > MAX_LOD_INDEX )
					{
						break;
					}

					TryGetAttributeValue( *doc, infoIterator.GetCurrent(), "distance", lodDistances[lodCounter++] );
				}
			}
		}
	}
	else
	{
		xmlWellFormed = false;
	}

	// Copy material lists
	{
		mesh::DataView view(*meshData);
		view.RemoveMaterials();

		if (xmlWellFormed)
		{
			ImportXMLMaterialsData( *doc, meshNode, meshData.Get(), view );
		}
		else
		{
			const auto defaultMaterialPath = res::ResourcePath::Build( "engine\\materials\\metal_base.remt" );
			const auto defaultMaterial = Cast< IMaterial >(GResourceLoader->IssueLoadingRequest(defaultMaterialPath)->WaitUntilLoaded());

			// default empty materials
			for (Uint32 i = 0; i < builder.m_materials.Size(); ++i)
			{
				const FBXMeshBuilder::Material& m = builder.m_materials[i];
				view.AddMaterial(defaultMaterial, RED_NAME(m.m_name));
			}
		}

#if defined( VANGUARD_ASSET_HOST )
			view.Apply(true);
#else
			view.Apply();
#endif
	}

	// Copy bones
	RED_LOG_SPAM( "CFBXMeshImporter: Found %d bones", builder.m_bones.Size() );
	red::DynArray< Uint32 > rigMapping( builder.m_bones.Size() );
	for (Uint32 i=0; i<builder.m_bones.Size(); ++i)
	{
		const FBXMeshBuilder::Bone& b = builder.m_bones[i];

		mesh::DataBuilder::UnpackedBone boneInfo;
		boneInfo.m_name = RED_NAME(b.m_name);

		Matrix t = b.m_localToWorld;
		t[0].Normalize3();
		t[1].Normalize3();
		t[2].Normalize3();		
		boneInfo.m_rig = t.FullInverted();

		initInfo.m_bones.PushBack(boneInfo);
	}

	// Build the chunks
	Uint32 realLodIndex = 0;
	for (Uint32 lodIndex=0; lodIndex<=MAX_LOD_INDEX; ++lodIndex)
	{
		// Get lod data
		FBXMeshBuilder::LODLevel* lod = builder.GetLodLevel(lodIndex);
		if (lod->m_triangles.Empty())
		{
			continue;
		}

		// Create LOD level
		auto& outLevel = initInfo.m_lods.EmplaceBack();
		outLevel.m_distance = lodDistances[realLodIndex];

		// Build chunks
		lod->GenerateConnectivity();

		// Update tangent space vectors (only triangles without valid tangent space)
		lod->CalculateNormals();
		lod->CalculateTangentSpace();

		// Build triangle lists
		lod->BuildTriangleLists();

		// Convert each triangle list into actual mesh chunks
		for (Uint32 i=0; i<lod->m_materialTriangles.Size(); ++i)
		{
			const FBXMeshBuilder::Triangle* triList = lod->m_materialTriangles[i];
			if (NULL != triList)
			{
				const auto lodMask = 1 << lodIndex;
				CreateMeshChunks(initInfo.m_bones.Size(), lod->m_points.GetNumPoints(), triList, initInfo, lodMask);
			}
		}

		// Done
		realLodIndex += 1;
	}

	// build geometry
#if defined( VANGUARD_ASSET_HOST )
	mesh::DataBuilder::FlushOptions flushOptions;
	flushOptions.m_uploadResource = false;
	flushOptions.m_updateStreaming = false;
	initInfo.Flush(*meshData, flushOptions);
#else
	initInfo.Flush(*meshData);
#endif

	// imported
	return ImportResult::Success( meshData );
}

void CFBXMeshImporter::ImportXMLMaterialsData( xml::Document& doc, const xml::Document::Node* docNode, CMesh* retMesh, mesh::DataView& view )
{
	const auto defaultMaterialPath = res::ResourcePath::Build( "engine\\materials\\metal_base.remt" );
	const auto defaultMaterial = Cast< IMaterial >(GResourceLoader->IssueLoadingRequest(defaultMaterialPath)->WaitUntilLoaded());
	const auto materialsNode = doc.GetFirstNode( docNode, "materials" );

	if ( !materialsNode )
	{
		RED_LOG_ERROR( "no materials node found" );
		return;
	}

	for ( xml::NodeIterator materialIterator( doc, materialsNode, "material" ); materialIterator; ++materialIterator )
	{
		const auto materialNode = materialIterator.GetCurrent();
		const auto nameAttribute = doc.GetFirstAttribute( materialNode, "name" );

		if ( !nameAttribute )
		{
			RED_LOG_ERROR( "'material' node 'name' attribute is missing" );
			continue;
		}

		const auto materialName = RED_NAME( doc.GetAttributeValue( nameAttribute ) );
		if ( -1 != mesh::utils::FindMaterialIndex( view, materialName ) )
		{
			// material already defined
			continue;
		}

		const auto localAttribute = doc.GetFirstAttribute( materialNode, "local" );
		if ( !localAttribute )
		{
			RED_LOG_ERROR( "'material' node 'local' attribute is missing" );
			continue;
		}

		Bool localInstance = true;
		if ( !::FromString( doc.GetAttributeValue( localAttribute ), localInstance ) )
		{
			RED_LOG_ERROR( "'material' node 'local' attribute is malformed" );
			continue;
		}

		const auto baseAttribute = doc.GetFirstAttribute( materialNode, "base" );
		if ( !baseAttribute )
		{
			RED_LOG_ERROR( "'material' node 'base' attribute is missing" );
			continue;
		}

		const String baseMaterialPath = doc.GetAttributeValue( baseAttribute );
		auto baseMaterial = Cast< IMaterial >( GResourceLoader->IssueLoadingRequest( res::ResourcePath::Build( baseMaterialPath ) )->WaitUntilLoaded() );
		
		if ( !baseMaterial )
		{
			baseMaterial = defaultMaterial;
		}

		if ( localInstance )
		{
			auto instance = CreateHandle< CMaterialInstance >( retMesh, baseMaterial );
			view.AddMaterial( instance, materialName );
			baseMaterial = instance;
		}

		for ( xml::NodeIterator parameterIterator( doc, materialNode, "param" ); parameterIterator; ++parameterIterator )
		{
			const auto parameterNode = parameterIterator.GetCurrent();
			const auto parameterTypeAttribute = doc.GetFirstAttribute( parameterNode, "type" );
			if ( !parameterTypeAttribute )
			{
				RED_LOG_ERROR( "'param' node 'type' attribute is missing" );
				continue;
			}
			const auto parameterValueAttribute = doc.GetFirstAttribute( parameterNode, "value" );
			if ( !parameterValueAttribute )
			{
				RED_LOG_ERROR( "'param' node 'type' attribute is missing" );
				continue;
			}
			const auto parameterNameAttribute = doc.GetFirstAttribute( parameterNode, "name" );
			if ( !parameterNameAttribute )
			{
				RED_LOG_ERROR( "'param' node 'name' attribute is missing" );
				continue;
			}

			const String parameterType = doc.GetAttributeValue( parameterTypeAttribute );
			const auto type = GetRttiSystem().FindType( RED_NAME_NOREG( parameterType ) );
			if ( !type )
			{
				RED_LOG_ERROR( "material parameter type not found" );
				continue;
			}

			const String parameterValue = doc.GetAttributeValue( parameterValueAttribute );
			const String parameterName = doc.GetAttributeValue( parameterNameAttribute );

			// TODO use scoped buffer
			const auto parameterData = RED_ALLOCATE_ALIGNED( red::PoolBackend, type->GetSize(), DEFAULT_MATERIAL_PARAMETER_INSTANCE_ALIGNMENT );
			red::Memzero( parameterData, type->GetSize() );
			type->Construct( parameterData );

			// FIXME FromString is deprecated
			if ( type->FromString( parameterData, parameterValue ) )
			{
				baseMaterial->WriteParameterRaw( RED_NAME( parameterName ), type, parameterData );
			}

			type->Destruct( parameterData );
			// Free memory allocated for parameter	
			RED_FREE( red::PoolBackend, parameterData );
		}
	}
}

void CFBXMeshImporter::WalkNodes(const FbxNode* node, FBXMeshBuilder& builder)
{
	// Get the local to parent matrix
	const FbxAMatrix matrix = ((FbxNode*)node)->EvaluateGlobalTransform(FBXSDK_TIME_INFINITE, FbxNode::eSourcePivot, false, true);
	Matrix localToWorld;
	localToWorld.SetIdentity();
	localToWorld[0].X = (Float)matrix.Get(0,0);
	localToWorld[0].Y = (Float)matrix.Get(0,1);
	localToWorld[0].Z = (Float)matrix.Get(0,2);
	localToWorld[1].X = (Float)matrix.Get(1,0);
	localToWorld[1].Y = (Float)matrix.Get(1,1);
	localToWorld[1].Z = (Float)matrix.Get(1,2);
	localToWorld[2].X = (Float)matrix.Get(2,0);
	localToWorld[2].Y = (Float)matrix.Get(2,1);
	localToWorld[2].Z = (Float)matrix.Get(2,2);
	localToWorld[3].X = (Float)matrix.Get(3,0);
	localToWorld[3].Y = (Float)matrix.Get(3,1);
	localToWorld[3].Z = (Float)matrix.Get(3,2);

	// Recurse to child nodes
	const Uint32 numChildren = node->GetChildCount();
	for (Uint32 i=0; i<numChildren; ++i)
	{
		const FbxNode* childNode = node->GetChild(i);
		if (childNode != NULL)
		{
			WalkNodes(childNode, builder);
		}
	}

	if (node->GetNodeAttribute() != NULL)
	{
		if (node->GetNodeAttribute()->GetAttributeType() == FbxNodeAttribute::eMesh)
		{
			const FbxMesh* mesh = (const FbxMesh*)node->GetNodeAttribute();

			((FbxMesh*)mesh)->ApplyPivot();

			const String nodeName( node->GetName() );
			String nodeNameLower = nodeName;
			nodeNameLower.ToLower();
	
			if ( nodeNameLower.EndsWith( ("_tri") ) )
			{
				RED_LOG_SPAM( "CFBXMeshImporter: Found triangle collision mesh data on node '%hs'", node->GetName() );
				builder.ExtractCollisionMesh( mesh, localToWorld, FBXMeshBuilder::CollisionMesh::CT_Trimesh );
			}
			else if ( nodeNameLower.EndsWith(("_coll") ) || nodeNameLower.EndsWith( ("_convex") ) 
				|| nodeNameLower.EndsWith( ("_col") ) || nodeNameLower.EndsWith( ("_cvx") ) )
			{
				RED_LOG_SPAM( "CFBXMeshImporter: Found convex collision mesh data on node '%hs'", node->GetName() );
				builder.ExtractCollisionMesh( mesh, localToWorld, FBXMeshBuilder::CollisionMesh::CT_Convex );
			}
			else
			{
				Uint32 lodIndex = 0;

				// extract LOD index
				if ( nodeNameLower.EndsWith( ("_lod0") ))
				{
					lodIndex = 0;
				}
				else if ( nodeNameLower.EndsWith( ("_lod1") ) )
				{
					lodIndex = 1;
				}
				else if ( nodeNameLower.EndsWith( ("_lod2") ) )
				{
					lodIndex = 2;
				}
				else if ( nodeNameLower.EndsWith( ("_lod3") ) )
				{
					lodIndex = 3;
				}
				else if ( nodeNameLower.EndsWith( ("_lod4") ) )
				{
					lodIndex = 4;
				}
				else if ( nodeNameLower.EndsWith( ("_lod5") ) )
				{
					lodIndex = 5;
				}
				else if ( nodeNameLower.EndsWith( ("_lod6") ) )
				{
					lodIndex = 6;
				}

				RED_LOG_SPAM( "CFBXMeshImporter: Found mesh data on node '%hs' (LOD%d)", node->GetName(), lodIndex );
				if (m_importAsSkinnedMesh)
				{
					builder.ExtractMesh(lodIndex, mesh, localToWorld, node);
				}
				else
				{
					builder.ExtractMesh(lodIndex, mesh, localToWorld);
				}
			}
		}
	}
}

struct FBXVertex
{
	Float m_position[3];
	Uint8 m_indices[4];
	Float m_weights[4];
	Float m_normal[3];
	Uint32 m_color;
	Float m_uv0[2];
	Float m_uv1[2];
	Float m_tangent[3];
	Float m_binormal[3];
};

static inline bool MatchVertex(const FBXVertex& a, const FBXVertex& b)
{
	return 0 == red::Memcmp(&a, &b, sizeof(a));
}

void CFBXMeshImporter::CreateMeshChunks(const Uint32 numBones, const Uint32 pointCount, const FBXMeshBuilder::Triangle* triList, mesh::DataBuilder& outInfo, const Uint8 lodMask)
{
	const Uint32 maxVerticesPerChunk = 65535;
	const Uint32 maxBonesPerChunk = 60;

	while (triList != NULL)
	{
		// Add chunk to lod
		const Uint16 chunkIndex = (Uint16)outInfo.m_chunks.Size();

		// Create new chunk
		auto& outChunk = outInfo.m_chunks.EmplaceBack();
		outChunk.m_materials.PushBack(RED_NAME(triList->m_material->m_name));
		outChunk.m_lodMask = lodMask;

		red::DynArray<FBXVertex> vertices;
		red::DynArray<Uint16> indices;

		vertices.Reserve(65536 + 8);
		indices.Reserve(65536 * 3);

		// Flags
		Uint8 hasUV1 = 0;
		Uint8 hasColor = 0;
		Uint8 hasSkinning = 0;

		// Per point list of matching vertices
		red::DynArray<red::DynArray<Uint32>> generatedVertices;
		generatedVertices.Resize(pointCount);

		// Bone mapping
		red::DynArray<Int32> boneMapping;
		boneMapping.Resize(numBones);
		for (Uint32 j=0; j<numBones; ++j)
		{
			boneMapping[j] = -1;
		}

		// Add triangles
		while (triList != NULL)
		{
			const Uint32 curVertexCount = vertices.Size();
			const Uint32 curIndexCount = indices.Size();

			// add vertices
			Uint32 rawIndices[3];
			for (Uint32 j = 0; j < 3; ++j)
			{
				const FBXMeshBuilder::Vertex& v = triList->m_v[j];

				// create export vertex
				FBXVertex exportVertex;
				exportVertex.m_position[0] = v.m_pos.X;
				exportVertex.m_position[1] = v.m_pos.Y;
				exportVertex.m_position[2] = v.m_pos.Z;
				exportVertex.m_normal[0] = v.m_normal.X;
				exportVertex.m_normal[1] = v.m_normal.Y;
				exportVertex.m_normal[2] = v.m_normal.Z;
				exportVertex.m_tangent[0] = v.m_tangent.X;
				exportVertex.m_tangent[1] = v.m_tangent.Y;
				exportVertex.m_tangent[2] = v.m_tangent.Z;
				exportVertex.m_binormal[0] = v.m_bitangent.X;
				exportVertex.m_binormal[1] = v.m_bitangent.Y;
				exportVertex.m_binormal[2] = v.m_bitangent.Z;
				exportVertex.m_uv0[0] = v.m_uv0.X;
				exportVertex.m_uv0[1] = v.m_uv0.Y;
				exportVertex.m_uv1[0] = v.m_uv1.X;
				exportVertex.m_uv1[1] = v.m_uv1.Y;
				exportVertex.m_weights[0] = v.m_weights[0];
				exportVertex.m_weights[1] = v.m_weights[1];
				exportVertex.m_weights[2] = v.m_weights[2];
				exportVertex.m_weights[3] = v.m_weights[3];

				Color cColor;

				cColor.R = (Uint8)(v.m_color.X * 255.f);
				cColor.G = (Uint8)(v.m_color.Y * 255.f);
				cColor.B = (Uint8)(v.m_color.Z * 255.f);
				cColor.A = (Uint8)(v.m_color.W * 255.f);

				exportVertex.m_color = cColor.ToUint32();

				// map bone indices
				for (Uint32 k=0; k<4; ++k)
				{
					if (v.m_weights[k] > 0.0f)
					{
						exportVertex.m_indices[k] = v.m_indices[ k ];
						hasSkinning = true;
					}
					else
					{
						exportVertex.m_indices[k] = 0;
					}
				}

				// try to match vertex
				Bool matched = false;
				red::DynArray<Uint32>& verticesForPoint = generatedVertices[v.m_point];
				for (Uint32 k=0; k<verticesForPoint.Size(); ++k)
				{
					const Uint32 vertexIndex = verticesForPoint[k];
					const FBXVertex& testVertex = vertices[vertexIndex];
					if (MatchVertex(exportVertex, testVertex))
					{
						matched = true;
						rawIndices[j] = vertexIndex;
						break;
					}
				}

				// Vertex data not matched with any existing vertex, create new vertex
				if (!matched)
				{
					const Uint32 vertexIndex = vertices.Size();
					vertices.PushBack(exportVertex);
					verticesForPoint.PushBack(vertexIndex);
					rawIndices[j] = vertexIndex;
				}
			}

			// chunk limit was reached
			if (vertices.Size() >= maxVerticesPerChunk)
			{
				vertices.Resize(curVertexCount);
				indices.Resize(curIndexCount);
				break;
			}

			// Create indices
				RED_FATAL_ASSERT(rawIndices[0] <= maxVerticesPerChunk);
				RED_FATAL_ASSERT(rawIndices[1] <= maxVerticesPerChunk);
				RED_FATAL_ASSERT(rawIndices[2] <= maxVerticesPerChunk);
			indices.PushBack((Uint16)rawIndices[2]);
			indices.PushBack((Uint16)rawIndices[1]);
			indices.PushBack((Uint16)rawIndices[0]);

			// Go to next triangle
			triList = triList->m_next;
		}
		
		// Assign vertex type
		outChunk.m_vertexType = hasSkinning ? MVT_SkinnedMesh : MVT_StaticMesh;

		// Save memory
		vertices.Shrink();
		indices.Shrink();

		outChunk.m_streams.Resize(10);
		outChunk.m_streams[0].m_type = MST_Position_3F;
		outChunk.m_streams[1].m_type = MST_Normal_3F;
		outChunk.m_streams[2].m_type = MST_Tangent_3F;
		outChunk.m_streams[3].m_type = MST_Binormal_3F;
		outChunk.m_streams[4].m_type = MST_TexCoord0_2F;
		outChunk.m_streams[5].m_type = MST_TexCoord1_2F;
		outChunk.m_streams[6].m_type = MST_Color_U32;
		outChunk.m_streams[7].m_type = MST_SkinningWeights_4F;
		outChunk.m_streams[8].m_type = MST_SkinningIndices_4U8;
		outChunk.m_streams[9].m_type = MST_Index_U16;

		mesh::StreamWriter<Vector3> positions(outChunk.m_streams[0].m_data, vertices.Size());
		mesh::StreamWriter<Vector3> normals(outChunk.m_streams[1].m_data, vertices.Size());
		mesh::StreamWriter<Vector3> tangents(outChunk.m_streams[2].m_data, vertices.Size());
		mesh::StreamWriter<Vector3> binormals(outChunk.m_streams[3].m_data, vertices.Size());
		mesh::StreamWriter<Vector2> uv0(outChunk.m_streams[4].m_data, vertices.Size());
		mesh::StreamWriter<Vector2> uv1(outChunk.m_streams[5].m_data, vertices.Size());
		mesh::StreamWriter<Color> colors(outChunk.m_streams[6].m_data, vertices.Size());
		mesh::StreamWriter<Float> skinningWeights(outChunk.m_streams[7].m_data, 4 * vertices.Size());
		mesh::StreamWriter<Uint8> skinningIndices(outChunk.m_streams[8].m_data, 4 * vertices.Size());
		mesh::StreamWriter<Uint16> indexStream(outChunk.m_streams[9].m_data, indices.Size());

		for (auto& vertex : vertices)
		{
			positions.Write(vertex.m_position);
			normals.Write(vertex.m_normal);
			tangents.Write(vertex.m_tangent);
			binormals.Write(vertex.m_binormal);
			uv0.Write(vertex.m_uv0);
			uv1.Write(vertex.m_uv1);
			colors.Write(Color(vertex.m_color));

			skinningWeights.Write(vertex.m_weights[0]);
			skinningWeights.Write(vertex.m_weights[1]);
			skinningWeights.Write(vertex.m_weights[2]);
			skinningWeights.Write(vertex.m_weights[3]);

			skinningIndices.Write(vertex.m_indices[0]);
			skinningIndices.Write(vertex.m_indices[1]);
			skinningIndices.Write(vertex.m_indices[2]);
			skinningIndices.Write(vertex.m_indices[3]);
		}

		for (auto& index : indices)
		{
			indexStream.Write(index);
		}
	}
}
