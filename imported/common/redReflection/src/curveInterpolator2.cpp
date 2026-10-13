#include "build.h"
#include "curveInterpolator2.h"
#include "mathVector2.h"
#include "mathColor.h"
#include "mathEulerAngles.h"

#include "../../../common/redMath/include/interpolation.h"

namespace curve
{
	namespace detail
	{
		Int32 get_key_frame( const Int32 numPoints, Float factor )
		{
			const Float f = factor - (Float)((Int32)factor);
			return std::min<Int32>( f > 0.f ? (Int32)factor : (Int32)(factor-0.5f), numPoints-1 );
		}

		void scale_factor_between_0_and_1( const Float min, const Float max, Float& factor )
		{
			factor = (max - min) > 0 ? (std::min(factor-min, 1.f) / (max - min)) : 1.f;
		}

		void calc_factor( const Int32 num_points, Float& factor )
		{
			Float min = (Float)get_key_frame(num_points, factor);
			Float max = min + 1.f;

			scale_factor_between_0_and_1(min, max, factor);
		}

		void calc_linear_factors( const Int32 numValues, Int32& src, Int32& dst, Float factor )
		{
			src = get_key_frame(numValues, factor);
			dst = std::min(src+1, numValues-1);
		}

		void calc_qbezier_factors( const Int32 numValues, Int32& p0, Int32& c0, Int32& p1, Float factor )
		{
			const auto key_frame_num = get_key_frame(numValues, factor);
			p0 = std::min( 2*key_frame_num, numValues - 1);
			c0 = std::min( p0 + 1, numValues - 1);
			p1 = std::min( c0 + 1, numValues - 1);
		}

		void calc_cbezier_factors( const Int32 numValues, Int32& p0, Int32& c0, Int32& c1, Int32& p1, Float factor )
		{
			constexpr Int32 num_control_points_per_key_frame = 2;
			const auto key_frame_num = get_key_frame(numValues, factor);
			p0 = std::min( 2*key_frame_num + key_frame_num, numValues - 1);
			c0 = std::min( p0 + 1, numValues - 1);
			c1 = std::min( c0 + 1, numValues - 1);
			p1 = std::min( c1 + 1, numValues - 1);
		}

        void calc_chermite_factors( const Int32 numValues, Int32& p0, Int32& c0, Int32& c1, Int32& p1, Float factor )
		{
			return calc_cbezier_factors(numValues, p0, c0, c1, p1, factor);
		}
	}

	template< typename T >
	const T EvalCurve_Constant( const Int32 numValues, const T* values, const Float factor  )
	{
		const auto src = detail::get_key_frame(numValues, factor);
		RED_FATAL_ASSERT( src < numValues, "Error: Key out of range" );
		return values[src];
	}

	template< typename T >
	const T EvalCurve_Linear( const Int32 numValues, const T* values, Float factor  )
	{
		Int32 src,dst;
		detail::calc_linear_factors(numValues, src, dst, factor);
		detail::calc_factor(numValues, factor);

		return math::Interpolation<T>::Linear(values[src],values[dst], factor);
	}

	template< typename T >
	const T EvalCurve_QuadradicBezier( const Int32 numValues, const T* values, Float factor  )
	{
		Int32 p0, c0, p1;
		detail::calc_qbezier_factors(numValues, p0, c0, p1, factor);
		detail::calc_factor(numValues, factor);

		return math::Interpolation<T>::QuadraticBezier(values[p0], values[c0], values[p1], factor);
	}

	template< typename T >
	const T EvalCurve_CubicBezier( const Int32 numValues, const T* values, Float factor  )
	{
		Int32 p0, c0, c1, p1;
		detail::calc_cbezier_factors(numValues, p0, c0, c1, p1, factor);
		detail::calc_factor(numValues, factor);

		return math::Interpolation<T>::CubicBezier(values[p0], values[c0], values[c1], values[p1], factor);
	}

    template< typename T >
	const T EvalCurve_CubicHermite( const Int32 numValues, const T* values, Float factor  )
	{
		Int32 p0, c0, c1, p1;
		detail::calc_chermite_factors(numValues, p0, c0, c1, p1, factor);
		detail::calc_factor(numValues, factor);

		return math::Interpolation<T>::CubicHermite(values[p0], values[c0], values[c1], values[p1], factor);
	}

	template<>
	RED_REFLECTION_API Float InterpolateCurve_Constant( const TSingleChannelCurve<Float>& data, const Float factor )
	{
		RED_FATAL_ASSERT(data.m_interpolationType == EIT_Constant, "Error: Cannot use this form of interpolation");
		return EvalCurve_Constant( data.GetNumValues(), (const Float*)data.GetValuesArray(), factor );
	}

    template<>
	RED_REFLECTION_API Vector2 InterpolateCurve_Constant( const TSingleChannelCurve<Vector2>& data, const Float factor )
	{
		RED_FATAL_ASSERT(data.m_interpolationType == EIT_Constant, "Error: Cannot use this form of interpolation");
		return EvalCurve_Constant( data.GetNumValues(), (const Vector2*)data.GetValuesArray(), factor );
	}

    template<>
	RED_REFLECTION_API Vector3 InterpolateCurve_Constant( const TSingleChannelCurve<Vector3>& data, const Float factor )
	{
		RED_FATAL_ASSERT(data.m_interpolationType == EIT_Constant, "Error: Cannot use this form of interpolation");
		return EvalCurve_Constant( data.GetNumValues(), (const Vector3*)data.GetValuesArray(), factor );
	}

