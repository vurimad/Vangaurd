/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#pragma once

using red::DynArray;

class BACKEND_MATERIAL_API TextureImportOptions : public ResourceImportOptions
{
	RTTI_DECLARE_TYPE( TextureImportOptions );
	RED_BASE_CLASS( ResourceImportOptions );

public:
	TextureImportOptions()
		: m_textureGroup( GpuWrapApi::eTextureGroup::TEXG_Generic_Color )
		, m_textureCompressionType( ETextureCompression::TCM_None)
		, m_platformMipBiasPC( 0 )
		, m_platformMipBiasConsole( 0 )
		, m_premultiply( false )
		, m_overrideGammaSetting( false )
		, m_isGamma( false )
	{}

	virtual ~TextureImportOptions() {}

	virtual Bool NeedOptions() const override;

	virtual bool ParseFromCommandLine( const red::CommandLine& stringOptions ) override;

	virtual void GetCustomEditableProperties( rtti::EditableProperties& outRootProperties ) const override;
	virtual void OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& newValue ) override;

	GpuWrapApi::eTextureGroup m_textureGroup;
	ETextureCompression m_textureCompressionType;
	Uint8 m_platformMipBiasPC;
	Uint8 m_platformMipBiasConsole;
	Bool m_premultiply;
	Bool m_overrideGammaSetting;
	Bool m_isGamma;

private:
	static Bool IsMipBiasValid( Int32 mipBias );

	static Bool GetTextureRawFormatByName( const AnsiChar* textureRawFormatName, ETextureRawFormat& textureRawFormat );

	static Bool GetCompressionTypeByName( const AnsiChar* compressionTypeName, ETextureCompression& textureCompressionType );

	virtual void OnResourcePathSet() override;

	static constexpr Uint8 MAX_MIP_BIAS_FOR_USER = 12;  // Last (1 px) mipmap for a 4096 px texture
};
