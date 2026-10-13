/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace curve
{
	namespace detail
	{
		void RED_REFLECTION_API linear_calculate_internal_indices( const Int32 keyframe, Int32& p );
		void RED_REFLECTION_API qbezier_calculate_internal_indices( const Int32 keyframe, Int32& c, Int32& p );
		void RED_REFLECTION_API cbezier_calculate_internal_indices( const Int32 keyframe, Int32& c0, Int32& c1, Int32& p );
		void RED_REFLECTION_API chermite_calculate_internal_indices( const Int32 keyframe, Int32& c0, Int32& c1, Int32& p );
	}

    template< typename T >
    T TSingleChannelCurve<T>::GetValue(const Uint32 keyFrame ) const
    {
        const void* data = TBaseClass::GetValue(keyFrame);
        return *reinterpret_cast<const T*>(data);
    }

    template< typename T >
    T TSingleChannelCurve<T>::GetMinValue() const
    {
        const void* data = TBaseClass::GetMinValue();
        return *reinterpret_cast<const T*>(data);
    }

    template< typename T >
    T TSingleChannelCurve<T>::GetMaxValue() const
    {
        const void* data = TBaseClass::GetMaxValue();
        return *reinterpret_cast<const T*>(data);
    }

	template< typename T >
	void SingleChannelCurveBuilder::DebugVerifyTypesCheck()
	{ 
		RED_FATAL_ASSERT( curveData.GetKeyType() == GetTypeObject<T>(), "Error: Type of Data for builder [%hs] is not the same type as [%hs] is being written as a value", curveData.GetKeyType()->GetName().AsChar(), GetTypeObject<T>()->GetName().AsChar() );
	}

	template< typename T >
	TSingleChannelCurveBuilder<T>& TSingleChannelCurveBuilder<T>::SetKeyFrame_Constant(Uint32 keyIndex, const T& value)	
	{
		RED_FATAL_ASSERT(curveData.m_interpolationType == EIT_Constant, "Error: Trying to set keyframe data for invalid interpolation type");
		SetDataInternal(keyIndex, value);
		return *this;
	}

	template< typename T >
	TSingleChannelCurveBuilder<T>& TSingleChannelCurveBuilder<T>::SetInitialKeyFrame_Linear( const T& p0, const T& p1 )
	{
		RED_FATAL_ASSERT(curveData.m_interpolationType == EIT_Linear, "Error: Trying to set keyframe data for invalid interpolation type");
		SetDataInternal(0, p0);
		SetDataInternal(1, p1);
		m_initialFrameSet = true;
		return *this;
	}

	template< typename T >
	TSingleChannelCurveBuilder<T>& TSingleChannelCurveBuilder<T>::SetKeyFrame_Linear(Uint32 keyFrame, const T& p)
	{
		RED_FATAL_ASSERT(curveData.m_interpolationType == EIT_Linear, "Error: Trying to set keyframe data for invalid interpolation type");
		RED_FATAL_ASSERT(keyFrame > 0, "Error: To set inital key data call the SetInitialKeyFrame_LinearFunction");
		RED_FATAL_ASSERT(keyFrame < curveData.GetNumKeys(), "Error: Key index out of bounds");
		RED_FATAL_ASSERT(m_initialFrameSet, "Error: Please set the initial keyframe using the SetInitiaKeyFrame_Linear function");

		Int32 idx;
		detail::linear_calculate_internal_indices(keyFrame, idx);

		SetDataInternal(idx, p);
		return *this;
	}

	template< typename T >
	TSingleChannelCurveBuilder<T>& TSingleChannelCurveBuilder<T>::SetInitialKeyFrame_QuadraticBezier( const T& p0, const T& c0, const T& p1 )
	{
		RED_FATAL_ASSERT(curveData.m_interpolationType == EIT_BezierQuadratic, "Error: Trying to set keyframe data for invalid interpolation type");
		SetDataInternal(0, p0);
		SetDataInternal(1, c0);
		SetDataInternal(2, p1);
		m_initialFrameSet = true;
		return *this;
	}

	template< typename T >
	TSingleChannelCurveBuilder<T>& TSingleChannelCurveBuilder<T>::SetKeyFrame_QuadraticBezier(Uint32 keyFrame, const T& c, const T& p)
	{
		RED_FATAL_ASSERT(curveData.m_interpolationType == EIT_BezierQuadratic, "Error: Trying to set keyframe data for invalid interpolation type");
		RED_FATAL_ASSERT(keyFrame > 0, "Error: To set inital key data call the SetInitialKeyFrame_QuadraticBezier");
		RED_FATAL_ASSERT(keyFrame < curveData.GetNumKeys(), "Error: Key index out of bounds");
		RED_FATAL_ASSERT(m_initialFrameSet, "Error: Please set the initial keyframe using the SetInitialKeyFrame_QuadraticBezier function");

		Int32 idx_c, idx_p;
		detail::qbezier_calculate_internal_indices( keyFrame, idx_c, idx_p);

		SetDataInternal(idx_c, c);
		SetDataInternal(idx_p, p);

		return *this;
	}

	template< typename T >
	TSingleChannelCurveBuilder<T>& TSingleChannelCurveBuilder<T>::SetInitialKeyFrame_CubicBezier( const T& p0, const T& c0, const T& c1, const T& p1 )
	{
		RED_FATAL_ASSERT(curveData.m_interpolationType == EIT_BezierCubic, "Error: Trying to set keyframe data for invalid interpolation type");
		SetDataInternal(0, p0);
		SetDataInternal(1, c0);
		SetDataInternal(2, c1);
		SetDataInternal(3, p1);
		m_initialFrameSet = true;

		return *this;
	}

	template< typename T >
	TSingleChannelCurveBuilder<T>& TSingleChannelCurveBuilder<T>::SetKeyFrame_CubicBezier(Uint32 keyFrame, const T& c0, const T& c1, const T& p )
	{
		RED_FATAL_ASSERT(curveData.m_interpolationType == EIT_BezierCubic, "Error: Trying to set keyframe data for invalid interpolation type");
		RED_FATAL_ASSERT(keyFrame > 0, "Error: To set inital key data call the SetInitialKeyFrame_CubicBezier");
		RED_FATAL_ASSERT(keyFrame < curveData.GetNumKeys(), "Error: Key index out of bounds");
		RED_FATAL_ASSERT(m_initialFrameSet, "Error: Call set initial frame first");

		Int32 idx_c0, idx_c1, idx_p;
		detail::cbezier_calculate_internal_indices( keyFrame, idx_c0, idx_c1, idx_p);

		SetDataInternal(idx_c0, c0);
		SetDataInternal(idx_c1, c1);
		SetDataInternal(idx_p, p);

		return *this;
	}

	template< typename T >
	TSingleChannelCurveBuilder<T>& TSingleChannelCurveBuilder<T>::SetInitialKeyFrame_CubicHermite( const T& p0, const T& c0, const T& c1, const T& p1 )
	{
		RED_FATAL_ASSERT(curveData.m_interpolationType == EIT_Hermite, "Error: Trying to set keyframe data for invalid interpolation type");
		SetDataInternal(0, p0);
		SetDataInternal(1, c0);
		SetDataInternal(2, c1);
		SetDataInternal(3, p1);

		return *this;
	}

	template< typename T >
	TSingleChannelCurveBuilder<T>& TSingleChannelCurveBuilder<T>::SetKeyFrame_CubicHermite(Uint32 keyFrame, const T& c0, const T& c1, const T& p)
	{
		RED_FATAL_ASSERT(curveData.m_interpolationType == EIT_Hermite, "Error: Trying to set keyframe data for invalid interpolation type");
		RED_FATAL_ASSERT(keyFrame > 0, "Error: To set inital key data call the SetInitialKeyFrame_CubicBezier");
		RED_FATAL_ASSERT(keyFrame < curveData.GetNumKeys(), "Error: Key index out of bounds");
		RED_FATAL_ASSERT(m_initialFrameSet, "Error: Call set initial frame first");

		Int32 idx_c0, idx_c1, idx_p;
		detail::chermite_calculate_internal_indices( keyFrame, idx_c0, idx_c1, idx_p);

		SetDataInternal(idx_c0, c0);
		SetDataInternal(idx_c1, c1);
		SetDataInternal(idx_p, p);

		return *this;
	}

	template< typename T >
	void TSingleChannelCurveBuilder<T>::SetDataInternal( Uint32 valueIndex, const T& _value )
	{
		DebugVerifyTypesCheck<T>();
		T* data = reinterpret_cast<T*>(GetValue(valueIndex));
		*data   = _value;
	}
}