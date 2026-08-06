/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "mathTCBInterpolator.h"

TCBCurveInterpolator::TCBCurveInterpolator( const TSingleChannelCurve<Vector4>& data, Float tension, Float continuity, Float bias, const Bool loop )
	: CurveInterpolator<Vector4>( data, loop )
	, m_tension( tension )
	, m_continuity( continuity )
	, m_bias( bias )
{
}

TCBCurveInterpolator::TCBCurveInterpolator( const TSingleChannelCurve<Vector4>& data, const Bool loop )
	: CurveInterpolator<Vector4>( data, loop )
	, m_tension( 0.0f )
	, m_continuity( 0.0f )
	, m_bias( 0.0f )
{
}

Uint32 TCBCurveInterpolator::CurveLowerBoundIndex( Uint32 beginIdx, Uint32 endIdx, Float val ) const
{
	Uint32 count = endIdx - beginIdx;

	while ( count > 0 )
	{
		Uint32 halfCount = count / 2;
		
		Uint32 middleIdx = beginIdx + halfCount;
		if ( m_times[ middleIdx ] < val )
		{
			if ( ( middleIdx + 1 < endIdx && m_times[ middleIdx ] > val ) || middleIdx + 1 == endIdx ) return middleIdx;
			beginIdx = middleIdx + 1;
			count = count - halfCount - 1;
		}
		else
		{
			count = halfCount;
		}
	}
	return beginIdx;
}

void TCBCurveInterpolator::FindLowerUpperBounds( Float time, Int32& lowerBoundIndex, Int32& upperBoundIndex ) const
{
	// If no control points, just return something...
	if ( m_numKeys == 0 )
	{
		lowerBoundIndex = -1;
		upperBoundIndex = -1;
		return;
	}

	Uint32 i1, i2;

	// If we aren't looping, check for boundaries.
	if ( !m_loop )
	{
		// Time < first control point time, so get value from first control point
		if ( time <= m_times[ 0 ] )
		{
			lowerBoundIndex = 0;
			upperBoundIndex = 0;
			return;
		}

		// Time > last control point time, so get the value from last control point
		if ( time >= m_times[ m_numKeys - 1 ] )
		{
			upperBoundIndex = m_numKeys - 1;
			lowerBoundIndex = upperBoundIndex;
			return;
		}

		Uint32 i = CurveLowerBoundIndex( 0, m_numKeys, time );// + 1;
															  // We already checked boundaries, so we know i != 0 and i != numValues.
		i1 = i - 1;
		i2 = i;
	}
	else
	{
		// LowerBoundIndex seems to have issues with boundaries...
		if ( time > m_times[ m_numKeys - 1 ] )
			time -= 1.0f;

		Uint32 i;
		if ( time <= m_times[ 0 ] )
			i = 0;
		else
			i = CurveLowerBoundIndex( 0, m_numKeys, time ); // + 1;

		i1 = ( i == 0 ? m_numKeys - 1 : i - 1 );
		i2 = ( i == m_numKeys ? 0 : i );
	}

	lowerBoundIndex = ( time == m_times[ i2 ] ? i2 : i1 );
	upperBoundIndex = i2;
}

void TCBCurveInterpolator::GetControlPointPosition( Uint32 idx, Vector4& result ) const
{
	RED_ASSERT( idx < m_numKeys );
	result = ( (Vector4*)m_values )[ idx ];
}

