#include "build.h"
#include "multiChannelCurve2.h"

namespace curve
{

MultiChannelCurve::MultiChannelCurve( const rtti::IType* keyType )
	: m_dataType( keyType )
	, m_numChannels(0)
	, m_linkType(ESLT_Normal)
{}

MultiChannelCurve::MultiChannelCurve( const MultiChannelCurve& other )
	: m_dataType(other.m_dataType)
	, m_numChannels(other.m_numChannels)
	, m_linkType( other.m_linkType )
{
	if( other.m_data.Size() > 0 )
	{
		void* data = RED_ALLOCATE_ALIGNED( red::PoolEngine, other.m_data.Size(), other.m_data.GetInternal().GetAlignment() );
		red::Memcpy( data, other.m_data.Data(), other.m_data.Size() );
		m_data = DataBuffer( red::MakeUniqueBuffer< red::PoolEngine >(data, other.m_data.Size(), other.m_data.GetInternal().GetAlignment() ) );
	}
}

MultiChannelCurve::MultiChannelCurve( MultiChannelCurve&& other )
{
	Swap(other);
}

MultiChannelCurve& MultiChannelCurve::operator=( const MultiChannelCurve& other )
{
	MultiChannelCurve(other).Swap(*this);
	return *this;
}

MultiChannelCurve& MultiChannelCurve::operator=( MultiChannelCurve&& other )
{
	MultiChannelCurve( std::move(other) ).Swap(*this);
	return *this;
}

MultiChannelCurve::~MultiChannelCurve() = default;
	
const rtti::IType* MultiChannelCurve::GetKeyType() const
{
	return m_dataType;
}

Uint32 MultiChannelCurve::GetNumChannels() const
{
	return m_numChannels;
}

Uint32 MultiChannelCurve::GetNumValues(red::Uint32 channel) const
{
	const ptrdiff_t offset = sizeof(header)*channel;
	return static_cast<const header*>(red::OffsetPtr(m_data.Data(), offset))->num_values;
}

Uint32 MultiChannelCurve::GetNumKeys(Uint32 channel) const
{
	const ptrdiff_t offset = sizeof(header)*channel;
	return static_cast<const header*>(red::OffsetPtr(m_data.Data(), offset))->num_keys;
}

Uint32 MultiChannelCurve::GetAlignment() const
{
	return m_dataType->GetAlignment();
}

EInterpolationType MultiChannelCurve::GetInterpolationType(Uint32 channel) const
{
	const ptrdiff_t offset = sizeof(header)*channel;
	return (EInterpolationType)static_cast<const header*>(red::OffsetPtr(m_data.Data(), offset))->interpolation_type;
}

const void* MultiChannelCurve::GetValuesArray(red::Uint32 channel) const
{
	const ptrdiff_t channelHeaderOffset = sizeof(header)*channel;
	return static_cast<const Float*>(red::OffsetPtr(m_data.Data(), static_cast<const header*>(red::OffsetPtr(m_data.Data(), channelHeaderOffset))->values_offset));
}

const void* MultiChannelCurve::GetValue( red::Uint32 channel, Uint32 key ) const
{
	const Uint32 KeyValueOffset = m_dataType->GetSize() * key;
	return red::OffsetPtr(GetValuesArray(channel), KeyValueOffset);
}

void MultiChannelCurve::SetNumChannels(Uint32 numChannels)
{
	m_numChannels = numChannels;
}

void MultiChannelCurve::Swap( MultiChannelCurve& other )
{
	std::swap(m_dataType, other.m_dataType);
	std::swap(m_data, other.m_data);

	std::swap(m_numChannels, other.m_numChannels);
	std::swap(m_linkType, other.m_linkType);
}

//--

MultiChannelCurveBuilder::MultiChannelCurveBuilder( MultiChannelCurve& data )
	: m_curveData(data)
{}

void MultiChannelCurveBuilder::Build( const red::memory::Pool& pool )
{
	RED_FATAL_ASSERT(!m_valuesPerChannel.Empty(), "Error: Please fill out data correctly");
	const auto pred = [](const Uint32 a, const channel_data& b) -> Uint32 { return a + b.values_per_channel; };
	auto total_values = std::accumulate( m_valuesPerChannel.Begin(), m_valuesPerChannel.End(), 0, pred);

	const Uint32 alignment   = m_curveData.m_dataType->GetAlignment();
	const Uint32 header_size = m_curveData.m_numChannels * sizeof(MultiChannelCurve::header);
	const Uint32 values_size = total_values * m_curveData.GetKeyType()->GetSize();

	const Uint32 data_size = header_size;
	const Uint32 padding = red::memory::RoundUp(data_size, alignment) - data_size;

	const auto new_size = header_size + padding + values_size;
	m_curveData.m_data = DataBuffer( red::CreateUniqueBuffer( pool, new_size, alignment ) );

#ifndef RED_CONFIGURATION_FINAL
	red::Memset(m_curveData.m_data.Data(), 0xEE, header_size);									//< Write into header
	red::Memset((Uint8*)m_curveData.m_data.Data() + header_size, 0xAD, padding);				//< Write into the padding
	red::Memset((Uint8*)m_curveData.m_data.Data() + header_size + padding, 0xBB, values_size);	//< Write into the values  
#endif

	RED_FATAL_ASSERT( red::memory::IsAligned(m_curveData.m_data.Data(), alignment), "Error: Data isn't aligned" );

	Uint32 keysSoFar = 0;
	for( Uint32 i = 0; i < m_curveData.m_numChannels; ++i )
	{
		auto* header = GetChannelHeader(i);
		header->num_values = GetNumValues(i);
		header->num_keys = 
		header->values_offset = header_size + padding + keysSoFar*m_curveData.GetKeyType()->GetSize();
		header->interpolation_type = m_valuesPerChannel[i].interpolation_type;
		keysSoFar += header->num_values;
		header++;
	}
}

Uint32 MultiChannelCurveBuilder::GetNumChannels() const
{
	return m_curveData.GetNumChannels();
}

Uint32 MultiChannelCurveBuilder::GetNumValues(Uint32 channel) const
{
	return m_valuesPerChannel[channel].values_per_channel;
}

void* MultiChannelCurveBuilder::GetValue(Uint32 channel, Uint32 key )
{
	RED_FATAL_ASSERT( channel < m_curveData.GetNumChannels(), "Error: Channel [%d] is out of bounds, max num channels is [%d]", key, m_curveData.GetNumChannels() );
	RED_FATAL_ASSERT( key < m_curveData.GetNumValues(channel), "Error: Key [%d] is out of bounds, max num keys for channel %d is [%d]", key, m_curveData.GetNumValues(channel), m_curveData.GetNumChannels() );
	return const_cast<void*>(m_curveData.GetValue(channel, key));
}

EInterpolationType MultiChannelCurveBuilder::GetInterpolationType(Uint32 channel) const
{
	RED_FATAL_ASSERT(channel < m_valuesPerChannel.Size(), "Error: Channel out of bounds");
	return (EInterpolationType)m_valuesPerChannel[channel].interpolation_type;
}

MultiChannelCurveBuilder& MultiChannelCurveBuilder::SetNumChannels(Uint32 channels)
{
	m_curveData.SetNumChannels(channels);
	m_valuesPerChannel.Resize(channels);
	return *this;
}

MultiChannelCurve::header* MultiChannelCurveBuilder::GetChannelHeader(Uint32 channel)
{
	return reinterpret_cast<MultiChannelCurve::header*>( red::OffsetPtr(m_curveData.m_data.Data(), sizeof(MultiChannelCurve::header)*channel) );
}

MultiChannelCurveBuilder& MultiChannelCurveBuilder::InitConstantCurve(const Uint32 channel, const Uint32 numValues)
{
	RED_FATAL_ASSERT( !m_valuesPerChannel.Empty(), "Error: Please specify number of channels for this curve builder");
	RED_FATAL_ASSERT( channel < m_valuesPerChannel.Size(), "Error: channel [%d] is out of bounds, max num channels is [%d]", channel, m_curveData.GetNumChannels() );
	m_valuesPerChannel[channel].values_per_channel = numValues;
	m_valuesPerChannel[channel].interpolation_type = (Uint8)EIT_Constant;
	return *this;
}

MultiChannelCurveBuilder& MultiChannelCurveBuilder::InitLinearCurve(const Uint32 channel, const Uint32 numValues)
{
	RED_FATAL_ASSERT( !m_valuesPerChannel.Empty(), "Error: Please specify number of channels for this curve builder");
	RED_FATAL_ASSERT( channel < m_valuesPerChannel.Size(), "Error: channel [%d] is out of bounds, max num channels is [%d]", channel, m_curveData.GetNumChannels() );
	m_valuesPerChannel[channel].values_per_channel = numValues;
	m_valuesPerChannel[channel].interpolation_type = (Uint8)EIT_Linear;
	return *this;
}

MultiChannelCurveBuilder& MultiChannelCurveBuilder::InitQuadraticBezier(const Uint32 channel, const Uint32 numValues)
{
	RED_FATAL_ASSERT( !m_valuesPerChannel.Empty(), "Error: Please specify number of channels for this curve builder");
	RED_FATAL_ASSERT( channel < m_valuesPerChannel.Size(), "Error: channel [%d] is out of bounds, max num channels is [%d]", channel, m_curveData.GetNumChannels() );
	m_valuesPerChannel[channel].values_per_channel = numValues;
	m_valuesPerChannel[channel].interpolation_type = (Uint8)EIT_BezierQuadratic;
	return *this;
}

MultiChannelCurveBuilder& MultiChannelCurveBuilder::InitCubicBezier(const Uint32 channel, const Uint32 numValues)
{
	RED_FATAL_ASSERT( !m_valuesPerChannel.Empty(), "Error: Please specify number of channels for this curve builder");
	RED_FATAL_ASSERT( channel < m_valuesPerChannel.Size(), "Error: channel [%d] is out of bounds, max num channels is [%d]", channel, m_curveData.GetNumChannels() );
	m_valuesPerChannel[channel].values_per_channel = numValues;
	m_valuesPerChannel[channel].interpolation_type = (Uint8)EIT_BezierCubic;
	return *this;
}

MultiChannelCurveBuilder& MultiChannelCurveBuilder::InitHermite(const Uint32 channel, const Uint32 numValues)
{
	RED_FATAL_ASSERT( !m_valuesPerChannel.Empty(), "Error: Please specify number of channels for this curve builder");
	RED_FATAL_ASSERT( channel < m_valuesPerChannel.Size(), "Error: channel [%d] is out of bounds, max num channels is [%d]", channel, m_curveData.GetNumChannels() );
	m_valuesPerChannel[channel].values_per_channel = numValues;
	m_valuesPerChannel[channel].interpolation_type = (Uint8)EIT_Hermite;
	return *this;
}

}