
#pragma once

namespace curve
{
	template< typename T >
	void MultiChannelCurveBuilder::DebugVerifyTypesCheck()
	{
		RED_FATAL_ASSERT(m_curveData.GetKeyType() == GetTypeObject<T>(), "Error: Types are incompatible");
	}

	template<typename T>
	TMultiChannelCurveBuilder<T>& TMultiChannelCurveBuilder<T>::SetKeyFrame_Constant(Uint32 channelIndex, Uint32 keyFrame, const T& value)
	{
		RED_FATAL_ASSERT(GetInterpolationType(channelIndex) == EIT_Constant, "Error: Trying to set keyframe data for invalid interpolation type");
		SetDataInternal(channelIndex, keyFrame, value);
		return *this;
	}

	template<typename T>
	TMultiChannelCurveBuilder<T>& TMultiChannelCurveBuilder<T>::SetInitialKeyFrame_Linear(Uint32 channelIndex, const T& p0, const T& p1 )
	{
		RED_FATAL_ASSERT(GetInterpolationType(channelIndex) == EIT_Linear, "Error: Trying to set keyframe data for invalid interpolation type");
		SetDataInternal(channelIndex, 0, p0);
		SetDataInternal(channelIndex, 1, p1);
		return *this;
	}

	template<typename T>
	TMultiChannelCurveBuilder<T>& TMultiChannelCurveBuilder<T>::SetKeyFrame_Linear(Uint32 channelIndex, Uint32 keyFrame, const T& p )
	{
		RED_FATAL_ASSERT(GetInterpolationType(channelIndex) == EIT_Linear, "Error: Trying to set keyframe data for invalid interpolation type");
		RED_FATAL_ASSERT(keyFrame > 0, "Error: To set inital key data call the SetInitialKeyFrame_LinearFunction");
		RED_FATAL_ASSERT(keyFrame < m_curveData.GetNumKeys(channelIndex), "Error: Key index out of bounds");
		RED_FATAL_ASSERT(!m_valuesPerChannel.Empty(), "Error: Please set the initial keyframe using the SetInitiaKeyFrame_Linear function");

		Int32 idx;
		detail::linear_calculate_internal_indices(keyFrame, idx);

		SetDataInternal(channelIndex, idx, p);
		return *this;
	}

	template<typename T>
	TMultiChannelCurveBuilder<T>& TMultiChannelCurveBuilder<T>::SetInitialKeyFrame_QuadraticBezier(Uint32 channelIndex, const T& p0, const T& c0, const T& p1 )
	{
		RED_FATAL_ASSERT(GetInterpolationType(channelIndex) == EIT_BezierQuadratic, "Error: Trying to set keyframe data for invalid interpolation type");
		SetDataInternal(channelIndex, 0, p0);
		SetDataInternal(channelIndex, 1, c0);
		SetDataInternal(channelIndex, 2, p1);
		return *this;
	}

	template<typename T>
	TMultiChannelCurveBuilder<T>& TMultiChannelCurveBuilder<T>::SetKeyFrame_QuadraticBezier(Uint32 channelIndex, Uint32 keyFrame, const T& c, const T& p )
	{
		RED_FATAL_ASSERT(GetInterpolationType(channelIndex) == EIT_BezierQuadratic, "Error: Trying to set keyframe data for invalid interpolation type");
		RED_FATAL_ASSERT(keyFrame > 0, "Error: To set inital key data call the SetInitialKeyFrame_QuadraticBezier");
		RED_FATAL_ASSERT(keyFrame < m_curveData.GetNumKeys(channelIndex), "Error: Key index out of bounds");
		RED_FATAL_ASSERT(!m_valuesPerChannel.Empty(), "Error: Please set the initial keyframe using the SetInitialKeyFrame_QuadraticBezier function");

		Int32 idx_c, idx_p;
		detail::qbezier_calculate_internal_indices( keyFrame, idx_c, idx_p);

		SetDataInternal(channelIndex, idx_c, c);
		SetDataInternal(channelIndex, idx_p, p);

		return *this;
	}

