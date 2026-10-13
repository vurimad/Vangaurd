/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "multilayerLayerTemplate.h"
#include "../../resourceMaterial/include/bitmapTexture.h"


struct Multilayer_LayerTemplate::Overrides::Color
{
	RTTI_DECLARE_TYPE(Color);

	CName m_n;
	Float m_v[3];
};

struct Multilayer_LayerTemplate::Overrides::Levels
{
	RTTI_DECLARE_TYPE(Levels);
	CName m_n;
	Float m_v[2];
};

struct Multilayer_LayerTemplate::Overrides::NormalStrength
{
	RTTI_DECLARE_TYPE(NormalStrength);
	CName m_n;
	Float m_v;
};


RTTI_BEGIN_TYPE_IN_NAMESPACE( Color, Multilayer_LayerTemplate, Overrides );
{
	RTTI_PROPERTY(m_n).readOnly();
	RTTI_PROPERTY(m_v).readOnly();
}
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( Levels, Multilayer_LayerTemplate, Overrides );
{
	RTTI_PROPERTY(m_n).readOnly();
	RTTI_PROPERTY(m_v).readOnly();
}
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( NormalStrength, Multilayer_LayerTemplate, Overrides );
{
	RTTI_PROPERTY(m_n).readOnly();
	RTTI_PROPERTY(m_v).readOnly();
}
RTTI_END_TYPE();


RTTI_BEGIN_TYPE( Multilayer_LayerOverrideSelection );
{
	RTTI_PROPERTY(colorScale).setName("colorScale").readOnly();
	RTTI_PROPERTY(normalStrength).setName("normalStrength").readOnly();
	RTTI_PROPERTY(roughLevelsIn).setName("roughLevelsIn").readOnly();
	RTTI_PROPERTY(roughLevelsOut).setName("roughLevelsOut").readOnly();
	RTTI_PROPERTY(metalLevelsIn).setName("metalLevelsIn").readOnly();
	RTTI_PROPERTY(metalLevelsOut).setName("metalLevelsOut").readOnly();
}
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( Overrides, Multilayer_LayerTemplate );
{
	RTTI_PROPERTY(m_colorScale).readOnly();
	RTTI_PROPERTY(m_roughLevelsIn).readOnly();
	RTTI_PROPERTY(m_roughLevelsOut).readOnly();
	RTTI_PROPERTY(m_metalLevelsIn).readOnly();
	RTTI_PROPERTY(m_metalLevelsOut).readOnly();
	RTTI_PROPERTY(m_normalStrength).readOnly();
}
RTTI_END_TYPE();

RTTI_BEGIN_TYPE( Multilayer_LayerTemplate );
RTTI_PARENT_TYPE( CResource );
{
	RTTI_PROPERTY(m_overrides).readOnly();
	RTTI_PROPERTY(m_defaultOverrides).readOnly();
	RTTI_PROPERTY(m_colorTexture).readOnly();
	RTTI_PROPERTY(m_normalTexture).readOnly();
	RTTI_PROPERTY(m_roughnessTexture).readOnly();
	RTTI_PROPERTY(m_metalnessTexture).readOnly();
	RTTI_PROPERTY(m_tilingMultiplier).readOnly();
	RTTI_PROPERTY(m_colorMaskLevelsIn).readOnly();
	RTTI_PROPERTY(m_colorMaskLevelsOut).readOnly();
}
RTTI_END_TYPE();



template<typename T>
static Bool SortOverridesByName(const T& a, const T& b)
{
	return a.m_n < b.m_n;
}

template<typename T>
static Bool OverrideNameEquals(const T& a, const T& b)
{
	return a.m_n < b.m_n;
}


#ifdef RED_LOGGING_ENABLED
static Bool IsInBindlessGroup( GpuWrapApi::eTextureGroup group )
{
	static_assert( GpuWrapApi::TEXG_MAX == 34, "Texture groups changed" );
	switch ( group )
	{
	case GpuWrapApi::TEXG_Multilayer_Color:
	case GpuWrapApi::TEXG_Multilayer_Normal:
	case GpuWrapApi::TEXG_Multilayer_Grayscale:
	case GpuWrapApi::TEXG_Multilayer_Microblend:
		return true;

	default:
		return false;
	}
}