	template<>
	RED_REFLECTION_API Vector4 InterpolateCurve_Constant(const TSingleChannelCurve<Vector4>& data, const Float factor)
	{
		return EvalCurve_Constant(data.GetNumValues(), (const Vector4*)data.GetValuesArray(), factor);
	}

	template<>
	RED_REFLECTION_API HDRColor InterpolateCurve_Constant(const TSingleChannelCurve<HDRColor>& data, const Float factor )
	{
		RED_FATAL_ASSERT(data.m_interpolationType == EIT_Constant, "Error: Cannot use this form of interpolation");
		return EvalCurve_Constant( data.GetNumValues(), (const HDRColor*)data.GetValuesArray(), factor);
	}

    template<>
	RED_REFLECTION_API Color InterpolateCurve_Constant(const TSingleChannelCurve<Color>& data, const Float factor )
	{
		RED_FATAL_ASSERT(data.m_interpolationType == EIT_Constant, "Error: Cannot use this form of interpolation");
		return EvalCurve_Constant( data.GetNumValues(), (const Color*)data.GetValuesArray(), factor);
	}

	//--

	template<>
	RED_REFLECTION_API Float InterpolateCurve_Constant( const TMultiChannelCurve<Float>& data, const Uint32 channel, const Float factor )
	{
		RED_FATAL_ASSERT(data.GetInterpolationType(channel) == EIT_Constant, "Error: Cannot use this form of interpolation");
		return EvalCurve_Constant( data.GetNumValues(channel), (const Float*)data.GetValuesArray(channel), factor );
	}

    template<>
	RED_REFLECTION_API Vector2 InterpolateCurve_Constant(const TMultiChannelCurve<Vector2>& data, const Uint32 channel, const Float factor)
	{
		RED_FATAL_ASSERT(data.GetInterpolationType(channel) == EIT_Constant, "Error: Cannot use this form of interpolation");
		return EvalCurve_Constant(data.GetNumValues(channel), (const Vector2*)data.GetValuesArray(channel), factor);
	}

    template<>
	RED_REFLECTION_API Vector3 InterpolateCurve_Constant(const TMultiChannelCurve<Vector3>& data, const Uint32 channel, const Float factor)
	{
		RED_FATAL_ASSERT(data.GetInterpolationType(channel) == EIT_Constant, "Error: Cannot use this form of interpolation");
		return EvalCurve_Constant(data.GetNumValues(channel), (const Vector3*)data.GetValuesArray(channel), factor);
	}

	template<>
	RED_REFLECTION_API Vector4 InterpolateCurve_Constant(const TMultiChannelCurve<Vector4>& data, const Uint32 channel, const Float factor)
	{
		RED_FATAL_ASSERT(data.GetInterpolationType(channel) == EIT_Constant, "Error: Cannot use this form of interpolation");
		return EvalCurve_Constant(data.GetNumValues(channel), (const Vector4*)data.GetValuesArray(channel), factor);
	}

	template<>
	RED_REFLECTION_API HDRColor InterpolateCurve_Constant(const TMultiChannelCurve<HDRColor>& data, const Uint32 channel, const Float factor )
	{
		RED_FATAL_ASSERT(data.GetInterpolationType(channel) == EIT_Constant, "Error: Cannot use this form of interpolation");
		return EvalCurve_Constant( data.GetNumValues(channel), (const HDRColor*)data.GetValuesArray(channel), factor);
	}

    template<>
	RED_REFLECTION_API Color InterpolateCurve_Constant(const TMultiChannelCurve<Color>& data, const Uint32 channel, const Float factor )
	{
		RED_FATAL_ASSERT(data.GetInterpolationType(channel) == EIT_Constant, "Error: Cannot use this form of interpolation");
		return EvalCurve_Constant( data.GetNumValues(channel), (const Color*)data.GetValuesArray(channel), factor);
	}

	//--

	template<>
	RED_REFLECTION_API Float InterpolateCurve_Linear( const TSingleChannelCurve<Float>& data, const Float factor )
	{
		RED_FATAL_ASSERT(data.m_interpolationType == EIT_Linear, "Error: Cannot use this form of interpolation");
		return EvalCurve_Linear( data.GetNumValues(), (const Float*)data.GetValuesArray(), factor);
	}

	template<>
	RED_REFLECTION_API Vector2 InterpolateCurve_Linear(const TSingleChannelCurve<Vector2>& data, const Float factor )
	{
		RED_FATAL_ASSERT(data.m_interpolationType == EIT_Constant, "Error: Cannot use this form of interpolation");
		return EvalCurve_Linear( data.GetNumValues(), (const Vector2*)data.GetValuesArray(), factor );
	}

	template<>
	RED_REFLECTION_API Vector3 InterpolateCurve_Linear(const TSingleChannelCurve<Vector3>& data, const Float factor )
	{
		RED_FATAL_ASSERT(data.m_interpolationType == EIT_Linear, "Error: Cannot use this form of interpolation");
		return EvalCurve_Linear( data.GetNumValues(), (const Vector3*)data.GetValuesArray(), factor );
	}

