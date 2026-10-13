/*
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "meshMaterialBuffer.h"
#include "../../redJobs2/include/jobCounterOwner.h"
#include "../../redFileSystem/include/memoryFileWriter.h"
#include "../../redReflection/include/serializationSaver.h"
#include "../../redReflection/include/serializationLoader.h"
#include "../../redJobs2/include/jobBuilder.h"
#include "../../redReflection/include/serializationAsyncSource.h"
#include "../../redReflection/src/serializationBinaryLoader.h"
#include "../../redReflection/include/resource.h"
#include "../../resourceMaterial/include/materialInstance.h"
#include "../../redFileSystem/include/nullFile.h"
#include "../../resource/include/serializationAsyncSourceImpl.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE(LocalMaterialHeader, mesh);
	RTTI_PROPERTY(m_offset);
	RTTI_PROPERTY(m_size);
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE(MeshMaterialBuffer, mesh);
	RTTI_PROPERTY(m_rawData);
	RTTI_PROPERTY(m_rawDataHeaders);
RTTI_END_TYPE();

namespace mesh
{
	void MeshMaterialBuffer::StaticResourceLoadedCallback(const serialization::LoadingResult& result, const res::ResourceTokenWeakHandle& tokenUserData, const serialization::LoadingContext& /*unused*/, job::CompletionDeferral&& deferral, void* userData)
	{
		auto token = tokenUserData.Lock();
		RED_FATAL_ASSERT(token, "Cancellation not supported yet - UNTESTED!");

		// #tbd: ensure kept alive, at a higher level since a bit too late to assert in the dtor
		auto* self = static_cast<MeshMaterialBuffer*>(userData);
		RED_FATAL_ASSERT(token->GetPath().IsValid());

		// #tbd: nothing here should ever really fail, unless CMaterialInstance ceases to exist
		RED_FATAL_ASSERT(!token->HasFailed());
		RED_FATAL_ASSERT(!result.IsFailed());

		RED_FATAL_ASSERT(!result.m_loadedRootObjects.Empty(), "Failed to create any root objects '%s'", token->GetPath().ToDebugString());
		auto loadedMaterial = Cast< CMaterialInstance >(result.m_loadedRootObjects[0]);
		RED_FATAL_ASSERT(loadedMaterial, "Root object of Resource is not an actual CMaterialInstance!");
		self->m_materialBank.RegisterMaterial(token->GetPath(), loadedMaterial);
		token->Internal_AssignLoadedResource(loadedMaterial, std::move(deferral));
	}

	namespace helper
	{
		// If no editor, can have a static pool resource paths to share, since all that matters is that it's unique to this particular
		// material buffer
		static res::ResourcePath CreateMaterialTokenResourcePath(Uint16 materialIndex, Uint32 editorBufferGeneration)
		{
			char buffer[64];
			red::SNPrintFUnsafe(buffer, RED_ARRAY_COUNT_U32(buffer), "localmaterial_%08X[%08X].res", (Uint32)materialIndex, editorBufferGeneration);
			return res::ResourcePath::Build(buffer);
		}
	}

	res::ResourceTokenHandle MeshMaterialBuffer::TryAcquireResourceToken_NoLock(const res::ResourcePath& resolvedPath)
	{
		res::ResourceTokenWeakHandle weakToken;
		m_resourceTokenDictionary.Find(resolvedPath, weakToken);
		return weakToken.Lock();
	}

	res::ResourceTokenHandle MeshMaterialBuffer::IssueLoadMaterialByIndex_UnsafeLifetime( Uint16 materialIndex, const serialization::LoadingContext* optLoadingContext, io::EAsyncPriority priorityIfNoLoadingContext )
	{
	#if !defined( NO_EDITOR ) || defined( RED_PLATFORM_LINUX ) || defined( VANGUARD_ASSET_HOST )
			RED_SCOPE_SHARED_LOCK(m_editorLock);
	#endif

		res::ResourceTokenHandle token;
		job::CompletionDeferral deferral;
		Bool isNewToken = false;

		// NOTE: not sufficient to clear the materialBank because a load could still be pending and insert itself after the clear,
		// so we look up tokens by a resource path that encodes the "editor generation".
		// Clearing the materialBank is more just a convenience to avoid keeping a lot of eventually expired weakhandles around
		//
		// And the token itself doesn't know its buffer generation unless it were to parse it out of the resource path.
		// Otherwise it could lock and check before inserting itself once loaded.
		Uint32 editorBufferGeneration = 0;
	#if !defined( NO_EDITOR ) || defined( RED_PLATFORM_LINUX ) || defined( VANGUARD_ASSET_HOST )
			editorBufferGeneration = m_editorBufferGeneration;
	#endif

		const auto resolvedPath = helper::CreateMaterialTokenResourcePath(materialIndex, editorBufferGeneration);

		// SHARED LOCK: Try to acquire already created token
		{
			RED_SCOPE_SHARED_LOCK(m_resourceTokenLock);
			token = TryAcquireResourceToken_NoLock(resolvedPath);
		}

		// EXCLUSIVE LOCK: Try to create new token
		if (!token)
		{
			// Create new resource token outside the lock.
			auto tokenDeferralPair = CreateResourceToken( resolvedPath );

			RED_SCOPE_LOCK(m_resourceTokenLock);

			// Got lock. Did someone beat us to it ?
			token = TryAcquireResourceToken_NoLock(resolvedPath);
			if (!token)
			{
				// If we got here, we have the lock, no one managed to beat us to it also. Insert safely
				token = std::move(tokenDeferralPair.first);
				deferral = std::move(tokenDeferralPair.second);
				isNewToken = true;
				m_resourceTokenDictionary[resolvedPath] = token;
			}
		}

		if (isNewToken)
		{
			// If we got here, we have token and we created it. 
			// First step, Is the Resource already loaded?			
			const THandle< CMaterialInstance > resource = m_materialBank.FindMaterial(resolvedPath);
			if (!resource)
			{
				res::IssueLoadingRequestParameter param;
				if (optLoadingContext)
				{
					param = res::ResourceLoader::GetImportLoadingRequestParameter(resolvedPath, *optLoadingContext);
				}
				else
				{
					param = res::IssueLoadingRequestParameter(resolvedPath);
					param.priority = priorityIfNoLoadingContext;
				}

				param.useGameCache = true;

				// Just in case, since nothing wil update this
				if (param.priority == io::eAsyncPriority_Streaming)
				{
					param.priority = io::eAsyncPriority_Normal;
				}

				// No? Kickstart loading job!
				ScheduleLoadingJob_NoLock(materialIndex, token, std::move(deferral), resolvedPath, param);
			}
			else
			{
				// Yes ? Assign to token.
				token->Internal_AssignLoadedResource(resource, std::move(deferral));
			}
		}

		return token;
	}

	namespace helper
	{
		static red::Atomic<Uint32> s_creationID;
	}

	void MeshMaterialBuffer::ScheduleLoadingJob_NoLock(Uint16 materialIndex, const res::ResourceTokenHandle& token, job::CompletionDeferral&& deferral, const res::ResourcePath& resolvedPath, const res::IssueLoadingRequestParameter & param)
	{
		//RED_FATAL_ASSERT(m_editorLock->IsLocked());
		const auto& header = m_rawDataHeaders[materialIndex];

		serialization::LoadingContext loadingContext;
		loadingContext.m_creationId = helper::s_creationID.Increment();
		loadingContext.m_getAllLoadedObjects = true;
		loadingContext.m_resourcePath = resolvedPath;
		loadingContext.m_initialResourcePath = param.parentPath.IsValid() ? param.parentPath : resolvedPath;
		loadingContext.m_skipImports = param.skipImports;
		loadingContext.m_skipPostLoad = param.skipPostLoad;
		loadingContext.m_postLoadFlags = param.postLoadFlags;
		loadingContext.m_verifyExportTypes = param.verifyExportTypes;
		loadingContext.m_debugStackTraceHandle = job::DebugTraceCall();
		loadingContext.m_priority = param.priority;

		serialization::ILoader::CallbackContext callbackContext;
		callbackContext.callback = &MeshMaterialBuffer::StaticResourceLoadedCallback;
		callbackContext.userData = this;
		callbackContext.tokenUserData = token;

		const red::BlobView rawMaterialDataView = m_rawData.GetView().Range(header.m_offset, header.m_size);
		red::SharedPtr< serialization::IAsyncSource > asyncSource;

		// If the editor, copy the data so there's no chance of it being modified while still using it to deserialize from
		// We could still be throttled, so there's no guarantee of any immediate loading
	#if !defined( NO_EDITOR ) || defined( VANGUARD_ASSET_HOST )
			// COPY data since could potentially get modified
			if (!IsGameMode())
		{
			auto dataToLoad = red::CreateUniqueBuffer<red::PoolEngine>(rawMaterialDataView.Size(), 16);
			red::Memcpy(dataToLoad.Data(), rawMaterialDataView.Data(), rawMaterialDataView.Size());
			asyncSource = serialization::CreateMemoryAsyncSource(std::move(dataToLoad));
		}
#endif

		if (!asyncSource)
		{
			asyncSource = serialization::CreateMemoryAsyncSource(rawMaterialDataView);
		}

		auto binaryLoader = serialization::ILoader::CreateLoader();
		RED_FATAL_ASSERT(binaryLoader);
		binaryLoader->LoadAsyncWithCallback(asyncSource, loadingContext, callbackContext, std::move(deferral));
	}

	namespace helper
	{
		void SerializeToBuffer(const THandle<ISerializable>& object, red::DynArray<Uint8>& outBuffer)
		{
			RED_FATAL_ASSERT(object);

			outBuffer.Clear();

			CMemoryFileWriter memoryWriter{ outBuffer };

			serialization::SavingContext context{ object };

			const Bool isDataSaved = serialization::SaveToMemory(context, memoryWriter);
			RED_FATAL_ASSERT(isDataSaved, "Serialization of object failed");
		}

		static Uint32 CalcNewRawDataSize(Uint32 patchAppendOrRemoveIndex, const red::DynArray<Uint8>& newEntryBuffer, const red::DynArray<LocalMaterialHeader>& oldHeaders)
		{
			// NOTE: patchIndex == oldHeaders.Size() if appending, so don't loop on i < patchIndex
			Uint32 newBufferSize = 0;
			for (Uint32 i : oldHeaders.Indices())
			{
				if (i != patchAppendOrRemoveIndex)
				{
					newBufferSize += oldHeaders[i].m_size;
				}
			}

			newBufferSize += newEntryBuffer.Size();		

			return newBufferSize;
		}

		static Uint32 CalcTotalOffset(const red::DynArray< LocalMaterialHeader >& headers)
		{
			Uint32 totalOffset = 0;
			for (const auto& it : headers)
			{
				RED_FATAL_ASSERT(it.m_offset == totalOffset, "No gaps! Offset should always be the sum of previous entry sizes!");
				totalOffset += it.m_size;
			}

			return totalOffset;
		}

		static void AppendEntryToRawBuffer(red::DynArray< LocalMaterialHeader >& newRawDataHeaders, red::UniqueBuffer& newRawBuffer, const void* sourceData, Uint32 sourceDataSize)
		{
			const Uint32 prevTotalOffset = CalcTotalOffset(newRawDataHeaders);
			RED_FATAL_ASSERT(prevTotalOffset + sourceDataSize <= newRawBuffer.GetSize());

			red::Memcpy(static_cast<Uint8*>(newRawBuffer.Get()) + prevTotalOffset, sourceData, sourceDataSize);

			LocalMaterialHeader newHeader;
			newHeader.m_offset = prevTotalOffset;
			newHeader.m_size = sourceDataSize;
			newRawDataHeaders.PushBack(newHeader);
		}
	}

	void MeshMaterialBuffer::Editor_InsertAt(Uint32 index, const THandle<CMaterialInstance>& material)
	{
		RED_FATAL_ASSERT(material, "Use Editor_RemoveAt() to remove an entry explicitly" );
		Editor_PatchAppendOrRemoveMaterial(index, material);
	}

	void MeshMaterialBuffer::Editor_RemoveAt(Uint32 index)
	{
		Editor_PatchAppendOrRemoveMaterial(index, nullptr);
	}

	void MeshMaterialBuffer::Editor_Clear()
	{
	#if !defined( NO_EDITOR ) || defined( RED_PLATFORM_LINUX ) || defined( VANGUARD_ASSET_HOST )
			RED_SCOPE_LOCK(m_editorLock);
		m_editorBufferGeneration += 1;
		m_materialBank.Clear();
		m_rawDataHeaders.Clear();
		m_rawData.Clear();
#else
		RED_FATAL("Cannot call during the game!");
#endif
	}

	// #fixme: this function shoudn't exist, or assert that RHS is NOT asynchronously loading anything
	// Preferable to assign an array of materials instead.
	// However friend meshView uses CMesh::InsertMaterialToInternalArray and works on internal data structures, so should refactor that first
	void MeshMaterialBuffer::Editor_CopyAssign( const MeshMaterialBuffer& rhs)
	{
	#if !defined( NO_EDITOR ) || defined( RED_PLATFORM_LINUX ) || defined( VANGUARD_ASSET_HOST )
			if (this != &rhs)
		{
			RED_SCOPE_LOCK(m_editorLock);
			auto& rhsEditorLock = rhs.m_editorLock;
			RED_SCOPE_LOCK(rhsEditorLock);

			m_materialBank.Clear();

			m_editorBufferGeneration += 1; // simply bump generation
			m_rawDataHeaders = rhs.m_rawDataHeaders;
			m_rawData = DataBuffer::Copy(rhs.m_rawData, rend::PoolMesh());
		}
#else
		RED_FATAL("Editor only!");
#endif
	}


	void MeshMaterialBuffer::Editor_PatchAppendOrRemoveMaterial(Uint32 patchAppendOrRemoveIndex, const THandle<CMaterialInstance>& material)
	{
	#if !defined( NO_EDITOR ) || defined( RED_PLATFORM_LINUX ) || defined( VANGUARD_ASSET_HOST )
			RED_SCOPE_LOCK(m_editorLock);

		m_editorBufferGeneration += 1;
		m_materialBank.Clear();

		const auto oldRawDataHeaders = std::move(m_rawDataHeaders);
		const auto oldRawData = std::move(m_rawData);

		// NOTE: Appending if patchIndex == oldRawDataHeaders.Size()
		RED_FATAL_ASSERT(patchAppendOrRemoveIndex <= oldRawDataHeaders.Size());

		// However, removing can't append
		RED_FATAL_ASSERT( material || patchAppendOrRemoveIndex < oldRawDataHeaders.Size(), "Removing a material at index %u (size %u)", patchAppendOrRemoveIndex, oldRawDataHeaders.Size());

		red::DynArray<Uint8> newEntryBuffer{ rend::PoolMesh() };
		RED_FATAL_ASSERT(newEntryBuffer.Size() == 0);
		if ( material )
		{
			helper::SerializeToBuffer( material, newEntryBuffer);
		}

		const Uint32 newBufferSize = helper::CalcNewRawDataSize(patchAppendOrRemoveIndex, newEntryBuffer, oldRawDataHeaders);

		red::DynArray< LocalMaterialHeader > newRawDataHeaders{ rend::PoolMesh() };

		// Could probably just be align=1
		auto newRawBuffer = red::CreateUniqueBuffer< rend::PoolMesh >(newBufferSize, 16); // oldRawData.GetAlignment() --> by default zero, which is useful for fatal asserting in debug build. 

		for (Uint32 i = 0; i < patchAppendOrRemoveIndex; ++i)
		{
			const auto& oldHeader = oldRawDataHeaders[i];
			const void* sourceData = static_cast<const Uint8*>(oldRawData.Data()) + oldHeader.m_offset;
			const Uint32 sourceDataSize = oldHeader.m_size;

			helper::AppendEntryToRawBuffer(newRawDataHeaders, newRawBuffer, sourceData, sourceDataSize);
		}

		if ( material )
		{
			const void* sourceData = newEntryBuffer.TypedData();
			const Uint32 sourceDataSize = newEntryBuffer.Size();
			helper::AppendEntryToRawBuffer(newRawDataHeaders, newRawBuffer, sourceData, sourceDataSize);
		}

		for (Uint32 i = patchAppendOrRemoveIndex + 1; i < oldRawDataHeaders.Size(); ++i)
		{
			const auto& oldHeader = oldRawDataHeaders[i];
			const void* sourceData = static_cast<const Uint8*>(oldRawData.Data()) + oldHeader.m_offset;
			const Uint32 sourceDataSize = oldHeader.m_size;

			helper::AppendEntryToRawBuffer(newRawDataHeaders, newRawBuffer, sourceData, sourceDataSize);
		}

		m_rawDataHeaders = std::move(newRawDataHeaders);
		m_rawData = DataBuffer{ std::move(newRawBuffer) };
#else
		RED_FATAL("Cannot call during the game!");
#endif
	}

	Uint32 MeshMaterialBuffer::GetNumMaterials() const
	{
	#if !defined( NO_EDITOR ) || defined( RED_PLATFORM_LINUX ) || defined( VANGUARD_ASSET_HOST )
			RED_SCOPE_SHARED_LOCK(m_editorLock);
#endif
		return m_rawDataHeaders.Size();
	}

}

