/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "rttiType.h"
#include "rttiSystem.h"
#include "rttiClassDeclarationMacros.h"
#include "reflectionPool.h"
#include "dataBuffer.h"

namespace curve
{

enum ESegmentsLinkType : Uint8
{
	ESLT_Normal,
	ESLT_Smooth,
	ESLT_SmoothSymmetric
};

enum EInterpolationType : Uint8
{
	EIT_Constant,
	EIT_Linear,
	EIT_BezierQuadratic,
	EIT_BezierCubic,
	EIT_Hermite
};

class RED_REFLECTION_API SingleChannelCurve
{
	RTTI_DECLARE_TYPE(SingleChannelCurve)

public:
	SingleChannelCurve( const rtti::IType* keyType = nullptr );
	SingleChannelCurve(const SingleChannelCurve& other);
	SingleChannelCurve( SingleChannelCurve&& other );
	SingleChannelCurve& operator=(const SingleChannelCurve& other);
	SingleChannelCurve& operator=( SingleChannelCurve&& other );

	red::Uint32 GetNumValues() const;
	red::Uint32 GetNumKeys() const;
	red::Uint32 GetAlignment() const;

	const void* GetValuesArray() const;
	const void* GetValue( const Uint32 keyIndex ) const;
	const rtti::IType* GetKeyType() const;

    const void* GetMinValue() const;
    const void* GetMaxValue() const;

	void Swap( SingleChannelCurve& other );

	EInterpolationType m_interpolationType;
	ESegmentsLinkType m_linkType;

private:
	struct header
	{
		Uint32 num_keys;
		Uint32 num_values;
		Uint32 alignment;
		Uint32 values_offset;
	};

	DataBuffer m_data;
	const rtti::IType* m_dataType;

	friend class SingleChannelCurveBuilder;
};

template< class T >
struct TSingleChannelCurve : public SingleChannelCurve
{
    RED_BASE_CLASS(SingleChannelCurve);

	using type = T;

	TSingleChannelCurve() 
		: SingleChannelCurve( GetTypeObject<T>() )
	{}

    T GetValue( const Uint32 keyFrame ) const;
    T GetMinValue() const;
    T GetMaxValue() const;
};

class RED_REFLECTION_API SingleChannelCurveBuilder
{
public:
	SingleChannelCurveBuilder( SingleChannelCurve& data );

	void InitConstantCurve(const Uint32 numValues, const red::memory::Pool& pool = red::PoolCurves());
	void InitLinearCurve(const Uint32 numValues, const red::memory::Pool& pool = red::PoolCurves());
	void InitQuadraticBezier(const Uint32 numValues, const red::memory::Pool& pool = red::PoolCurves());
	void InitCubicBezier(const Uint32 numValues, const red::memory::Pool& pool = red::PoolCurves());
	void InitHermite(const Uint32 numValues, const red::memory::Pool& pool = red::PoolCurves());

	void* GetValue( const Uint32 keyIndex );
	void SetLinkType( ESegmentsLinkType newType );

protected:
	template< typename T >
	void DebugVerifyTypesCheck();

	SingleChannelCurve& curveData;

private:
	void InitInternal(const Uint32 numKeys, const Uint32 numValues, EInterpolationType type, const red::memory::Pool& pool = red::PoolCurves());
};

template< typename T >
class TSingleChannelCurveBuilder : public SingleChannelCurveBuilder
{
public:
	TSingleChannelCurveBuilder( SingleChannelCurve& data ) : SingleChannelCurveBuilder(data), m_initialFrameSet(false) {}

	TSingleChannelCurveBuilder& SetKeyFrame_Constant(Uint32 keyIndex, const T& value);

	TSingleChannelCurveBuilder& SetInitialKeyFrame_Linear( const T& p0, const T& p1 );
	TSingleChannelCurveBuilder& SetKeyFrame_Linear( Uint32 keyIndex, const T& p );

	TSingleChannelCurveBuilder& SetInitialKeyFrame_QuadraticBezier( const T& p0, const T& c0, const T& p1 );
	TSingleChannelCurveBuilder& SetKeyFrame_QuadraticBezier( Uint32 keyIndex, const T& c, const T& p );

	TSingleChannelCurveBuilder& SetInitialKeyFrame_CubicBezier( const T& p0, const T& c0, const T& c1, const T& p1 );
	TSingleChannelCurveBuilder& SetKeyFrame_CubicBezier( Uint32 keyIndex, const T& c0, const T& c1, const T& p );

	TSingleChannelCurveBuilder& SetInitialKeyFrame_CubicHermite( const T& p0, const T& c0, const T& c1, const T& p1 );
	TSingleChannelCurveBuilder& SetKeyFrame_CubicHermite( Uint32 keyIndex, const T& c0, const T& c1, const T& p );

private:

	void SetDataInternal( Uint32 valueIndex, const T& _value );
	Bool m_initialFrameSet;
};

namespace FOR_UNIT_TESTS
{
	void RED_REFLECTION_API test_linear_calculate_internal_indices_from_keyframe(const Int32 keyframe, Int32& idx_p );
	void RED_REFLECTION_API test_qbezier_calculate_internal_indices_from_keyframe(const Int32 keyframe, Int32& idx_c, Int32& idx_p );
	void RED_REFLECTION_API test_cbezier_calculate_internal_indices_from_keyframe(const Int32 keyframe, Int32& idx_c0, Int32& idx_c1, Int32& idx_p);
}

}

#include "singleChannelCurve2.hpp"