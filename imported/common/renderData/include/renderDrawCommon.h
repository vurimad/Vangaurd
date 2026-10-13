/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "pipelineStateObject.h"


namespace rend
{

	enum class PrimitiveType : Uint8
	{
		TriangleList,
		TriangleStrip,
		LineList,
		LineStrip,
		PointList,
	};

	enum class IndexType : Uint8
	{
		Uint16,
		Uint32,

		MAX
	};

	enum class DrawType : Uint8
	{
		Invalid,
		Draw,
		DrawIndexed,
		DrawText,
	};

	RED_INLINE ePrimitiveType MapPrimitiveType( PrimitiveType primType )
	{
		switch ( primType )
		{
		case PrimitiveType::PointList:		return PRIMTYPE_PointList;
		case PrimitiveType::LineList:		return PRIMTYPE_LineList;
		case PrimitiveType::LineStrip:		return PRIMTYPE_LineStrip;
		case PrimitiveType::TriangleList:	return PRIMTYPE_TriangleList;
		case PrimitiveType::TriangleStrip:	return PRIMTYPE_TriangleStrip;
		default:
			RED_HALT( "Invalid primitive type %d", primType );
			return PRIMTYPE_Invalid;
		}
	}

}
