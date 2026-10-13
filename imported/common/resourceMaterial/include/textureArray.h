/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "texture.h"
#include "../../renderData/include/textureGroup.h"
#include "../../renderData/include/renderObjectPtr.h"

class CBitmapTexture;

enum ECookingPlatform : Uint8;

//***************************************************************************************************************************//
//***************************************************************************************************************************//

/// Atlas entry
class RESOURCEMATERIAL_API CTextureArrayEntry
{
	RTTI_DECLARE_TYPE( CTextureArrayEntry );

public:
	TResRef< CBitmapTexture >	m_texture;			//!< Source texture
	
public:
	CTextureArrayEntry();
	CTextureArrayEntry( const THandle< CBitmapTexture >& texture );
	~CTextureArrayEntry();
};

//***************************************************************************************************************************//
//***************************************************************************************************************************//

/// Bitmap based texture
class RESOURCEMATERIAL_API CTextureArray : public ITexture
{
	RTTI_DECLARE_TYPE( CTextureArray );
	IMPLEMENT_RESOURCE_INTERFACE( "texarray", "texarray", "Texture Array" );
	RED_BASE_CLASS( ITexture );

protected:
	const THandle< IRenderResourceBlob >& GetRenderResourceBlob() const;

	mutable red::RWSpinLock				m_resourceCreationLock;
	TResourcePtr						m_renderResource;				// Rendering resource
	res::ResourcePath					m_resourcePath;

#ifndef RED_CONFIGURATION_FINAL
	red::DynArray< CTextureArrayEntry >	m_bitmaps{ rend::PoolTexture() };
#endif // RED_CONFIGURATION_FINAL
	
	STextureGroupSetup					m_setup;

	// DEPRECATED! Needs a resave
	THandle< IRenderResourceBlob >	m_renderResourceBlob;			// Render resource blob
	Uint32 m_renderResourceBlobSize;
	
	rend::RenderTextureResource		m_renderTextureResource;

public:

	CTextureArray();

	virtual ~CTextureArray();

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

	//! Get the width of the bitmap
	virtual ITexture::SizeDesc		GetSizeDesc() const override final;

	virtual const STextureGroupSetup& GetTextureSetup() const override final { return m_setup; }

#ifdef RED_CONFIGURATION_FINAL
	RED_FORCE_INLINE Uint32			GetTextureCount() const { return 0; }
#else
	RED_FORCE_INLINE Uint32			GetTextureCount() const { return m_bitmaps.Size(); }
#endif // RED_CONFIGURATION_FINAL

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

#ifndef RED_CONFIGURATION_FINAL
	Uint8							GetMaxMipCount() const;
#endif // RED_CONFIGURATION_FINAL

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

	//! Get the associated rendering resource
	virtual const TResourcePtr		GetRenderResource() const override;
	//! Upload texture array to the renderer
	virtual void					CreateRenderResource() override;
	//! Release render resource
	virtual void					ReleaseRenderResource() override;

	virtual Bool					IsRenderResourceCreated() const override { return static_cast< Bool >( m_renderResource ); }

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

	void							GetTextureNames( red::DynArray< CName >& textureNames ) const;

#ifndef RED_CONFIGURATION_FINAL	
	// TEMP : Just to fix texarray cooking with minimal impact. We should be able to use GetTextures() above, but it filters out any
	// null textures. It should be fixed so that it can return nulls, and anything that's using it should handle that. In the interests
	// of a safe fix for cooking, a separate function...
	THandle< CBitmapTexture >		GetTexture( Uint32 index ) const;
#endif // RED_CONFIGURATION_FINAL	

	void							SetTextures( const red::DynArray< THandle< CBitmapTexture > >& textures );
	
	//! Fill render resource blob during import and cooking
	void							InitRenderResourceBlob( ECookingPlatform platform, const THandle< rend::IRenderTextureBlob >& blob );

	// Drop non-platform texture blobs during cooking
	bool							Cooking_OptimiseRenderResourceBlob( ECookingPlatform platform );

	Bool							AddTexture( const THandle< CBitmapTexture >& texture );

	void							InsertTexture( const THandle< CBitmapTexture >& texture, Uint32 index );

	Bool							RemoveTextureAt( Uint32 index );
	Bool							RemoveTexture( const res::ResourcePath& texturePath );

	virtual Uint32					GetRenderResourceSize() const override;

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -


	//! Get additional resource info, displayed in editor
	virtual void					GetAdditionalInfo( red::DynArray< red::String >& info ) const override;

	//! Property was changed
	virtual void					OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& value ) override;

	// Property was read from file that is no longer in the object, returns true if data was handled
	virtual Bool					OnPropertyMissing( CName propertyName, const rtti::Variant& readValue ) override final;

	virtual void					OnPreSave( const PreSaveContext& context ) override;

	//! Resource was loaded from file
	virtual void					OnPostLoad( const PostLoadContext& context ) override;

#ifndef NO_DATA_VALIDATION
	virtual void					OnCheckDataErrors() const override;
#endif
};

//***************************************************************************************************************************//
//***************************************************************************************************************************//
