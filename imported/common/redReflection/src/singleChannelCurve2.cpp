#include "build.h"
#include "singleChannelCurve2.h"

#include "rttiClassBuilder.h"
#include "enumBuilder.h"

RTTI_BEGIN_ENUM_IN_NAMESPACE(ESegmentsLinkType, curve);
	RTTI_ENUM_OPTION(ESLT_Normal);
	RTTI_ENUM_OPTION(ESLT_Smooth);
	RTTI_ENUM_OPTION(ESLT_SmoothSymmetric);
RTTI_END_ENUM();

RTTI_BEGIN_ENUM_IN_NAMESPACE(EInterpolationType, curve);
	RTTI_ENUM_OPTION(EIT_Constant);
	RTTI_ENUM_OPTION(EIT_Linear);
	RTTI_ENUM_OPTION(EIT_BezierQuadratic);
	RTTI_ENUM_OPTION(EIT_BezierCubic);
	RTTI_ENUM_OPTION(EIT_Hermite);
RTTI_END_ENUM();

RTTI_BEGIN_TYPE_IN_NAMESPACE(SingleChannelCurve, curve)
	RTTI_PROPERTY(m_interpolationType);
	RTTI_PROPERTY(m_linkType);
	RTTI_PROPERTY(m_data);
RTTI_END_TYPE();

namespace curve
{

SingleChannelCurve::SingleChannelCurve( const rtti::IType* keyType )
	: m_dataType( keyType )
	, m_interpolationType( EIT_Linear )
	, m_linkType( ESLT_Normal )
{}

SingleChannelCurve::SingleChannelCurve( const SingleChannelCurve& other )
{
	if( other.m_data.Size() > 0 )
	{
		void* data = RED_ALLOCATE_ALIGNED( red::PoolCurves, other.m_data.Size(), other.m_data.GetInternal().GetAlignment() );
		red::Memcpy( data, other.m_data.Data(), other.m_data.Size() );
		m_data = DataBuffer::Copy( data, other.m_data.Size(), other.m_data.GetInternal().GetAlignment(), red::PoolCurves() );
	}

	m_dataType			= other.m_dataType;
	m_interpolationType = other.m_interpolationType;
	m_linkType			= other.m_linkType;
}

SingleChannelCurve::SingleChannelCurve( SingleChannelCurve&& other )
{
	Swap(other);
}

SingleChannelCurve& SingleChannelCurve::operator=(const SingleChannelCurve& other)
{
	SingleChannelCurve(other).Swap(*this);
	return *this;
}

SingleChannelCurve& SingleChannelCurve::operator=( SingleChannelCurve&& other )
{
	SingleChannelCurve( std::move(other) ).Swap(*this);
	return *this;
}

Uint32 SingleChannelCurve::GetNumValues() const
{
	if ( m_data.Empty() )
	{
		return 0;
	}

	return reinterpret_cast<const SingleChannelCurve::header*>(m_data.Data())->num_values;
}

Uint32 SingleChannelCurve::GetNumKeys() const
{
	if ( m_data.Empty() )
	{
		return 0;
	}

	return reinterpret_cast<const SingleChannelCurve::header*>(m_data.Data())->num_keys;
}

Uint32 SingleChannelCurve::GetAlignment() const
{
	return reinterpret_cast<const SingleChannelCurve::header*>(m_data.Data())->alignment;
}

const void* SingleChannelCurve::GetValuesArray() const
{
	const ptrdiff_t offsetToValueArray = reinterpret_cast<const SingleChannelCurve::header*>(m_data.Data())->values_offset;
	return red::OffsetPtr<const Float>(reinterpret_cast<const Float*>(m_data.Data()), offsetToValueArray);
}

const void* SingleChannelCurve::GetValue( const Uint32 index ) const
{
	const Uint32 KeyValueOffset = m_dataType->GetSize() * index;
	return red::OffsetPtr<const void>(GetValuesArray(), KeyValueOffset);
}

const rtti::IType* SingleChannelCurve::GetKeyType() const
{
	return m_dataType;
}

const void* SingleChannelCurve::GetMinValue() const
{
    return GetValue(0);
}

const void* SingleChannelCurve::GetMaxValue() const
{
    return GetValue(GetNumValues() - 1);
}

void SingleChannelCurve::Swap( SingleChannelCurve& other )
{
	std::swap(m_data, other.m_data);
	std::swap(m_dataType, other.m_dataType);
	std::swap(m_interpolationType, other.m_interpolationType);
	std::swap(m_linkType, other.m_linkType);
}

SingleChannelCurveBuilder::SingleChannelCurveBuilder( SingleChannelCurve& data )
	: curveData(data)
{}

void SingleChannelCurveBuilder::InitConstantCurve(const Uint32 numValues, const red::memory::Pool& pool)
{
	RED_FATAL_ASSERT(numValues > 0, "Error: Curve must contain some values");
	InitInternal(numValues, numValues, EIT_Constant, pool );
}

void SingleChannelCurveBuilder::InitLinearCurve(const Uint32 numValues, const red::memory::Pool& pool)
{
	RED_FATAL_ASSERT(numValues > 1, "Error: Curve must contain some a minimum of two points");
	const auto num_keys = numValues-1;
	InitInternal(num_keys, numValues, EIT_Linear, pool );
}

void SingleChannelCurveBuilder::InitQuadraticBezier(const Uint32 numValues, const red::memory::Pool& pool)
{
	RED_FATAL_ASSERT(numValues > 2, "Error: Curve must contain least 3 values, 2 points and 1 control point");
	RED_FATAL_ASSERT((numValues%2) != 0, "Error: Curve must contain an odd number of points");

	const auto num_keys = (numValues+1) / 3;
	InitInternal(num_keys, numValues, EIT_BezierQuadratic, pool );
}

void SingleChannelCurveBuilder::InitCubicBezier(const Uint32 numValues, const red::memory::Pool& pool)
{
	RED_FATAL_ASSERT(numValues > 3, "Error: Curve must contain at least 4 values, 2 points and 2 control points per key frame");
	RED_FATAL_ASSERT(numValues > 4 ? (numValues%2!=0) : true, "Error: If more than 4 vaules are specified, there must be an odd number");

	const auto num_keys = (numValues-1) / 3;
	InitInternal(num_keys, numValues, EIT_BezierCubic, pool );
}

void SingleChannelCurveBuilder::InitHermite(const Uint32 numValues, const red::memory::Pool& pool)
{
	RED_FATAL_ASSERT(numValues > 3, "Error: Curve must contain at least 4 values, 2 points and 2 control points per key frame");
	RED_FATAL_ASSERT(numValues%2 == 0, "Error: Curve must contain an even number of values");

	const auto num_keys = (numValues-1) / 3;
	InitInternal(num_keys, numValues, EIT_Hermite, pool );
}

void SingleChannelCurveBuilder::InitInternal(const Uint32 numKeys, const Uint32 numValues, EInterpolationType type, const red::memory::Pool& pool)
{
	curveData.m_interpolationType = type;

	const Uint32 alignment   = curveData.m_dataType->GetAlignment();
	const Uint32 header_size = sizeof(SingleChannelCurve::header);
	const Uint32 data_size   = header_size;
	const Uint32 padding     = red::memory::RoundUp(data_size, alignment) - data_size;

	const Uint32 new_size = header_size + padding + curveData.m_dataType->GetSize()*numValues;
	curveData.m_data = DataBuffer( red::CreateUniqueBuffer( pool, new_size, alignment ) );

#ifndef RED_CONFIGURATION_FINAL
	memset(curveData.m_data.Data(), 0xEE, header_size);					 //< Write into header
	memset((Uint8*)curveData.m_data.Data() + header_size, 0xAD, padding); //< Write into the padding
	memset((Uint8*)curveData.m_data.Data() + header_size + padding, 0xBB, //< Write into the values
		curveData.m_dataType->GetSize()*numValues);  
#endif

	RED_FATAL_ASSERT( red::memory::IsAligned(curveData.m_data.Data(), alignment), "Error: Data isn't aligned" );

	SingleChannelCurve::header* data = static_cast<SingleChannelCurve::header*>(curveData.m_data.Data());
	data->num_keys = numKeys;
	data->num_values = numValues;
	data->alignment = alignment;
	data->values_offset = header_size + padding;

	RED_FATAL_ASSERT( red::memory::IsAligned(curveData.GetValuesArray(), alignment), "Error: Key values aren't aligned" );
}

void* SingleChannelCurveBuilder::GetValue( const Uint32 index )
{
	RED_FATAL_ASSERT( index < curveData.GetNumValues(), "Error: Index [%d] is out of bounds, max num keys is [%d]", index, curveData.GetNumValues() );
	return const_cast<void*>(curveData.GetValue(index));
}

void SingleChannelCurveBuilder::SetLinkType( ESegmentsLinkType newType )
{
	curveData.m_linkType = newType;
}

namespace detail
{
	void linear_calculate_internal_indices( const Int32 keyframe, Int32& p )
	{
		p = keyframe + 1;
	}