static void ValidateTexture( const AnsiChar* assetPath, const AnsiChar* textureName, const TResRef<CBitmapTexture>& texture )
{
	RED_DATA_ERROR_BEGIN(red::LoggerCategory_Rendering, texture.IsValid(), "Layer template has missing %hs texture '%hs'", textureName, texture.GetPath().ToDebugString() );
	RED_DATA_ERROR_ACTION_SHOW_ASSET(assetPath);
	RED_DATA_ERROR_END();

	// Not-great way of validating for a missing texture resource. If texture is missing, it gets replaced with a
	// default texture, so we can't just check for null... Should be checking for Bindless group anyways, but it would
	// be nice to check something else whether the texture exists or not...
	if ( texture.IsValid() )
	{
		// #TODO: Narrow it down further, show error if it's not using a Multilayer* group?
		const GpuWrapApi::eTextureGroup group = texture->GetTextureSetup().m_group;
		RED_DATA_ERROR_BEGIN(red::LoggerCategory_Rendering, IsInBindlessGroup( group ),
			"Layer template %hs texture must have correct texture group set (Multilayer_*). Maybe the texture is missing? '%hs'", textureName, texture.GetPath().ToDebugString());
		RED_DATA_ERROR_ACTION_SHOW_ASSET(assetPath);
		RED_DATA_ERROR_END();
	}
}

template<typename T>
static void ValidateOverrides( const AnsiChar* assetPath, const AnsiChar* overridesName, const red::DynArray<T>& overrides, CName defaultName )
{
	// #TODO: Check for duplicate names.

	auto defaultIter = red::alg::BinarySearch(overrides.Begin(), overrides.End(), T{defaultName}, &OverrideNameEquals<T> );
	RED_DATA_ERROR_BEGIN(red::LoggerCategory_Rendering, defaultIter != overrides.End(),
		"Layer template default %hs '%hs' not found. Probably going to get some artifacts when trying to use it.", overridesName, defaultName.AsChar());
	RED_DATA_ERROR_ACTION_SHOW_ASSET(assetPath);
	RED_DATA_ERROR_END();
}
#endif


Multilayer_LayerTemplate::Overrides::Overrides()
	: m_colorScale{rend::PoolRenderData()}
	, m_roughLevelsIn{rend::PoolRenderData()}
	, m_roughLevelsOut{rend::PoolRenderData()}
	, m_metalLevelsIn{rend::PoolRenderData()}
	, m_metalLevelsOut{rend::PoolRenderData()}
	, m_normalStrength{rend::PoolRenderData()}
#ifndef RED_CONFIGURATION_FINAL
	, m_colorScaleOriginal{rend::PoolRenderData()}
	, m_roughLevelsInOriginal{rend::PoolRenderData()}
	, m_roughLevelsOutOriginal{rend::PoolRenderData()}
	, m_metalLevelsInOriginal{rend::PoolRenderData()}
	, m_metalLevelsOutOriginal{rend::PoolRenderData()}
	, m_normalStrengthOriginal{rend::PoolRenderData()}
#endif
{
}

Multilayer_LayerTemplate::Overrides::~Overrides() = default;




Multilayer_LayerTemplate::Multilayer_LayerTemplate()
{
	m_tilingMultiplier = 1.0f;

	m_defaultOverrides.colorScale = RED_NAME_CONSTEXPR("null_null");
	m_defaultOverrides.normalStrength = RED_NAME_CONSTEXPR("null");
	m_defaultOverrides.roughLevelsIn = RED_NAME_CONSTEXPR("null");
	m_defaultOverrides.roughLevelsOut = RED_NAME_CONSTEXPR("null");
	m_defaultOverrides.metalLevelsIn = RED_NAME_CONSTEXPR("null");
	m_defaultOverrides.metalLevelsOut = RED_NAME_CONSTEXPR("null");

	// Default is to always use full coloring
	m_colorMaskLevelsIn[0] = 0.0f;
	m_colorMaskLevelsIn[1] = 1.0f;
	m_colorMaskLevelsOut[0] = 0.0f;
	m_colorMaskLevelsOut[1] = 1.0f;
}

Multilayer_LayerTemplate::~Multilayer_LayerTemplate() = default;


