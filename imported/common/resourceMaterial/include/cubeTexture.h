/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "texture.h"
#include "../../renderData/include/textureGroup.h"
#include "../../renderData/include/renderObjectPtr.h"

class CSourceTexture;

enum ECookingPlatform : Uint8;

enum ECubeSourceTextureType
{
	CST_CrossHorizontal,
	CST_CrossVertical,
	CST_Panorama,

	CST_Invalid,
};

namespace rend
{
	class IRenderTextureBlob;
}

namespace img
{
	class Image;
	typedef red::SharedPtr<Image> ImagePtr;
}

RTTI_DECLARE_ENUM( ECubeSourceTextureType );

/// Bitmap based texture
class RESOURCEMATERIAL_API CCubeTexture : public ITexture
{
	RTTI_DECLARE_TYPE( CCubeTexture );
	IMPLEMENT_RESOURCE_INTERFACE( "xcube", "cubemap", "Cubemap" );
	RED_BASE_CLASS( ITexture );

protected:

	const THandle< IRenderResourceBlob >& GetRenderResourceBlob() const;

	Uint32							m_size;							//!< Size (Width/Height) of the cube face
#ifndef RED_CONFIGURATION_FINAL
	Uint32							m_targetFaceSize;				//!< Override source data target size
#endif // RED_CONFIGURATION_FINAL

	STextureGroupSetup				m_setup;

#ifndef RED_CONFIGURATION_FINAL
	ECubeSourceTextureType			m_sourceTextureType;			//!< Source data type
	THandle< CSourceTexture >		m_sourceData;					//!< Source artist data
#endif // RED_CONFIGURATION_FINAL

	mutable red::RWSpinLock			m_resourceCreationLock;
	TResourcePtr					m_renderResource;				//!< Rendering resource
	
	// DEPRECATED! Needs a resave
	THandle< IRenderResourceBlob >	m_renderResourceBlob;			// Render resource blob
	Uint32 m_renderResourceBlobSize;

	rend::RenderTextureResource		m_renderTextureResource;
	res::ResourcePath				m_resourcePath;

	bool m_createRenderResourceFailed;

public:
	CCubeTexture();
	virtual ~CCubeTexture();

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

	RED_INLINE virtual const STextureGroupSetup& GetTextureSetup() const override { return m_setup; }

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

	//! Get the width of the bitmap
	virtual ITexture::SizeDesc				GetSizeDesc() const override { return{ (Uint16)m_size, (Uint16)m_size, (Uint16)1, (Uint8)1 }; }

#ifndef RED_CONFIGURATION_FINAL
	//! Get the width/height since cube textures ought to be rectangular anyway
	RED_INLINE Uint32						GetSize() const { return m_targetFaceSize > 0 ? m_targetFaceSize : m_size; }

	//! Get source data
	RED_INLINE CSourceTexture*				GetSourceData() const { return m_sourceData.Get(); }

	RED_INLINE ECubeSourceTextureType		GetSourceTextureType() const { return m_sourceTextureType; }
#endif // RED_CONFIGURATION_FINAL

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

	Bool									InitFromSourceData( const THandle< CSourceTexture >& sourceTexture, const STextureGroupSetup& setup, ECubeSourceTextureType sourceType );
	//! Fill render resource blob during import and cooking
	void									InitRenderResourceBlob( ECookingPlatform platform, const THandle< rend::IRenderTextureBlob >& blob );

	// Drop non-platform texture blobs during cooking
	bool									Cooking_OptimiseRenderResourceBlob( ECookingPlatform platform );

#ifndef RED_CONFIGURATION_FINAL
	Uint8									GetMaxMipCount() const;
#endif // RED_CONFIGURATION_FINAL

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

	//! Get the associated rendering resource
	virtual const TResourcePtr				GetRenderResource() const override;

	//! Upload cubemap to the renderer
	virtual void							CreateRenderResource() override;

	//! Release render resource
	virtual void							ReleaseRenderResource() override;

	virtual Bool							IsRenderResourceCreated() const override { return static_cast< Bool >( m_renderResource ); }

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

#ifndef RED_CONFIGURATION_FINAL
	img::ImagePtr							GetImage() const;

	img::ImagePtr							GetImage( Uint16 texSlice ) const;
#endif // RED_CONFIGURATION_FINAL

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

	//! Property was changed
	void									OnPropertyPostChange( const rtti::Property* property );

	//! Serialize data
	virtual void							OnSerialize( IFile& file ) override;

	virtual void							OnPreSave( const PreSaveContext& context ) override;

	//! Resource loaded
	virtual void							OnPostLoad( const PostLoadContext& context ) override;

	virtual Uint32							GetRenderResourceSize() const override;

};

