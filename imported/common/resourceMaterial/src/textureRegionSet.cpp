/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "textureRegionSet.h"
#include "../../renderData/include/renderResource.h"
#include "../../renderData/include/renderer.h"

using red::DynArray;
using red::HashMap;
using red::HashSet;
using red::Map;

RTTI_BEGIN_TYPE( CTextureRegionSet );
	RTTI_PARENT_TYPE( CResource );
	RTTI_PROPERTY( m_regions ).readOnly();
RTTI_END_TYPE();


CTextureRegionSet::CTextureRegionSet()
{}

CTextureRegionSet::~CTextureRegionSet()
{
}

void CTextureRegionSet::UpdateRenderResource()
{
	if ( !GRender || GRender->IsDeviceLost() )
	{
		return;
	}

	if ( m_renderResource )
	{
		/*
		rend::SkinProfileData data;
		data.kernelSize = m_blurSize;
		data.diffuse = m_diffuse;
		data.falloff = m_falloff;
		GRender->UpdateSkinProfile( m_renderResource, &data );
		*/
	}
}

void CTextureRegionSet::CreateRenderResource()
{
	// Release current one
	m_renderResource = nullptr;

	if ( !GRender || GRender->IsDeviceLost() )
	{
		return;
	}

	// Create rendering resource for TextureRegionSet
	rend::TextureRegionSet data;
	data.m_regions = m_regions;
	m_renderResource = GRender->UploadTextureRegionSet( &data );
}

const TRenderPtr<IRenderResource>& CTextureRegionSet::GetRenderResource() const
{
	if ( !m_renderResource )
	{
		const_cast< CTextureRegionSet* >( this )->CreateRenderResource();
	}
	return m_renderResource;
}