	template<>
	RED_REFLECTION_API Vector4 InterpolateCurve_Linear(const TSingleChannelCurve<Vector4>& data, const Float factor )
	{
		RED_FATAL_ASSERT(data.m_interpolationType == EIT_Linear, "Error: Cannot use this form of interpolation");
		return EvalCurve_Linear( data.GetNumValues(), (const Vector4*)data.GetValuesArray(), factor );
	}

	template<>
	RED_REFLECTION_API HDRColor InterpolateCurve_Linear(const TSingleChannelCurve<HDRColor>& data, const Float factor )
	{
		RED_FATAL_ASSERT(data.m_interpolationType == EIT_Linear, "Error: Cannot use this form of interpolation");
		return EvalCurve_Linear( data.GetNumValues(), (const HDRColor*)data.GetValuesArray(), factor );
	}

    template<>
	RED_REFLECTION_API Color InterpolateCurve_Linear(const TSingleChannelCurve<Color>& data, const Float factor )
	{
		RED_FATAL_ASSERT(data.m_interpolationType == EIT_Linear, "Error: Cannot use this form of interpolation");
		return EvalCurve_Linear( data.GetNumValues(), (const Color*)data.GetValuesArray(), factor );
	}

	//--

	template<>
	RED_REFLECTION_API Float InterpolateCurve_Linear( const TMultiChannelCurve<Float>& data, const Uint32 channel, const Float factor )
	{
		RED_FATAL_ASSERT(data.GetInterpolationType(channel) == EIT_Linear, "Error: Cannot use this form of interpolation");
		return EvalCurve_Linear( data.GetNumValues(channel), (const Float*)data.GetValuesArray(channel), factor);
	}

	template<>
	RED_REFLECTION_API Vector2 InterpolateCurve_Linear(const TMultiChannelCurve<Vector2>& data, const Uint32 channel, const Float factor )
	{
		RED_FATAL_ASSERT(data.GetInterpolationType(channel) == EIT_Constant, "Error: Cannot use this form of interpolation");
		return EvalCurve_Linear( data.GetNumValues(channel), (const Vector2*)data.GetValuesArray(channel), factor );
	}

	template<>
	RED_REFLECTION_API Vector3 InterpolateCurve_Linear(const TMultiChannelCurve<Vector3>& data, const Uint32 channel, const Float factor )
	{
		RED_FATAL_ASSERT(data.GetInterpolationType(channel) == EIT_Linear, "Error: Cannot use this form of interpolation");
		return EvalCurve_Linear( data.GetNumValues(channel), (const Vector3*)data.GetValuesArray(channel), factor );
	}

	template<>
	RED_REFLECTION_API Vector4 InterpolateCurve_Linear(const TMultiChannelCurve<Vector4>& data, const Uint32 channel, const Float factor )
	{
		RED_FATAL_ASSERT(data.GetInterpolationType(channel) == EIT_Linear, "Error: Cannot use this form of interpolation");
		return EvalCurve_Linear( data.GetNumValues(channel), (const Vector4*)data.GetValuesArray(channel), factor );
	}

	template<>
	RED_REFLECTION_API HDRColor InterpolateCurve_Linear(const TMultiChannelCurve<HDRColor>& data, const Uint32 channel, const Float factor )
	{
		RED_FATAL_ASSERT(data.GetInterpolationType(channel) == EIT_Linear, "Error: Cannot use this form of interpolation");
		return EvalCurve_Linear( data.GetNumValues(channel), (const HDRColor*)data.GetValuesArray(channel), factor );
	}

    template<>
	RED_REFLECTION_API Color InterpolateCurve_Linear(const TMultiChannelCurve<Color>& data, const Uint32 channel, const Float factor )
	{
		RED_FATAL_ASSERT(data.GetInterpolationType(channel) == EIT_Linear, "Error: Cannot use this form of interpolation");
		return EvalCurve_Linear( data.GetNumValues(channel), (const Color*)data.GetValuesArray(channel), factor );
	}

	//--

	template<>
	RED_REFLECTION_API Float InterpolateCurve_QuadraticBezier(const TSingleChannelCurve<Float>& data, const Float factor )
	{
		RED_FATAL_ASSERT(data.m_interpolationType == EIT_BezierQuadratic, "Error: Cannot use this form of interpolation");
		return EvalCurve_QuadradicBezier( data.GetNumValues(), (const Float*)data.GetValuesArray(), factor );
	}

	template<>
	RED_REFLECTION_API Vector2 InterpolateCurve_QuadraticBezier(const TSingleChannelCurve<Vector2>& data, const Float factor )
	{
		RED_FATAL_ASSERT(data.m_interpolationType == EIT_BezierQuadratic, "Error: Cannot use this form of interpolation");
		return EvalCurve_QuadradicBezier( data.GetNumValues(), (const Vector2*)data.GetValuesArray(), factor );
	}

	template<>
	RED_REFLECTION_API Vector3 InterpolateCurve_QuadraticBezier(const TSingleChannelCurve<Vector3>& data, const Float factor )
	{
		RED_FATAL_ASSERT(data.m_interpolationType == EIT_BezierQuadratic, "Error: Cannot use this form of interpolation");
		return EvalCurve_QuadradicBezier( data.GetNumValues(), (const Vector3*)data.GetValuesArray(), factor );
	}

