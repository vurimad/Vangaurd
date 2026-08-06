/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#pragma once

namespace math
{
	RED_INLINE Cylinder::Cylinder( const Cylinder& cyl )
		: Cylinder{ cyl.m_positionAndRadius, cyl.m_normalAndHeight, cyl.m_positionAndRadius.W, cyl.m_normalAndHeight.W }
	{}

	RED_INLINE Cylinder::Cylinder( const Vector4& pos, const Vector4& normal, Float radius, Float height )
		: m_positionAndRadius( pos.X, pos.Y, pos.Z, radius )
		, m_normalAndHeight( normal.X, normal.Y, normal.Z, height )
	{}

	RED_INLINE Vector4 Cylinder::GetMassCenter() const
	{
		return m_positionAndRadius + m_normalAndHeight * m_normalAndHeight.W * 0.5f;
	}

	RED_INLINE Float Cylinder::GetMass() const
	{
		return RED_PI * m_positionAndRadius.W * m_positionAndRadius.W * m_normalAndHeight.W;
	}

	RED_INLINE Cylinder Cylinder::operator+( const Vector4& dir ) const
	{
		return { m_positionAndRadius + dir, m_normalAndHeight, m_positionAndRadius.W - dir.W, m_normalAndHeight.W };
	}

	RED_INLINE Cylinder Cylinder::operator-( const Vector4& dir ) const
	{
		return { m_positionAndRadius - dir, m_normalAndHeight, m_positionAndRadius.W + dir.W, m_normalAndHeight.W };
	}

	RED_INLINE Cylinder& Cylinder::operator+=( const Vector4& dir )
	{
		m_positionAndRadius += dir;
		m_positionAndRadius.W -= dir.W;
		return *this;
	}

	RED_INLINE Cylinder& Cylinder::operator-=( const Vector4& dir )
	{
		m_positionAndRadius -= dir;
		m_positionAndRadius.W += dir.W;
		return *this;
	}

	RED_INLINE Float Cylinder::GetHeight() const
	{
		return m_normalAndHeight.W; 
	}

	RED_INLINE Float Cylinder::GetRadius() const
	{
		return m_positionAndRadius.W; 
	}

	RED_INLINE Vector4 Cylinder::GetPosition() const
	{ 
		return { m_positionAndRadius.X, m_positionAndRadius.Y, m_positionAndRadius.Z };
	}

	RED_INLINE Vector4 Cylinder::GetPosition2() const
	{
		return GetPosition() + GetNormal() * GetHeight();
	}

	RED_INLINE Vector4 Cylinder::GetNormal() const
	{
		return { m_normalAndHeight.X, m_normalAndHeight.Y, m_normalAndHeight.Z };
	}

} // math