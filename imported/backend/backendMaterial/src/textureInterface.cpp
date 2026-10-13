/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "textureInterface.h"

#include "../../../common/redReflection/include/resourcePath.h"
#include "../../../common/resourceMaterial/include/material.h"
#include "../../../common/resourceMaterial/include/bitmapTexture.h"

//***************************************************************************************************************************//
//***************************************************************************************************************************//

namespace tools
{

	Uint8 ChannelMask::GetMask() const
	{
		Uint8 mask = 0;
		for ( Uint32 i = 0; i < m_channelMask.Size(); i++ )
		{
			mask |= m_channelMask[i] << i;
		}
		return mask;
	}

	void ChannelMask::Resize( Uint32 size )
	{
		if ( size > 8 )
		{
			size = 8;
		}

		m_channelMask.Resize( size );

		for ( auto& channel : m_channelMask )
		{
			channel = true;
		}
	}

	void GroupHelper::SetTexture( THandle< ITexture >& texture )
	{
		m_texture = texture;
	}

	void GroupHelper::SetGroup( const String& groupStr )
	{
		GpuWrapApi::eTextureGroup* group = m_groupMap.FindPtr( groupStr );
		if ( group )
		{
			//m_texture->SetTextureGroup() ????
		}
	}

}

//***************************************************************************************************************************//
//***************************************************************************************************************************//
