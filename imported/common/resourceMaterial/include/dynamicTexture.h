/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "texture.h"
#include "../../renderData/include/textureGroup.h"
#include "../../renderData/include/renderSimpleTypes.h"
#include "../../renderData/include/renderResource.h"
#include "../../redReflection/include/resourceAsyncReference.h"

class IMaterial;
class IDynamicTextureGenerator;

//***************************************************************************************************************************//
//***************************************************************************************************************************//

enum class DynamicTextureDataFormat : Uint8
{
	R_Uint8,
	R_Float16,
	R_Float32,
	RG_Float16,
	RG_Float32,
	RGBA_Uint8,
	RGBA_Uint8_SRGB,
	RGBA_Float16,
	RGBA_Float32
};

//***************************************************************************************************************************//
//***************************************************************************************************************************//

class RESOURCEMATERIAL_API DynamicTexture : public ITexture
{
	RTTI_DECLARE_TYPE( DynamicTexture );
	IMPLEMENT_RESOURCE_INTERFACE( "dtex", "dtex", "2D Dynamic Texture" );
	RED_BASE_CLASS( ITexture );

public:
	struct InitInfo
	{
		Uint32 width = 0;
		Uint32 height = 0;
		Bool mipChain = false;
		Uint8 samplesCount = 1;
		DynamicTextureDataFormat format = DynamicTextureDataFormat::RGBA_Uint8_SRGB;
		GpuWrapApi::eTextureGroup group = GpuWrapApi::TEXG_Unknown;
		TRenderPtr< IRenderResource > renderResource;
	};

	DynamicTexture();
	DynamicTexture( const InitInfo& initInfo );
	virtual ~DynamicTexture();

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 
	// ISerializable interface

	virtual void						OnPostLoad( const PostLoadContext& context ) override;

	virtual Bool						OnPropertyMissing(CName propertyName, const rtti::Variant& readValue) override;

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 
	// ITexture interface

	virtual const TResourcePtr			GetRenderResource() const override;
	virtual void						CreateRenderResource() override;
	virtual void						ReleaseRenderResource() override;
	virtual Bool						IsRenderResourceCreated() const override;

	virtual const STextureGroupSetup&	GetTextureSetup() const override;

	virtual ITexture::SizeDesc			GetSizeDesc() const override { return{ ( Uint16 )m_width, ( Uint16 )m_height, Uint16( 1 ), ( Uint8 )1 }; }
	ITexture::SizeDesc			        GetDynamicSizeDesc() const { return{ ( Uint16 )m_dynamicWidth, ( Uint16 )m_dynamicHeight, Uint16( 1 ), ( Uint8 )1 }; }

	Bool								HasMSAA() const { return m_samplesCount > 1; }
	Bool								HasMipChain() const { return m_mipChain; }
	Bool								HasDynamicSize() const { return m_width == 0 || m_height == 0; }
	Bool                                HasScaleToViewport() const { return m_scaleToViewport; }

	Bool                                IsDynamicSizeReady() const { return m_dynamicWidth > 0 && m_dynamicHeight > 0; }

	void								ApplyDynamicSize( Uint32 width, Uint32 height );

	virtual Uint32						GetRenderResourceSize() const override final;

	// get render texture for render target
	// NOTE: will be different than the one returned from GetRenderResource when MSAA is enabled
	TResourcePtr						GetRenderTargetResource() const;

	void								RecreateRenderResource_Safe();

	const THandle<IDynamicTextureGenerator>& GetGeneratorHandle() const { return m_generator; }
	void                                RegisterResolutionChangeListener( const THandle<ISerializable> &listener, red::Function< void() >&& callback );
	void                                UnRegisterResolutionChangeListener( const THandle<ISerializable> &listener );

	void                                TryAdjustToViewportResolution( Uint32 width, Uint32 height );
private:

	Uint32								GetRenderResourceSize_NoLock() const;
	void								RecreateRenderResource_NoLock();

	// static texture dimensions
	// Note: when either of these is 0, the dynamic size is used instead
	Uint32							m_width;
	Uint32							m_height;

	// custom texture dimension applied externally
	Bool                            m_scaleToViewport;
	Uint32							m_dynamicWidth;
	Uint32							m_dynamicHeight;

	DynamicTextureDataFormat		m_dataFormat;
	Bool							m_mipChain;						// Does the texture have a mip chain?
	Uint8							m_samplesCount;

	mutable red::RWSpinLock			m_resourceCreationLock;
	mutable red::RWSpinLock			m_listenerAccessLock;

	STextureGroupSetup				m_setup;

	TResourcePtr					m_renderResource;				// Always 1 sample
	TResourcePtr					m_renderTargetResource;			// Same as 'renderResource' if no MSSA

	res::ResourcePath 				m_resourcePath;

	THandle<IDynamicTextureGenerator>	m_generator;

	struct Callback
	{
		WeakHandle<ISerializable> m_listener;
		red::Function< void() > m_callback;
	};
	red::DynArray< Callback> m_listeners{ rend::PoolRenderResources() };

};

//***************************************************************************************************************************//
//***************************************************************************************************************************//
