/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#pragma once

namespace math
{
	RED_INLINE CutCone::CutCone( const CutCone& cone )
		: CutCone{ cone.m_positionAndRadius1, cone.m_normalAndRadius2, cone.m_positionAndRadius1.W, cone.m_normalAndRadius2.W, cone.m_height }
	{}

	RED_INLINE CutCone::CutCone( const Vector4& pos1, const Vector4& pos2, Float radius1, Float radius2 )
		: m_positionAndRadius1( pos1 )
	{
		Vector4 d = ( pos2 - pos1 );
		m_height = d.Mag3();
		m_normalAndRadius2 = d / m_height;
		m_positionAndRadius1.W = radius1;
		m_normalAndRadius2.W = radius2;
	}

	RED_INLINE CutCone::CutCone( const Vector4& pos, const Vector4& normal, Float radius1, Float radius2, Float height )
		: m_positionAndRadius1( pos.X, pos.Y, pos.Z, radius1 )
		, m_normalAndRadius2( normal.X, normal.Y, normal.Z, radius2 )
		, m_height( height )
	{}

	RED_INLINE CutCone CutCone::operator+( const Vector4& dir ) const
	{
		return { m_positionAndRadius1 + dir, m_normalAndRadius2, m_positionAndRadius1.W - dir.W, m_normalAndRadius2.W, m_height };
	}

	RED_INLINE CutCone CutCone::operator-( const Vector4& dir ) const
	{
		return { m_positionAndRadius1 - dir, m_normalAndRadius2, m_positionAndRadius1.W + dir.W, m_normalAndRadius2.W, m_height };
	}

	RED_INLINE CutCone& CutCone::operator+=( const Vector4& dir )
	{
		m_positionAndRadius1 += dir;
		m_positionAndRadius1.W -= dir.W;
		return *this;
	}

	RED_INLINE CutCone& CutCone::operator-=( const Vector4& dir )
	{
		m_positionAndRadius1 -= dir;
		m_positionAndRadius1.W += dir.W;
		return *this;
	}

	RED_INLINE Vector4 CutCone::GetMassCenter() const
	{
		return m_positionAndRadius1 + m_normalAndRadius2 * ( GetRadius1() + 2 * GetRadius2() ) * m_height / ( 3 * ( GetRadius1() + GetRadius2() ) );
	}

	RED_INLINE Float CutCone::GetMass() const
	{
		return RED_PI * (( GetRadius1() + GetRadius2() ) * 0.5f) * (( GetRadius1() + GetRadius2() ) * 0.5f) * m_height;
	}

	RED_INLINE Float CutCone::GetHeight() const
	{
		return m_height;
	}

	RED_INLINE Float CutCone::GetRadius1() const
	{
		return m_positionAndRadius1.W;
	}

	RED_INLINE Float CutCone::GetRadius2() const
	{
		return m_normalAndRadius2.W;
	}

	RED_INLINE Vector4 CutCone::GetPosition() const
	{
		return { m_positionAndRadius1.X, m_positionAndRadius1.Y, m_positionAndRadius1.Z };
	}

	RED_INLINE Vector4 CutCone::GetPosition2() const
	{
		return GetPosition() + GetNormal() * GetHeight();
	}

	RED_INLINE Vector4 CutCone::GetNormal() const
	{
		return { m_normalAndRadius2.X, m_normalAndRadius2.Y, m_normalAndRadius2.Z };
	}

} // math