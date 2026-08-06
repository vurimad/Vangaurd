#include "build.h"
#include "multiChannelCurve.h"
#include "singleChannelCurve.h"
#include "rttiSystem.h"
#include "rttiValueHolder.h"
#include "rttiAccessPath.h"
#include "rttiUtils.h"

#include "dataBuffer.h"
#include "../../redFileSystem/include/file.h"
#include "../../../common/redContainers/include/string/stringUtils.h"

#include <numeric>

namespace
{
	constexpr red::StringView c_multiChannelCurveTypeKeyword = { "multiChannelCurve:" };
}

const CName FormatMultiChannelCurveTypeName( const CName curveDataTypeName )
{
	RED_FATAL_ASSERT( curveDataTypeName, "Invalid name of the handle type" );

	red::String typeName = red::StrCat( c_multiChannelCurveTypeKeyword, curveDataTypeName.AsStringView() );

	return RED_NAME( typeName );
}

red::UniquePtr< rtti::IType > CreateMultiChannelCurveType( const red::StringView typeName, rtti::ITypeSystem * typeSystem )
{
	const rtti::IType* curveType = typeSystem->FindType( RED_NAME_NOREG( typeName ) );
	if ( curveType )
	{
		return red::CreateUniquePtr< MultiChannelCurveType > ( curveType );
	}

	return nullptr;
}

void RegisterMultiChannelCurveTypeCreator()
{
	GetRttiSystem().RegisterDynamicTypeCreator( c_multiChannelCurveTypeKeyword, &CreateMultiChannelCurveType );
}

MultiChannelCurve::MultiChannelCurve( const rtti::IType* keyType )
	: m_name( keyType ? FormatMultiChannelCurveTypeName( keyType->GetName() ) : CName::NONE() )
	, m_dataType( keyType )
	, m_numChannels(0)
	, m_linkType(ESLT_Normal)
	, m_interpolationType(EIT_Linear)
{}

