/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "multilayerSetup.h"
#include "multilayerLayerTemplate.h"
#include "../../renderData/include/renderer.h"
#include "../../renderData/include/renderResource.h"
#include "../../renderData/include/renderMultilayer.h"
#include "../../resourceMaterial/include/bitmapTexture.h"


RTTI_BEGIN_TYPE( Multilayer_Layer );
{
	RTTI_PROPERTY(m_matTile).readOnly();
	RTTI_PROPERTY(m_mbTile).readOnly();
	RTTI_PROPERTY(m_microblend).readOnly();
	RTTI_PROPERTY(m_microblendContrast).readOnly();
	RTTI_PROPERTY(m_microblendNormalStrength).readOnly();
	RTTI_PROPERTY(m_microblendOffsetU).readOnly();
	RTTI_PROPERTY(m_microblendOffsetV).readOnly();
	RTTI_PROPERTY(m_opacity).readOnly();
	RTTI_PROPERTY(m_offsetU).readOnly();
	RTTI_PROPERTY(m_offsetV).readOnly();
	RTTI_PROPERTY(m_material).readOnly();
	RTTI_PROPERTY(m_colorScale).readOnly();
	RTTI_PROPERTY(m_normalStrength).readOnly();
	RTTI_PROPERTY(m_roughLevelsIn).readOnly();
	RTTI_PROPERTY(m_roughLevelsOut).readOnly();
	RTTI_PROPERTY(m_metalLevelsIn).readOnly();
	RTTI_PROPERTY(m_metalLevelsOut).readOnly();
	RTTI_PROPERTY(m_overrides).readOnly();
}
RTTI_END_TYPE();

RTTI_BEGIN_TYPE( Multilayer_Setup );
RTTI_PARENT_TYPE( CResource );
{
	RTTI_PROPERTY(m_layers).readOnly();
	RTTI_PROPERTY(m_ratio).readOnly();
	RTTI_PROPERTY(m_useNormal).readOnly();
}
RTTI_END_TYPE();

RTTI_BEGIN_TYPE( CookedMultilayer_Setup );
RTTI_PARENT_TYPE( CResource );
	RTTI_PROPERTY( m_dependencies );
RTTI_END_TYPE();

red::Mutex Multilayer_Setup::s_onDemandCreationMutex;
red::Atomic<Int32> CookedMultilayer_Setup::s_loadingCounter( 0 );



static Bool ExtractSegment( const red::StringView& fullName, Uint32 segmentSize, Uint32& startPosition, CName& outName )
{
	Uint32 nextSep = startPosition;
	for ( Uint32 i = 0; i < segmentSize; ++i )
	{
		if ( nextSep >= fullName.Length() )
		{
			return false;
		}
		nextSep = fullName.Find('_', nextSep);
		if ( nextSep == red::StringView::npos )
		{
			nextSep = fullName.Length();
		}

		++nextSep;
	}

	red::StringView segView = fullName.Slice(startPosition, nextSep-1);
	outName = RED_NAME( segView );
	startPosition = nextSep;

	return true;
}

static void ParseLegacyOverrideSelection( const AnsiChar* assetPath, Multilayer_Layer& layer )
{
	// #HACK: Need to break up the full override name into its individual components. This should eventually not be
	// needed in the final game, if the mlsetups can be flattened down.

	red::StringView nameAsString = layer.m_overrides.AsStringView();

	// colorScale_roughIn_roughOut_metalIn_metalOut_normalStrength
	// colorScale segment has two pieces.
	Uint32 sepPos = 0;
	CName colorScale;
	CName roughLevelsIn, roughLevelsOut;
	CName metalLevelsIn, metalLevelsOut;
	CName normalStrength;
	if (ExtractSegment(nameAsString, 2, sepPos, colorScale) &&
		ExtractSegment(nameAsString, 1, sepPos, roughLevelsIn) &&
		ExtractSegment(nameAsString, 1, sepPos, roughLevelsOut) &&
		ExtractSegment(nameAsString, 1, sepPos, metalLevelsIn) &&
		ExtractSegment(nameAsString, 1, sepPos, metalLevelsOut) &&
		ExtractSegment(nameAsString, 1, sepPos, normalStrength) &&
		sepPos >= nameAsString.Length())
	{
		layer.m_overrides = CName::NONE();
		layer.m_colorScale = colorScale;
		layer.m_roughLevelsIn = roughLevelsIn;
		layer.m_roughLevelsOut = roughLevelsOut;
		layer.m_metalLevelsIn = metalLevelsIn;
		layer.m_metalLevelsOut = metalLevelsOut;
		layer.m_normalStrength = normalStrength;
	}
	else
	{
		RED_DATA_ERROR_BEGIN(red::LoggerCategory_Rendering, false, "Failed to parse override name '%hs'", layer.m_overrides.AsChar());
		RED_DATA_ERROR_ACTION_SHOW_ASSET( assetPath );
		RED_DATA_ERROR_END();
	}
}


