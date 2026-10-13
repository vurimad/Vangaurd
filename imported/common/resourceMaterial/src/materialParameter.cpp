/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "materialParameter.h"
#include "materialDefinition.h"
#include "texture.h"

/*
properties in GetParameterProperty() are not "static" anymore in the function call because of the multithreading issues
https://blogs.msdn.microsoft.com/oldnewthing/20040308-00/?p=40363
*/

RTTI_BEGIN_ABSTRACT_TYPE( CMaterialParameter );
RTTI_PARENT_TYPE( ISerializable );
	RTTI_PROPERTY( m_parameterName ).editable();
	RTTI_PROPERTY( m_register );
#ifndef RED_CONFIGURATION_FINAL
	RTTI_PROPERTY( m_tooltip ).notCooked();
	RTTI_PROPERTY( m_hidden ).notCooked();
	RTTI_PROPERTY( m_customEditorName ).notCooked();
#endif // RED_CONFIGURATION_FINAL
RTTI_END_TYPE();


THandle< CMaterialParameter > CreateParameter( const IMaterialDataProviderDesc::EParameterType type, IMaterialDefinition* parent, Float min, Float max )
{
	THandle< CMaterialParameter > ret;
	switch ( type )
	{
		static_assert(IMaterialDataProviderDesc::PT_MAX == 17, "EParameterType changed, check here!");
		case IMaterialDataProviderDesc::PT_Color:			ret = CreateHandle< CMaterialParameterColor >(); break;
		case IMaterialDataProviderDesc::PT_Scalar:			ret = CreateHandle< CMaterialParameterScalar >(min, max); break;
		case IMaterialDataProviderDesc::PT_Texture:			ret = CreateHandle< CMaterialParameterTexture >(); break;
		case IMaterialDataProviderDesc::PT_TextureArray:	ret = CreateHandle< CMaterialParameterTextureArray >(); break;
		case IMaterialDataProviderDesc::PT_Cube:			ret = CreateHandle< CMaterialParameterCube >(); break;
		case IMaterialDataProviderDesc::PT_Vector:			ret = CreateHandle< CMaterialParameterVector >(); break;
			case IMaterialDataProviderDesc::PT_StructBuffer:	ret = CreateHandle< CMaterialParameterStructBuffer >(); break;
			case IMaterialDataProviderDesc::PT_Cpu_NameU64:		ret = CreateHandle< CMaterialParameterCpuNameU64 >(); break;
#if !defined( VANGUARD_ASSET_HOST )
			case IMaterialDataProviderDesc::PT_SkinProfile:		ret = CreateHandle< CMaterialParameterSkinParameters >(); break;
			case IMaterialDataProviderDesc::PT_HairProfile:		ret = CreateHandle< CMaterialParameterHairParameters >(); break;
			case IMaterialDataProviderDesc::PT_Gradient:		ret = CreateHandle< CMaterialParameterGradient >(); break;
			case IMaterialDataProviderDesc::PT_FoliageProfile:	ret = CreateHandle< CMaterialParameterFoliageParameters >(); break;
			case IMaterialDataProviderDesc::PT_MultilayerSetup:	ret = CreateHandle< CMaterialParameterMultilayerSetup >(); break;
			case IMaterialDataProviderDesc::PT_MultilayerMask:	ret = CreateHandle< CMaterialParameterMultilayerMask >(); break;
			case IMaterialDataProviderDesc::PT_TerrainSetup:	ret = CreateHandle< CMaterialParameterTerrainSetup >(); break;
#endif
		}
	if ( ret )
	{
		ret->SetParent( parent );
	}
	return ret;
}

THandle< CMaterialParameter > CMaterialParameter::Create( IMaterialDefinition* parent, const CName& name, const IMaterialDataProviderDesc::EParameterType type, Uint32 reg, Float min, Float max, const red::String& tooltip /*=String::EMPTY()*/, const red::String& customEditorName /*=String::EMPTY()*/ )
{
	auto result = CreateParameter( type, parent, min, max );
	if ( result )
	{
		result->SetName( name );
		result->SetRegister( reg );
#ifndef RED_CONFIGURATION_FINAL
		result->SetTooltip( tooltip );
		result->SetCustomEditorName( customEditorName );
#endif // RED_CONFIGURATION_FINAL
	}
	return result;
}

