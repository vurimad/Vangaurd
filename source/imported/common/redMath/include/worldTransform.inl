#pragma once

namespace math
{
	RED_INLINE WorldTransform::WorldTransform() {}

	RED_INLINE WorldTransform::WorldTransform( EIdentity )
		: m_position( WorldPosition::ZEROS() ), m_orientation( Quaternion::IDENTITY() ) 
	{}

	RED_INLINE WorldTransform::WorldTransform( const WorldPosition& p )
		: m_position( p ), m_orientation( Quaternion::IDENTITY() ) 
	{}

	RED_INLINE WorldTransform::WorldTransform( const WorldTransform& xform )
		: m_position( xform.m_position ), m_orientation( xform.m_orientation )
	{}

	RED_INLINE WorldTransform::WorldTransform( const WorldPosition& p, const Quaternion& q )
		: m_position( p ), m_orientation( q.Normalized() )
	{
		RED_MATH_PARANOID_SANITY_CHECK( IsOk() );
	}

	RED_INLINE WorldTransform::WorldTransform( const Quaternion& q ) 
		: m_position( WorldPosition::ZEROS() ), m_orientation( q.Normalized() ) 
	{
		RED_MATH_PARANOID_SANITY_CHECK( IsOk() );
	}
	
	RED_INLINE WorldTransform::WorldTransform( const Transform& xform ) 
		: m_position( xform.GetPosition3() ), m_orientation( xform.GetOrientation() ) 
	{
		RED_MATH_PARANOID_SANITY_CHECK( IsOk() );
	}
	RED_INLINE WorldTransform::WorldTransform( const Vector3& p ) 
		: m_position( p ), m_orientation( Quaternion::IDENTITY() ) 
	{
		RED_MATH_PARANOID_SANITY_CHECK( IsOk() );
	}
	RED_INLINE WorldTransform::WorldTransform( const Vector3& p, const Quaternion& q ) 
		: m_position( p ), m_orientation( q.Normalized() ) 
	{
		RED_MATH_PARANOID_SANITY_CHECK( IsOk() );
	}

	RED_FORCE_INLINE Bool WorldTransform::operator == ( const WorldTransform& v ) const
	{
		return m_position == v.m_position && m_orientation.IsAlmostEqual( v.m_orientation, 1e-5f );
	}
	RED_FORCE_INLINE Bool WorldTransform::operator != ( const WorldTransform& v ) const
	{
		return !(*this == v);
	}

	RED_INLINE void WorldTransform::SetPosition( const WorldPosition& value )
	{
		RED_MATH_PARANOID_SANITY_CHECK( value.IsOk() );
		m_position = value;
	}
	RED_INLINE void WorldTransform::SetPosition( const Vector3& value )
	{
		RED_MATH_PARANOID_SANITY_CHECK( value.IsOk() );
		m_position = WorldPosition( value );
	}
	RED_INLINE void WorldTransform::SetOrientation( const Quaternion& value )
	{
		RED_MATH_PARANOID_SANITY_CHECK( value.IsOk() );
		m_orientation = value.Normalized();
	}
	RED_INLINE const WorldPosition& WorldTransform::GetPosition() const
	{
		return m_position;
	}
	RED_INLINE const Quaternion& WorldTransform::GetOrientation() const
	{
		return m_orientation;
	}

	RED_INLINE WorldTransform WorldTransform::TransformXForm( const Transform& xform ) const
	{
		RED_MATH_PARANOID_SANITY_CHECK( xform.IsOk() );
		const Quaternion outputRotation = (m_orientation * xform.GetOrientation());
		const WorldPosition outputPosition = m_position + m_orientation.TransformUnsafe( xform.GetPosition() );
		return WorldTransform( outputPosition, outputRotation );
	}

	RED_INLINE WorldTransform WorldTransform::TransformXForm( const WorldTransform& xform ) const
	{
		RED_MATH_PARANOID_SANITY_CHECK( xform.IsOk() );
        const Quaternion outputRotation = (m_orientation * xform.m_orientation);
		const WorldPosition outputPosition = m_position + m_orientation.TransformUnsafe( xform.m_position.AsVector3() );
		return WorldTransform( outputPosition, outputRotation );
	}
	
	RED_INLINE WorldPosition WorldTransform::TransformPoint( const Vector3& pt ) const
	{
		RED_MATH_PARANOID_SANITY_CHECK( pt.IsOk() );
		return m_position + m_orientation.TransformUnsafe( pt );
	}

