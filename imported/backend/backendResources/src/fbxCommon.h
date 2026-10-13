/**
* Copyright (c) 2007-2020 CD Projekt Red. All Rights Reserved.
*/

#pragma once

//-----------------------------------------------------------------------------

// FBX SDK 2013.1
#define FBXSDK_NEW_API
#define FBXSDK_SHARED
#include <fbxsdk.h>
#include "../../../common/resource/include/basicTextBuilder.h"

//-----------------------------------------------------------------------------

class CBitmapTexture;
class CMesh;
class IMaterial;
class ITexture;

namespace mesh
{
class DataViewRO;
}

namespace xml
{
class Writer;
}

class FBXImportScene
{
	RED_USE_MEMORY_POOL( red::PoolBackend );

public:
	FbxManager* m_manager;
	FbxScene* m_scene;

public:
	FBXImportScene();
	~FBXImportScene();

	static FBXImportScene* Load( const red::String& filename );
};


class FBXExportScene
{
	RED_USE_MEMORY_POOL( red::PoolBackend );

public:
	FbxManager* m_manager;
	FbxScene* m_scene;

	red::DynArray<FbxNode*> m_nodes;
	red::HashMap<red::String, Int32> m_nodeMap;

	red::HashMap<res::ResourcePath, FbxFileTexture*> m_textures;

	FbxArray<FbxNode*> m_clusteredFbxNodes;

private:
	red::UniquePtr< CBasicTextBuilder, red::PoolBackend > m_xmlBuffer;
	red::DynArray< Uint32 > m_xmlSavedMaterials;

	Uint32 AppendPhongMaterial( FbxNode* lNode, const CName& name, const FbxDouble3& diffuse );

	void ExportXMLMeshData( xml::Writer& writer, const mesh::DataViewRO& view );
	void ExportXMLMaterialData( xml::Writer& writer, const CMesh* srcModel, const IMaterial* material, Uint32 materialID, const FbxSurfacePhong* newMaterial );

public:
	FBXExportScene( const red::String& saveFilePath );
	~FBXExportScene();

	Bool Save( const red::String& filePath );

	void ExportMesh( const Matrix& modelLocalToWorld, Bool exportSkinning, const CMesh* srcModel, const red::String& name );

	Bool ExtractTexture( const IMaterial* srcMaterial, const red::String& paramName, ITexture*& outTexture );

	void ExtractTextures( FbxSurfacePhong* material, const IMaterial* srcMaterial, const Bool bMaterialNameSet );

	Bool ExtractTextureData( const CBitmapTexture* bitmapTexture, red::String& outFileName );
};

//-----------------------------------------------------------------------------

template<const Uint32 NUM_BUCKETS = 1024>
class FBXPointCache
{
protected:
	struct Entry
	{
		Vector3	m_point;
		Int32	m_next;

		RED_INLINE Entry()
		{}

		RED_INLINE Entry(const Vector3& point)
			: m_point(point)
			, m_next(-1)
		{}
	};

protected:
	Int32					m_cacheHeads[NUM_BUCKETS];
	red::DynArray< Entry >	m_entries;

public:
	RED_INLINE const Uint32 GetNumPoints() const
	{
		return m_entries.Size();
	}

	RED_INLINE const Vector3& GetPoint(const Uint32 index) const
	{
		return m_entries[index].m_point;
	}

	RED_INLINE FBXPointCache()
	{
		m_entries.Reserve( NUM_BUCKETS * 4 );

		for (Uint32 i=0; i<NUM_BUCKETS; ++i)
		{
			m_cacheHeads[i] = -1;
		}
	}

	RED_INLINE void Clear()
	{
		m_entries.Clear();

		for (Uint32 i=0; i<NUM_BUCKETS; ++i)
		{
			m_cacheHeads[i] = -1;
		}
	}

	RED_INLINE Uint32 MapPoint(const Vector3& point)
	{
		const Uint32 hash = CalcHash(point);

		Int32 entryIndex = m_cacheHeads[ hash ];
		while (entryIndex != -1)
		{
			const Entry& entry = m_entries[ entryIndex ];
			if ( entry.m_point.X == point.X && entry.m_point.Y == point.Y && entry.m_point.Z == point.Z )
			{
				return entryIndex;
			}

			entryIndex = entry.m_next;
		}

		const Uint32 newEntryIndex = m_entries.Size();
		Entry& newEntry = m_entries.EmplaceBack(point);

		newEntry.m_next = m_cacheHeads[hash];
		m_cacheHeads[hash] = newEntryIndex;

		return newEntryIndex;
	}

protected:
	RED_INLINE static const Uint32 CalcHash(const Vector3& point)
	{
		// works good for integer ranges
		const Uint32 x = ( ( Uint32& ) point.X );
		const Uint32 y = ( ( Uint32& ) point.Y );
		const Uint32 z = ( ( Uint32& ) point.Z );
		return ( x ^ y ^ z ) % NUM_BUCKETS;
	}
};