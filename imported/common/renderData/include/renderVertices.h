/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "vertexLayoutDesc.h"
#include "../../redMath/include/half.h"

// Debug vertex, position only
struct DebugVertexBase
{
	Float x, y, z;

	RED_FORCE_INLINE DebugVertexBase() = default;
	RED_FORCE_INLINE DebugVertexBase( const DebugVertexBase& other ) = default;
	RED_FORCE_INLINE DebugVertexBase& operator = ( const DebugVertexBase& other ) = default;

	RED_FORCE_INLINE DebugVertexBase( const math::Vector4& p )
		: x ( p.X )
		, y ( p.Y )
		, z ( p.Z )
	{
	}

	RED_FORCE_INLINE void Set( const math::Vector4& p )
	{
		x = p.X;
		y = p.Y;
		z = p.Z;		
	}

	RED_FORCE_INLINE math::Vector4 GetPosVector() const
	{
		return math::Vector4 ( x, y, z );
	}

	static const GpuWrapApi::VertexLayoutDesc& GetVertexLayoutDesc()
	{
		static GpuWrapApi::VertexPacking::PackingElement packingElements[] =
		{
			{ GpuWrapApi::VertexPacking::PT_Float3, GpuWrapApi::VertexPacking::PS_Position,	0, 0, GpuWrapApi::VertexPacking::ST_PerVertex },
			GpuWrapApi::VertexPacking::PackingElement::END_OF_ELEMENTS
		};
		static GpuWrapApi::VertexLayoutDesc desc( packingElements );
		return desc;
	}
};

// Debug vertex, position and color only
struct DebugVertex
{
	Float x, y, z;
	math::Half4 colorLinearSpace;

	RED_FORCE_INLINE DebugVertex() = default;
	RED_FORCE_INLINE DebugVertex( const DebugVertex& other ) = default;
	RED_FORCE_INLINE DebugVertex& operator = ( const DebugVertex& other ) = default;

	RED_FORCE_INLINE DebugVertex( const Vector4& p, const math::Color& ldrColor )
		: x ( p.X )
		, y ( p.Y )
		, z ( p.Z )
		, colorLinearSpace( ldrColor.ToHalfLinearAccu() )
	{}

	RED_FORCE_INLINE DebugVertex( const Vector4& p, const math::Vector4& hdrColor )
		: x( p.X )
		, y( p.Y )
		, z( p.Z )
		, colorLinearSpace( hdrColor )
	{}

	RED_FORCE_INLINE DebugVertex( const Vector4& p, const math::Half4& hdrColor )
		: x( p.X )
		, y( p.Y )
		, z( p.Z )
		, colorLinearSpace( hdrColor )
	{
	}

	RED_FORCE_INLINE void Set( const Vector4& p, const math::Color& ldrColor )
	{
		x = p.X;
		y = p.Y;
		z = p.Z;
		colorLinearSpace = ldrColor.ToHalfLinearAccu();
	}

	RED_FORCE_INLINE void Set( const Vector4& p, const math::Vector4& hdrColor )
	{
		x = p.X;
		y = p.Y;
		z = p.Z;
		colorLinearSpace = hdrColor;
	}

	RED_INLINE Bool operator==( const DebugVertex& v ) const
	{
		return x == v.x && y == v.y && z == v.z && colorLinearSpace == v.colorLinearSpace;
	}

	RED_FORCE_INLINE Uint32 CalcHash() const
	{
		return red::CalculateHash32( this, sizeof( DebugVertex ) );
	}

	static const GpuWrapApi::VertexLayoutDesc& GetVertexLayoutDesc()
	{
		GpuWrapApi::VertexPacking::PackingElement packingElements[] =
		{
			{ GpuWrapApi::VertexPacking::PT_Float3,		GpuWrapApi::VertexPacking::PS_Position,	0, 0, GpuWrapApi::VertexPacking::ST_PerVertex },
			{ GpuWrapApi::VertexPacking::PT_Float16_4,	GpuWrapApi::VertexPacking::PS_Color, 0, 0, GpuWrapApi::VertexPacking::ST_PerVertex },
			GpuWrapApi::VertexPacking::PackingElement::END_OF_ELEMENTS
		};
		static GpuWrapApi::VertexLayoutDesc desc( packingElements );
		return desc;
	}
};

// Debug vertex with UV
struct DebugVertexUV
{
	Float x, y, z;
	math::Half4 colorLinearSpace;
	math::Half u,v;

	RED_FORCE_INLINE DebugVertexUV() = default;
	RED_FORCE_INLINE DebugVertexUV( const DebugVertexUV& other ) = default;
	RED_FORCE_INLINE DebugVertexUV& operator = ( const DebugVertexUV& other ) = default;

	RED_FORCE_INLINE DebugVertexUV( const math::Vector4& p, const math::Color& ldrColor )
		: x( p.X )
		, y( p.Y )
		, z( p.Z )
		, colorLinearSpace( ldrColor.ToHalfLinearAccu() )
		, u( math::Half::Zero() )
		, v( math::Half::Zero() )
	{
	}

	RED_FORCE_INLINE DebugVertexUV( const math::Vector4& p, const math::Color& ldrColor, Float inU, Float inV )
		: x ( p.X )
		, y ( p.Y )
		, z ( p.Z )
		, colorLinearSpace( ldrColor.ToHalfLinearAccu() )
		, u ( inU )
		, v ( inV )
	{
	}

	RED_FORCE_INLINE DebugVertexUV( const math::Vector4& p, const math::Half4& colorLspace, const math::Half inU, const math::Half inV )
		: x( p.X )
		, y( p.Y )
		, z( p.Z )
		, colorLinearSpace( colorLspace )
		, u( inU )
		, v( inV )
	{}

