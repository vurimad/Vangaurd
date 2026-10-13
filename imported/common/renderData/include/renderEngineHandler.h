/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#pragma once

//class CMask;

class IRenderEngineHandler
{
	RED_USE_MEMORY_POOL( rend::PoolRenderingCore );

public:
	virtual ~IRenderEngineHandler () {}

	virtual void ReloadEngineShaders() const = 0;
	virtual void ReloadTextures() const = 0;
	virtual void ReloadMultilayerMaterial() const = 0;
	virtual void CreateTextureResourcesOnInitDevice() const = 0;
	virtual void ReleaseTexturesOnCloseDevice() const = 0;
	virtual void ReleaseRenderingResources() const = 0;
	virtual void ResetDevice() const = 0;
	virtual void LoadRenderResources( job::Builder& builder ) = 0;

	virtual TRenderPtr<IRenderResource> GetFallbackMaterial() const = 0;
	virtual TRenderPtr<IRenderResource> GetDefaultMeshMaterial() const = 0;
	virtual TRenderPtr<IRenderResource> GetMultiLayerBakedMaterial() const = 0;
	virtual TRenderPtr<IRenderResource> GetDebugColoringMaterial() const = 0;
	virtual TRenderPtr<IRenderResource> GetMultilayeredDebugMaterial() const = 0;
	virtual TRenderPtr<IRenderResource> GetGIBackFaceDebugMaterial() const = 0;
	virtual TRenderPtr<IRenderResource> GetDebugMaterial() const = 0;
	virtual TRenderPtr<IRenderResource> GetOverdrawDebugMaterial() const = 0;
	virtual TRenderPtr<IRenderResource> GetOverdrawSeethroughDebugMaterial() const = 0;
	virtual TRenderPtr<IRenderResource> GetWireframeMaterial() const = 0;
	virtual TRenderPtr<IRenderResource> GetHitProxyMaterial() const = 0;
	virtual TRenderPtr<IRenderResource> GetSelectionMaterial() const = 0;
	virtual TRenderPtr<IRenderResource> GetUvDensityDebugMaterial() const = 0;
	virtual TRenderPtr<IRenderResource> GetFontMaterial() const = 0;
	virtual TRenderPtr<IRenderResource> GetDebugWireframeMaterial() const = 0;
	virtual TRenderPtr<IRenderResource> GetBiasedDebugMaterial() const = 0;
	virtual TRenderPtr<IRenderResource> GetBiasedDebugWireframeMaterial() const = 0;
	virtual TRenderPtr<IRenderResource> GetDefaultTextureRegionSet() const = 0;
	virtual TRenderPtr<IRenderResource> GetShadowsDebugMaterial() const = 0;
	virtual TRenderPtr<IRenderResource> GetLODColoringDebugMaterial() const = 0;
	virtual TRenderPtr<IRenderResource> GetNoiseTexArray() const = 0;
	virtual TRenderPtr<IRenderResource> GetBlueNoiseTexArray() const = 0;
	virtual TRenderPtr<IRenderResource> GetBetFitNormalTexture() const = 0;

	virtual Float GetRawEngineTime() const = 0;

	virtual void FlushAllLoading() const = 0;
	virtual void RequestExit() const = 0;

	virtual Bool IsTakingScreenshot() const = 0;
};