void TCBCurveInterpolator::GetPosition( Uint32 index, Float localTime, Vector4& result ) const
{

	Vector4 p0, p1, p2, p3;
	GetControlPointPosition( index, p1 );
	GetControlPointPosition( ( index + 1 ) % m_numKeys, p2 );

	if ( !m_loop && index == 0 )
	{
		p0 = p1;
	}
	else
	{
		GetControlPointPosition( ( index - 1 ) % m_numKeys, p0 );
	}

	if ( !m_loop && index + 2 >= m_numKeys )
	{
		p3 = p2;
	}
	else
	{
		GetControlPointPosition( ( index + 2 ) % m_numKeys, p3 );
	}

	const Float sp1 = localTime;
	const Float sp2 = sp1 * sp1;
	const Float sp3 = sp2 * sp1;

	const Float h1 = 2.0f * sp3 - 3.0f * sp2 + 1.0f;
	const Float h2 = -2.0f * sp3 + 3.0f * sp2;
	const Float h3 = sp3 - 2.0f * sp2 + sp1;
	const Float h4 = sp3 - sp2;

	Vector4 TD = ( p1 - p0 ) * ( ( 1.0f - m_tension ) * ( 1.0f + m_continuity ) * ( 1.0f + m_bias ) / 2.0f ) + ( p2 - p1 ) * ( ( 1.0f - m_tension ) * ( 1.0f - m_continuity ) * ( 1.0f - m_bias ) / 2.0f );

	Vector4 TS = ( p2 - p1 ) * ( ( 1.0f - m_tension ) * ( 1.0f - m_continuity ) * ( 1.0f + m_bias ) / 2.0f ) + ( p3 - p2 ) * ( ( 1.0f - m_tension ) * ( 1.0f + m_continuity ) * ( 1.0f + m_bias ) / 2.0f );

	result = p1 * h1 + p2 * h2 + TD * h3 + TS * h4;
}

Vector4 TCBCurveInterpolator::EvalAt( Float t ) const
{
	Vector4 result;
	Int32 i1, i2;

	FindLowerUpperBounds( t, i1, i2 );

	if ( i1 == i2 )
	{
		GetControlPointPosition( i1, result );
		return result;
	}

	Float t1 = m_times[ i1 ];
	Float t2 = m_times[ i2 ];
	if ( m_loop )
	{
		while ( t2 <= t1 )
		{
			t2 += 1.0f;
		}
		while ( t < t1 )
		{
			t += 1.0f;
		}
	}

	const Float localTime = ( t - t1 ) / ( t2 - t1 );
	RED_ASSERT( t1 < t2 );
	RED_ASSERT( localTime >= 0.0f && localTime <= 1.0f );

	GetPosition( i1, localTime, result );

	return result;
}

Uint32 TCBCurveInterpolator::FindNearestEdgeIndex( const Vector4& pos ) const
{
	Int32 closestEdge = -1;
	Float closestDistance = RED_FLT_MAX;

	const Uint32 numEdges = IsLooping() ? Size() : ( Size() - 1 );

	for ( Uint32 i = 0; i < numEdges; i++ )
	{
		Vector4 a, b;
		GetControlPointPosition( i, a );
		GetControlPointPosition( ( i + 1 ) == Size() ? 0 : ( i + 1 ), b );

		const Float distanceToEdge = pos.DistanceToEdge( a, b );
		if ( distanceToEdge < closestDistance )
		{
			closestDistance = distanceToEdge;
			closestEdge = i;
		}
	}

	return closestEdge;
}


void TCBCurveInterpolator::GetCurvePoint( const CurvePosition& curvePosition, Vector4& outComputedSpot ) const
{
	GetPosition( curvePosition.edgeIdx, curvePosition.edgeAlpha, outComputedSpot );
}

Bool TCBCurveInterpolator::GetPointOnCurveInDistance( const CurvePosition& curvePosition, Float distance, Vector4& outComputedSpot, Uint32 iterationsCount ) const
{
	Uint32 startSegmentIdx = curvePosition.edgeIdx;
	const Uint32 endSegmentIdx = startSegmentIdx + 1 < m_numKeys ? startSegmentIdx + 1 : ( m_loop ? 0 : startSegmentIdx );

	if ( startSegmentIdx == endSegmentIdx )
	{
		GetCurvePoint( curvePosition, outComputedSpot );
		return true;
	}

	Bool isEndOfPath = false;
	Float accumulatedDistance = 0.0f;
	Uint32 currentSegmentIdx = curvePosition.edgeIdx;
	Float currentAlpha = curvePosition.edgeAlpha;

	while ( accumulatedDistance < distance && !isEndOfPath )
	{
		const Float step = ( 1.0f - currentAlpha ) / (Float)iterationsCount;

		CurvePosition cp( currentSegmentIdx, currentAlpha );
		Vector4 currentPosition;
		GetCurvePoint( cp, currentPosition );
		for ( Uint32 i = 0; i < iterationsCount; ++i )
		{
			cp.edgeAlpha += step;
			Vector4 nextPosition;
			GetCurvePoint( cp, nextPosition );
			accumulatedDistance += currentPosition.DistanceTo( nextPosition );
			currentPosition = nextPosition;
			if ( accumulatedDistance >= distance )
			{
				outComputedSpot = nextPosition;
				break;
			}
		}

		currentSegmentIdx = ( currentSegmentIdx + 1 ) % m_numKeys;

		currentAlpha = 0.0f;

		isEndOfPath = !m_loop && currentSegmentIdx == m_numKeys;
	}

	return isEndOfPath;
}

