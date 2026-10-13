/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/
#pragma once

namespace GpuApi
{
	// Maximum number of slots allowed in a vertex layout.
	static constexpr Uint32 VERTEX_LAYOUT_MAX_SLOTS = 8;

	// Maximum number of usage indices allowed (usageIndex < INDEX_COUNT)
	// Changes here should be reflected in gpuWrapApiVertexPacking.h -- PackingVertexLayout::USAGE_INDEX_COUNT
	static constexpr Uint32 VERTEX_LAYOUT_USAGE_INDEX_COUNT = 16;

	// Maximum number of vertex elements allowed in a vertex layout.
	static constexpr Uint32 VERTEX_LAYOUT_MAX_ELEMENTS = 32;

	static constexpr Uint32 MAX_UAV_BINDINGS		= 15;

	// usable SRV slots is 63, last one is reserved for bindless
	static constexpr Uint32 MAX_SRV_BINDINGS		= 64;

	static constexpr Uint32 MAX_SAMPLER_STATES		= 16;
	static constexpr Uint32 MAX_PS_SAMPLER_STATES	= 16;
	static constexpr Uint32 MAX_VS_SAMPLER_STATES	= 16;
	static constexpr Uint32 MAX_CONSTANT_BUFFERS	= 14;
	static constexpr Uint32 MAX_TEXTURE_2D_SIZE		= 16384;

	// Maximum number of command lists that can be submitted in a single CloseAndSubmitCommandLists().
	static constexpr Uint32 MAX_COMMANDLIST_SUBMIT_COUNT = 100;

	static constexpr Uint32 MAX_ANISOTROPY = 16;

	static constexpr Uint32 MAX_BINDLESS_GEOMETRY_STREAMS = 2;

	struct TextureRef;
	struct BufferRef;
	struct ResourcePackRef;
	struct SamplerStateRef;
	struct ShaderRef;
	struct VertexLayoutRef;
	struct SwapChainRef;
	struct OMStateRef;
	struct CommandListRef;
	struct AccelerationStructureRef;
	struct StateObjectRef;

	template<typename T> struct MaxResourceCount;
	template<> struct MaxResourceCount<TextureRef>		{ static constexpr Uint32 Value = 32768; };
	template<> struct MaxResourceCount<BufferRef>		{ static constexpr Uint32 Value = 32768; };
	template<> struct MaxResourceCount<ResourcePackRef>	{ static constexpr Uint32 Value =  8192; };
	template<> struct MaxResourceCount<SamplerStateRef>	{ static constexpr Uint32 Value =   128; };
	template<> struct MaxResourceCount<ShaderRef>		{ static constexpr Uint32 Value =  4096; };
	template<> struct MaxResourceCount<VertexLayoutRef>	{ static constexpr Uint32 Value =   512; };
	template<> struct MaxResourceCount<SwapChainRef>	{ static constexpr Uint32 Value =    32; };
	template<> struct MaxResourceCount<OMStateRef>		{ static constexpr Uint32 Value =  1024; };
	template<> struct MaxResourceCount<CommandListRef>	{ static constexpr Uint32 Value =   128; };
	template<> struct MaxResourceCount<AccelerationStructureRef> { static constexpr Uint32 Value = 24000; };
	template<> struct MaxResourceCount<StateObjectRef> { static constexpr Uint32 Value = 128; };
}