	template<>
	RED_REFLECTION_API Vector4 InterpolateCurve_QuadraticBezier(const TSingleChannelCurve<Vector4>& data, const Float factor )
	{
		RED_FATAL_ASSERT(data.m_interpolationType == EIT_BezierQuadratic, "Error: Cannot use this form of interpolation");
		return EvalCurve_QuadradicBezier( data.GetNumValues(), (const Vector4*)data.GetValuesArray(), factor );
	}

	template<>
	RED_REFLECTION_API HDRColor InterpolateCurve_QuadraticBezier(const TSingleChannelCurve<HDRColor>& data, const Float factor )
	{
		RED_FATAL_ASSERT(data.m_interpolationType == EIT_BezierQuadratic, "Error: Cannot use this form of interpolation");
		return EvalCurve_QuadradicBezier( data.GetNumValues(), (const HDRColor*)data.GetValuesArray(), factor );
	}

    template<>
	RED_REFLECTION_API Color InterpolateCurve_QuadraticBezier(const TSingleChannelCurve<Color>& data, const Float factor )
	{
		RED_FATAL_ASSERT(data.m_interpolationType == EIT_BezierQuadratic, "Error: Cannot use this form of interpolation");
		return EvalCurve_QuadradicBezier( data.GetNumValues(), (const Color*)data.GetValuesArray(), factor );
	}

	//--

	template<>
	RED_REFLECTION_API Float InterpolateCurve_QuadraticBezier(const TMultiChannelCurve<Float>& data, const Uint32 channel, const Float factor )
	{
		RED_FATAL_ASSERT(data.GetInterpolationType(channel) == EIT_BezierQuadratic, "Error: Cannot use this form of interpolation");
		return EvalCurve_QuadradicBezier( data.GetNumValues(channel), (const Float*)data.GetValuesArray(channel), factor );
	}

	template<>
	RED_REFLECTION_API Vector2 InterpolateCurve_QuadraticBezier(const TMultiChannelCurve<Vector2>& data, const Uint32 channel, const Float factor )
	{
		RED_FATAL_ASSERT(data.GetInterpolationType(channel) == EIT_BezierQuadratic, "Error: Cannot use this form of interpolation");
		return EvalCurve_QuadradicBezier( data.GetNumValues(channel), (const Vector2*)data.GetValuesArray(channel), factor );
	}

	template<>
	RED_REFLECTION_API Vector3 InterpolateCurve_QuadraticBezier(const TMultiChannelCurve<Vector3>& data, const Uint32 channel, const Float factor )
	{
		RED_FATAL_ASSERT(data.GetInterpolationType(channel) == EIT_BezierQuadratic, "Error: Cannot use this form of interpolation");
		return EvalCurve_QuadradicBezier( data.GetNumValues(channel), (const Vector3*)data.GetValuesArray(channel), factor );
	}

	template<>
	RED_REFLECTION_API Vector4 InterpolateCurve_QuadraticBezier(const TMultiChannelCurve<Vector4>& data, const Uint32 channel, const Float factor )
	{
		RED_FATAL_ASSERT(data.GetInterpolationType(channel) == EIT_BezierQuadratic, "Error: Cannot use this form of interpolation");
		return EvalCurve_QuadradicBezier( data.GetNumValues(channel), (const Vector4*)data.GetValuesArray(channel), factor );
	}

	template<>
	RED_REFLECTION_API HDRColor InterpolateCurve_QuadraticBezier(const TMultiChannelCurve<HDRColor>& data, const Uint32 channel, const Float factor )
	{
		RED_FATAL_ASSERT(data.GetInterpolationType(channel) == EIT_BezierQuadratic, "Error: Cannot use this form of interpolation");
		return EvalCurve_QuadradicBezier( data.GetNumValues(channel), (const HDRColor*)data.GetValuesArray(channel), factor );
	}

    template<>
	RED_REFLECTION_API Color InterpolateCurve_QuadraticBezier(const TMultiChannelCurve<Color>& data, const Uint32 channel, const Float factor )
	{
		RED_FATAL_ASSERT(data.GetInterpolationType(channel) == EIT_BezierQuadratic, "Error: Cannot use this form of interpolation");
		return EvalCurve_QuadradicBezier( data.GetNumValues(channel), (const Color*)data.GetValuesArray(channel), factor );
	}

	//--

	template<>
	RED_REFLECTION_API Float InterpolateCurve_CubicBezier(const TSingleChannelCurve<Float>& data, const Float factor )
	{
		RED_FATAL_ASSERT(data.m_interpolationType == EIT_BezierCubic, "Error: Cannot use this form of interpolation");
		return EvalCurve_CubicBezier( data.GetNumValues(), (const Float*)data.GetValuesArray(), factor );
	}

	template<>
	RED_REFLECTION_API Vector2 InterpolateCurve_CubicBezier(const TSingleChannelCurve<Vector2>& data, const Float factor )
	{
		RED_FATAL_ASSERT(data.m_interpolationType == EIT_BezierCubic, "Error: Cannot use this form of interpolation");
		return EvalCurve_CubicBezier( data.GetNumValues(), (const Vector2*)data.GetValuesArray(), factor );
	}

	template<>
	RED_REFLECTION_API Vector3 InterpolateCurve_CubicBezier(const TSingleChannelCurve<Vector3>& data, const Float factor )
	{
		RED_FATAL_ASSERT(data.m_interpolationType == EIT_BezierCubic, "Error: Cannot use this form of interpolation");
		return EvalCurve_CubicBezier( data.GetNumValues(), (const Vector3*)data.GetValuesArray(), factor );
	}

