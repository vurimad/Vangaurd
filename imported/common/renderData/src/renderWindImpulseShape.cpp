/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "renderWindImpulseShape.h"

RTTI_DECLARE_ENUM_IN_NAMESPACE( WindShapeAnchorPointVert, rend );
RTTI_DECLARE_ENUM_IN_NAMESPACE( WindShapeAnchorPointHorz, rend );
RTTI_DECLARE_ENUM_IN_NAMESPACE( WindShapeAnchorPointDepth, rend );

RTTI_BEGIN_ENUM_IN_NAMESPACE( WindShapeAnchorPointVert, rend )
RTTI_ENUM_OPTION( AP_CENTER );
RTTI_ENUM_OPTION( AP_TOP );
RTTI_ENUM_OPTION( AP_BOTTOM );
RTTI_END_ENUM();

RTTI_BEGIN_ENUM_IN_NAMESPACE( WindShapeAnchorPointHorz, rend )
RTTI_ENUM_OPTION( AP_CENTER );
RTTI_ENUM_OPTION( AP_RIGHT );
RTTI_ENUM_OPTION( AP_LEFT );
RTTI_END_ENUM();

RTTI_BEGIN_ENUM_IN_NAMESPACE( WindShapeAnchorPointDepth, rend )
RTTI_ENUM_OPTION( AP_CENTER );
RTTI_ENUM_OPTION( AP_FRONT );
RTTI_ENUM_OPTION( AP_BACK);
RTTI_END_ENUM();

using namespace rend;

WindImpulseShape::WindImpulseShape()
	: m_transformation( Matrix::IDENTITY() )
	, m_localPos( Vector3::ZEROS() )
	, m_lifeTimeNormalized( 1.0f )
	, m_totalLifeTime( 1.0f )
	, m_componentId( -1 )
	, m_bounds( 1.0f, 1.0f, 1.0f )
	, m_bendIntensity( 1.0f )
{
}

WindImpulseShape::WindImpulseShape( Matrix transformation, Float lifeTime, Vector3 bounds, Float bendIntensity, Uint32 componentId /*= -1*/ )
	: m_transformation( transformation )
	, m_localPos( Vector3::ZEROS() )
	, m_lifeTimeNormalized( 1.0f )
	, m_totalLifeTime( lifeTime )
	, m_bounds( bounds )
	, m_bendIntensity( bendIntensity )
	, m_timeSinceCreationNormalized( 0.0f )
	, m_componentId( componentId )
{
}

WindImpulseSphere::WindImpulseSphere()
{
}

WindImpulseSphere::WindImpulseSphere( Matrix transformation, Float lifeTime, Float radius, Float bendIntensity,
					WindShapeAnchorPointVert anchorPointVert /*= WindShapeAnchorPointVert::AP_CENTER*/,
					WindShapeAnchorPointHorz anchorPointHorz /*= WindShapeAnchorPointHorz::AP_CENTER*/,
					WindShapeAnchorPointDepth anchorPointDepth /*= WindShapeAnchorPointDepth::AP_CENTER*/,
					Uint32 componentId /*= -1*/ )
	: m_base( transformation, lifeTime, Vector3::ONES() * radius, bendIntensity, componentId )
{
	m_base.m_localPos = ResolveAnchorPointLocation( anchorPointVert, anchorPointHorz, anchorPointDepth );
}

Vector3 WindImpulseSphere::ResolveAnchorPointLocation( WindShapeAnchorPointVert anchorPointVert, WindShapeAnchorPointHorz anchorPointHorz, WindShapeAnchorPointDepth anchorPointDepth )
{
	Vector3 location = Vector3::ZEROS();
	const Float radius = m_base.m_bounds.X;
	switch ( anchorPointVert )
	{
	case WindShapeAnchorPointVert::AP_TOP:
		location.Z = -radius;
		break;
	case WindShapeAnchorPointVert::AP_BOTTOM:
		location.Z = radius;
		break;
	}

	switch ( anchorPointHorz )
	{
	case WindShapeAnchorPointHorz::AP_RIGHT:
		location.X = -radius;
		break;
	case WindShapeAnchorPointHorz::AP_LEFT:
		location.X = radius;
		break;
	}

	switch ( anchorPointDepth )
	{
	case WindShapeAnchorPointDepth::AP_FRONT:
		location.Y = -radius;
		break;
	case WindShapeAnchorPointDepth::AP_BACK:
		location.Y = radius;
		break;
	}
	return location;
}


WindImpulseCapsule::WindImpulseCapsule()
{
}

WindImpulseCapsule::WindImpulseCapsule( Matrix transformation, Float lifeTime, Float radius, Float height, Float bendIntensity,
					WindShapeAnchorPointVert anchorPointVert /*= WindShapeAnchorPointVert::AP_CENTER*/,
					WindShapeAnchorPointHorz anchorPointHorz /*= WindShapeAnchorPointHorz::AP_CENTER*/,
					WindShapeAnchorPointDepth anchorPointDepth /*= WindShapeAnchorPointDepth::AP_CENTER*/, Uint32 componentId /*= -1*/ )
	: m_base( transformation, lifeTime, Vector3( radius, radius, height ), bendIntensity, componentId )
{
	m_base.m_localPos = ResolveAnchorPointLocation( anchorPointVert, anchorPointHorz, anchorPointDepth );
}

Vector3 WindImpulseCapsule::ResolveAnchorPointLocation( WindShapeAnchorPointVert anchorPointVert, WindShapeAnchorPointHorz anchorPointHorz, WindShapeAnchorPointDepth anchorPointDepth )
{
	const Float radius = m_base.m_bounds.X;
	const Float height = m_base.m_bounds.Z;

	Vector3 location = Vector3::ZEROS();
	switch ( anchorPointVert )
	{
	case WindShapeAnchorPointVert::AP_CENTER:
		location.Z = -height / 2.0f;
		break;
	case WindShapeAnchorPointVert::AP_TOP:
		location.Z = -height - radius;
		break;
	case WindShapeAnchorPointVert::AP_BOTTOM:
		location.Z = radius;
		break;
	}

	switch ( anchorPointHorz )
	{
	case WindShapeAnchorPointHorz::AP_RIGHT:
		location.X = -radius;
		break;
	case WindShapeAnchorPointHorz::AP_LEFT:
		location.X = radius;
		break;
	}

	switch ( anchorPointDepth )
	{
	case WindShapeAnchorPointDepth::AP_FRONT:
		location.Y = -radius;
		break;
	case WindShapeAnchorPointDepth::AP_BACK:
		location.Y = radius;
		break;
	}
	return location;
}
