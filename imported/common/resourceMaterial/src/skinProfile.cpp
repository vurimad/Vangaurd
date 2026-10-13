/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "skinProfile.h"
#include "../../renderData/include/renderResource.h"
#include "../../renderData/include/renderSkinData.h"
#include "../../renderData/include/renderer.h"

using red::DynArray;
using red::HashMap;
using red::HashSet;
using red::Map;

RTTI_BEGIN_TYPE( CSkinProfile );
	RTTI_PARENT_TYPE( CResource );
	RTTI_PROPERTY( m_blurSize ).editable().range( 0.1f, 20.f );
	RTTI_PROPERTY( m_diffuse ).editable();
	RTTI_PROPERTY( m_falloff ).editable();
	RTTI_PROPERTY( m_roughness0 ).editable().range( 0.1f, 2.f );
	RTTI_PROPERTY( m_roughness1 ).editable().range( 0.1f, 2.f );
	RTTI_PROPERTY( m_lobeMix ).editable().range( 0.0f, 1.0f );
RTTI_END_TYPE();

CSkinProfile::CSkinProfile()
	: m_blurSize( 1.2f )
	, m_diffuse( Color( 184, 172, 144 ) ) // ( 0.48, 0.41, 0.28) in linear accu space
	, m_falloff( Color( 255, 164, 149 ) ) // ( 1.00, 0.37, 0.30) in linear accu space
	, m_roughness0( 0.75f )
	, m_roughness1( 1.25f )
	, m_lobeMix( 0.8f )
{}

CSkinProfile::~CSkinProfile()
{
}

void CSkinProfile::UpdateRenderResource()
{
	if ( !GRender || GRender->IsDeviceLost() )
	{
		return;
	}

	RED_SCOPE_LOCK( m_resourceCreationLock );

	if ( m_renderResource )
	{
		rend::SkinProfileData data;
		data.kernelSize = m_blurSize;
		data.diffuse = m_diffuse;
		data.falloff = m_falloff;
		data.roughness0 = m_roughness0;
		data.roughness1 = m_roughness1;
		data.lobeMix = m_lobeMix;
		GRender->UpdateSkinProfile( m_renderResource, &data );
	}
}

void CSkinProfile::CreateRenderResource()
{
	// Release current one
	m_renderResource = nullptr;

	if ( !GRender || GRender->IsDeviceLost() )
	{
		return;
	}

	// Create rendering resource for skinProfile
	rend::SkinProfileData data;
	data.kernelSize = m_blurSize;
	data.diffuse = m_diffuse;
	data.falloff = m_falloff;
	data.roughness0 = m_roughness0;
	data.roughness1 = m_roughness1;
	data.lobeMix = m_lobeMix;
	m_renderResource = GRender->UploadSkinProfile( &data );
}

const TRenderPtr<IRenderResource>& CSkinProfile::GetRenderResource() const
{
	RED_SCOPE_LOCK( m_resourceCreationLock );
	if ( !m_renderResource )
	{
		CSkinProfile* nonConstThis = const_cast< CSkinProfile* >( this );
		nonConstThis->CreateRenderResource();
	}
	return m_renderResource;
}