	template<>
	RED_REFLECTION_API Vector4 InterpolateCurve_CubicBezier(const TSingleChannelCurve<Vector4>& data, const Float factor )
	{
		RED_FATAL_ASSERT(data.m_interpolationType == EIT_BezierCubic, "Error: Cannot use this form of interpolation");
		return EvalCurve_CubicBezier( data.GetNumValues(), (const Vector4*)data.GetValuesArray(), factor );
	}

	template<>
	RED_REFLECTION_API HDRColor InterpolateCurve_CubicBezier(const TSingleChannelCurve<HDRColor>& data, const Float factor )
	{
		RED_FATAL_ASSERT(data.m_interpolationType == EIT_BezierCubic, "Error: Cannot use this form of interpolation");
		return EvalCurve_CubicBezier( data.GetNumValues(), (const HDRColor*)data.GetValuesArray(), factor );
	}

    template<>
	RED_REFLECTION_API Color InterpolateCurve_CubicBezier(const TSingleChannelCurve<Color>& data, const Float factor )
	{
		RED_FATAL_ASSERT(data.m_interpolationType == EIT_BezierCubic, "Error: Cannot use this form of interpolation");
		return EvalCurve_CubicBezier( data.GetNumValues(), (const Color*)data.GetValuesArray(), factor );
	}

	//--

	template<>
	RED_REFLECTION_API Float InterpolateCurve_CubicBezier(const TMultiChannelCurve<Float>& data, const Uint32 channel, const Float factor )
	{
		RED_FATAL_ASSERT(data.GetInterpolationType(channel) == EIT_BezierCubic, "Error: Cannot use this form of interpolation");
		return EvalCurve_CubicBezier( data.GetNumValues(channel), (const Float*)data.GetValuesArray(channel), factor );
	}

	template<>
	RED_REFLECTION_API Vector2 InterpolateCurve_CubicBezier(const TMultiChannelCurve<Vector2>& data, const Uint32 channel, const Float factor )
	{
		RED_FATAL_ASSERT(data.GetInterpolationType(channel) == EIT_BezierCubic, "Error: Cannot use this form of interpolation");
		return EvalCurve_CubicBezier( data.GetNumValues(channel), (const Vector2*)data.GetValuesArray(channel), factor );
	}

	template<>
	RED_REFLECTION_API Vector3 InterpolateCurve_CubicBezier(const TMultiChannelCurve<Vector3>& data, const Uint32 channel, const Float factor )
	{
		RED_FATAL_ASSERT(data.GetInterpolationType(channel) == EIT_BezierCubic, "Error: Cannot use this form of interpolation");
		return EvalCurve_CubicBezier( data.GetNumValues(channel), (const Vector3*)data.GetValuesArray(channel), factor );
	}

	template<>
	RED_REFLECTION_API Vector4 InterpolateCurve_CubicBezier(const TMultiChannelCurve<Vector4>& data, const Uint32 channel, const Float factor )
	{
		RED_FATAL_ASSERT(data.GetInterpolationType(channel) == EIT_BezierCubic, "Error: Cannot use this form of interpolation");
		return EvalCurve_CubicBezier( data.GetNumValues(channel), (const Vector4*)data.GetValuesArray(channel), factor );
	}

	template<>
	RED_REFLECTION_API HDRColor InterpolateCurve_CubicBezier(const TMultiChannelCurve<HDRColor>& data, const Uint32 channel, const Float factor )
	{
		RED_FATAL_ASSERT(data.GetInterpolationType(channel) == EIT_BezierCubic, "Error: Cannot use this form of interpolation");
		return EvalCurve_CubicBezier( data.GetNumValues(channel), (const HDRColor*)data.GetValuesArray(channel), factor );
	}

    template<>
	RED_REFLECTION_API Color InterpolateCurve_CubicBezier(const TMultiChannelCurve<Color>& data, const Uint32 channel, const Float factor )
	{
		RED_FATAL_ASSERT(data.GetInterpolationType(channel) == EIT_BezierCubic, "Error: Cannot use this form of interpolation");
		return EvalCurve_CubicBezier( data.GetNumValues(channel), (const Color*)data.GetValuesArray(channel), factor );
	}

	//--

    template<>
	RED_REFLECTION_API Float InterpolateCurve_CubicHermite(const TSingleChannelCurve<Float>& data, const Float factor )
	{
		RED_FATAL_ASSERT(data.m_interpolationType == EIT_Hermite, "Error: Cannot use this form of interpolation");
		return EvalCurve_CubicBezier( data.GetNumValues(), (const Float*)data.GetValuesArray(), factor );
	}

	template<>
	RED_REFLECTION_API Vector2 InterpolateCurve_CubicHermite(const TSingleChannelCurve<Vector2>& data, const Float factor )
	{
		RED_FATAL_ASSERT(data.m_interpolationType == EIT_Hermite, "Error: Cannot use this form of interpolation");
		return EvalCurve_CubicBezier( data.GetNumValues(), (const Vector2*)data.GetValuesArray(), factor );
	}

