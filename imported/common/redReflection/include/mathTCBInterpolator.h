/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "singleChannelCurve.h"
#include "curveInterpolator.h"
#include "mathVector4.h"

class RED_REFLECTION_API TCBCurveInterpolator : public CurveInterpolator<Vector4>
{
public:
	struct CurvePosition
	{
		CurvePosition() : edgeIdx( 0 ), edgeAlpha( 0.0f ) {}
		CurvePosition( Uint32 edgeIdx, Float edgeAlpha ) : edgeIdx( edgeIdx ), edgeAlpha( edgeAlpha ) {}

		Uint32 edgeIdx;
		Float edgeAlpha;
	};

					TCBCurveInterpolator( const TSingleChannelCurve<Vector4>& data, Float tension, Float continuity, Float bias, const Bool loop = false );
					TCBCurveInterpolator( const TSingleChannelCurve<Vector4>& data, const Bool loop = false );

	Uint32			CurveLowerBoundIndex( Uint32 beginIdx, Uint32 endIdx, Float val ) const;
	void			FindLowerUpperBounds( Float time, Int32& lowerBoundIndex, Int32& upperBoundIndex ) const;

	void			GetControlPointPosition( Uint32 idx, Vector4& result ) const;
	void			GetPosition( Uint32 index, Float localTime, Vector4& result ) const;

	Vector4			EvalAt( Float t ) const;

	Bool			IsLooping() const { return m_loop; }
	Uint32			Size() const { return m_numKeys; }

	Uint32			FindNearestEdgeIndex( const Vector4& pos ) const;

	void			GetCurvePoint( const CurvePosition& curvePosition, Vector4& outComputedSpot ) const;
	Bool			GetPointOnCurveInDistance( const CurvePosition& curvePosition, Float distance, Vector4& outComputedSpot, Uint32 iterationsCount ) const;
	void			GetPointOnCurveInDistance( CurvePosition& curvePosition, Float distance, Vector4& outComputedSpot, Bool &isEndOfPath ) const;

	void			CalculateTangentFromCurveDirection( Float time, Vector4& tangent ) const;
	void			CalculateTangentFromCurveDirection( const CurvePosition& curvePosition, Vector4& tangent ) const;

	Float			GetControlPointTime( Uint32 index ) const;

private:
	Float m_tension;
	Float m_continuity;
	Float m_bias;
};