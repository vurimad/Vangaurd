/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "mesh.h"
#include "meshDataViewRO.h"
#include "../../redReflection/include/streamedResource.h"
#include "../../renderData/include/morphTargetCommon.h"
#include "../../renderData/include/renderResource.h"
#include "../../resourceMaterial/include/bitmapTexture.h"
#include "../../redReflection/include/serializationLoadingToken.h"

class MorphTargetMesh;
namespace serialization
{
	class DeferredDataBuffer;
}
struct SMorphTargetBuildContext;

class RESOURCEMESH_API MorphTargetMeshEntry
{
	RTTI_DECLARE_TYPE( MorphTargetMeshEntry );
	RED_USE_MEMORY_POOL( rend::PoolMesh );

public:
	MorphTargetMeshEntry();

	RED_INLINE CName GetName() const { return m_name; }
	RED_INLINE CName GetRegionName() const { return m_regionName; }
#ifndef RED_CONFIGURATION_FINAL
	RED_INLINE const THandle< CMesh > GetMesh() const { return m_mesh.Get(); }
	RED_INLINE  CName GetMeshAppearance() const { return m_meshAppearance; }
	RED_INLINE const THandle< CBitmapTexture > GetMask() const { return m_maskTexture.Get(); }
#endif // RED_CONFIGURATION_FINAL

#ifndef RED_CONFIGURATION_FINAL
	RED_INLINE Bool IsNameValid() const { return !m_name.Empty(); }
	RED_INLINE Bool IsMeshValid() const { return m_mesh.IsValid(); }
	RED_INLINE Bool IsMaskValid() const { return m_maskTexture.IsValid(); }
#endif // RED_CONFIGURATION_FINAL

	RED_INLINE MorphTargets::FaceRegion GetFaceRegion() const { return m_faceRegion; }

#ifndef RED_CONFIGURATION_FINAL
	RED_INLINE void PrepareForCalculation() { GetMesh(); GetMask(); }
	RED_INLINE Bool IsValid() const	{ return IsNameValid() && IsMeshValid() && IsMaskValid(); }

	Float GetDebugWeight() const { return m_weight; }
	Bool IsPreviewEnabled() const { return m_preview; }
#endif // RED_CONFIGURATION_FINAL

public:
	Bool Build( const SMorphTargetBuildContext& context, MorphTargets::TargetVertexDiffsData& targetVertexDiffsData, red::StaticArray< DataBuffer, MorphTargets::BTT_MAX >& targetTextureDiffs, red::StaticArray< Uint16, MorphTargets::BTT_MAX >& targetTextureDiffsWidth, red::StaticArray< Uint8, MorphTargets::BTT_MAX >& targetTextureDiffsMipLevelOffsets, red::StaticArray< Vector4, MorphTargets::BTT_MAX >& targetTextureQuantizationScale, red::StaticArray< Vector4, MorphTargets::BTT_MAX >& targetTextureQuantizationOffset );

	void CacheFaceRegion();
	void CacheBonesAndRigMatrices();

	Matrix GetBoneMatrix( mesh::BoneIndex boneIndex ) const	{ return ( boneIndex < m_boneRigMatrices.Size() ) ? m_boneRigMatrices[ boneIndex ] : Matrix::IDENTITY(); }
	Bool FindBoneIndex( CName boneName, mesh::BoneIndex& boneIndex ) const { boneIndex = m_boneNames.GetIndex( boneName ); return boneIndex != (mesh::BoneIndex)red::INVALID_INDEX; }

#ifndef RED_CONFIGURATION_FINAL
	void SetMeshAppearance( CName meshAppearance );
	void SetTextureBlend( Bool blendTexture );
#endif // RED_CONFIGURATION_FINAL

private:
	Bool Build_VertexDifferences( const SMorphTargetBuildContext& context, MorphTargets::TargetVertexDiffsData& outVertexDiffsData ) const;
	Bool Build_TextureDifferences( const CBitmapTexture* baseTexture, const CBitmapTexture* targetTexture, const MorphTargets::TextureBlendInfo& textureBlendInfo, DataBuffer& outTextureDiff, Uint16& outTextureDiffWidth, Uint8& outTextureDiffMipLevelCount, Vector4& outQuantizationScale, Vector4& outQuantizationBias ) const;

private:
#ifndef RED_CONFIGURATION_FINAL
	TResRef< CMesh >				m_mesh;
	TResRef< CBitmapTexture >		m_maskTexture;
	CName							m_meshAppearance;
#endif // RED_CONFIGURATION_FINAL
	CName							m_name;
	CName							m_regionName;
	MorphTargets::FaceRegion		m_faceRegion;