Multilayer_Layer::Multilayer_Layer()
	: m_matTile(1.0f)
	, m_mbTile(1.0f)
	, m_microblendContrast(0.5f)
	, m_microblendNormalStrength(1.0f)
	, m_microblendOffsetU(0.0f)
	, m_microblendOffsetV(0.0f)
	, m_opacity(1.0f)
	, m_offsetU(0.0f)
	, m_offsetV(0.0f)
{
}


Multilayer_Setup::Multilayer_Setup()
	: m_ratio(1.0f)
	, m_useNormal(true)
{
}

Multilayer_Setup::~Multilayer_Setup() = default;


void Multilayer_Setup::OnPostLoad( const PostLoadContext& context )
{
	TBaseClass::OnPostLoad( context );

	const Uint32 numLayers = m_layers.Size();

	// #TEMP: Until we have proper DX12 bindless resources, we are limited to 20 layers. Any more than that, and we run
	// out of binding slots in the material.
	RED_DATA_ERROR_BEGIN(red::LoggerCategory_Rendering, numLayers <= 20, "Multilayer Setup has %u layers. Only up to 20 are currently supported. This setup might not render correctly.", numLayers);
	RED_DATA_ERROR_ACTION_SHOW_ASSET(GetPath().ToDebugString());
	RED_DATA_ERROR_END();

	RED_DATA_ERROR_BEGIN(red::LoggerCategory_Rendering, numLayers > 0, "Multilayer Setup has 0 layers.");
	RED_DATA_ERROR_ACTION_SHOW_ASSET(GetPath().ToDebugString());
	RED_DATA_ERROR_END();


	if ( !context.flags.skipUploadRenderData )
	{
		CreateRenderResource();
	}
}