	RED_FORCE_INLINE void Set( const math::Vector4& p, const math::Color& ldrColor )
	{
		x = p.X;
		y = p.Y;
		z = p.Z;
		colorLinearSpace = ldrColor.ToHalfLinearAccu();
		u = v = math::Half::Zero();
	}

	RED_FORCE_INLINE void Set( const math::Vector4& p, const math::Color& ldrColor, Float inU, Float inV )
	{
		x = p.X;
		y = p.Y;
		z = p.Z;
		colorLinearSpace = ldrColor.ToHalfLinearAccu();
		u = math::Half( inU );
		v = math::Half( inV );
	}

	RED_FORCE_INLINE DebugVertexUV( const math::Vector4& p, const math::Vector4& hdrColor )
		: x( p.X )
		, y( p.Y )
		, z( p.Z )
		, colorLinearSpace( hdrColor )
		, u( math::Half::Zero() )
		, v( math::Half::Zero() )
	{
	}

	RED_FORCE_INLINE DebugVertexUV( const math::Vector4& p, const math::Vector4& hdrColor, Float inU, Float inV )
		: x( p.X )
		, y( p.Y )
		, z( p.Z )
		, colorLinearSpace( hdrColor )
		, u( inU )
		, v( inV )
	{
	}

	RED_FORCE_INLINE void Set( const math::Vector4& p, const math::Vector4& hdrColor )
	{
		x = p.X;
		y = p.Y;
		z = p.Z;
		colorLinearSpace = hdrColor;
		u = v = math::Half::Zero();
	}

	RED_FORCE_INLINE void Set( const math::Vector4& p, const math::Vector4& hdrColor, Float inU, Float inV )
	{
		x = p.X;
		y = p.Y;
		z = p.Z;
		colorLinearSpace = hdrColor;
		u = math::Half( inU );
		v = math::Half( inV );
	}

	RED_FORCE_INLINE void Set( const DebugVertex& debugVertex, Float inU, Float inV )
	{
		x = debugVertex.x;
		y = debugVertex.y;
		z = debugVertex.z;
		colorLinearSpace = debugVertex.colorLinearSpace;
		u = math::Half( inU );
		v = math::Half( inV );
	}
	   
	static const GpuWrapApi::VertexLayoutDesc& GetVertexLayoutDesc()
	{
		GpuWrapApi::VertexPacking::PackingElement packingElements[] =
		{
			{ GpuWrapApi::VertexPacking::PT_Float3,		GpuWrapApi::VertexPacking::PS_Position,	0, 0, GpuWrapApi::VertexPacking::ST_PerVertex },
			{ GpuWrapApi::VertexPacking::PT_Float16_4,	GpuWrapApi::VertexPacking::PS_Color,	0, 0, GpuWrapApi::VertexPacking::ST_PerVertex },
			{ GpuWrapApi::VertexPacking::PT_Float16_2,	GpuWrapApi::VertexPacking::PS_TexCoord,	0, 0, GpuWrapApi::VertexPacking::ST_PerVertex },
			GpuWrapApi::VertexPacking::PackingElement::END_OF_ELEMENTS
		};
		static GpuWrapApi::VertexLayoutDesc desc( packingElements );
		return desc;
	}
};

// This struct is used because we want to be able to set up instancing by calling the static function 
// and the only way we can override that right now is to cast the original data into this one
// eventually the code that necessitates this structure should go away and then this structure can be removed
struct DebugVertexUVInstanced : public DebugVertexUV
{
	RED_INLINE DebugVertexUVInstanced() {}
	RED_INLINE DebugVertexUVInstanced( const math::Vector4& p, const math::Color& inColor, Float inU, Float inV )
		: DebugVertexUV( p, inColor, inU, inV )
	{}

	RED_INLINE DebugVertexUVInstanced( const math::Vector4& p, const math::Half4& inColor, math::Half inU, math::Half inV )
		: DebugVertexUV( p, inColor, inU, inV )
	{}


	static const GpuWrapApi::VertexLayoutDesc& GetVertexLayoutDesc()
	{
		GpuWrapApi::VertexPacking::PackingElement packingElements[] =
		{
			{ GpuWrapApi::VertexPacking::PT_Float3,		GpuWrapApi::VertexPacking::PS_Position,				0, 0, GpuWrapApi::VertexPacking::ST_PerVertex },
			{ GpuWrapApi::VertexPacking::PT_Float16_4,	GpuWrapApi::VertexPacking::PS_Color,				0, 0, GpuWrapApi::VertexPacking::ST_PerVertex },
			{ GpuWrapApi::VertexPacking::PT_Float16_2,	GpuWrapApi::VertexPacking::PS_TexCoord,				0, 0, GpuWrapApi::VertexPacking::ST_PerVertex },
			{ GpuWrapApi::VertexPacking::PT_Float4,		GpuWrapApi::VertexPacking::PS_InstanceTransform,	0, 7, GpuWrapApi::VertexPacking::ST_PerInstance },
			{ GpuWrapApi::VertexPacking::PT_Float4,		GpuWrapApi::VertexPacking::PS_InstanceTransform,	1, 7, GpuWrapApi::VertexPacking::ST_PerInstance },
			{ GpuWrapApi::VertexPacking::PT_Float4,		GpuWrapApi::VertexPacking::PS_InstanceTransform,	2, 7, GpuWrapApi::VertexPacking::ST_PerInstance },
			GpuWrapApi::VertexPacking::PackingElement::END_OF_ELEMENTS
		};
		static GpuWrapApi::VertexLayoutDesc desc(packingElements);
		return desc;
	}
};