void TCBCurveInterpolator::GetPointOnCurveInDistance( CurvePosition& curvePosition, Float distance, Vector4& outComputedSpot, Bool &isEndOfPath ) const
{
	Uint32 nVertices = Size();
	Bool closed = IsLooping() && nVertices > 2;
	Uint32 nEdges = closed ? nVertices : nVertices - 1;
	isEndOfPath = false;

	Int32 edgeIdx = curvePosition.edgeIdx;
	Float edgeAlpha = curvePosition.edgeAlpha;

	RED_ASSERT( edgeIdx >= 0 && edgeIdx < (Int32)nEdges );
	RED_ASSERT( edgeAlpha >= 0.f && edgeAlpha <= 1.f );
	RED_ASSERT( distance != 0.f );

	do
	{
		Vector4 p0, p1;
		GetControlPointPosition( edgeIdx, p0 );
		GetControlPointPosition( ( edgeIdx + 1 ) % nEdges, p1 );

		Float alpha = distance > 0.f ? 1.f - edgeAlpha : edgeAlpha;
		Float edgeLenght = ( p1 - p0 ).Mag3();
		Float edgeLengthLeft = edgeLenght * alpha;

		if ( edgeLengthLeft >= MAbs( distance ) )
		{
			edgeAlpha = edgeAlpha + distance / edgeLengthLeft * alpha;
			GetPosition( edgeIdx, edgeAlpha, outComputedSpot );
			break;
		}

		if ( distance > 0 )
			distance -= edgeLengthLeft;
		else
			distance += edgeLengthLeft;

		if ( distance > 0.f )
		{
			if ( !closed && edgeIdx + 1 > (Int32)nEdges - 1 )
			{
				edgeIdx = nEdges - 1;
				edgeAlpha = 1.f;
				outComputedSpot = p1;
				isEndOfPath = true;
				break;
			}
			edgeAlpha = 0.f;
			if ( ++edgeIdx == (Int32)nEdges )
				edgeIdx = 0;
		}
		else
			if ( distance < 0.f )
			{
				if ( !closed && edgeIdx - 1 < 0 )
				{
					edgeIdx = 0;
					edgeAlpha = 0.f;
					outComputedSpot = p0;
					isEndOfPath = true;
					break;
				}
				edgeAlpha = 1.f;
				if ( --edgeIdx < 0 )
					edgeIdx += nEdges;
			}

	} while ( true );

	curvePosition.edgeIdx = edgeIdx;
	curvePosition.edgeAlpha = edgeAlpha;
}

void TCBCurveInterpolator::CalculateTangentFromCurveDirection( Float time, Vector4& tangent ) const
{
	const Float epsilon = 0.001f;

	Vector4 p0 = EvalAt( Max( time - epsilon, 0.f ) );
	Vector4 p1 = EvalAt( Min( time + epsilon, 1.0f ) );
	tangent = ( p1 - p0 ).Normalized3();
}

void TCBCurveInterpolator::CalculateTangentFromCurveDirection( const CurvePosition& curvePosition, Vector4& tangent ) const
{
	const Int32 nEdges = IsLooping() ? Size() : Size() - 1;
	Float time = 0.0f;
	if ( nEdges == curvePosition.edgeIdx )
	{
		time = 1.0f;
	}
	else
	{
		const Float timeA = GetControlPointTime( curvePosition.edgeIdx );
		const Float timeB = GetControlPointTime( ( curvePosition.edgeIdx + 1 ) % m_numKeys );
		time = timeA + ( timeB - timeA ) * curvePosition.edgeAlpha;
	}

	CalculateTangentFromCurveDirection( time, tangent );
}

Float TCBCurveInterpolator::GetControlPointTime( Uint32 index ) const
{
	return m_times[ index ];
}