void CMaterialParameter::OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& value )
{
	TBaseClass::OnPropertyPostChange( path, oldValue, value );
}

//////////////////////////////////////////////////////////////////////////

RTTI_BEGIN_TYPE( CMaterialParameterColor );
RTTI_PARENT_TYPE( CMaterialParameter );
	RTTI_PROPERTY( m_color ).editable();
RTTI_END_TYPE();

const rtti::Property* CMaterialParameterColor::GetProperty() const
{
	const rtti::Property* prop = GetClass()->FindProperty( RED_NAME_CONSTEXPR_NOREG( "color" ) );
	RED_ASSERT( prop );
	return prop;
}

RTTI_BEGIN_TYPE( CMaterialParameterScalar );
RTTI_PARENT_TYPE( CMaterialParameter );
	RTTI_PROPERTY( m_scalar ).editable();
	RTTI_PROPERTY( m_min ).editable();
	RTTI_PROPERTY( m_max ).editable();
RTTI_END_TYPE();

CMaterialParameterScalar::CMaterialParameterScalar()
	: m_min(-RED_FLT_MAX)
	, m_max(RED_FLT_MAX)
{}

CMaterialParameterScalar::CMaterialParameterScalar( Float min, Float max )
	: m_min( min )
	, m_max( max )
{}

const rtti::Property* CMaterialParameterScalar::GetProperty() const
{
	const rtti::Property* prop = GetClass()->FindProperty( RED_NAME_CONSTEXPR_NOREG( "scalar" ) );
	RED_ASSERT( prop );
	return prop;
}

RTTI_BEGIN_TYPE( CMaterialParameterTexture );
RTTI_PARENT_TYPE( CMaterialParameter );
	RTTI_PROPERTY( m_texture ).editable();
RTTI_END_TYPE();

const rtti::Property* CMaterialParameterTexture::GetProperty() const
{
	const rtti::Property* prop = GetClass()->FindProperty( RED_NAME_CONSTEXPR_NOREG( "texture" ) );
	RED_ASSERT( prop );
	return prop;
}

Bool CMaterialParameterTexture::Validate( red::StringBuilder< String >& errorMessageBuilder ) const
{
#ifndef RED_CONFIGURATION_FINAL
	if ( IsHidden() )
	{
		errorMessageBuilder.Append( String::Printf( "Warning: HIDDEN Param '%hs' with path '%hs' is invalid.\n", GetName().AsChar(), m_texture.GetPath().ToDebugString() ) );
		return true;
	}
#endif // RED_CONFIGURATION_FINAL
	if ( !m_texture.Get() )
	{
		errorMessageBuilder.Append( String::Printf( "Param '%hs' with path '%hs' is invalid.\n", GetName().AsChar(), m_texture.GetPath().ToDebugString() ) );
		return false;
	}
	return true;
}

#ifdef MATERIAL_PARAMETER_RESAVE
Bool CMaterialParameterTexture::OnPropertyTypeMismatch( CName propertyName, const rtti::Property* existingProperty, const rtti::Variant& readValue )
{
	if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "texture" ) )
	{
		THandle< ITexture > textureHandle;
		if ( readValue.AsType< THandle< ITexture > >( textureHandle ) )
		{
			m_texture.SetPath( textureHandle->GetPath() );
			return true;
		}
	}

	return TBaseClass::OnPropertyTypeMismatch( propertyName, existingProperty, readValue );
}
#endif // MATERIAL_PARAMETER_RESAVE

RTTI_BEGIN_TYPE( CMaterialParameterTextureArray );
RTTI_PARENT_TYPE( CMaterialParameter );
	RTTI_PROPERTY( m_texture ).editable();
RTTI_END_TYPE();

const rtti::Property* CMaterialParameterTextureArray::GetProperty() const
{
	const rtti::Property* prop = GetClass()->FindProperty( RED_NAME_CONSTEXPR_NOREG( "texture" ) );
	RED_ASSERT( prop );
	return prop;
}