	red::DynArray< CName >			m_boneNames { rend::PoolMesh() };
	red::DynArray< Matrix >			m_boneRigMatrices { rend::PoolMesh() };

#ifndef RED_CONFIGURATION_FINAL
	MorphTargets::TextureBlendInfo m_normalmapBlendInfo;
#endif // RED_CONFIGURATION_FINAL

#ifndef RED_CONFIGURATION_FINAL
	Float m_weight;
	Bool m_preview;
#endif // RED_CONFIGURATION_FINAL
};

class RESOURCEMESH_API MorphTargetMesh : public res::StreamedResource
{
	RTTI_DECLARE_TYPE( MorphTargetMesh );
	IMPLEMENT_RESOURCE_INTERFACE( "", "morphtarget", "Morph Target Mesh Resource" );
	RED_BASE_CLASS( res::StreamedResource );
	RED_USE_MEMORY_POOL( rend::PoolMesh );

public:
	typedef red::DynArray< MorphTargetMeshEntry >	MorphTargetArray;
	typedef red::DynArray< CName >					RegionNames;

public:
	MorphTargetMesh();
	virtual ~MorphTargetMesh() = default;

public:
	RED_INLINE const TRenderPtr< IRenderResource >& GetRenderResource() const { return m_renderResource;  }
	RED_INLINE const Box& GetBoundingBox() const { return m_boundingBox; }

#ifndef RED_CONFIGURATION_FINAL
	RED_INLINE const CName GetAppearance() const { return m_baseMeshAppearance; }
#endif // RED_CONFIGURATION_FINAL

	RED_INLINE const THandle< CMesh > GetMesh() const { return m_baseMesh.Get(); }
	RED_INLINE const TResRef< CMesh >& GetMeshRef() const { return m_baseMesh; }
	RED_INLINE const CName GetBaseTextureParamName( const MorphTargets::BlendedTextureType texType ) const { return m_baseTextureParamName; }
	RED_INLINE const THandle< ITexture > GetBaseTexture( const MorphTargets::BlendedTextureType texType ) const { return m_baseTexture.Get(); }
	RED_INLINE const TResRef< ITexture >& GetBaseTextureRef( const MorphTargets::BlendedTextureType texType ) const { return m_baseTexture; }
	RED_INLINE const MorphTargetArray& GetTargets() const { return m_targets; }

	// Returned CMorphTarget is valid until AddTarget or something is called.
	const MorphTargetMeshEntry* GetTargetEntry( CName name ) const;
	const MorphTargetMeshEntry& GetTargetEntry( MorphTargets::TMorphTargetTargetIndex index ) const;
	
	// Get index of the given target. Can be used for indexing into
	MorphTargets::TMorphTargetTargetIndex GetTargetIndex( const MorphTargetMeshEntry& target ) const;
	
	Bool Rebuild();

	virtual void OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& newValue ) override;
	virtual Bool OnPropertyTypeMismatch( CName propertyName, const rtti::Property* existingProperty, const rtti::Variant& readValue ) override;

	virtual void OnPreSave( const PreSaveContext& context ) override;
	virtual void OnPostLoad( const PostLoadContext& context ) override;

	void AppendMorphTargetIDSet( MorphTargets::MorphTargetIDSet& morphTargetIDSet );

private:
	void Rebuild_BoundingBox();
	CName Rebuild_ExtractBaseTexture( const MorphTargets::BlendedTextureType texType );
	Bool Rebuild_Targets();
	Bool ShouldRebuild( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& newValue );

private:
	// Apply any changes imposed by m_resourceVersion.
	void UpgradeVersion();

	Box													m_boundingBox;
	TResRef< CMesh >									m_baseMesh;
	CName												m_baseTextureParamName;
	TResRef< ITexture >									m_baseTexture;
	CName												m_baseMeshAppearance;
	THandle< IRenderResourceBlob >						m_blob;
	MorphTargetArray									m_targets { rend::PoolMesh() };			// Morph targets
	TRenderPtr< IRenderResource >						m_renderResource;
#ifndef RED_CONFIGURATION_FINAL
	Uint8												m_resourceVersion;
	Bool												m_blendTexture;	// EDITOR ONLY!!!
#endif // RED_CONFIGURATION_FINAL
};
