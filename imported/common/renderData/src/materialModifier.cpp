/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "materialModifier.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

RTTI_BEGIN_ENUM( EMaterialModifier );
{
	RTTI_ENUM_OPTION( EMATMOD_HitProxy );
	RTTI_ENUM_OPTION( EMATMOD_WindData );
	RTTI_ENUM_OPTION( EMATMOD_ParticleParams );
	RTTI_ENUM_OPTION( EMATMOD_RemoteCamera );
	RTTI_ENUM_OPTION( EMATMOD_Mirror );
	RTTI_ENUM_OPTION( EMATMOD_CustomStructBuffer );
	RTTI_ENUM_OPTION( EMATMOD_MotionMatrix );
	RTTI_ENUM_OPTION( EMATMOD_ColorAndTexture );
	RTTI_ENUM_OPTION( EMATMOD_MaterialParams );
	RTTI_ENUM_OPTION( EMATMOD_Eye );
	RTTI_ENUM_OPTION( EMATMOD_Skin );
	RTTI_ENUM_OPTION( EMATMOD_Dismemberment );
	RTTI_ENUM_OPTION( EMATMOD_Garments );
	RTTI_ENUM_OPTION( EMATMOD_ShadowsDebugParams );
	RTTI_ENUM_OPTION( EMATMOD_MultilayeredDebug );
	RTTI_ENUM_OPTION( EMATMOD_ParallaxParams );
	RTTI_ENUM_OPTION( EMATMOD_HighlightsParams );
	RTTI_ENUM_OPTION( EMATMOD_DebugColoring );
	RTTI_ENUM_OPTION( EMATMOD_DrawBufferMask );
	RTTI_ENUM_OPTION( EMATMOD_AutoSpawnData );
	RTTI_ENUM_OPTION( EMATMOD_DestructionRegions );
	RTTI_ENUM_OPTION( EMATMOD_VehicleParams );
	RTTI_ENUM_OPTION( EMATMOD_EffectParams );
	RTTI_ENUM_OPTION( EMATMOD_FloatTracks );
	RTTI_ENUM_OPTION( EMATMOD_AutoHideDistance );
	RTTI_ENUM_OPTION( EMATMOD_Rain );
	RTTI_ENUM_OPTION( EMATMOD_PlanarReflections );
	RTTI_ENUM_OPTION( EMATMOD_MAX );
}
RTTI_END_ENUM();

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

SMatModifierBuilder& SMatModifierBuilder::AddImpl( EMaterialModifier modType, Uint32 modDataSize, const void *modData )
{
	// Ensure buffer space for modifier
	Uint32 modOffset = 0;
	{
		const Uint32 modTotalSize = sizeof(SMaterialModifierEntryHeader) + modDataSize;
		if ( m_data.Empty() )
		{				
			m_data.Resize( sizeof( SMaterialModifierBufferHeader ) + modTotalSize );
			static_cast< SMaterialModifierBufferHeader* >( m_data.Data() )->m_numModifiers = 0;
			modOffset = sizeof( SMaterialModifierBufferHeader );
		}
		else
		{
			RED_ASSERT( m_data.Size() >= sizeof( SMaterialModifierBufferHeader ), "Internal error" );
			modOffset = m_data.Size();
			m_data.Grow( modTotalSize );
		}
	}

	// Update header
	{
		SMaterialModifierBufferHeader* header = static_cast< SMaterialModifierBufferHeader* >( m_data.Data() );
		header->m_modifiersMask.Set( modType );
		header->m_numModifiers += 1;
	}

	// Copy modifier data
	{
		SMaterialModifierEntryHeader* entryHeader = reinterpret_cast< SMaterialModifierEntryHeader* >( m_data.TypedData() + modOffset );
		// #TODO: fails to compile on Orbis, because it's always true
		//RED_FATAL_ASSERT( (Uint16)modType < 8 * sizeof(entryHeader->m_type), "" );
		entryHeader->m_type = modType;
		entryHeader->m_size = modDataSize;

		void *entryData = m_data.TypedData() + modOffset + sizeof( SMaterialModifierEntryHeader );
		red::Memcpy( entryData, modData, modDataSize );
	}

	//
	return *this;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
