/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "terrainSetup.h"
#include "../../renderData/include/renderResource.h"
#include "../../renderData/include/renderTerrainSetupData.h"
#include "../../renderData/include/renderer.h"


RTTI_BEGIN_TYPE( CTerrainSetup )
	RTTI_PARENT_TYPE( CResource );
	RTTI_PROPERTY( m_tiling ).editable().resizable();
	RTTI_PROPERTY( m_physicalMaterial ).editable().resizable().customInnerTypeEditor( "TerrainSetupPhysicalMaterialName" );
RTTI_END_TYPE();



CTerrainSetup::CTerrainSetup()
{
	m_tiling.Reserve( 10 );
	m_tiling.Reserve( 10 );
}

CTerrainSetup::~CTerrainSetup()
{
}

void CTerrainSetup::UpdateRenderResource()
{
	if ( !GRender || GRender->IsDeviceLost() )
	{
		return;
	}

	RED_SCOPE_LOCK( m_resourceCreationLock );

	if ( m_renderResource )
	{
		rend::TerrainMaterialData data;
		data.m_tiling = m_tiling;
	}
}

void CTerrainSetup::CreateRenderResource()
{
	// Release current one
	m_renderResource = nullptr;

	if ( !GRender || GRender->IsDeviceLost() )
	{
		return;
	}

	// Create rendering resource for hairProfile
	rend::TerrainMaterialData data;
	data.m_tiling = m_tiling;
	SRenderDataSource dataSource = { GetPath() };
	m_renderResource = GRender->UploadTerrainSetup( dataSource, data );
}

const TRenderPtr< IRenderResource >& CTerrainSetup::GetRenderResource() const
{
	RED_SCOPE_LOCK( m_resourceCreationLock );
	if ( !m_renderResource )
	{
		CTerrainSetup* nonConstThis = const_cast<CTerrainSetup*>(this);
		nonConstThis->CreateRenderResource();
	}
	return m_renderResource;
}

void CTerrainSetup::OnPostLoad( const PostLoadContext& context )
{
	for( auto& name : m_physicalMaterial )
	{
		String nameStr = name.AsChar();
		if( !nameStr.EndsWith( ".physmat" ) )
		{
			String nameFixed = nameStr + ".physmat";
			name = RED_NAME( nameFixed );
		}
	}
}

void CTerrainSetup::ReleaseRenderResource()
{
	RED_SCOPE_LOCK( m_resourceCreationLock );
	m_renderResource = nullptr;
}