	RED_INLINE WorldPosition WorldTransform::TransformPoint( const Vector4& pt ) const
	{
		RED_MATH_PARANOID_SANITY_CHECK( pt.IsOk() );
		return m_position + m_orientation.TransformUnsafe( pt );
	}

	RED_INLINE WorldPosition WorldTransform::TransformPoint( const WorldPosition& pt ) const
	{
		RED_MATH_PARANOID_SANITY_CHECK( pt.IsOk() );
		return m_position + m_orientation.TransformUnsafe( pt.AsVector3() );
	}

	RED_INLINE Vector3 WorldTransform::TransformInvPoint( const WorldPosition& pt ) const
	{
		RED_MATH_PARANOID_SANITY_CHECK( pt.IsOk() );
		return m_orientation.TransformInverse( pt - m_position );
	}

	RED_INLINE Transform WorldTransform::TransformInv( const WorldTransform& xform ) const
	{
		RED_MATH_PARANOID_SANITY_CHECK( xform.IsOk() );
		const Quaternion rotInv = m_orientation.Conjugate();
		return Transform( rotInv.Transform( xform.m_position - m_position ), rotInv * xform.m_orientation );
	}

	RED_INLINE Transform WorldTransform::TransformInv( const Transform& xform ) const
	{
		RED_MATH_PARANOID_SANITY_CHECK( xform.IsOk() );
		const Quaternion rotInv = m_orientation.Conjugate();
		return Transform( rotInv.Transform( xform.GetPosition3() - m_position.AsVector3() ), rotInv * xform.GetOrientation() );
	}

	RED_INLINE WorldTransform WorldTransform::GetInverse() const
	{
		return WorldTransform( m_orientation.TransformInverse( -m_position.AsVector3() ), m_orientation.Conjugate() );
	}

	RED_INLINE Vector3 WorldTransform::GetForward() const
	{
		return m_orientation.GetYAxis3();
	}
	RED_INLINE Vector3 WorldTransform::GetRight() const
	{
		return m_orientation.GetXAxis3();
	}
	RED_INLINE Vector3 WorldTransform::GetUp() const
	{
		return m_orientation.GetZAxis3();
	}

	RED_INLINE Box WorldTransform::TransformBox( const Box& box ) const
	{
		if ( box.IsEmpty() )
			return Box::EMPTY();

		RED_MATH_PARANOID_SANITY_CHECK( box.IsOk() );

		const Vector3 axisX = m_orientation.GetXAxisUnsafe();
		const Vector3 axisY = m_orientation.GetYAxisUnsafe();
		const Vector3 axisZ = m_orientation.GetZAxisUnsafe();

		const Vector3 xa = axisX * box.Min.X;
		const Vector3 xb = axisX * box.Max.X;
		const Vector3 ya = axisY * box.Min.Y;
		const Vector3 yb = axisY * box.Max.Y;
		const Vector3 za = axisZ * box.Min.Z;
		const Vector3 zb = axisZ * box.Max.Z;
		const Vector3 posV3 = m_position.AsVector3();
		return Box(
			posV3 + Vector3::Min( xa, xb ) + Vector3::Min( ya, yb ) + Vector3::Min( za, zb ),
			posV3 + Vector3::Max( xa, xb ) + Vector3::Max( ya, yb ) + Vector3::Max( za, zb )
		);
	}

	RED_INLINE WorldTransform WorldTransform::IDENTITY()
	{ 
		return WorldTransform( EIDENTITY ); 
	}

	RED_INLINE Bool WorldTransform::IsOk() const
	{
		return m_position.IsOk() && m_orientation.IsOk();
	}

	RED_INLINE Matrix WorldTransform::ToMatrix() const
	{
		Matrix mtx = m_orientation.ToMatrix();
		mtx.SetTranslation( m_position.AsVector3() );
		return mtx;
	}

	RED_INLINE Matrix WorldTransform::ToMatrixUnsafe() const
	{
		Matrix mtx = m_orientation.ToMatrixUnsafe();
		mtx.SetTranslation( m_position.AsVector3() );
		return mtx;
	}

	RED_INLINE Transform WorldTransform::_ToXForm() const
	{
		return Transform( m_position.AsVector3(), m_orientation );
	}

	// ---
	RED_INLINE Float DistanceSquared( const WorldPosition& a, const WorldPosition& b )
	{
		return (b - a).SquareMag();
	}

	RED_INLINE Float DistanceSquared( const WorldPosition& a, const Vector3& b )
	{
		return (a - b).AsVector3().SquareMag();
	}
}//