	template<>
	RED_REFLECTION_API Vector3 InterpolateCurve_CubicHermite(const TSingleChannelCurve<Vector3>& data, const Float factor )
	{
		RED_FATAL_ASSERT(data.m_interpolationType == EIT_Hermite, "Error: Cannot use this form of interpolation");
		return EvalCurve_CubicBezier( data.GetNumValues(), (const Vector3*)data.GetValuesArray(), factor );
	}

	template<>
	RED_REFLECTION_API Vector4 InterpolateCurve_CubicHermite(const TSingleChannelCurve<Vector4>& data, const Float factor )
	{
		RED_FATAL_ASSERT(data.m_interpolationType == EIT_Hermite, "Error: Cannot use this form of interpolation");
		return EvalCurve_CubicBezier( data.GetNumValues(), (const Vector4*)data.GetValuesArray(), factor );
	}

	template<>
	RED_REFLECTION_API HDRColor InterpolateCurve_CubicHermite(const TSingleChannelCurve<HDRColor>& data, const Float factor )
	{
		RED_FATAL_ASSERT(data.m_interpolationType == EIT_Hermite, "Error: Cannot use this form of interpolation");
		return EvalCurve_CubicBezier( data.GetNumValues(), (const HDRColor*)data.GetValuesArray(), factor );
	}

    template<>
	RED_REFLECTION_API Color InterpolateCurve_CubicHermite(const TSingleChannelCurve<Color>& data, const Float factor )
	{
		RED_FATAL_ASSERT(data.m_interpolationType == EIT_Hermite, "Error: Cannot use this form of interpolation");
		return EvalCurve_CubicBezier( data.GetNumValues(), (const Color*)data.GetValuesArray(), factor );
	}

    //--

    template<>
	RED_REFLECTION_API Float InterpolateCurve_CubicHermite(const TMultiChannelCurve<Float>& data, const Uint32 channel, const Float factor )
	{
		RED_FATAL_ASSERT(data.GetInterpolationType(channel) == EIT_Hermite, "Error: Cannot use this form of interpolation");
		return EvalCurve_CubicHermite( data.GetNumValues(channel), (const Float*)data.GetValuesArray(channel), factor );
	}

	template<>
	RED_REFLECTION_API Vector2 InterpolateCurve_CubicHermite(const TMultiChannelCurve<Vector2>& data, const Uint32 channel, const Float factor )
	{
		RED_FATAL_ASSERT(data.GetInterpolationType(channel) == EIT_Hermite, "Error: Cannot use this form of interpolation");
		return EvalCurve_CubicHermite( data.GetNumValues(channel), (const Vector2*)data.GetValuesArray(channel), factor );
	}

	template<>
	RED_REFLECTION_API Vector3 InterpolateCurve_CubicHermite(const TMultiChannelCurve<Vector3>& data, const Uint32 channel, const Float factor )
	{
		RED_FATAL_ASSERT(data.GetInterpolationType(channel) == EIT_Hermite, "Error: Cannot use this form of interpolation");
		return EvalCurve_CubicHermite( data.GetNumValues(channel), (const Vector3*)data.GetValuesArray(channel), factor );
	}

	template<>
	RED_REFLECTION_API Vector4 InterpolateCurve_CubicHermite(const TMultiChannelCurve<Vector4>& data, const Uint32 channel, const Float factor )
	{
		RED_FATAL_ASSERT(data.GetInterpolationType(channel) == EIT_Hermite, "Error: Cannot use this form of interpolation");
		return EvalCurve_CubicHermite( data.GetNumValues(channel), (const Vector4*)data.GetValuesArray(channel), factor );
	}

	template<>
	RED_REFLECTION_API HDRColor InterpolateCurve_CubicHermite(const TMultiChannelCurve<HDRColor>& data, const Uint32 channel, const Float factor )
	{
		RED_FATAL_ASSERT(data.GetInterpolationType(channel) == EIT_Hermite, "Error: Cannot use this form of interpolation");
		return EvalCurve_CubicHermite( data.GetNumValues(channel), (const HDRColor*)data.GetValuesArray(channel), factor );
	}

    template<>
	RED_REFLECTION_API Color InterpolateCurve_CubicHermite(const TMultiChannelCurve<Color>& data, const Uint32 channel, const Float factor )
	{
		RED_FATAL_ASSERT(data.GetInterpolationType(channel) == EIT_Hermite, "Error: Cannot use this form of interpolation");
		return EvalCurve_CubicHermite( data.GetNumValues(channel), (const Color*)data.GetValuesArray(channel), factor );
	}

    //--

    template<>
	RED_REFLECTION_API Float Interpolate(const TSingleChannelCurve<Float>& data, const Float factor )
	{
        switch( data.m_interpolationType )
        {
            case EIT_Constant:        return InterpolateCurve_Constant<Float>(data, factor);
            case EIT_Linear:          return InterpolateCurve_Linear<Float>(data, factor);
            case EIT_BezierQuadratic: return InterpolateCurve_QuadraticBezier<Float>(data, factor);
            case EIT_BezierCubic:     return InterpolateCurve_CubicBezier<Float>(data, factor);
            case EIT_Hermite:         return InterpolateCurve_CubicHermite<Float>(data, factor);
        }

        RED_FATAL( "Invalid curve type" );
        return 0.f;
	}

