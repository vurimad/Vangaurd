/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "meshParameter.h"

class CMesh;

namespace mesh
{
	class RESOURCEMESH_API MeshParamGpuBuffer : public mesh::MeshParameter
	{
		RTTI_DECLARE_TYPE(MeshParamGpuBuffer);
		RED_EMPTY_DEFAULT_CONSTRUCTOR(MeshParamGpuBuffer);

	public:
		MeshParamGpuBuffer( Uint32 stride, Uint32 dataSize, const void* dataBuffer );

		RED_FORCE_INLINE DataBuffer& GetBufferData() { return m_data; }
		RED_FORCE_INLINE const DataBuffer& GetBufferData() const { return m_data; }
		RED_FORCE_INLINE Uint32 GetStride() const { return m_stride; }

	private:
		virtual void OnMeshChanged(const CMesh& mesh) override final;

		Uint32		m_stride;
		DataBuffer	m_data;
	};

	class RESOURCEMESH_API MeshParamTopologyData : public mesh::MeshParameter
	{
		RTTI_DECLARE_TYPE( MeshParamTopologyData );
		RED_EMPTY_DEFAULT_CONSTRUCTOR( MeshParamTopologyData );

	public:
		MeshParamTopologyData( const DataBuffer& dataBuffer, const red::DynArray< Uint32 >& offsets, const red::DynArray< Uint32 >& sizes );

		RED_FORCE_INLINE DataBuffer& GetBufferData() { return m_data; }
		RED_FORCE_INLINE const DataBuffer& GetBufferData() const { return m_data; }
		RED_FORCE_INLINE Uint32 GetDataSize( Uint32 index ) { return m_sizes[ index ]; }
		RED_FORCE_INLINE Uint32 GetDataOffset( Uint32 index ) { return m_offsets[ index ]; }
		RED_FORCE_INLINE Uint32 GetStride() const { return sizeof( Uint32 ); }
		RED_FORCE_INLINE Uint32 GetNumChunks() const { return m_sizes.Size(); }

		virtual void OnPostLoad( const PostLoadContext& context ) override;

		virtual Bool ShouldBeStrippedWhenCooking() const override { return true; }

	private:
		virtual void OnMeshChanged( const CMesh& mesh ) override final;

		DataBuffer	m_data;
		red::DynArray< Uint32 > m_offsets { rend::PoolMesh() };
		red::DynArray< Uint32 > m_sizes { rend::PoolMesh() };
	};

	class RESOURCEMESH_API MeshParamTopologyMetadata : public mesh::MeshParameter
	{
		RTTI_DECLARE_TYPE( MeshParamTopologyMetadata );
		RED_EMPTY_DEFAULT_CONSTRUCTOR( MeshParamTopologyMetadata );

	public:
		MeshParamTopologyMetadata( const DataBuffer& dataBuffer, const red::DynArray< Uint32 >& offsets, const red::DynArray< Uint32 >& sizes );

		RED_FORCE_INLINE DataBuffer& GetBufferData() { return m_data; }
		RED_FORCE_INLINE const DataBuffer& GetBufferData() const { return m_data; }
		RED_FORCE_INLINE Uint32 GetDataSize(Uint32 index) { return m_sizes[index]; }
		RED_FORCE_INLINE Uint32 GetDataOffset(Uint32 index) { return m_offsets[index]; }
		RED_FORCE_INLINE Uint32 GetStride() const { return sizeof( Uint32 ); }
		RED_FORCE_INLINE Uint32 GetNumChunks() const { return m_sizes.Size(); }

		virtual void OnPostLoad( const PostLoadContext& context ) override;

		virtual Bool ShouldBeStrippedWhenCooking() const override { return true; }

	private:
		virtual void OnMeshChanged( const CMesh& mesh ) override final;

		DataBuffer	m_data;
		red::DynArray< Uint32 > m_offsets { rend::PoolMesh() };
		red::DynArray< Uint32 > m_sizes{ rend::PoolMesh() };
	};
}