void Multilayer_Setup::CreateRenderResource()
{
	if ( GRender && !GRender->IsDeviceLost() )
	{
		const Uint32 numLayers = m_layers.Size();
		red::DynArray< rend::MultilayerLayer > layers( numLayers, job::PoolJobScope() );
		m_layerTextures.Reserve( numLayers );
		m_layerMaterials.Reserve( numLayers );

		// #TODO: Cook this somehow, so we don't have to look up and rebuild everything, going through several layers of
		// json resources. Could save Multilayer_Setup as a binary resource, with all parameters flattened out and m_layers
		// cleared out. Or could embed the flattened data directly into the material that uses it. Or something.
		Bool didParseLegacyOverrides = false;
		for ( Uint32 i = 0; i < numLayers; ++i )
		{
			Multilayer_Layer& srcLayer = m_layers[i];

			if ( srcLayer.m_overrides != CName::NONE() )
			{
				ParseLegacyOverrideSelection( GetPath().ToDebugString(), srcLayer );
				didParseLegacyOverrides = true;
			}

			RED_DATA_ERROR_BEGIN( red::LoggerCategory_Rendering, srcLayer.m_material.IsValid(), "Layer %u: Layer template is invalid '%hs'. Maybe doesn't exist?", i, srcLayer.m_material.GetPath().ToDebugString() );
			RED_DATA_ERROR_ACTION_SHOW_ASSET( GetPath().ToDebugString() );
			RED_DATA_ERROR_END();

			if ( srcLayer.m_material.IsValid() )
			{
				THandle<const Multilayer_LayerTemplate> mtl = srcLayer.m_material.Get();

				rend::MultilayerLayer& layer = layers[i];
				layer.colorTexture		= (mtl->GetColorTexture() ? mtl->GetColorTexture()->GetRenderResource() : ITexture::TResourcePtr());
				layer.normalTexture		= (mtl->GetNormalTexture() ? mtl->GetNormalTexture()->GetRenderResource() : ITexture::TResourcePtr());
				layer.roughnessTexture	= (mtl->GetRoughnessTexture() ? mtl->GetRoughnessTexture()->GetRenderResource() : ITexture::TResourcePtr());
				layer.metalnessTexture	= (mtl->GetMetalnessTexture() ? mtl->GetMetalnessTexture()->GetRenderResource() : ITexture::TResourcePtr());
				layer.microblendTexture = (srcLayer.m_microblend.IsValid() ? srcLayer.m_microblend.Get()->GetRenderResource() : ITexture::TResourcePtr());

				layer.materialTile = srcLayer.m_matTile * mtl->GetTilingMultiplier();
				layer.microblendTile = srcLayer.m_mbTile;
				layer.microblendContrast = srcLayer.m_microblendContrast;
				layer.microblendNormalStrength = srcLayer.m_microblendNormalStrength;
				layer.microblendOffset = Vector2( srcLayer.m_microblendOffsetU, srcLayer.m_microblendOffsetV );
				layer.layerOpacity = srcLayer.m_opacity;
				layer.layerOffset = Vector2( srcLayer.m_offsetU, srcLayer.m_offsetV );

				Multilayer_LayerOverrideSelection overrideSelection;
				overrideSelection.colorScale = srcLayer.m_colorScale;
				overrideSelection.normalStrength = srcLayer.m_normalStrength;
				overrideSelection.roughLevelsIn = srcLayer.m_roughLevelsIn;
				overrideSelection.roughLevelsOut = srcLayer.m_roughLevelsOut;
				overrideSelection.metalLevelsIn = srcLayer.m_metalLevelsIn;
				overrideSelection.metalLevelsOut = srcLayer.m_metalLevelsOut;

				Multilayer_LayerOverride layerOverride = mtl->GetOverrideParameters( overrideSelection, GetPath() );
				layer.colorScale = Vector3( layerOverride.colorScale );
				layer.normalScale = layerOverride.normalScale;
				layer.roughScaleIn = layerOverride.roughScaleIn;
				layer.roughBiasIn = layerOverride.roughBiasIn;
				layer.roughScaleOut = layerOverride.roughScaleOut;
				layer.roughBiasOut = layerOverride.roughBiasOut;
				layer.metalScaleIn = layerOverride.metalScaleIn;
				layer.metalBiasIn = layerOverride.metalBiasIn;
				layer.metalScaleOut = layerOverride.metalScaleOut;
				layer.metalBiasOut = layerOverride.metalBiasOut;
				layer.colorMaskScaleIn = layerOverride.colorMaskScaleIn;
				layer.colorMaskBiasIn = layerOverride.colorMaskBiasIn;
				layer.colorMaskScaleOut = layerOverride.colorMaskScaleOut;
				layer.colorMaskBiasOut = layerOverride.colorMaskBiasOut;

				m_layerTextures.PushBack( srcLayer.m_microblend.Get() );
				m_layerMaterials.PushBack( srcLayer.m_material.Get() );
			}
		}

		RED_DATA_ERROR_BEGIN( red::LoggerCategory_Rendering, !didParseLegacyOverrides, "Multilayer Setup has old-style overrides specified. Please re-export it." );
		RED_DATA_ERROR_ACTION_SHOW_ASSET( GetPath().ToDebugString() );
		RED_DATA_ERROR_END();

		rend::MultilayerSetupData setupData;
		setupData.ratio = m_ratio;
		setupData.useNormal = m_useNormal;
		setupData.layers = layers;

		// #HACK: Assume that if we're currently loading a CookedMultilayer_Setup, then any Multilayer_Setup that comes
		// in is owned by it. Technically something could come in independently, but that shouldn't break anything.
		setupData.hintAlwaysLoaded = ( CookedMultilayer_Setup::s_loadingCounter.GetValue() > 0 );

		SRenderDataSource dataSource = { GetPath() };

		m_renderResource = GRender->UploadMultilayerSetup( dataSource, setupData );
		
#ifdef NO_EDITOR
		// unload the layers, texture and material TResRefs are stored separately, so they will not be unloaded
		// the rest of the data is now transferred to render-side structures
		m_layers.Clear();
		m_layers.Shrink();
#endif // NO_EDITOR
	}
}

TRenderPtr<IRenderResource> Multilayer_Setup::GetRenderResource() const
{
	RED_SCOPE_LOCK( s_onDemandCreationMutex );

	if ( m_renderResource == nullptr && GRender != nullptr )
	{
		const_cast<Multilayer_Setup*>(this)->CreateRenderResource();
	}
	return m_renderResource;
}

void Multilayer_Setup::ReleaseRenderResource()
{
	RED_SCOPE_LOCK( s_onDemandCreationMutex );
	m_renderResource = nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CookedMultilayer_Setup::CookedMultilayer_Setup()
	: m_dependencies( rend::PoolMaterials() )
	, m_resources( rend::PoolMaterials() )
{
}

CookedMultilayer_Setup::~CookedMultilayer_Setup()
{
}


void CookedMultilayer_Setup::OnSerialize( IFile& file )
{
	s_loadingCounter.Increment();

	CResource::OnSerialize( file );
}

void CookedMultilayer_Setup::OnPostLoad( const PostLoadContext& context )
{
	s_loadingCounter.Decrement();

	CResource::OnPostLoad( context );
	m_resources.Reserve(  m_dependencies.Size() );
	for( auto & dependency : m_dependencies )
	{
		m_resources.PushBack( dependency.Get() );
	}
}

res::ResourcePath CookedMultilayer_Setup::GetCookedResourcePath()
{
	return RED_CONST_RESOURCEPATH( "base\\resource.cooked_mlsetup" ); 
}

void CookedMultilayer_Setup::AddDependency( const res::ResourcePath& path )
{
	m_dependencies.PushBack( path );
}

