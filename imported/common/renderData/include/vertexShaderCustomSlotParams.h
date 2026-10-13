#pragma once

struct RENDER_DATA_API SVertexShaderCustomSlotParams
{
	// Index of vertex custom slot, e.g. 0 means VSC_Custom_0
	Uint32 m_vertexCustomSlotIndex;

	// Value to set
	Vector4 m_value;

	SVertexShaderCustomSlotParams();
	SVertexShaderCustomSlotParams( Uint32 vertexCustomSlotIndex, Vector4 value );
};