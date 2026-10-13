/**
* Copyright (c) 2008 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "../../../backend/backendEngine/include/importer.h"
#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/resourceMaterial/include/materialInstance.h"
#include "../../../common/redMemory/include/sharedPtr.h"
#include "../../../common/redReflection/include/editableView.h"
#include "smdParser.h"

#include "../../../common/resourceMaterial/include/texture.h"

#include "../../../backend/backendMesh/include/meshDataView.h"
#include "../../../backend/backendMesh/include/meshDataBuilder.h"

#include "../../../backend/backendMesh/include/meshRenderBlobUtils.h"

using red::DynArray;

/// SMD format importer
class CSMDMeshImporter : public IImporter
{
	RTTI_DECLARE_TYPE(CSMDMeshImporter);

public:
	CSMDMeshImporter();
	virtual ImportResult DoImport( THandle< CResource > inputResource, const THandle<ResourceImportOptions>& options ) override;
};

RTTI_BEGIN_TYPE(CSMDMeshImporter)
RTTI_PARENT_TYPE(IImporter)
RTTI_END_TYPE()



CSMDMeshImporter::CSMDMeshImporter()
{
	m_resourceClass = ClassID< CMesh >();
	m_formats.PushBack(CFileFormat("smd", "Source Mesh Data"));
}

ImportResult CSMDMeshImporter::DoImport( THandle< CResource > inputResource, const THandle<ResourceImportOptions>& options )
{
	// get mesh to import into
	THandle< CMesh > mesh = Cast< CMesh >( inputResource );
	if (!mesh)
	{
		return ImportResult::Error("Core: Unable to import mesh into something that is not a mesh");
	}

	// load data
	const red::AbsolutePath& sourceFilePath = options->GetSourcePath();

	// parse data
	smd::FileData data;
	String errorText;
	if ( !data.Parse(sourceFilePath, errorText))
	{
		return ImportResult::Error("Core: Unable to load SMD data from '%hs': %hs", sourceFilePath.AsChar(), errorText.AsChar());
	}

	mesh::DataBuilder builder;

	// add bones
	if (data.m_nodes.Size() && data.m_frames.Size())
	{
		const auto numBones = data.m_nodes.Size();

		// compute local space bone matrices
		red::DynArray<Matrix> localBoneMatrices(numBones);
		for (Uint32 i = 0; i < numBones; ++i)
		{
			const auto& nodePos = data.m_frames[0].m_keys[i];
			localBoneMatrices[i] = nodePos.m_rotation.ToMatrix();
			localBoneMatrices[i].SetTranslation(nodePos.m_position);
		}

		// compute model space bone matrices
		red::DynArray<Matrix> modelBoneMatrices(numBones);
		for (Uint32 i = 0; i < numBones; ++i)
		{
			const auto& nodeInfo = data.m_nodes[i];
			if (nodeInfo.m_parent != -1)
			{
				modelBoneMatrices[i] = localBoneMatrices[i] * modelBoneMatrices[nodeInfo.m_parent];
			}
			else
			{
				modelBoneMatrices[i] = localBoneMatrices[i];
			}
		}

		// create final rig
		for (Uint32 i = 0; i < numBones; ++i)
		{
			const auto& nodeInfo = data.m_nodes[i];

			mesh::DataBuilder::UnpackedBone boneInfo;
			boneInfo.m_name = RED_NAME(nodeInfo.m_name);
			boneInfo.m_rig = modelBoneMatrices[i].FullInverted();;
			builder.m_bones.PushBack(boneInfo);
		}
	}

	// add LODs
	{
		mesh::DataBuilder::UnpackedLOD lodInfo;
		lodInfo.m_distance = 0.0f;
		builder.m_lods.PushBack(lodInfo);

		// add chunks, one for each material
		for (const auto& mat : data.m_materials)
		{
			// no triangle for this material
			if ( !mat.m_triangles )
				continue;

			// create chunk
			mesh::DataBuilder::UnpackedChunk chunkInfo;
			chunkInfo.m_materials.PushBack(RED_NAME(mat.m_name));
			chunkInfo.m_vertexType = MVT_SkinnedMesh;
			chunkInfo.m_lodMask = 1;
			
			/*
			const auto* tri = mat.m_triangles;
			while (tri != nullptr)
			{
				for (Uint32 i = 0; i < 3; ++i)
				{
					const auto& src = tri->m_verts[i];

					SMeshVertex vertex;
					memset(&vertex, 0, sizeof(vertex));
					vertex.m_position[0] = src.m_pos.X;
					vertex.m_position[1] = src.m_pos.Y;
					vertex.m_position[2] = src.m_pos.Z;
					vertex.m_normal[0] = src.m_normal.X;
					vertex.m_normal[1] = src.m_normal.Y;
					vertex.m_normal[2] = src.m_normal.Z;
					vertex.m_uv0[0] = src.m_uv.X;
					vertex.m_uv0[1] = src.m_uv.Y;
					vertex.m_color = 0xFFFFFFFF;

					for (Uint32 j = 0; j < src.m_numSkin; ++j)
					{
						vertex.m_indices[j] = (Uint8)src.m_skin[j].m_bone;
						vertex.m_weights[j] = src.m_skin[j].m_weight;
					}

					chunk.m_vertices.PushBack(vertex);
				}

				tri = tri->m_next;
			}

			// TODO: proper support for 32-bit indices
			chunk.m_indices.Reserve(chunk.m_numIndices);
			for (Uint32 i = 0; i < chunk.m_numIndices; i += 3)
			{
				chunk.m_indices.PushBack((Uint16)i + 2);
				chunk.m_indices.PushBack((Uint16)i + 1);
				chunk.m_indices.PushBack((Uint16)i + 0);
			}*/
		}
	}

	// load base material
	auto baseMaterial = Cast< IMaterial >(GResourceLoader->IssueLoadingRequest(res::ResourcePath::Build("engine\\materials\\metal_base.remt"))->WaitUntilLoaded());
		
	// create default materials
	{
		mesh::DataView matView(*mesh);
		for (Uint32 matIndex = 0; matIndex < data.m_materials.Size(); ++matIndex)
		{
			const auto& matInfo = data.m_materials[matIndex];

			THandle< CMaterialInstance > mi = CreateHandle< CMaterialInstance >();
			mi->SetBaseMaterial(baseMaterial);

			mi->SetParent(mesh.Get());
			matView.SetMaterialData(matIndex, mi);
			matView.SetMaterialName(matIndex, RED_NAME(matInfo.m_name));
		}
	}

	builder.Flush( *mesh );

	return ImportResult::Success( mesh );
}
