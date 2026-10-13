/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../../common/redReflection/include/resource.h"


class CBitmapTexture;


struct Multilayer_LayerOverrideSelection
{
	RTTI_DECLARE_TYPE( Multilayer_LayerOverrideSelection );

	CName colorScale;
	CName normalStrength;
	CName roughLevelsIn;
	CName roughLevelsOut;
	CName metalLevelsIn;
	CName metalLevelsOut;
};

struct Multilayer_LayerOverride
{
	Vector3 colorScale;
	Float normalScale;
	Float roughScaleIn;
	Float roughBiasIn;
	Float roughScaleOut;
	Float roughBiasOut;
	Float metalScaleIn;
	Float metalBiasIn;
	Float metalScaleOut;
	Float metalBiasOut;
	Float colorMaskScaleIn;
	Float colorMaskBiasIn;
	Float colorMaskScaleOut;
	Float colorMaskBiasOut;
};


class RESOURCEMATERIAL_API Multilayer_LayerTemplate : public CResource
{
	RTTI_DECLARE_TYPE( Multilayer_LayerTemplate );
	RED_BASE_CLASS( CResource );
	IMPLEMENT_RESOURCE_INTERFACE( "mltemplate", "mltemplate", "Multilayer Layer Template" );
	RED_USE_MEMORY_POOL( rend::PoolRenderData );

	// #TODO: Shouldn't actually be public, but needs to be for rtti registration...
public:
	struct Overrides
	{
		RTTI_DECLARE_TYPE(Overrides);

		struct Color;
		struct Levels;
		struct NormalStrength;

		red::DynArray<Color> m_colorScale;
		red::DynArray<Levels> m_roughLevelsIn;
		red::DynArray<Levels> m_roughLevelsOut;
		red::DynArray<Levels> m_metalLevelsIn;
		red::DynArray<Levels> m_metalLevelsOut;
		red::DynArray<NormalStrength> m_normalStrength;

#ifndef RED_CONFIGURATION_FINAL
		red::DynArray<Color> m_colorScaleOriginal;
		red::DynArray<Levels> m_roughLevelsInOriginal;
		red::DynArray<Levels> m_roughLevelsOutOriginal;
		red::DynArray<Levels> m_metalLevelsInOriginal;
		red::DynArray<Levels> m_metalLevelsOutOriginal;
		red::DynArray<NormalStrength> m_normalStrengthOriginal;
#endif

		Overrides();
		~Overrides();
	};


public:
	Multilayer_LayerTemplate();
	virtual ~Multilayer_LayerTemplate();

	virtual void OnPostLoad( const PostLoadContext& context ) override;

	RED_INLINE const TResRef<CBitmapTexture>& GetColorTextureRef() const { return m_colorTexture; }
	RED_INLINE const TResRef<CBitmapTexture>& GetNormalTextureRef() const { return m_normalTexture; }
	RED_INLINE const TResRef<CBitmapTexture>& GetRoughnessTextureRef() const { return m_roughnessTexture; }
	RED_INLINE const TResRef<CBitmapTexture>& GetMetalnessTextureRef() const { return m_metalnessTexture; }

	THandle<CBitmapTexture> GetColorTexture() const;
	THandle<CBitmapTexture> GetNormalTexture() const;
	THandle<CBitmapTexture> GetRoughnessTexture() const;
	THandle<CBitmapTexture> GetMetalnessTexture() const;

	RED_INLINE const Multilayer_LayerOverrideSelection& GetDefaultOverrides() const { return m_defaultOverrides; }

	Multilayer_LayerOverride GetOverrideParameters( const Multilayer_LayerOverrideSelection& selection, const res::ResourcePath& callerResourcePath ) const;

	RED_INLINE Float GetTilingMultiplier() const { return m_tilingMultiplier; }

#ifndef RED_CONFIGURATION_FINAL
	red::DynArray<CName> GetOverrideColorScaleNames() const;
	red::DynArray<CName> GetOverrideRoughLevelsInNames() const;
	red::DynArray<CName> GetOverrideRoughLevelsOutNames() const;
	red::DynArray<CName> GetOverrideMetalLevelsInNames() const;
	red::DynArray<CName> GetOverrideMetalLevelsOutNames() const;
	red::DynArray<CName> GetOverrideNormalStrengthNames() const;
#endif

private:
	Overrides m_overrides;
	Multilayer_LayerOverrideSelection m_defaultOverrides;

	TResRef<CBitmapTexture> m_colorTexture;
	TResRef<CBitmapTexture> m_normalTexture;
	TResRef<CBitmapTexture> m_roughnessTexture;
	TResRef<CBitmapTexture> m_metalnessTexture;

	Float m_tilingMultiplier;

	// For roughness-based tint masking. Scale/bias values
	Float m_colorMaskLevelsIn[2];
	Float m_colorMaskLevelsOut[2];
};
