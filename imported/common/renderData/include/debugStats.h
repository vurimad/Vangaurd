#pragma once

namespace rend
{
	constexpr Uint32 debugStatsBackBufferCount = 3;

	struct DynamicInstanceBufferStats
	{
		Uint32 m_capacity = 0;
		Uint32 m_usage = 0;
		Bool m_wasOverflow = false;
	};

	struct InstanceBufferStats
	{
		DynamicInstanceBufferStats m_dynamicBufferStats;
		DynamicInstanceBufferStats m_dynamicSkinnedBufferStats;
		DynamicInstanceBufferStats m_decalParamsBufferStats;
		Uint32 m_staticBufferCapacity = 0;
		Uint32 m_staticBufferAllocationCount = 0;
		Uint32 m_staticBufferFreeSize = 0;
		Uint32 m_staticBufferWasted = 0;
	};

	struct DebugStats
	{
		enum DebugStatsSupport : Uint32
		{
			DSS_RESOURCE_REFS,
			DSS_COPY_QUEUE,
			DSS_RESIDENCY_MANAGER,
			DSS_IN_FRAME_UPLOADS,
			DSS_SYSTEM_PRIMITIVES,
			DSS_FRAME_INFO,
			DSS_ALLOCATED_CPU_DESCRIPTORS,
			DSS_ROOT_PARAMETERS,
			DSS_D3D12_MEMORY_ALLOCATOR,
		};
		Uint64 m_supportMask;

		RED_INLINE void SetSupport( DebugStatsSupport dss )	{ m_supportMask |= ( 1ULL << dss );	}
		RED_INLINE Bool GetSupport( DebugStatsSupport dss )	{ return ( m_supportMask & ( 1ULL << dss ) ) != 0; }


		struct
		{
			Uint32 m_usedTextures;
			Uint32 m_usedBuffers;
			Uint32 m_usedResourcePacks;
			Uint32 m_usedSamplerStates;
			Uint32 m_usedShaders;
			Uint32 m_usedVertexLayouts;
			Uint32 m_usedSwapChains;
			Uint32 m_usedOMStates;
			Uint32 m_usedCommandLists;
			Uint32 m_usedAccelerationStructures;

			Uint32 m_maxTextures;
			Uint32 m_maxBuffers;
			Uint32 m_maxResourcePacks;
			Uint32 m_maxSamplerStates;
			Uint32 m_maxShaders;
			Uint32 m_maxVertexLayouts;
			Uint32 m_maxSwapChains;
			Uint32 m_maxOMStates;
			Uint32 m_maxCommandLists;
			Uint32 m_maxAccelerationStructures;

		} m_resourceRefs;

		// Copy queue
		struct 
		{
			struct BufferStats
			{
				Uint32 m_resourcesCount;
				Uint64 m_offset;
				Bool m_isActive;
			};

			BufferStats m_buffer0;
			BufferStats m_buffer1;
			Uint64 m_maxSize;
		} m_copyQueue;

		// Residency manager
		struct 
		{
			struct MemoryLayout
			{
				Uint64 m_budget;
				Uint64 m_currentUsage;
				Uint64 m_availableForReservation;
				Uint64 m_currentReservation;
			};

			MemoryLayout m_local;
			MemoryLayout m_nonLocal;
			Uint64 m_lastSubmitedCLsResidencySetSize;

			Uint64 m_totalTrackedSize;
		} m_residencyManager;

		// In-frame Uploads memory
		struct
		{
			Uint32 m_size;

			Uint32 m_perFrameThroughput[ debugStatsBackBufferCount ];
			Uint32 m_perFrameStartOffset[debugStatsBackBufferCount];
			Bool m_perFrameInUse[debugStatsBackBufferCount];
		} m_inFrameUploads;

		// DrawSystemPrimitives
		struct
		{
			Uint32 m_vertexInFlight;
			Uint32 m_vertexAllocated;
			Uint32 m_vertexChunks;
			Uint32 m_indexInFlight;
			Uint32 m_indexAllocated;
			Uint32 m_indexChunks;
		} m_systemPrimitives;

		struct
		{
			Bool m_wasMissingPSO;
			Bool m_didImmediateCompilePSO;
			Bool m_didOverflowUploadRing;
		} m_frameInfo;

		struct
		{
			Uint32 m_usedRTVs;
			Uint32 m_usedDSVs;
			Uint32 m_usedUAVs;
			Uint32 m_usedSamplers;

			Uint32 m_maxRTVs;
			Uint32 m_maxDSVs;
			Uint32 m_maxUAVs;
			Uint32 m_maxSamplers;

			Uint32 m_usedSamplerSets;
			Uint32 m_maxSamplerSets;
		} m_allocatedCPUDescriptors;

		struct
		{
			Uint32 m_paramCount;
			Uint32 m_updates[64];
			Uint32 m_copies[64];
		} m_rootParameters;

		// Memory layout is equivalant to D3D12MA::Budget.
		struct D3d12maBudget
		{
			Uint64 m_blockBytes;
			Uint64 m_allocationBytes;
			Uint64 m_usage;
			Uint64 m_budget;
		};

		struct D3d12MemoryAllocatorStats
		{
			D3d12maBudget m_gpu;
			D3d12maBudget m_cpu;
		} m_d3d12MemoryAllocator;

		InstanceBufferStats m_instanceBufferStats;
	};
	

} // rend
 