void Multilayer_LayerTemplate::OnPostLoad( const PostLoadContext& context )
{
	TBaseClass::OnPostLoad( context );

	// Make sure everything's in sorted order. Maybe not worth building a separate map/hashmap for each array, but we
	// can still ensure they're ordered for faster lookups.
#ifndef RED_CONFIGURATION_FINAL
	// In non-final, copy out the override arrays in their original ordering. This is used mainly by editor, for showing
	// the lists in a somewhat reasonable order.
	m_overrides.m_colorScaleOriginal = m_overrides.m_colorScale;
	m_overrides.m_roughLevelsInOriginal = m_overrides.m_roughLevelsIn;
	m_overrides.m_roughLevelsOutOriginal = m_overrides.m_roughLevelsOut;
	m_overrides.m_metalLevelsInOriginal = m_overrides.m_metalLevelsIn;
	m_overrides.m_metalLevelsOutOriginal = m_overrides.m_metalLevelsOut;
	m_overrides.m_normalStrengthOriginal = m_overrides.m_normalStrength;
#endif
	std::sort( m_overrides.m_colorScale.Begin(),     m_overrides.m_colorScale.End(),     &SortOverridesByName<Overrides::Color> );
	std::sort( m_overrides.m_roughLevelsIn.Begin(),  m_overrides.m_roughLevelsIn.End(),  &SortOverridesByName<Overrides::Levels> );
	std::sort( m_overrides.m_roughLevelsOut.Begin(), m_overrides.m_roughLevelsOut.End(), &SortOverridesByName<Overrides::Levels> );
	std::sort( m_overrides.m_metalLevelsIn.Begin(),  m_overrides.m_metalLevelsIn.End(),  &SortOverridesByName<Overrides::Levels> );
	std::sort( m_overrides.m_metalLevelsOut.Begin(), m_overrides.m_metalLevelsOut.End(), &SortOverridesByName<Overrides::Levels> );
	std::sort( m_overrides.m_normalStrength.Begin(), m_overrides.m_normalStrength.End(), &SortOverridesByName<Overrides::NormalStrength> );

#ifdef RED_LOGGING_ENABLED
	const char* pathString = GetPath().ToDebugString();
	ValidateTexture( pathString, "color", m_colorTexture );
	ValidateTexture( pathString, "normal", m_normalTexture );
	ValidateTexture( pathString, "roughness", m_roughnessTexture );
	ValidateTexture( pathString, "metalness", m_metalnessTexture );
	ValidateOverrides( pathString, "colorScale", m_overrides.m_colorScale, m_defaultOverrides.colorScale );
	ValidateOverrides( pathString, "roughLevelsIn", m_overrides.m_roughLevelsIn, m_defaultOverrides.roughLevelsIn );
	ValidateOverrides( pathString, "roughLevelsOut", m_overrides.m_roughLevelsOut, m_defaultOverrides.roughLevelsOut );
	ValidateOverrides( pathString, "metalLevelsIn", m_overrides.m_metalLevelsIn, m_defaultOverrides.metalLevelsIn );
	ValidateOverrides( pathString, "metalLevelsOut", m_overrides.m_metalLevelsOut, m_defaultOverrides.metalLevelsOut );
	ValidateOverrides( pathString, "normalStrength", m_overrides.m_normalStrength, m_defaultOverrides.normalStrength );
#endif
}

THandle<CBitmapTexture> Multilayer_LayerTemplate::GetColorTexture() const
{
	return m_colorTexture.IsValid() ? m_colorTexture.Get() : nullptr;
}

THandle<CBitmapTexture> Multilayer_LayerTemplate::GetNormalTexture() const
{
	return m_normalTexture.IsValid() ? m_normalTexture.Get() : nullptr;
}

THandle<CBitmapTexture> Multilayer_LayerTemplate::GetRoughnessTexture() const
{
	return m_roughnessTexture.IsValid() ? m_roughnessTexture.Get() : nullptr;
}

THandle<CBitmapTexture> Multilayer_LayerTemplate::GetMetalnessTexture() const
{
	return m_metalnessTexture.IsValid() ? m_metalnessTexture.Get() : nullptr;
}


template<typename T>
static T FindOverrideParam( const red::DynArray<T>& overrides, CName name, CName defaultName, const AnsiChar* param, const res::ResourcePath& assetPath, const res::ResourcePath& callerAssetPath )
{
	auto iter = red::alg::BinarySearch(overrides.Begin(), overrides.End(), T{name}, &OverrideNameEquals<T> );
	Bool foundMatch = ( iter != overrides.End() );
	Bool foundDefault = foundMatch;
	if ( !foundMatch )
	{
		// Didn't find match, try to find the given default.
		iter = red::alg::BinarySearch(overrides.Begin(), overrides.End(), T{defaultName}, &OverrideNameEquals<T> );
		foundDefault = ( iter != overrides.End() );

		// Error is found while in Multilayer_LayerTemplate, but the error is actually coming from the resource referencing
		// the template. So use that as the asset to show.
		// #TODO: This will generally show the mlsetup that references the mltemplate. But the ideal would probably be to
		// show the mesh that's using the mlsetup. Not so easy, would have to change how this is all done, or keep a list of
		// errors to later show elsewhere.
		RED_DATA_ERROR_BEGIN(red::LoggerCategory_Rendering, foundMatch,
			foundDefault
			? "Unable to find %hs with name %hs in layer template. Using default %hs"
			: "Unable to find %hs with name %hs in layer template. Couldn't find default named %hs either! Expect some visual artifacts",
			param, name.AsChar(), defaultName.AsChar());
		if ( callerAssetPath.IsValid() )
		{
			RED_DATA_ERROR_ACTION_SHOW_CUSTOM("Show usage in Asset Browser", callerAssetPath.ToDebugString());
		}
		RED_DATA_ERROR_ACTION_SHOW_CUSTOM("Show mltemplate in Asset Browser", assetPath.ToDebugString());
		RED_DATA_ERROR_END();
	}

	T result{};
	if ( iter != overrides.End() )
	{
		result = *iter;
	}
	return result;
}

