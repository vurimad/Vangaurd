/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "materialLayerLibrary.h"
#include "../../redReflection/include/gatheredResource.h"


//**************************************************************************************************************//
// RTTI definition
//**************************************************************************************************************//


RTTI_BEGIN_TYPE( MaterialLayerDef );
	RTTI_PROPERTY( m_name ).editable();
	RTTI_PROPERTY( m_size ).editable();
	RTTI_PROPERTY( m_colorPalette ).editable().resizable();
	RTTI_PROPERTY( m_material ).editable().inlined();
RTTI_END_TYPE();

RTTI_BEGIN_TYPE( MicroblendDef );
RTTI_PROPERTY( m_name ).editable();
RTTI_PROPERTY( m_texture ).editable().inlined();
RTTI_END_TYPE();

RTTI_BEGIN_TYPE( CMaterialLayerLibrary );

RTTI_PARENT_TYPE( CResource );
	RTTI_PROPERTY( m_uvTiling ).editable();
	RTTI_PROPERTY( m_mbTiling ).editable();
	RTTI_PROPERTY( m_microblendContrast ).editable();
	RTTI_PROPERTY( m_paletteColorIndex ).editable();
	RTTI_PROPERTY( m_layers ).editable().resizable().inlined();
	RTTI_PROPERTY( m_microblends ).editable().resizable();
RTTI_END_TYPE();

//**************************************************************************************************************//
// Implementation
//**************************************************************************************************************//

CMaterialLayerLibrary::CMaterialLayerLibrary()
	: m_uvTiling( 1.0f )
	, m_mbTiling( 4.0f )
	, m_microblendContrast( 1.0f )
	, m_microblendNormalStrength( 2.0f )
	, m_paletteColorIndex( 0 )
{
}


CMaterialLayerLibrary::~CMaterialLayerLibrary()
{

}

void CMaterialLayerLibrary::ClearResources()
{
	m_layers.Clear();
	m_microblends.Clear();
}


//**************************************************************************************************************//
//**************************************************************************************************************//


