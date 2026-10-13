/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "texture.h"
#include "../../renderData/include/textureGroup.h"
#include "../../renderData/include/renderObjectPtr.h"
#include "../../redReflection/include/resourceAsyncReference.h"
#include "mipMapGenParams.h"

class CSourceTexture;
struct STextureGroupSetup;
enum ECookingPlatform : Uint8;

namespace rend
{
	class IRenderTextureBlob;
}

namespace img
{
	class Image;
	typedef red::SharedPtr<Image> ImagePtr;
}

class RESOURCEMATERIAL_API CBitmapTexture;

/// Bitmap based texture
class RESOURCEMATERIAL_API CBitmapTexture : public ITexture
{
	RTTI_DECLARE_TYPE( CBitmapTexture );
	IMPLEMENT_RESOURCE_INTERFACE( "xbm", "xbm", "Bitmap Texture" );
	RED_BASE_CLASS( ITexture );

public:

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

	CBitmapTexture();

	virtual ~CBitmapTexture();

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

	//! Get the width of the bitmap
	virtual ITexture::SizeDesc		GetSizeDesc() const override final { return{ (Uint16)m_width, (Uint16)m_height, (Uint16)m_depth, m_mipMapCount ? (Uint8)m_mipMapCount : GetMaxMipCount() }; }

	//! Setters, needed only for resizing
	RED_INLINE void					SetWidth(Uint32 width) { m_width = width; }
	RED_INLINE void					SetHeight(Uint32 height) { m_height = height; }
	RED_INLINE void					SetDepth(Uint32 depth) { m_depth = depth; }

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

	RED_INLINE virtual const STextureGroupSetup& GetTextureSetup() const override { return m_setup; }

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

	Uint32							GetDownscaleBiasForCookingPlatform( ECookingPlatform platform ) const;

	Uint32							GetHighestMipForCookingPlatform( ECookingPlatform platform ) const;

	void							SetDownscaleBiasForCookingPlatform( ECookingPlatform platform, Uint32 bias );

	// Get the mip level that should remain resident, relative to the highest cooked mip.
	Uint32							GetResidentMipForCookingPlatform( ECookingPlatform platform ) const;

	//! Get histogram stretch bias
	RED_INLINE Vector3				GetHistogramBiasMulCoef() const { return m_histBiasMulCoef; }
	RED_INLINE Vector3				GetHistogramBiasAddCoef() const { return m_histBiasAddCoef; }
	RED_INLINE void					SetHistogramBiasMulCoef( Vector3 coef ) { m_histBiasMulCoef = coef; }
	RED_INLINE void					SetHistogramBiasAddCoef( Vector3 coef ) { m_histBiasAddCoef = coef; }

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

	void							EnableMipChain( Bool enable );
	Uint8							GetMaxMipCount() const;

#ifndef RED_CONFIGURATION_FINAL
	img::ImagePtr					GetImage() const;
	void							SetMipMapGenParams( const MipMapGenParams& mipMapGenParams ) { m_mipMapGenParams = mipMapGenParams; }
	const MipMapGenParams&			GetMipMapGenParams() const { return m_mipMapGenParams; }

	RED_INLINE const THandle< CSourceTexture >& GetSourceData() const { return m_sourceData; }
#endif // RED_CONFIGURATION_FINAL

	//! Fill from source data
	Bool							InitFromSourceData( const THandle< CSourceTexture >& sourceTexture, const STextureGroupSetup& setup, Bool cancelTextureStreaming = true, Bool recreateRenderResource = true );

	//! Fill render resource blob during import and cooking
	void							InitRenderResourceBlob( ECookingPlatform platform, const THandle< rend::IRenderTextureBlob >& blob );

	// Drop non-platform texture blobs during cooking
	bool							Cooking_OptimiseRenderResourceBlob( ECookingPlatform platform );

#ifndef RED_CONFIGURATION_FINAL
	//! Set source data
	RED_INLINE void					SetSourceData( THandle< CSourceTexture> sourceData ) { m_sourceData = sourceData; }
#endif // RED_CONFIGURATION_FINAL

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

	virtual void					OnPreSave( const PreSaveContext& context ) override;

	//! Bitmap was loaded from file
	virtual void					OnPostLoad( const PostLoadContext& context ) override;

#ifndef NO_DATA_VALIDATION
	//! Validate this resource using the data error reporter
	virtual void					OnCheckDataErrors() const override;
#endif

	//! Get additional resource info, displayed in editor
	virtual void					GetAdditionalInfo( red::DynArray< red::String >& info ) const override;

	//! How many bytes does this resource occupy on the GPU?
	virtual Uint32					GetGPUSize() const override;

#ifndef RED_CONFIGURATION_FINAL
	Uint32 GetGPUSize( ECookingPlatform platform ) const;
#endif // RED_CONFIGURATION_FINAL

	// Property was read from file that is no longer in the object, returns true if data was handled
	virtual Bool					OnPropertyMissing( CName propertyName, const rtti::Variant& readValue ) override final;

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
	
	//! Get the related rendering resource
	virtual const TResourcePtr		GetRenderResource() const override;
	//! Create rendering resource for this bitmap texture
	virtual void					CreateRenderResource() override;
	//! Release rendering resource
	virtual void					ReleaseRenderResource() override;
	
	virtual Bool					IsRenderResourceCreated() const override;

	virtual Uint32					GetRenderResourceSize() const override;

private:
	const THandle< IRenderResourceBlob >& GetRenderResourceBlob() const;

	Uint32							m_width;						// Width of the texture
	Uint32							m_height;						// Height of the texture
	Uint32							m_depth;						// Depth of the texture
	STextureGroupSetup				m_setup;

	mutable red::RWSpinLock			m_resourceCreationLock;
	TResourcePtr					m_renderResource;				// Rendering resource

	//////////////////////////////////////////////////////////////////////////
	// DEPRECATED! Needs a resave
	THandle< IRenderResourceBlob >	m_renderResourceBlob;			// Render resource blob
	Uint32							m_renderResourceBlobSize;
	
	rend::RenderTextureResource		m_renderTextureResource;
	res::ResourcePath				m_resourcePath;

	Uint8                           m_mipMapCount;

	Vector3							m_histBiasMulCoef;
	Vector3							m_histBiasAddCoef;

	bool							m_createRenderResourceFailed;

#ifndef RED_CONFIGURATION_FINAL
	THandle< CSourceTexture >		m_sourceData;					// Source artist data
	MipMapGenParams					m_mipMapGenParams;
#endif // RED_CONFIGURATION_FINAL
};

