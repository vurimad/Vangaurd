/*
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "meshMaterialBank.h"

class CMaterialInstance;

namespace job
{
	class Counter;
}

namespace serialization
{
	class LoadingContext;
	class LoadingResult;
}

namespace mesh
{
	struct LocalMaterialHeader
	{
		RTTI_DECLARE_TYPE(LocalMaterialHeader);

		Uint32 m_offset; // NOTE: no gaps, offset always sum of all previous sizes; purely for convenience
		Uint32 m_size;
	};

	// #tbd: could lazily recreate buffers after editing, but a lot more error prone
	class RESOURCEMESH_API MeshMaterialBuffer : red::NonCopyable
	{
		RTTI_DECLARE_TYPE(MeshMaterialBuffer);

	public:
		MeshMaterialBuffer() = default;

		// Loading refers back to this-ptr as userData
		MeshMaterialBuffer(MeshMaterialBuffer&&) = delete;
		MeshMaterialBuffer& operator=(MeshMaterialBuffer&&) = delete;

		// Unsafe because the MeshMaterialBuffer *must* remain alive until loading finishes for two reasons:
		// It will refer back to this-ptr in the loading callback, and the data buffer it reads from has to wait for
		// the serialization::MemoryAllocator callback so we can redundantly copy the data into it for the BinaryLoader to process.
		// Normally not a problem because this should be part of a job chain which keeps a handle to the mesh
		RED_NODISCARD res::ResourceTokenHandle IssueLoadMaterialByIndex_UnsafeLifetime(Uint16 materialIndex, const serialization::LoadingContext* optLoadingContext, io::EAsyncPriority priorityIfNoLoadingContext);

		Uint32 GetNumMaterials() const;

		void Editor_InsertAt(Uint32 index, const THandle<CMaterialInstance>& material);
		void Editor_RemoveAt(Uint32 index);
		void Editor_Clear();
		void Editor_CopyAssign( const MeshMaterialBuffer& rhs );

	private:
		void Editor_PatchAppendOrRemoveMaterial(Uint32 patchAppendOrRemoveIndex, const THandle<CMaterialInstance>& material);

		static void StaticResourceLoadedCallback(const serialization::LoadingResult& result, const res::ResourceTokenWeakHandle& tokenUserData, const serialization::LoadingContext&, job::CompletionDeferral&& deferral, void* userData);
		void ScheduleLoadingJob_NoLock( Uint16 materialIndex, const res::ResourceTokenHandle& token, job::CompletionDeferral&& deferral, const res::ResourcePath& resolvedPath, const res::IssueLoadingRequestParameter & param);

		res::ResourceTokenHandle TryAcquireResourceToken_NoLock(const res::ResourcePath& resolvedPath);


		DataBuffer m_rawData{ rend::PoolMesh() };
		red::DynArray< LocalMaterialHeader > m_rawDataHeaders{ rend::PoolMesh() };

		using ResourceTokenDictionary = red::HashMap< res::ResourcePath, res::ResourceTokenWeakHandle >;
		ResourceTokenDictionary m_resourceTokenDictionary{ rend::PoolMesh() };
		mutable red::RWSpinLock m_resourceTokenLock;

		MeshMaterialBank m_materialBank;

	#if !defined( NO_EDITOR ) || defined( RED_PLATFORM_LINUX ) || defined( VANGUARD_ASSET_HOST )
			mutable red::RWSpinLock m_editorLock;
			Uint32 m_editorBufferGeneration{ 0 };
	#endif
	};
}
