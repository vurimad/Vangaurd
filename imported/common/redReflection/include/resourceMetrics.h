/*
 * Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
 */
#pragma once

#include "resourcePath.h"

namespace red
{
	class AbsolutePath;
}

namespace res
{
	enum TextureGroup
	{
		TG_Unknown,
		TG_Generic,
		TG_Multilayer,
		TG_System
	};

	struct RED_REFLECTION_API DeferredDataBufferMetrics
	{
		struct Segment
		{
			Uint32 loadedCount{ 0 };
			Uint32 sizeOnDisk{ 0 };
			Bool isInline{ false };
		};

		red::HashMap<Uint32, Segment> m_offsetToSegments{ red::PoolDebug() };
	};

	struct RED_REFLECTION_API ColdLoadedMetrics
	{
		Uint32 resourceLoadedCount{ 0 };
		DeferredDataBufferMetrics m_ddbMetrics;
	};

	struct RED_REFLECTION_API SingleResourceMetrics
	{
		res::ResourcePath path;
		red::Uint32 cpuBytesUsed = 0u;
		red::Uint32 gpuBytesUsed = 0u;
		red::Uint32 refCount = 0u;

		struct
		{
			TextureGroup group = TG_Unknown;
		} texture;

		struct
		{
			Uint32 inRange = 0u;
			Bool isSkinned = false;
			Bool hasPhysics = false;
		} mesh;
	};

	struct RED_REFLECTION_API RawResourceMetrics
	{
		RawResourceMetrics();
		~RawResourceMetrics();

		struct ResType
		{
			ResType();
			~ResType();

			red::DynArray< SingleResourceMetrics > metrics{ red::PoolDebug() };
			Uint64 usedCPUBytes = 0u;
			Uint64 usedGPUBytes = 0u;
		};

		ResType textures;
		ResType meshes;
		ResType materials;
		ResType videos;
	};


	enum class ResourceMetricsType : Uint8
	{
		Mesh = 0,
		Material,
		Texture,
		Video,

		COUNT
	};
}