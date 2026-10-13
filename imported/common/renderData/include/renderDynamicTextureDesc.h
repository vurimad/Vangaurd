/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "renderObjectPtr.h"
#include "renderResource.h"
#include "renderSimpleTypes.h"


//***************************************************************************************************************************//
//***************************************************************************************************************************//

class RENDER_DATA_API IDynamicTextureGenerator : public ISerializable
{
	RTTI_DECLARE_TYPE(IDynamicTextureGenerator);
	RED_USE_MEMORY_POOL(rend::PoolTexture);

public:

	IDynamicTextureGenerator();

	virtual ~IDynamicTextureGenerator();

	// - - - - - - - - - - - - - - - - - - - - - - 

	Bool ShouldRenderThisFrame( Uint32 currentFrameIndex ) const;

	// can this generator be run on async compute during shadows pass?
	virtual Bool CanRunOnAsyncCompute() const { return false; }

	virtual Bool CanRunInOffscreenPass() const { return true; }

	virtual void ForceFormatAndSize(GpuWrapApi::eTextureFormat& format, Uint32& width, Uint32& height) {}

	virtual void OnAttach(IRenderResource* res, Uint32 width, Uint32 height) = 0;

	virtual void OnDetach() = 0;

	// - - - - - - - - - - - - - - - - - - - - - - 

	virtual void Generate( IRenderResource* res) = 0;

	// - - - - - - - - - - - - - - - - - - - - - - 

private:

	mutable red::Atomic<Uint32> m_lastFrameRendered;
};

//***************************************************************************************************************************//
//***************************************************************************************************************************//

//
struct DynamicTextureDesc
{
	Uint32								width = 0;
	Uint32								height = 0;
	const void*							initData = nullptr;
	THandle< IDynamicTextureGenerator >	generator;
	Bool								mipChain = false;
	Uint8								samplesCount = 1;
	GpuWrapApi::eTextureFormat			format = GpuWrapApi::TEXFMT_R8G8B8A8_Unorm;
	GpuWrapApi::eTextureGroup			group;

#ifndef RED_CONFIGURATION_FINAL
	red::StringView						debugName;
#endif // RED_CONFIGURATION_FINAL
};

// 
struct DynamicVolumeTextureDesc
{
	Uint32								width = 0;
	Uint32								height = 0;
	Uint32								depth = 0;
	const void*							initData = nullptr;
};

//***************************************************************************************************************************//
//***************************************************************************************************************************//