#ifdef MATERIAL_PARAMETER_RESAVE
Bool CMaterialParameterTextureArray::OnPropertyTypeMismatch( CName propertyName, const rtti::Property* existingProperty, const rtti::Variant& readValue )
{
	if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "texture" ) )
	{
		THandle< ITexture > textureHandle;
		if ( readValue.AsType< THandle< ITexture > >( textureHandle ) )
		{
			m_texture.SetPath( textureHandle->GetPath() );
			return true;
		}
	}

	return TBaseClass::OnPropertyTypeMismatch( propertyName, existingProperty, readValue );
}
#endif // MATERIAL_PARAMETER_RESAVE

RTTI_BEGIN_TYPE( CMaterialParameterCube );
RTTI_PARENT_TYPE( CMaterialParameter );
	RTTI_PROPERTY( m_texture ).editable();
RTTI_END_TYPE();

const rtti::Property* CMaterialParameterCube::GetProperty() const
{
	const rtti::Property* prop = GetClass()->FindProperty( RED_NAME_CONSTEXPR_NOREG( "texture" ) );
	RED_ASSERT( prop );
	return prop;
}

#ifdef MATERIAL_PARAMETER_RESAVE
Bool CMaterialParameterCube::OnPropertyTypeMismatch( CName propertyName, const rtti::Property* existingProperty, const rtti::Variant& readValue )
{
	if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "texture" ) )
	{
		THandle< ITexture > textureHandle;
		if ( readValue.AsType< THandle< ITexture > >( textureHandle ) )
		{
			m_texture.SetPath( textureHandle->GetPath() );
			return true;
		}
	}

	return TBaseClass::OnPropertyTypeMismatch( propertyName, existingProperty, readValue );
}
#endif // MATERIAL_PARAMETER_RESAVE

RTTI_BEGIN_TYPE( CMaterialParameterVector );
RTTI_PARENT_TYPE( CMaterialParameter );
	RTTI_PROPERTY( m_vector ).editable();
RTTI_END_TYPE();

const rtti::Property* CMaterialParameterVector::GetProperty() const
{
	const rtti::Property* prop = GetClass()->FindProperty( RED_NAME_CONSTEXPR_NOREG( "vector" ) );
	RED_ASSERT( prop );
	return prop;
}

RTTI_BEGIN_TYPE( CMaterialParameterStructBuffer );
RTTI_PARENT_TYPE( CMaterialParameter );
RTTI_END_TYPE();

const rtti::Property* CMaterialParameterStructBuffer::GetProperty() const
{
	return nullptr;
}


RTTI_BEGIN_TYPE( CMaterialParameterCpuNameU64 );
RTTI_PARENT_TYPE( CMaterialParameter );
RTTI_PROPERTY( m_name ).editable();
RTTI_END_TYPE();

const rtti::Property* CMaterialParameterCpuNameU64::GetProperty() const
{
	const rtti::Property* prop = GetClass()->FindProperty( RED_NAME_CONSTEXPR_NOREG( "name" ) );
	RED_ASSERT( prop );
	return prop;
}

//////////////////////////////////////////////////////////////////////////

RTTI_BEGIN_TYPE( CMaterialParameterSkinParameters );
RTTI_PARENT_TYPE( CMaterialParameter );
#if !defined( VANGUARD_ASSET_HOST )
	RTTI_PROPERTY( m_skinProfile ).editable();
#endif
RTTI_END_TYPE();

const rtti::Property* CMaterialParameterSkinParameters::GetProperty() const
{
#if defined( VANGUARD_ASSET_HOST )
	return nullptr;
#else
	const rtti::Property* prop = GetClass()->FindProperty( RED_NAME_CONSTEXPR_NOREG( "skinProfile" ) );
	RED_ASSERT( prop );
	return prop;
#endif
}

//////////////////////////////////////////////////////////////////////////

RTTI_BEGIN_TYPE( CMaterialParameterHairParameters );
RTTI_PARENT_TYPE( CMaterialParameter );
#if !defined( VANGUARD_ASSET_HOST )
	RTTI_PROPERTY( m_hairProfile ).editable();