	template<>
	RED_REFLECTION_API Vector2 Interpolate(const TSingleChannelCurve<Vector2>& data, const Float factor )
	{
		switch( data.m_interpolationType )
        {
            case EIT_Constant:        return InterpolateCurve_Constant<Vector2>(data, factor);
            case EIT_Linear:          return InterpolateCurve_Linear<Vector2>(data, factor);
            case EIT_BezierQuadratic: return InterpolateCurve_QuadraticBezier<Vector2>(data, factor);
            case EIT_BezierCubic:     return InterpolateCurve_CubicBezier<Vector2>(data, factor);
            case EIT_Hermite:         return InterpolateCurve_CubicHermite<Vector2>(data, factor);
        }

        RED_FATAL( "Invalid curve type" );
        return Vector2{0.f, 0.f};
	}

	template<>
	RED_REFLECTION_API Vector3 Interpolate(const TSingleChannelCurve<Vector3>& data, const Float factor )
	{
		switch( data.m_interpolationType )
        {
            case EIT_Constant:        return InterpolateCurve_Constant<Vector3>(data, factor);
            case EIT_Linear:          return InterpolateCurve_Linear<Vector3>(data, factor);
            case EIT_BezierQuadratic: return InterpolateCurve_QuadraticBezier<Vector3>(data, factor);
            case EIT_BezierCubic:     return InterpolateCurve_CubicBezier<Vector3>(data, factor);
            case EIT_Hermite:         return InterpolateCurve_CubicHermite<Vector3>(data, factor);
        }

        RED_FATAL( "Invalid curve type" );
        return Vector3(0.f, 0.f, 0.f);
	}

	template<>
	RED_REFLECTION_API Vector4 Interpolate(const TSingleChannelCurve<Vector4>& data, const Float factor )
	{
		switch( data.m_interpolationType )
        {
            case EIT_Constant:        return InterpolateCurve_Constant<Vector4>(data, factor);
            case EIT_Linear:          return InterpolateCurve_Linear<Vector4>(data, factor);
            case EIT_BezierQuadratic: return InterpolateCurve_QuadraticBezier<Vector4>(data, factor);
            case EIT_BezierCubic:     return InterpolateCurve_CubicBezier<Vector4>(data, factor);
            case EIT_Hermite:         return InterpolateCurve_CubicHermite<Vector4>(data, factor);
        }

        RED_FATAL( "Invalid curve type" );
        return Vector4(0.f, 0.f, 0.f, 0.f);
	}

	template<>
	RED_REFLECTION_API HDRColor Interpolate(const TSingleChannelCurve<HDRColor>& data, const Float factor )
	{
		switch( data.m_interpolationType )
        {
            case EIT_Constant:        return InterpolateCurve_Constant<HDRColor>(data, factor);
            case EIT_Linear:          return InterpolateCurve_Linear<HDRColor>(data, factor);
            case EIT_BezierQuadratic: return InterpolateCurve_QuadraticBezier<HDRColor>(data, factor);
            case EIT_BezierCubic:     return InterpolateCurve_CubicBezier<HDRColor>(data, factor);
            case EIT_Hermite:         return InterpolateCurve_CubicHermite<HDRColor>(data, factor);
        }

        RED_FATAL( "Invalid curve type" );
        return HDRColor(0.f, 0.f, 0.f, 0.f);
	}

    template<>
	RED_REFLECTION_API Color Interpolate(const TSingleChannelCurve<Color>& data, const Float factor )
	{
		switch( data.m_interpolationType )
        {
            case EIT_Constant:        return InterpolateCurve_Constant<Color>(data, factor);
            case EIT_Linear:          return InterpolateCurve_Linear<Color>(data, factor);
            case EIT_BezierQuadratic: return InterpolateCurve_QuadraticBezier<Color>(data, factor);
            case EIT_BezierCubic:     return InterpolateCurve_CubicBezier<Color>(data, factor);
            case EIT_Hermite:         return InterpolateCurve_CubicHermite<Color>(data, factor);
        }

        RED_FATAL( "Invalid curve type" );
        return Color::BLACK();
	}

    //--

    template<>
	RED_REFLECTION_API Float Interpolate(const TMultiChannelCurve<Float>& data, const Uint32 channel, const Float factor )
	{
        switch( data.GetInterpolationType(channel) )
        {
            case EIT_Constant:        return InterpolateCurve_Constant<Float>(data, channel, factor);
            case EIT_Linear:          return InterpolateCurve_Linear<Float>(data, channel, factor);
            case EIT_BezierQuadratic: return InterpolateCurve_QuadraticBezier<Float>(data, channel, factor);
            case EIT_BezierCubic:     return InterpolateCurve_CubicBezier<Float>(data, channel, factor);
            case EIT_Hermite:         return InterpolateCurve_CubicHermite<Float>(data, channel, factor);
        }

        RED_FATAL( "Invalid curve type" );
        return 0.f;
	}

	template<>
	RED_REFLECTION_API Vector2 Interpolate(const TMultiChannelCurve<Vector2>& data, const Uint32 channel, const Float factor )
	{
		switch( data.GetInterpolationType(channel) )
        {
            case EIT_Constant:        return InterpolateCurve_Constant<Vector2>(data, channel, factor);
            case EIT_Linear:          return InterpolateCurve_Linear<Vector2>(data, channel, factor);
            case EIT_BezierQuadratic: return InterpolateCurve_QuadraticBezier<Vector2>(data, channel, factor);
            case EIT_BezierCubic:     return InterpolateCurve_CubicBezier<Vector2>(data, channel, factor);
            case EIT_Hermite:         return InterpolateCurve_CubicHermite<Vector2>(data, channel, factor);
        }

        RED_FATAL( "Invalid curve type" );
        return Vector2(0.f, 0.f);
	}