	template<typename T>
	TMultiChannelCurveBuilder<T>& TMultiChannelCurveBuilder<T>::SetInitialKeyFrame_CubicBezier(Uint32 channelIndex, const T& p0, const T& c0, const T& c1, const T& p1 )
	{
		RED_FATAL_ASSERT(GetInterpolationType(channelIndex) == EIT_BezierCubic, "Error: Trying to set keyframe data for invalid interpolation type");
		SetDataInternal(channelIndex, 0, p0);
		SetDataInternal(channelIndex, 1, c0);
		SetDataInternal(channelIndex, 2, c1);
		SetDataInternal(channelIndex, 3, p1);

		return *this;
	}

	template<typename T>
	TMultiChannelCurveBuilder<T>& TMultiChannelCurveBuilder<T>::SetKeyFrame_CubicBezier(Uint32 channelIndex, Uint32 keyFrame, const T& c0, const T& c1, const T& p )
	{
		RED_FATAL_ASSERT(GetInterpolationType(channelIndex) == EIT_BezierCubic, "Error: Trying to set keyframe data for invalid interpolation type");
		RED_FATAL_ASSERT(keyFrame > 0, "Error: To set inital key data call the SetInitialKeyFrame_CubicBezier");
		RED_FATAL_ASSERT(keyFrame < m_curveData.GetNumKeys(channelIndex), "Error: Key index out of bounds");
		RED_FATAL_ASSERT(!m_valuesPerChannel.Empty(), "Error: Call set initial frame first");

		Int32 idx_c0, idx_c1, idx_p;
		detail::cbezier_calculate_internal_indices( keyFrame, idx_c0, idx_c1, idx_p);

		SetDataInternal(channelIndex, idx_c0, c0);
		SetDataInternal(channelIndex, idx_c1, c1);
		SetDataInternal(channelIndex, idx_p, p);

		return *this;
	}

	template<typename T>
	TMultiChannelCurveBuilder<T>& TMultiChannelCurveBuilder<T>::SetInitialKeyFrame_CubicHermite(Uint32 channelIndex, const T& p0, const T& c0, const T& c1, const T& p1 )
	{
		RED_FATAL_ASSERT(GetInterpolationType(channelIndex) == EIT_Hermite, "Error: Trying to set keyframe data for invalid interpolation type");
		SetDataInternal(channelIndex, 0, p0);
		SetDataInternal(channelIndex, 1, c0);
		SetDataInternal(channelIndex, 2, c1);
		SetDataInternal(channelIndex, 3, p1);

		return *this;
	}

	template<typename T>
	TMultiChannelCurveBuilder<T>& TMultiChannelCurveBuilder<T>::SetKeyFrame_CubicHermite(Uint32 channelIndex, Uint32 keyFrame, const T& c0, const T& c1, const T& p )
	{
		RED_FATAL_ASSERT(GetInterpolationType(channelIndex) == EIT_Hermite, "Error: Trying to set keyframe data for invalid interpolation type");
		RED_FATAL_ASSERT(keyFrame > 0, "Error: To set inital key data call the SetInitialKeyFrame_CubicBezier");
		RED_FATAL_ASSERT(keyFrame < m_curveData.GetNumKeys(channelIndex), "Error: Key index out of bounds");
		RED_FATAL_ASSERT(!m_valuesPerChannel.Empty(), "Error: Call set initial frame first");

		Int32 idx_c0, idx_c1, idx_p;
		detail::chermite_calculate_internal_indices( keyFrame, idx_c0, idx_c1, idx_p);

		SetDataInternal(channelIndex, idx_c0, c0);
		SetDataInternal(channelIndex, idx_c1, c1);
		SetDataInternal(channelIndex, idx_p, p);

		return *this;
	}

	template<typename T>
	void TMultiChannelCurveBuilder<T>::SetDataInternal(Uint32 channel, Uint32 idx, const T& value)
	{
		DebugVerifyTypesCheck<T>();
		T* data = reinterpret_cast<T*>(GetValue(channel, idx));
		*data   = value;
	}
}