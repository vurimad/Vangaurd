/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once
#include "../../renderData/include/materialDataProvider.h"
#include "skinProfile.h"
#include "hairProfile.h"
#include "foliageProfile.h"
#include "../../redContainers/include/string/stringBuilder.h"
#include "gradient.h"

//#define MATERIAL_PARAMETER_RESAVE

class IMaterialDefinition;
class ITexture;
class Multilayer_Setup;
class Multilayer_Mask;
class CTerrainSetup;


class RESOURCEMATERIAL_API CMaterialParameter : public ISerializable
{
	RTTI_DECLARE_TYPE( CMaterialParameter );
	RED_BASE_CLASS( ISerializable );
	RED_USE_MEMORY_POOL( rend::PoolMaterials );

protected:
	CName		m_parameterName;		//!< Name of the parameter
	Uint32		m_register;
#ifndef RED_CONFIGURATION_FINAL
	String		m_tooltip;
	Bool		m_hidden;				//!< Is hidden in editor
	String		m_customEditorName;
#endif // RED_CONFIGURATION_FINAL

public:
	//! Get parameter name
	RED_INLINE const CName& GetName() const				{ return m_parameterName;			}
	RED_INLINE const Uint32 GetRegister() const			{ return m_register;				}
#ifndef RED_CONFIGURATION_FINAL
	RED_INLINE const String& GetTooltip() const			{ return m_tooltip;					}
	RED_INLINE Bool IsHidden() const					{ return m_hidden;					}
	RED_INLINE const String& GetCustomEditorName() const{ return m_customEditorName;		}
#endif // RED_CONFIGURATION_FINAL

	//! Get parameter name
	RED_INLINE void SetName( const CName paramName )	{ m_parameterName = paramName;		}
	RED_INLINE void SetRegister( Uint32 reg )			{ m_register = reg;					}
#ifndef RED_CONFIGURATION_FINAL
	RED_INLINE void SetTooltip( const String& tooltip )	{ m_tooltip = tooltip;				}
	RED_INLINE void SetHidden( Bool hidden )			{ m_hidden = hidden;				}
	RED_INLINE void SetCustomEditorName( const String& customEditorName ) { m_customEditorName = customEditorName; }
#endif // RED_CONFIGURATION_FINAL

public:
	void OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& value ) override;

	virtual const rtti::Property* GetProperty() const = 0;
	virtual IMaterialDataProviderDesc::EParameterType GetType() const { return IMaterialDataProviderDesc::PT_None; }
	virtual Uint32 GetDataSize() const = 0;

	virtual void SetMin(Float min){ }
	virtual void SetMax(Float max){ }

	virtual Float GetMin() const { return 0.f; }
	virtual Float GetMax() const { return 0.f; }

	virtual Bool CanHaveMinMax() const { return false; }

	virtual Bool Validate( red::StringBuilder< String >& errorMessageBuilder ) const { return true; }

	static THandle< CMaterialParameter > Create( IMaterialDefinition* parent, const CName& name, const IMaterialDataProviderDesc::EParameterType type, Uint32 reg, Float min = -RED_FLT_MAX, Float max = RED_FLT_MAX, const red::String& tooltip = String::EMPTY(), const red::String& customEditorName = String::EMPTY() );
};


class RESOURCEMATERIAL_API CMaterialParameterColor : public CMaterialParameter
{
	RTTI_DECLARE_TYPE( CMaterialParameterColor );

public:
	virtual const rtti::Property* GetProperty() const override;
	virtual IMaterialDataProviderDesc::EParameterType GetType() const { return IMaterialDataProviderDesc::PT_Color; }
	virtual Uint32 GetDataSize() const { return sizeof( Color ); }
	RED_INLINE Color GetData() const { return m_color; };
	RED_INLINE void SetData( Color color ) { m_color = color; };

private:
	Color	m_color;
};


class RESOURCEMATERIAL_API CMaterialParameterScalar : public CMaterialParameter
{
	RTTI_DECLARE_TYPE( CMaterialParameterScalar );

public:
	CMaterialParameterScalar();
	CMaterialParameterScalar(Float min, Float max);
	virtual const rtti::Property* GetProperty() const override;
	virtual IMaterialDataProviderDesc::EParameterType GetType() const { return IMaterialDataProviderDesc::PT_Scalar; }
	virtual Uint32 GetDataSize() const { return sizeof( Float ); }

