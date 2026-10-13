/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "foliageProfile.h"
#include "../../renderData/include/renderResource.h"
#include "../../renderData/include/renderFoliageData.h"
#include "../../renderData/include/renderer.h"
#include "gradient.h"

using red::DynArray;
using red::HashMap;
using red::HashSet;
using red::Map;

//FoliageProfileMap has pixel format of R8G8B8A8, therefore don't allow too big values in those parameters.
//If it's convenient to have values > 1 of some parameter in the editor, then this parameter's value should be scaled down when saving to texture and scaled up in the speedtree_3d_v8.fx shader
RTTI_BEGIN_TYPE( CFoliageProfile );
	RTTI_PARENT_TYPE( CResource );
	RTTI_PROPERTY( m_cutoffAlphaMinMip ).editable().range( 0.01f, 1.0f );
	RTTI_PROPERTY( m_cutoffAlphaMaxMip ).editable().range( 0.01f, 1.0f );
	RTTI_PROPERTY( m_billboardCutoffAlpha ).editable().range( 0.01f, 1.0f );
	RTTI_PROPERTY( m_aoScale ).editable().range(0.01f, 1.0f);
	RTTI_PROPERTY( m_terrainBlendScale ).editable().range( 0.01f, rend::FoliageProfileData::MAX_TERRAIN_BLEND_SCALE );
    RTTI_PROPERTY( m_terrainBlendBias ).editable().range( 0.f, rend::FoliageProfileData::MAX_TERRAIN_BLEND_BIAS );
	RTTI_PROPERTY( m_billboardDepthScale ).editable().range( 0.01f, rend::FoliageProfileData::MAX_BILLBOARD_DEPTH_SCALE );
	RTTI_PROPERTY( m_billboardRoughnessBias ).editable().range( 0.01f, 1.0f );
	RTTI_PROPERTY( m_colorGradient ).editable();
	RTTI_PROPERTY( m_colorGradientWeight ).editable().range( 0.f, 1.0f );
	RTTI_PROPERTY( m_colorGradientDarkenWeight ).editable().range( 0.f, 1.0f );
	RTTI_PROPERTY( m_preserveOriginalColor ).editable().range( 0.f, 1.0f );
RTTI_END_TYPE();

CFoliageProfile::CFoliageProfile()
	: m_cutoffAlphaMinMip( 0.45f )
	, m_cutoffAlphaMaxMip( 0.12f )
	, m_billboardCutoffAlpha( 0.6f )
	, m_aoScale( 0.5f )
	, m_terrainBlendScale( 0.2f )
	, m_terrainBlendBias( 0.f )
	, m_colorGradientWeight( 0.0f )
	, m_colorGradientDarkenWeight( 0.0f )
	, m_billboardDepthScale( 1.0f )
	, m_preserveOriginalColor( 0.0f )
	, m_billboardRoughnessBias( 0.1f )
{}

CFoliageProfile::~CFoliageProfile()
{
}

void CFoliageProfile::FillFoliageProfileData( rend::FoliageProfileData* data, const CFoliageProfile& fp )
{
    data->cutoffAlphaMinMip				= fp.m_cutoffAlphaMinMip;
	data->cutoffAlphaMaxMip				= fp.m_cutoffAlphaMaxMip;
	data->billboardCutoffAlpha			= fp.m_billboardCutoffAlpha;
    data->aoScale						= fp.m_aoScale;
    data->terrainBlendScale				= fp.m_terrainBlendScale;
    data->terrainBlendBias				= fp.m_terrainBlendBias;
	data->colorGradientWeight			= fp.m_colorGradientWeight;
	data->colorGradientDarkenWeight		= fp.m_colorGradientDarkenWeight;
	data->billboardDepthScale			= fp.m_billboardDepthScale;
	data->preserveOriginalColor			= fp.m_preserveOriginalColor;
	data->billboardRoughnessBias		= fp.m_billboardRoughnessBias;
	if ( fp.m_colorGradient.IsValid() )
	{
		data->colorGradient = fp.m_colorGradient->GetRenderResource();
	}
}

void CFoliageProfile::UpdateRenderResource()
{
	if ( !GRender || GRender->IsDeviceLost() )
	{
		return;
	}

	RED_SCOPE_LOCK( m_resourceCreationLock );

	if ( m_renderResource )
	{
		rend::FoliageProfileData data;
        FillFoliageProfileData( &data, *this );
		GRender->UpdateFoliageProfile( m_renderResource, &data );
	}
}

void CFoliageProfile::CreateRenderResource()
{
	// Release current one
	m_renderResource = nullptr;

	if ( !GRender || GRender->IsDeviceLost() )
	{
		return;
	}

	// Create rendering resource for skinProfile
	rend::FoliageProfileData data;
    FillFoliageProfileData( &data, *this );
	//Passing resource hash to avoid duplicating foliage profiles in the FoliageProfilesManager
	//(for example if 2 different materials use the same foliage profile, we still want to allocate just 1 foliage profile)
	m_renderResource = GRender->UploadFoliageProfile( &data, GetPath().GetHash() );
}

const TRenderPtr<IRenderResource>& CFoliageProfile::GetRenderResource() const
{
	RED_SCOPE_LOCK( m_resourceCreationLock );
	if ( !m_renderResource )
	{
		CFoliageProfile* nonConstThis = const_cast< CFoliageProfile* >( this );
		nonConstThis->CreateRenderResource();
	}
	return m_renderResource;
}