	template<>
	RED_REFLECTION_API Vector3 Interpolate(const TMultiChannelCurve<Vector3>& data, const Uint32 channel, const Float factor )
	{
		switch( data.GetInterpolationType(channel) )
        {
            case EIT_Constant:        return InterpolateCurve_Constant<Vector3>(data, channel, factor);
            case EIT_Linear:          return InterpolateCurve_Linear<Vector3>(data, channel, factor);
            case EIT_BezierQuadratic: return InterpolateCurve_QuadraticBezier<Vector3>(data, channel, factor);
            case EIT_BezierCubic:     return InterpolateCurve_CubicBezier<Vector3>(data, channel, factor);
            case EIT_Hermite:         return InterpolateCurve_CubicHermite<Vector3>(data, channel, factor);
        }

        RED_FATAL( "Invalid curve type" );
        return Vector3(0.f, 0.f, 0.f);
	}

	template<>
	RED_REFLECTION_API Vector4 Interpolate(const TMultiChannelCurve<Vector4>& data, const Uint32 channel, const Float factor )
	{
		switch( data.GetInterpolationType(channel) )
        {
            case EIT_Constant:        return InterpolateCurve_Constant<Vector4>(data, channel, factor);
            case EIT_Linear:          return InterpolateCurve_Linear<Vector4>(data, channel, factor);
            case EIT_BezierQuadratic: return InterpolateCurve_QuadraticBezier<Vector4>(data, channel, factor);
            case EIT_BezierCubic:     return InterpolateCurve_CubicBezier<Vector4>(data, channel, factor);
            case EIT_Hermite:         return InterpolateCurve_CubicHermite<Vector4>(data, channel, factor);
        }

        RED_FATAL( "Invalid curve type" );
        return Vector4(0.f, 0.f, 0.f, 0.f);
	}

	template<>
	RED_REFLECTION_API HDRColor Interpolate(const TMultiChannelCurve<HDRColor>& data, const Uint32 channel, const Float factor )
	{
		switch( data.GetInterpolationType(channel) )
        {
            case EIT_Constant:        return InterpolateCurve_Constant<HDRColor>(data, channel, factor);
            case EIT_Linear:          return InterpolateCurve_Linear<HDRColor>(data, channel, factor);
            case EIT_BezierQuadratic: return InterpolateCurve_QuadraticBezier<HDRColor>(data, channel, factor);
            case EIT_BezierCubic:     return InterpolateCurve_CubicBezier<HDRColor>(data, channel, factor);
            case EIT_Hermite:         return InterpolateCurve_CubicHermite<HDRColor>(data, channel, factor);
        }

        RED_FATAL( "Invalid curve type" );
        return HDRColor(0.f, 0.f, 0.f, 0.f);
	}

    template<>
	RED_REFLECTION_API Color Interpolate(const TMultiChannelCurve<Color>& data, const Uint32 channel, const Float factor )
	{
		switch( data.GetInterpolationType(channel) )
        {
            case EIT_Constant:        return InterpolateCurve_Constant<Color>(data, channel, factor);
            case EIT_Linear:          return InterpolateCurve_Linear<Color>(data, channel, factor);
            case EIT_BezierQuadratic: return InterpolateCurve_QuadraticBezier<Color>(data, channel, factor);
            case EIT_BezierCubic:     return InterpolateCurve_CubicBezier<Color>(data, channel, factor);
            case EIT_Hermite:         return InterpolateCurve_CubicHermite<Color>(data, channel, factor);
        }

        RED_FATAL( "Invalid curve type" );
        return Color::BLACK();
	}

    //--

	namespace FOR_UNIT_TESTS
	{
		Int32 RED_REFLECTION_API test_get_key_frame( const Int32 numKeys, Float factor )
		{
			return curve::detail::get_key_frame(numKeys, factor);
		}

		Float RED_REFLECTION_API test_scale_factor_between_0_and_1( const Float min, const Float max, Float in )
		{
			Float result = in;
			curve::detail::scale_factor_between_0_and_1(min, max, result);
			return result;
		}

		Float RED_REFLECTION_API test_calc_factor( const Int32 numValues, Float in )
		{
			Float result = in;
			curve::detail::calc_factor(numValues, result);
			return result;
		}

		void RED_REFLECTION_API test_calc_linear_factors( const Int32 numValues, Int32& src, Int32& dst, Float factor )
		{
			Float unused = factor;
			curve::detail::calc_linear_factors(numValues, src, dst, unused);
		}

		void RED_REFLECTION_API test_calc_qbezier_factors( const Int32 numValues, Int32& p0, Int32& c0, Int32& p1, Float factor )
		{
			Float unused = factor;
			curve::detail::calc_qbezier_factors(numValues, p0, c0, p1, unused);
		}

		void RED_REFLECTION_API test_calc_cbezier_factors( const Int32 numValues, Int32& p0, Int32& c0, Int32& c1, Int32& p1, Float factor )
		{
			Float unused = factor;
			curve::detail::calc_cbezier_factors(numValues, p0, c0, c1, p1, unused);
		}
	}
}