	virtual Float GetMin() const { return m_min; }
	virtual Float GetMax() const { return m_max; }

	virtual void SetMin(Float min) override { m_min = min; }
	virtual void SetMax(Float max) override { m_max = max; }
	virtual Bool CanHaveMinMax() const { return true; }

	RED_INLINE Float GetData() const { return m_scalar; }
	RED_INLINE void SetData( Float scalar ) { m_scalar = scalar; }

private:
	Float	m_scalar;
	Float	m_min;
	Float	m_max;
};

class RESOURCEMATERIAL_API CMaterialParameterTexture : public CMaterialParameter
{
	RTTI_DECLARE_TYPE( CMaterialParameterTexture );

public:
	virtual const rtti::Property* GetProperty() const override;
	virtual IMaterialDataProviderDesc::EParameterType GetType() const { return IMaterialDataProviderDesc::PT_Texture; }
	virtual Uint32 GetDataSize() const { return sizeof( void* ); } // this is 32/64 bit compatible

	void SetTextureGroup(GpuWrapApi::eTextureGroup textureGroup) { m_textureGroup = textureGroup; }
	GpuWrapApi::eTextureGroup GetTextureGroup() const { return m_textureGroup; }

	virtual Bool Validate( red::StringBuilder< String >& errorMessageBuilder ) const;

#ifdef MATERIAL_PARAMETER_RESAVE
	virtual Bool OnPropertyTypeMismatch( CName propertyName, const rtti::Property* existingProperty, const rtti::Variant& readValue );
#endif // MATERIAL_PARAMETER_RESAVE

public:
	TResRef< ITexture > m_texture;
	GpuWrapApi::eTextureGroup m_textureGroup;
};


class RESOURCEMATERIAL_API CMaterialParameterTextureArray : public CMaterialParameter
{
	RTTI_DECLARE_TYPE( CMaterialParameterTextureArray );

public:
	virtual const rtti::Property* GetProperty() const override;
	virtual IMaterialDataProviderDesc::EParameterType GetType() const { return IMaterialDataProviderDesc::PT_TextureArray; }
	virtual Uint32 GetDataSize() const { return sizeof( void* ); } // this is 32/64 bit compatible

#ifdef MATERIAL_PARAMETER_RESAVE
	virtual Bool OnPropertyTypeMismatch( CName propertyName, const rtti::Property* existingProperty, const rtti::Variant& readValue );
#endif // MATERIAL_PARAMETER_RESAVE

public:
	TResRef< ITexture > m_texture;
};

class RESOURCEMATERIAL_API CMaterialParameterCube : public CMaterialParameter
{
	RTTI_DECLARE_TYPE( CMaterialParameterCube );

public:
	virtual const rtti::Property* GetProperty() const override;
	virtual IMaterialDataProviderDesc::EParameterType GetType() const { return IMaterialDataProviderDesc::PT_Cube; }
	virtual Uint32 GetDataSize() const { return sizeof( void* ); } // this is 32/64 bit compatible

#ifdef MATERIAL_PARAMETER_RESAVE
	virtual Bool OnPropertyTypeMismatch( CName propertyName, const rtti::Property* existingProperty, const rtti::Variant& readValue );
#endif // MATERIAL_PARAMETER_RESAVE

public:
	TResRef< ITexture > m_texture;
};


class RESOURCEMATERIAL_API CMaterialParameterVector : public CMaterialParameter
{
	RTTI_DECLARE_TYPE( CMaterialParameterVector );

public:
	virtual const rtti::Property* GetProperty() const override;
	virtual IMaterialDataProviderDesc::EParameterType GetType() const { return IMaterialDataProviderDesc::PT_Vector; }
	virtual Uint32 GetDataSize() const { return sizeof( Vector4 ); }

public:
	Vector4	m_vector;
};


class RESOURCEMATERIAL_API CMaterialParameterStructBuffer : public CMaterialParameter
{
	RTTI_DECLARE_TYPE( CMaterialParameterStructBuffer );

public:
	virtual const rtti::Property* GetProperty() const override;
	virtual IMaterialDataProviderDesc::EParameterType GetType() const { return IMaterialDataProviderDesc::PT_StructBuffer; }
	virtual Uint32 GetDataSize() const { return sizeof( void* ); } // this is 32/64 bit compatible
};