#endif
RTTI_END_TYPE();

const rtti::Property* CMaterialParameterHairParameters::GetProperty() const
{
#if defined( VANGUARD_ASSET_HOST )
	return nullptr;
#else
	const rtti::Property* prop = GetClass()->FindProperty( RED_NAME_CONSTEXPR_NOREG( "hairProfile" ) );
	RED_ASSERT( prop );
	return prop;
#endif
}

//////////////////////////////////////////////////////////////////////////

RTTI_BEGIN_TYPE(CMaterialParameterGradient);
	RTTI_PARENT_TYPE(CMaterialParameter);
#if !defined( VANGUARD_ASSET_HOST )
	RTTI_PROPERTY(m_gradient).editable();
#endif
RTTI_END_TYPE();

const rtti::Property* CMaterialParameterGradient::GetProperty() const
{
#if defined( VANGUARD_ASSET_HOST )
	return nullptr;
#else
	const rtti::Property* prop = GetClass()->FindProperty(RED_NAME_CONSTEXPR_NOREG("gradient"));
	RED_ASSERT(prop);
	return prop;
#endif
}

//////////////////////////////////////////////////////////////////////////

RTTI_BEGIN_TYPE(CMaterialParameterFoliageParameters);
RTTI_PARENT_TYPE(CMaterialParameter);
#if !defined( VANGUARD_ASSET_HOST )
	RTTI_PROPERTY(m_foliageProfile).editable();
#endif
RTTI_END_TYPE();

const rtti::Property* CMaterialParameterFoliageParameters::GetProperty() const
{
#if defined( VANGUARD_ASSET_HOST )
	return nullptr;
#else
	const rtti::Property* prop = GetClass()->FindProperty(RED_NAME_CONSTEXPR_NOREG("foliageProfile"));
	RED_ASSERT(prop);
	return prop;
#endif
}

//////////////////////////////////////////////////////////////////////////

RTTI_BEGIN_TYPE( CMaterialParameterMultilayerSetup );
RTTI_PARENT_TYPE( CMaterialParameter );
#if !defined( VANGUARD_ASSET_HOST )
	RTTI_PROPERTY( m_setup ).editable();
#endif
RTTI_END_TYPE();

const rtti::Property* CMaterialParameterMultilayerSetup::GetProperty() const
{
#if defined( VANGUARD_ASSET_HOST )
	return nullptr;
#else
	const rtti::Property* prop = GetClass()->FindProperty( RED_NAME_CONSTEXPR_NOREG("setup") );
	RED_ASSERT( prop );
	return prop;
#endif
}

//////////////////////////////////////////////////////////////////////////

RTTI_BEGIN_TYPE( CMaterialParameterMultilayerMask );
RTTI_PARENT_TYPE( CMaterialParameter );
#if !defined( VANGUARD_ASSET_HOST )
	RTTI_PROPERTY( m_mask ).editable();
#endif
RTTI_END_TYPE();

const rtti::Property* CMaterialParameterMultilayerMask::GetProperty() const
{
#if defined( VANGUARD_ASSET_HOST )
	return nullptr;
#else
	const rtti::Property* prop = GetClass()->FindProperty( RED_NAME_CONSTEXPR_NOREG("mask") );
	RED_ASSERT( prop );
	return prop;
#endif
}

//////////////////////////////////////////////////////////////////////////

RTTI_BEGIN_TYPE( CMaterialParameterTerrainSetup );
RTTI_PARENT_TYPE( CMaterialParameter );
#if !defined( VANGUARD_ASSET_HOST )
	RTTI_PROPERTY( m_setup ).editable();
#endif
RTTI_END_TYPE();

const rtti::Property* CMaterialParameterTerrainSetup::GetProperty() const
{
#if defined( VANGUARD_ASSET_HOST )
	return nullptr;
#else
	const rtti::Property* prop = GetClass()->FindProperty( RED_NAME_CONSTEXPR_NOREG("setup") );
	RED_ASSERT( prop );
	return prop;
#endif
}
