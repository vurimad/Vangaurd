/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "renderDynamicTextureDesc.h"

//***************************************************************************************************************************//
//***************************************************************************************************************************//

RTTI_BEGIN_ABSTRACT_TYPE(IDynamicTextureGenerator);
{
	RTTI_PARENT_TYPE(ISerializable);
}
RTTI_END_TYPE();

//***************************************************************************************************************************//
//***************************************************************************************************************************//

IDynamicTextureGenerator::IDynamicTextureGenerator()
	: m_lastFrameRendered( 0 )
{

}

IDynamicTextureGenerator::~IDynamicTextureGenerator()
{

}

Bool IDynamicTextureGenerator::ShouldRenderThisFrame( Uint32 currentFrameIndex ) const
{
	return m_lastFrameRendered.Exchange( currentFrameIndex ) != currentFrameIndex;
}

//***************************************************************************************************************************//
//***************************************************************************************************************************//