Multilayer_LayerOverride Multilayer_LayerTemplate::GetOverrideParameters( const Multilayer_LayerOverrideSelection& selection, const res::ResourcePath& callerResourcePath ) const
{
	Multilayer_LayerOverride parameters;

	const res::ResourcePath& assetPath = GetPath();

	Overrides::Color colorScale = FindOverrideParam( m_overrides.m_colorScale, selection.colorScale, m_defaultOverrides.colorScale, "colorScale", assetPath, callerResourcePath );
	Overrides::Levels roughLevelsIn = FindOverrideParam( m_overrides.m_roughLevelsIn, selection.roughLevelsIn, m_defaultOverrides.roughLevelsIn, "roughLevelsIn", assetPath, callerResourcePath );
	Overrides::Levels roughLevelsOut = FindOverrideParam( m_overrides.m_roughLevelsOut, selection.roughLevelsOut, m_defaultOverrides.roughLevelsOut, "roughLevelsOut", assetPath, callerResourcePath );
	Overrides::Levels metalLevelsIn = FindOverrideParam( m_overrides.m_metalLevelsIn, selection.metalLevelsIn, m_defaultOverrides.metalLevelsIn, "metalLevelsIn", assetPath, callerResourcePath );
	Overrides::Levels metalLevelsOut = FindOverrideParam( m_overrides.m_metalLevelsOut, selection.metalLevelsOut, m_defaultOverrides.metalLevelsOut, "metalLevelsOut", assetPath, callerResourcePath );
	Overrides::NormalStrength normalStrength = FindOverrideParam( m_overrides.m_normalStrength, selection.normalStrength, m_defaultOverrides.normalStrength, "normalStrength", assetPath, callerResourcePath );

	parameters.colorScale = Vector3(colorScale.m_v);
	parameters.roughScaleIn = roughLevelsIn.m_v[0];
	parameters.roughBiasIn = roughLevelsIn.m_v[1];
	parameters.roughScaleOut = roughLevelsOut.m_v[0];
	parameters.roughBiasOut = roughLevelsOut.m_v[1];
	parameters.metalScaleIn = metalLevelsIn.m_v[0];
	parameters.metalBiasIn = metalLevelsIn.m_v[1];
	parameters.metalScaleOut = metalLevelsOut.m_v[0];
	parameters.metalBiasOut = metalLevelsOut.m_v[1];
	parameters.normalScale = normalStrength.m_v;

	// Not override-specific, but it's convenient to have here.
	parameters.colorMaskScaleIn = m_colorMaskLevelsIn[0];
	parameters.colorMaskBiasIn = m_colorMaskLevelsIn[1];
	parameters.colorMaskScaleOut = m_colorMaskLevelsOut[0];
	parameters.colorMaskBiasOut = m_colorMaskLevelsOut[1];

	return parameters;
}


#ifndef RED_CONFIGURATION_FINAL

template<typename T>
static red::DynArray<CName> GetOverrideNames( const red::DynArray<T>& overrides )
{
	red::DynArray<CName> names{ rend::PoolMaterials() };
	names.Reserve( overrides.Size() );
	for ( const T& ovr : overrides )
	{
		names.PushBack( ovr.m_n );
	}
	return names;
}

red::DynArray<CName> Multilayer_LayerTemplate::GetOverrideColorScaleNames() const
{
	return GetOverrideNames( m_overrides.m_colorScaleOriginal );
}

red::DynArray<CName> Multilayer_LayerTemplate::GetOverrideRoughLevelsInNames() const
{
	return GetOverrideNames( m_overrides.m_roughLevelsInOriginal );
}

red::DynArray<CName> Multilayer_LayerTemplate::GetOverrideRoughLevelsOutNames() const
{
	return GetOverrideNames( m_overrides.m_roughLevelsOutOriginal );
}

red::DynArray<CName> Multilayer_LayerTemplate::GetOverrideMetalLevelsInNames() const
{
	return GetOverrideNames( m_overrides.m_metalLevelsInOriginal );
}

red::DynArray<CName> Multilayer_LayerTemplate::GetOverrideMetalLevelsOutNames() const
{
	return GetOverrideNames( m_overrides.m_metalLevelsOutOriginal );
}

red::DynArray<CName> Multilayer_LayerTemplate::GetOverrideNormalStrengthNames() const
{
	return GetOverrideNames( m_overrides.m_normalStrengthOriginal );
}

#endif
