#include "build.h"
#include "vertexShaderCustomSlotParams.h"

SVertexShaderCustomSlotParams::SVertexShaderCustomSlotParams()
	: m_vertexCustomSlotIndex( 0 )
	, m_value( Vector4::ZEROS() )
{
}

SVertexShaderCustomSlotParams::SVertexShaderCustomSlotParams( Uint32 vertexCustomSlotIndex, Vector4 value )
	: m_vertexCustomSlotIndex( vertexCustomSlotIndex )
	, m_value( value )
{
}