class RESOURCEMATERIAL_API CMaterialParameterCpuNameU64 : public CMaterialParameter
{
	RTTI_DECLARE_TYPE( CMaterialParameterCpuNameU64 );

public:
	virtual const rtti::Property* GetProperty() const override;
	virtual IMaterialDataProviderDesc::EParameterType GetType() const { return IMaterialDataProviderDesc::PT_Cpu_NameU64; }
	virtual Uint32 GetDataSize() const { return sizeof( Uint64 ); }

public:
	CName m_name;
};

class RESOURCEMATERIAL_API CMaterialParameterSkinParameters : public CMaterialParameter
{
	RTTI_DECLARE_TYPE( CMaterialParameterSkinParameters );

public:
	virtual const rtti::Property* GetProperty() const override;
	virtual IMaterialDataProviderDesc::EParameterType GetType() const { return IMaterialDataProviderDesc::PT_SkinProfile; }
	virtual Uint32 GetDataSize() const { return sizeof( void* ); } // this is 32/64 bit compatible

public:
	TResRef< CSkinProfile > m_skinProfile;
};

class RESOURCEMATERIAL_API CMaterialParameterHairParameters : public CMaterialParameter
{
	RTTI_DECLARE_TYPE( CMaterialParameterHairParameters );

public:
	virtual const rtti::Property* GetProperty() const override;
	virtual IMaterialDataProviderDesc::EParameterType GetType() const { return IMaterialDataProviderDesc::PT_HairProfile; }
	virtual Uint32 GetDataSize() const { return sizeof( void* ); } // this is 32/64 bit compatible

public:
	TResRef< CHairProfile > m_hairProfile;
};

class RESOURCEMATERIAL_API CMaterialParameterGradient : public CMaterialParameter
{
	RTTI_DECLARE_TYPE(CMaterialParameterGradient);

public:
	virtual const rtti::Property* GetProperty() const override;
	virtual IMaterialDataProviderDesc::EParameterType GetType() const { return IMaterialDataProviderDesc::PT_Gradient; }
	virtual Uint32 GetDataSize() const { return sizeof(void*); } // this is 32/64 bit compatible

public:
	TResRef< CGradient > m_gradient;
};

class RESOURCEMATERIAL_API CMaterialParameterFoliageParameters : public CMaterialParameter
{
	RTTI_DECLARE_TYPE(CMaterialParameterFoliageParameters);

public:
	virtual const rtti::Property* GetProperty() const override;
	virtual IMaterialDataProviderDesc::EParameterType GetType() const { return IMaterialDataProviderDesc::PT_FoliageProfile; }
	virtual Uint32 GetDataSize() const { return sizeof(void*); } // this is 32/64 bit compatible

public:
	TResRef< CFoliageProfile > m_foliageProfile;
};

class RESOURCEMATERIAL_API CMaterialParameterMultilayerSetup : public CMaterialParameter
{
	RTTI_DECLARE_TYPE( CMaterialParameterMultilayerSetup );

public:
	virtual const rtti::Property* GetProperty() const override;
	virtual IMaterialDataProviderDesc::EParameterType GetType() const { return IMaterialDataProviderDesc::PT_MultilayerSetup; }
	virtual Uint32 GetDataSize() const { return sizeof( void* ); }

public:
	TResRef< Multilayer_Setup > m_setup;
};

class RESOURCEMATERIAL_API CMaterialParameterMultilayerMask : public CMaterialParameter
{
	RTTI_DECLARE_TYPE( CMaterialParameterMultilayerMask );

public:
	virtual const rtti::Property* GetProperty() const override;
	virtual IMaterialDataProviderDesc::EParameterType GetType() const { return IMaterialDataProviderDesc::PT_MultilayerMask; }
	virtual Uint32 GetDataSize() const { return sizeof( void* ); }

public:
	TResRef< Multilayer_Mask > m_mask;
};


class RESOURCEMATERIAL_API CMaterialParameterTerrainSetup : public CMaterialParameter
{
	RTTI_DECLARE_TYPE( CMaterialParameterTerrainSetup );

public:
	virtual const rtti::Property* GetProperty() const override;
	virtual IMaterialDataProviderDesc::EParameterType GetType() const { return IMaterialDataProviderDesc::PT_TerrainSetup; }
	virtual Uint32 GetDataSize() const { return sizeof( void* ); }

public:
	TResRef< CTerrainSetup > m_setup;
};