	void qbezier_calculate_internal_indices( const Int32 keyframe, Int32& c, Int32& p )
	{
		c = 2*keyframe + 1;
		p = c + 1;
	}

	void cbezier_calculate_internal_indices( const Int32 keyframe, Int32& c0, Int32& c1, Int32& p )
	{
		c0 = 3*keyframe + 1;
		c1 = c0 + 1;
		p =  c1 + 1;
	}

	void chermite_calculate_internal_indices( const Int32 keyframe, Int32& c0, Int32& c1, Int32& p )
	{
		return cbezier_calculate_internal_indices(keyframe, c0, c1, p);
	}
}

namespace FOR_UNIT_TESTS
{
	void test_linear_calculate_internal_indices_from_keyframe(const Int32 keyframe, Int32& idx_p )
	{
		detail::linear_calculate_internal_indices(keyframe, idx_p);
	}

	void test_qbezier_calculate_internal_indices_from_keyframe(const Int32 keyframe, Int32& idx_c, Int32& idx_p )
	{
		detail::qbezier_calculate_internal_indices(keyframe, idx_c, idx_p);
	}

	void test_cbezier_calculate_internal_indices_from_keyframe(const Int32 keyframe, Int32& idx_c0, Int32& idx_c1, Int32& idx_p)
	{
		detail::cbezier_calculate_internal_indices(keyframe, idx_c0, idx_c1, idx_p);
	}
}

}