MultiChannelCurve::MultiChannelCurve( const MultiChannelCurve& other )
	: m_name( other.m_name )
	, m_dataType(other.m_dataType)
	, m_numChannels(other.m_numChannels)
	, m_interpolationType( other.m_interpolationType )
	, m_linkType( other.m_linkType )
{
	if( other.m_data.GetSize() > 0 )
	{
		void* data = RED_ALLOCATE_ALIGNED( red::PoolEngine, other.m_data.GetSize(), other.m_data.GetAlignment() );
		red::Memcpy( data, other.m_data.Get(), other.m_data.GetSize() );
		m_data = red::MakeUniqueBuffer< red::PoolEngine >(data, other.m_data.GetSize(), other.m_data.GetAlignment() );
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

MultiChannelCurve::~MultiChannelCurve() =  default;
	
const rtti::IType* MultiChannelCurve::GetKeyType() const
{
	return m_dataType;
}

red::Uint32 MultiChannelCurve::GetNumChannels() const
{
	return m_numChannels;
}

red::Uint32 MultiChannelCurve::GetNumKeys(red::Uint32 channel) const
{
	const ptrdiff_t offset = sizeof(header)*channel;
	return static_cast<header*>(red::OffsetPtr(m_data.Get(), offset))->size;
}

red::Uint32 MultiChannelCurve::GetAlignment() const
{
	return m_dataType->GetAlignment();
}

const Float* MultiChannelCurve::GetKeyTimesArray(red::Uint32 channel) const
{
	const ptrdiff_t channelHeaderOffset = sizeof(header)*channel;
	return static_cast<const Float*>(red::OffsetPtr(m_data.Get(), static_cast<const header*>(red::OffsetPtr(m_data.Get(), channelHeaderOffset))->timesOffset));
}

const void* MultiChannelCurve::GetKeyValuesArray(red::Uint32 channel) const
{
	const ptrdiff_t channelHeaderOffset = sizeof(header)*channel;
	return static_cast<const Float*>(red::OffsetPtr(m_data.Get(), static_cast<const header*>(red::OffsetPtr(m_data.Get(), channelHeaderOffset))->valuesOffset));
}

const void* MultiChannelCurve::GetKeyValue( red::Uint32 channel, Uint32 key ) const
{
	const Uint32 KeyValueOffset = m_dataType->GetSize() * key;
	return red::OffsetPtr(GetKeyValuesArray(channel), KeyValueOffset);
}

Float MultiChannelCurve::GetKeyTime( red::Uint32 channel, const Uint32 key ) const
{
	return GetKeyTimesArray(channel)[key];
}

Float MultiChannelCurve::GetMinTime(red::Uint32 channel) const
{
	Uint32 numKeys = GetNumKeys(channel);
	if (numKeys == 0)
	{
		return 0.0f;
	}

	return GetKeyTime(channel, 0);
}

Float MultiChannelCurve::GetMaxTime(red::Uint32 channel) const
{
	Uint32 numKeys = GetNumKeys(channel);
	if ( numKeys == 0 )
	{
		return 0.0f;
	}

	return GetKeyTime( channel, numKeys-1 );
}

void MultiChannelCurve::SetNumChannels(Uint32 numChannels)
{
	m_numChannels = numChannels;
}

void MultiChannelCurve::Serialize( IFile& file )
{
	if ( file.IsWriter() )
	{
		file << m_numChannels;
		Uint8 interpolationType = (Uint8)m_interpolationType;
		file << interpolationType;

		Uint8 linkType = (Uint8)m_linkType;
		file << linkType;

		Uint32 alignment = m_data.GetAlignment();
		file << alignment;

		Uint32 size = m_data.GetSize();
		file << size;

		file.Serialize(m_data.Get(), size);
	}
	else if ( file.IsReader() )
	{
		file << m_numChannels;

		Uint8 interpolationType, linkType;
		
		file << interpolationType;
		m_interpolationType = (EInterpolationType)interpolationType;

		file << linkType;
		m_linkType = (ESegmentsLinkType)linkType;

		Uint32 alignment, size;
		file << alignment;
		file << size;

		m_data = red::CreateUniqueBuffer<red::PoolEngine>(size, alignment);
		file.Serialize(m_data.Get(), size);
	}
}

CName MultiChannelCurve::GetName() const
{
	return m_name;
}

Bool MultiChannelCurve::Compare( const MultiChannelCurve& other ) const
{
	if( m_dataType != other.m_dataType )
	{
		return false;
	}

	if( m_numChannels != other.m_numChannels )
	{
		return false;
	}

	if( m_data.GetSize() != other.m_data.GetSize() )
	{
		return false;
	}

	if( m_interpolationType != other.m_interpolationType )
	{
		return false;
	}

	if( m_linkType != other.m_linkType )
	{
		return false;
	}

	return red::Memcmp( m_data.Get(), other.m_data.Get(), m_data.GetSize() ) == 0;
}

Bool MultiChannelCurve::operator==(const MultiChannelCurve& other ) const
{
	return Compare(other);
}

Bool MultiChannelCurve::operator!=(const MultiChannelCurve& other ) const
{
	return !operator==(other);
}

void MultiChannelCurve::Swap( MultiChannelCurve& other )
{
	std::swap(m_name, other.m_name);
	std::swap(m_dataType, other.m_dataType);
	std::swap(m_data, other.m_data);

	std::swap(m_numChannels, other.m_numChannels);
	std::swap(m_interpolationType, other.m_interpolationType);
	std::swap(m_linkType, other.m_linkType);
}

EInterpolationType MultiChannelCurve::GetInterpolationType() const 
{ 
	return m_interpolationType; 
}

void MultiChannelCurve::SetInterpolationType( EInterpolationType newType ) 
{ 
	m_interpolationType = newType; 
}

ESegmentsLinkType MultiChannelCurve::GetLinkType() const 
{ 
	return m_linkType; 
}

void MultiChannelCurve::SetLinkType( ESegmentsLinkType newType ) 
{ 
	m_linkType = newType; 
}

//--

MultiChannelCurveType::MultiChannelCurveType( const rtti::IType *innerType )
	: m_data(innerType)
{}

const CName MultiChannelCurveType::GetName() const
{
	return m_data.GetName();
}

Uint32 MultiChannelCurveType::GetSize() const
{
	return sizeof(MultiChannelCurve);
}

Uint32 MultiChannelCurveType::GetAlignment() const
{
	return __alignof(MultiChannelCurve);
}

ERTTITypeType MultiChannelCurveType::GetType() const
{
	return RT_Simple;
}

CName MultiChannelCurveType::GetRefName() const
{
	return rtti::FormatScriptedReferenceTypeName( m_data.GetName() );
}

void MultiChannelCurveType::Construct( void *object ) const
{
	MultiChannelCurve* data = ::new(object) MultiChannelCurve(m_data);
}

void MultiChannelCurveType::Destruct( void *object ) const
{
	reinterpret_cast<MultiChannelCurve*>(object)->~MultiChannelCurve();
}

Bool MultiChannelCurveType::Compare( const void* data1, const void* data2, Uint32 ) const
{
	return reinterpret_cast<const MultiChannelCurve*>(data1)->Compare(*reinterpret_cast<const MultiChannelCurve*>(data2));
}

void MultiChannelCurveType::Copy( void* dest, const void* src ) const
{
	*reinterpret_cast<MultiChannelCurve*>(dest) = *reinterpret_cast<const MultiChannelCurve*>(src);
}

Bool MultiChannelCurveType::Serialize( IFile& file, void* data, ISerializable* owner) const
{
	reinterpret_cast<MultiChannelCurve*>(data)->Serialize( file );
	return true;
}

const Bool MultiChannelCurveType::ReadValue( IRTTIContext& ctx, const void* data, const rtti::AccessPath& path, rtti::ValuePtr& outValue ) const
{
	const MultiChannelCurve* curve = reinterpret_cast<const MultiChannelCurve*>(data);

	outValue = rtti::ValueHolder::CreateStructure();
	outValue->SetStructElement( RED_NAME_CONSTEXPR("type"), rtti::ValueHolder::CreateSingle( curve->GetKeyType()->GetName().AsChar() ) );
	outValue->SetStructElement( RED_NAME_CONSTEXPR( "interpolationType" ), rtti::ValueHolder::CreateSingle( String::Printf( "%d", curve->GetInterpolationType() ).AsChar() ) );
	outValue->SetStructElement( RED_NAME_CONSTEXPR( "linkType" ), rtti::ValueHolder::CreateSingle( String::Printf( "%d", curve->GetLinkType() ).AsChar() ) );

	auto channels = rtti::ValueHolder::CreateArray();
	outValue->SetStructElement(RED_NAME_CONSTEXPR( "channels" ), channels);

	for( Uint32 channel = 0; channel < curve->GetNumChannels(); ++channel )
	{
		auto channel_structure = rtti::ValueHolder::CreateStructure();
		channels->AddArrayElement(channel_structure);

		auto values_array = rtti::ValueHolder::CreateArray();
		channel_structure->SetStructElement(RED_NAME_CONSTEXPR("values"), values_array);

		for( Uint32 key = 0; key < curve->GetNumKeys(channel); ++key)
		{
			auto keyTime = curve->GetKeyTime(channel, key);
			const void* keyValuePtr = curve->GetKeyValue(channel, key);

			auto keyElem = rtti::ValueHolder::CreateStructure();
			values_array->AddArrayElement( keyElem );

			const auto* curveKeyType = curve->GetKeyType();
			rtti::ValuePtr keyValue;
			curveKeyType->ReadValue( ctx, keyValuePtr, rtti::AccessPath(), keyValue );

			String keyTimeStr = String::Printf("%f", keyTime);

			keyElem->SetStructElement( RED_NAME_CONSTEXPR("time"), rtti::ValueHolder::CreateSingle(keyTimeStr.AsChar()) );
			keyElem->SetStructElement( RED_NAME_CONSTEXPR("value"), keyValue );			
		}
	}

	return true;
}

const Bool MultiChannelCurveType::WriteValue( IRTTIContext& ctx, void* data, const rtti::AccessPath& path, const rtti::ValueHolder& newValue, bool clone ) const
{
	MultiChannelCurve* curve = reinterpret_cast<MultiChannelCurve*>(data);

	const auto typeName = newValue.GetStructElement(RED_NAME_CONSTEXPR_NOREG("type"))->GetSingleValue();
	if ( !typeName.EqualsNC( curve->GetKeyType()->GetName().AsChar() ) )
	{
		// key types are not compatible
		ctx.ReportError( this, "Curve type should be '%hs' not '%hs'", curve->GetKeyType()->GetName().AsChar(), typeName.AsChar());
		return false;
	}

	const String& interpolationType = newValue.GetStructElement( RED_NAME_CONSTEXPR_NOREG( "interpolationType" ) )->GetSingleValue();
	Uint8 interpolationTypeVal = -1;
	if ( !FromStringT<Uint8>( interpolationType, interpolationTypeVal ) )
	{
		// interpolation type is corrupted
		ctx.ReportError( this, "Interpolation type not found" );
		return false;
	}

	const String& linkType = newValue.GetStructElement( RED_NAME_CONSTEXPR_NOREG( "linkType" ) )->GetSingleValue();
	Uint8 linkTypeVal = -1;
	if ( !FromStringT<Uint8>( linkType, linkTypeVal ) )
	{
		// interpolation type is corrupted
		ctx.ReportError( this, "Link type not found" );
		return false;
	}

	rtti::ValuePtr channels = newValue.GetStructElement(RED_NAME_CONSTEXPR_NOREG("channels"));
	if ( !channels->IsArray() )
	{
		return false;
	}

	MultiChannelCurveBuilder builder(*curve);
	builder.SetInterpolationType((EInterpolationType)interpolationTypeVal);
	builder.SetLinkType((ESegmentsLinkType)linkTypeVal);
	builder.SetNumChannels(channels->GetNumElements());

	for( Uint32 channel = 0; channel < channels->GetNumElements(); ++channel )
	{
		auto channelPtr = channels->GetArrayElement(channel);
		if( !channelPtr->IsStructure() )
		{
			ctx.ReportError(GetTypeObject<MultiChannelCurveType>(), "Error: Element isn't a structure");
			return false;
		}

		auto values_array = channelPtr->GetStructElement(RED_NAME_CONSTEXPR_NOREG("values"));
		if( !values_array->IsArray() )
		{
			ctx.ReportError(GetTypeObject<MultiChannelCurveType>(), "Error: Element isn't an array");
			return false;
		}

		if ( values_array->GetNumElements() > std::numeric_limits<Uint8>::max() )
		{
			ctx.ReportError( GetTypeObject<MultiChannelCurveType>(), "Error: Too many keys (255 maximum)" );
			return false;
		}

		builder.SetNumKeys(channel, values_array->GetNumElements());
	}

	builder.Resize();

	for(Uint32 channel = 0; channel < channels->GetNumElements(); ++channel)
	{
		auto channelPtr = channels->GetArrayElement(channel);
		auto values = channelPtr->GetStructElement(RED_NAME_CONSTEXPR_NOREG("values"));

		for( Uint32 key = 0; key < values->GetNumElements(); ++key )
		{
			const auto value = values->GetArrayElement(key);
			if( !value->IsStructure() )
			{
				ctx.ReportError(GetTypeObject<MultiChannelCurveType>(), "Error: Element isn't a structure");
				return false;
			}

			auto srcKeyTime = value->GetStructElement(RED_NAME_CONSTEXPR_NOREG("time"));
			const float time = FromStringDirect< Float >(srcKeyTime->GetSingleValue());
			builder.SetKeyTime(channel,key,time);

			auto srcKeyValue = value->GetStructElement(RED_NAME_CONSTEXPR_NOREG("value"));
			void* destKeyData = builder.GetKeyValue(channel,key);
			curve->GetKeyType()->WriteValue(ctx,destKeyData,rtti::AccessPath(),*srcKeyValue,clone);
		}
	}

	return true;
}

//--

MultiChannelCurveBuilder::MultiChannelCurveBuilder( MultiChannelCurve& data )
	: m_curveData(data)
{}

void MultiChannelCurveBuilder::Clear()
{
	m_curveData.m_numChannels = 0;
	m_curveData.m_data.Reset();
	m_curveData.m_dataType = nullptr;
}

void MultiChannelCurveBuilder::Resize( const red::memory::Pool& pool )
{
	RED_FATAL_ASSERT(!m_keysPerChannel.Empty(), "Error: Please fill out data correctly");

	Uint32 totalKeys = std::accumulate( m_keysPerChannel.Begin(), m_keysPerChannel.End(), 0);

	const Uint32 alignment   = m_curveData.m_dataType->GetAlignment();
	const Uint32 header_size = m_curveData.m_numChannels * sizeof(MultiChannelCurve::header);
	const Uint32 times_size  = totalKeys*sizeof(Float);
	const Uint32 values_size = totalKeys*m_curveData.m_dataType->GetSize();

	const Uint32 data_size   = times_size+header_size;
	const Uint32 padding     = red::memory::RoundUp(data_size, alignment) - data_size;

	const auto new_size = header_size + times_size + padding + values_size;
	m_curveData.m_data = red::CreateUniqueBuffer( pool, new_size, alignment );

#ifndef RED_CONFIGURATION_FINAL
	red::Memset(m_curveData.m_data.Get(), 0xEE, header_size);												//< Write into header
	red::Memset((Uint8*)m_curveData.m_data.Get() + header_size, 0xFF, times_size);							//< Write into times
	red::Memset((Uint8*)m_curveData.m_data.Get() + header_size + times_size, 0xAD, padding);				//< Write into the padding
	red::Memset((Uint8*)m_curveData.m_data.Get() + header_size + times_size + padding, 0xBB, values_size);	//< Write into the values  
#endif

	RED_FATAL_ASSERT( red::memory::IsAligned(m_curveData.m_data.Get(), alignment), "Error: Data isn't aligned" );

	Uint32 keysSoFar = 0;
	for( Uint32 i = 0; i < m_curveData.m_numChannels; ++i )
	{
		auto* header = GetChannelHeader(i);
		header->size = GetNumKeys(i);
		header->timesOffset	= header_size + keysSoFar*sizeof(Float);
		header->valuesOffset = header_size + times_size + padding + keysSoFar*m_curveData.GetKeyType()->GetSize();
		keysSoFar += header->size;
		header++;
	}
}

MultiChannelCurveBuilder& MultiChannelCurveBuilder::SetInterpolationType( EInterpolationType newType )
{
	m_curveData.SetInterpolationType(newType);
	return *this;
}

MultiChannelCurveBuilder& MultiChannelCurveBuilder::SetLinkType( ESegmentsLinkType newType )
{
	m_curveData.SetLinkType(newType);
	return *this;
}

Uint32 MultiChannelCurveBuilder::GetNumChannels() const
{
	return m_curveData.GetNumChannels();
}

Uint32 MultiChannelCurveBuilder::GetNumKeys(Uint32 channel) const
{
	return m_keysPerChannel[channel];
}

Float MultiChannelCurveBuilder::GetKeyTime(Uint32 channel, Uint32 key )
{
	RED_FATAL_ASSERT( channel < m_curveData.GetNumChannels(), "Error: Channel [%d] is out of bounds, max num channels is [%d]", key, m_curveData.GetNumChannels() );
	RED_FATAL_ASSERT( key < m_curveData.GetNumKeys(channel), "Error: Key [%d] is out of bounds, max num keys for channel %d is [%d]", key, m_curveData.GetNumKeys(channel), m_curveData.GetNumChannels() );
	return m_curveData.GetKeyTime(channel, key);
}

void MultiChannelCurveBuilder::SetKeyTime( const Uint32 channel, const Uint32 key, const Float time )
{
	RED_FATAL_ASSERT( channel < m_curveData.GetNumChannels(), "Error: Channel [%d] is out of bounds, max num channels is [%d]", key, m_curveData.GetNumChannels() );
	RED_FATAL_ASSERT( key < m_curveData.GetNumKeys(channel), "Error: Key [%d] is out of bounds, max num keys for channel %d is [%d]", key, m_curveData.GetNumKeys(channel), m_curveData.GetNumChannels() );
	const_cast<Float*>(m_curveData.GetKeyTimesArray(channel))[key] = time;
}

void* MultiChannelCurveBuilder::GetKeyValue(Uint32 channel, Uint32 key )
{
	RED_FATAL_ASSERT( channel < m_curveData.GetNumChannels(), "Error: Channel [%d] is out of bounds, max num channels is [%d]", key, m_curveData.GetNumChannels() );
	RED_FATAL_ASSERT( key < m_curveData.GetNumKeys(channel), "Error: Key [%d] is out of bounds, max num keys for channel %d is [%d]", key, m_curveData.GetNumKeys(channel), m_curveData.GetNumChannels() );
	return const_cast<void*>(m_curveData.GetKeyValue(channel, key));
}

MultiChannelCurveBuilder& MultiChannelCurveBuilder::SetNumChannels(Uint32 channels)
{
	m_curveData.SetNumChannels(channels);
	m_keysPerChannel.Resize(channels);
	return *this;
}

MultiChannelCurveBuilder&  MultiChannelCurveBuilder::SetNumKeys(Uint32 channel, Uint32 numKeys)
{
	RED_FATAL_ASSERT( channel < m_keysPerChannel.Size(), "Error: channel [%d] is out of bounds, max num channels is [%d]", channel, m_curveData.GetNumChannels() );
	RED_FATAL_ASSERT( numKeys < std::numeric_limits<Uint8>::max(), "Error: max number of keys per channel is %d", std::numeric_limits<Uint8>::max() );
	m_keysPerChannel[channel] = numKeys;
	return *this;
}

MultiChannelCurve::header* MultiChannelCurveBuilder::GetChannelHeader(Uint32 channel)
{
	return reinterpret_cast<MultiChannelCurve::header*>( red::OffsetPtr(m_curveData.m_data.Get(), sizeof(MultiChannelCurve::header)*channel) );
}