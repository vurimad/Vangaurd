/**
* Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../redReflection/include/mathWorldTransform.h"


namespace rend
{

	using WorldCoordinate = WorldPosition::FixedPoint;
	static_assert( WorldCoordinate::ONE == (1u << 17), "WorldPosition::FixedPoint changed, need to update shaders to unpack it correctly" );


	struct RenderProxyTransform
	{
		RED_INLINE RenderProxyTransform();
		RED_INLINE RenderProxyTransform( const WorldTransform& tx, const Vector3& s = Vector3::ONES() );
		RED_INLINE RenderProxyTransform( const Transform& tx, const Vector3& s = Vector3::ONES() );

		RED_INLINE void Set( const WorldTransform& tx, const Vector3& s = Vector3::ONES() );
		RED_INLINE void Set( const Transform& tx, const Vector3& s = Vector3::ONES() );

		RED_INLINE void SetPosition( const WorldPosition& value );
		RED_INLINE void SetPosition( const Vector3& value );
		RED_INLINE void SetOrientation( const Quaternion& value );
		RED_INLINE void SetScale( const Vector3& value );
		RED_INLINE void SetScale( Float value );

		RED_INLINE const WorldPosition& GetPosition() const;
		RED_INLINE const Quaternion& GetOrientation() const;
		RED_INLINE const Vector3& GetScale() const;


		Box TransformBox( const Box& box ) const;

		Matrix ToMatrix() const;

		static RenderProxyTransform IDENTITY();

		static RenderProxyTransform SLOW_FromMatrix( const Matrix& mtx );


		WorldTransform tx;
		Vector3 scale;
	};


	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


	RED_INLINE RenderProxyTransform::RenderProxyTransform() = default;

	RED_INLINE RenderProxyTransform::RenderProxyTransform( const WorldTransform& t, const Vector3& s )
		: tx( t )
		, scale( s )
	{
	}

	RED_INLINE RenderProxyTransform::RenderProxyTransform( const Transform& t, const Vector3& s )
		: tx( t.GetPosition3(), t.GetOrientation() )
		, scale( s )
	{
	}

	RED_INLINE void RenderProxyTransform::Set( const WorldTransform& t, const Vector3& s )
	{
		tx = t;
		scale = s;
	}

	RED_INLINE void RenderProxyTransform::Set( const Transform& t, const Vector3& s )
	{
		tx.SetPosition( t.GetPosition3() );
		tx.SetOrientation( t.GetOrientation() );
		scale = s;
	}

	RED_INLINE void RenderProxyTransform::SetPosition( const WorldPosition& value )
	{
		tx.SetPosition( value );
	}

	RED_INLINE void RenderProxyTransform::SetPosition( const Vector3& value )
	{
		tx.SetPosition( value );
	}

	RED_INLINE void RenderProxyTransform::SetOrientation( const Quaternion& value )
	{
		tx.SetOrientation( value );
	}

	RED_INLINE void RenderProxyTransform::SetScale( const Vector3& value )
	{
		scale = value;
	}

	RED_INLINE void RenderProxyTransform::SetScale( Float value )
	{
		scale = Vector3( value, value, value );
	}

	RED_INLINE const WorldPosition& RenderProxyTransform::GetPosition() const
	{
		return tx.GetPosition();
	}

	RED_INLINE const Quaternion& RenderProxyTransform::GetOrientation() const
	{
		return tx.GetOrientation();
	}

	RED_INLINE const Vector3& RenderProxyTransform::GetScale() const
	{
		return scale;
	}

	RED_INLINE Box RenderProxyTransform::TransformBox( const Box& box ) const
	{
		Box scaledBox = box;
		scaledBox.Min *= Vector4( scale, 1.0f );
		scaledBox.Max *= Vector4( scale, 1.0f );
		return tx.TransformBox( scaledBox );
	}

	RED_INLINE Matrix RenderProxyTransform::ToMatrix() const
	{
		Matrix mtx = tx.ToMatrix();
		mtx.SetScale33( scale );
		return mtx;
	}


	RED_INLINE RenderProxyTransform RenderProxyTransform::IDENTITY()
	{
		return RenderProxyTransform( WorldTransform::IDENTITY(), Vector3::ONES() );
	}

	RED_INLINE RenderProxyTransform RenderProxyTransform::SLOW_FromMatrix( const Matrix& mtx )
	{
		Vector4 translation;
		Matrix rotation;
		Vector4 scale;
		Matrix::SLOW_Decompose( mtx, translation, rotation, scale );
		return RenderProxyTransform( Transform( translation.AsVector3(), rotation.ToQuat() ), scale );
	}

	RED_INLINE RenderProxyTransform operator *( const RenderProxyTransform& a, const WorldTransform& b )
	{
		RenderProxyTransform result;
		result.tx = b.TransformXForm( a.tx );
		result.scale = a.scale;
		return result;
	}

	RED_INLINE RenderProxyTransform operator *( const RenderProxyTransform& a, const Transform& b )
	{
		RenderProxyTransform result;
		result.tx = WorldTransform( b ).TransformXForm( a.tx );
		result.scale = a.scale;
		return result;
	}

}
