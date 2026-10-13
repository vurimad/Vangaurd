
#include "build.h"
#include "singleChannelCurve.h"
#include "rttiSystem.h"
#include "rttiValueHolder.h"
#include "rttiAccessPath.h"
#include "rttiUtils.h"
#include "../../redContainers/include/fundamentalStringConversion.h"
#include "../../redFileSystem/include/file.h"
#include "packageSerializer.h"
#include "packageReadWriteStream.h"
#include "../../../common/redContainers/include/string/stringUtils.h"

namespace
{
	constexpr red::StringView c_singleChannelCurveTypeKeyword = { "curveData:" };
}

const CName FormatSingleChannelCurveTypeName( const CName curveDataTypeName )
{
	RED_FATAL_ASSERT( curveDataTypeName, "Invalid name of the handle type" );

	red::String typeName = red::StrCat( c_singleChannelCurveTypeKeyword, curveDataTypeName.AsStringView() );

	return RED_NAME( typeName );
}

red::UniquePtr< rtti::IType > CreateSingleChannelCurveType( const red::StringView typenameString, rtti::ITypeSystem * typeSystem )
{
	const rtti::IType* curveType = typeSystem->FindType( RED_NAME_NOREG( typenameString ) );
	if ( curveType )
	{
		return red::CreateUniquePtr< SingleChannelCurveType > ( curveType );
	}

	return nullptr;
}

SingleChannelCurveType::SingleChannelCurveType( const rtti::IType *innerType )
	: m_data( innerType )
{
}

const CName SingleChannelCurveType::GetName() const  
{
	return m_data.GetName();
}

Uint32 SingleChannelCurveType::GetSize() const  
{
	return sizeof(SingleChannelCurve);
}

Uint32 SingleChannelCurveType::GetAlignment() const  
{
	return __alignof(SingleChannelCurve);
}

ERTTITypeType SingleChannelCurveType::GetType() const  
{
	return RT_LegacySingleChannelCurve;
}

CName SingleChannelCurveType::GetRefName() const
{
	return rtti::FormatScriptedReferenceTypeName( m_data.GetName() );
}

void SingleChannelCurveType::Construct(void *object) const  
{
	SingleChannelCurve* data = new ( object ) SingleChannelCurve(m_data);
}

void SingleChannelCurveType::Destruct(void *object) const  
{
	reinterpret_cast<SingleChannelCurve*>(object)->~SingleChannelCurve();
}

Bool SingleChannelCurveType::Compare(const void* data1, const void* data2, Uint32 DEPRECATED_flags) const  
{
	return reinterpret_cast<const SingleChannelCurve*>(data1)->Compare(*reinterpret_cast<const SingleChannelCurve*>(data2));
}

void SingleChannelCurveType::Copy(void* dest, const void* src) const  
{
	*reinterpret_cast<SingleChannelCurve*>(dest) = *reinterpret_cast<const SingleChannelCurve*>(src);
}

Bool SingleChannelCurveType::Serialize(IFile& file, void* data, ISerializable* owner ) const
{
	reinterpret_cast<SingleChannelCurve*>(data)->Serialize( file );
	return true;
}

const Bool SingleChannelCurveType::ReadValue(IRTTIContext& ctx, const void* data, const rtti::AccessPath& path, rtti::ValuePtr& outValue) const  
{
	const SingleChannelCurve* curve = reinterpret_cast<const SingleChannelCurve*>(data);

	outValue = rtti::ValueHolder::CreateStructure();
	outValue->SetStructElement( RED_NAME_CONSTEXPR("type"), rtti::ValueHolder::CreateSingle( curve->GetKeyType()->GetName().AsChar() ) );
	outValue->SetStructElement( RED_NAME_CONSTEXPR( "interpolationType" ), rtti::ValueHolder::CreateSingle( String::Printf( "%d", curve->GetInterpolationType() ).AsChar() ) );
	outValue->SetStructElement( RED_NAME_CONSTEXPR( "linkType" ), rtti::ValueHolder::CreateSingle( String::Printf( "%d", curve->GetLinkType() ).AsChar() ) );

	auto values = rtti::ValueHolder::CreateArray();
	outValue->SetStructElement( RED_NAME_CONSTEXPR("values"), values );

	const auto numKeys = curve->GetNumKeys();
	for ( Uint32 i=0; i<numKeys; ++i )
	{
		auto keyTime = curve->GetKeyTime( i );
		const void* keyValuePtr = curve->GetKeyValue( i );

		auto keyElem = rtti::ValueHolder::CreateStructure();
		values->AddArrayElement( keyElem );

		const rtti::IType* curveKeyType = curve->GetKeyType();
		rtti::ValuePtr keyValue;
		curveKeyType->ReadValue( ctx, keyValuePtr, rtti::AccessPath(), keyValue );

		String keyTimeStr = String::Printf("%f", keyTime);

		keyElem->SetStructElement( RED_NAME_CONSTEXPR("time"), rtti::ValueHolder::CreateSingle(keyTimeStr.AsChar()) );
		keyElem->SetStructElement( RED_NAME_CONSTEXPR("value"), keyValue );			
	}

	String value = outValue->ToString();

	return true;
}

const Bool SingleChannelCurveType::WriteValue(IRTTIContext& ctx, void* data, const rtti::AccessPath& path, const rtti::ValueHolder& newValue, bool clone) const  
{
	SingleChannelCurve* curve = reinterpret_cast<SingleChannelCurve*>(data);

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

	rtti::ValuePtr values = newValue.GetStructElement(RED_NAME_CONSTEXPR_NOREG("values"));
	if ( !values->IsArray() )
	{
		return false;
	}

	const Uint32 numPoints = values->GetNumElements();

	SingleChannelCurveBuilder builder( *curve );
	builder.Resize( numPoints );
	builder.SetInterpolationType( (EInterpolationType)interpolationTypeVal );
	builder.SetLinkType( (ESegmentsLinkType)linkTypeVal );

	for ( Uint32 i=0; i<numPoints; ++i )
	{
		auto value = values->GetArrayElement(i);
		if ( !value->IsStructure() )
		{
			continue;
		}

		auto srcKeyTime = value->GetStructElement(RED_NAME_CONSTEXPR("time"));
		const float time = FromStringDirect< Float >( srcKeyTime->GetSingleValue() ); // no error handling
		builder.SetKeyTime( i, time );

		auto srcKeyValue = value->GetStructElement(RED_NAME_CONSTEXPR("value"));
		void* destKeyData = builder.GetKeyValue(i);
		curve->GetKeyType()->WriteValue( ctx, destKeyData, rtti::AccessPath(), *srcKeyValue, clone );			
	}

	return true;
}

const red::memory::Pool & SingleChannelCurveType::GetInnerTypeMemoryPool() const
{
	return m_data.GetKeyType()->GetInnerTypeMemoryPool();
}

Uint32 SingleChannelCurveType::GetKeySize() const
{
	return m_data.GetKeyType()->GetSize();
}

void RegisterSingleChannelCurveTypeCreator()
{
	GetRttiSystem().RegisterDynamicTypeCreator( c_singleChannelCurveTypeKeyword, &CreateSingleChannelCurveType );
}

PackageSingleChannelCurveSerializer::PackageSingleChannelCurveSerializer() = default;

PackageSingleChannelCurveSerializer::~PackageSingleChannelCurveSerializer() = default;

void PackageSingleChannelCurveSerializer::OnWriteValue( const WriteContext & context, const red::PackageSerializeTypeParameter & param ) const
{
	const SingleChannelCurve * curve = static_cast< const SingleChannelCurve* >( param.buffer );

	const rtti::IType * keyType = curve->GetKeyType();
	
	Uint32 numKeys = curve->GetNumKeys();
	context.serializer << numKeys;

	for ( Uint32 i = 0; i < numKeys; ++i )
	{  
		auto time = curve->GetKeyTime(i);
		context.serializer << time;

		auto* value = curve->GetKeyValue(i);
		context.serializer.Serialize( (void*)value, keyType->GetSize() );
	}

	Uint8 interpolationType = (Uint8)curve->GetInterpolationType();
	context.serializer << interpolationType;

	Uint8 linkType = (Uint8)curve->GetLinkType();
	context.serializer << linkType;
}

void PackageSingleChannelCurveSerializer::OnReadValue( const ReadContext & context, const red::PackageSerializeTypeParameter & param ) const
{
	SingleChannelCurve * curve = static_cast< SingleChannelCurve* >( param.buffer );
	const rtti::IType * keyType = curve->GetKeyType();

	Float  current_time	= 0.f;
	Uint32 numKeys = 0;

	context.serializer >> numKeys;

	SingleChannelCurveBuilder builder(*curve);
	builder.Resize(numKeys);

	for(Uint32 i = 0; i < numKeys; ++i)
	{
		context.serializer >> current_time;
		builder.SetKeyTime(i,current_time);

		void* keyValue = builder.GetKeyValue(i);
		context.serializer.Serialize(keyValue, keyType->GetSize());
	}

	Uint8 interpolationType, linkType;
		
	context.serializer >> interpolationType;
	curve->SetInterpolationType( (EInterpolationType)interpolationType );

	context.serializer >> linkType;
	curve->SetLinkType( (ESegmentsLinkType)linkType );
	
}

void PackageSingleChannelCurveSerializer::OnRemapValue( const RemapContext& context, const red::PackageSerializeTypeParameter & param ) const
{
	const SingleChannelCurveType * curveType = static_cast< const SingleChannelCurveType * >( param.type );
	
	Uint32 numKeys = 0;
	Float  current_time	= 0.f;
	context.serializer >> numKeys;

	for(Uint32 i = 0; i < numKeys; ++i)
	{
		context.serializer >> current_time;
		context.serializer.Serialize( nullptr, curveType->GetKeySize() );
	}

	Uint8 interpolationType, linkType;
	context.serializer >> interpolationType;
	context.serializer >> linkType;
}


SingleChannelCurveBuilder::SingleChannelCurveBuilder( SingleChannelCurve& data )
	: curveData(data)
{}

void SingleChannelCurveBuilder::Clear()
{
	curveData.m_data.Reset();
	curveData.m_dataType = nullptr;
}


void SingleChannelCurveBuilder::Resize(const Uint32 numKeys, const red::memory::Pool& pool)
{
	const Uint32 alignment   = curveData.m_dataType->GetAlignment();
	const Uint32 header_size = sizeof(SingleChannelCurve::header);
	const Uint32 times_size  = sizeof(Float) * numKeys;
	const Uint32 data_size   = header_size+times_size;
	const Uint32 padding     = red::memory::RoundUp(data_size, alignment) - data_size;

	const Uint32 new_size = header_size + times_size + padding + curveData.m_dataType->GetSize()*numKeys;
	curveData.m_data = red::CreateUniqueBuffer( pool, new_size, alignment );

#ifdef _DEBUG
	memset(curveData.m_data.Get(), 0xEE, header_size);								  //< Write into header
	memset((Uint8*)curveData.m_data.Get() + header_size, 0xFF, times_size);			  //< Write into times
	memset((Uint8*)curveData.m_data.Get() + header_size + times_size, 0xAD, padding); //< Write into the padding
	memset((Uint8*)curveData.m_data.Get() + header_size + times_size + padding, 0xBB, //< Write into the values
		curveData.m_dataType->GetSize()*numKeys);  
#endif

	RED_FATAL_ASSERT( red::memory::IsAligned(curveData.m_data.Get(), alignment), "Error: Data isn't aligned" );

	SingleChannelCurve::header* data = static_cast<SingleChannelCurve::header*>(curveData.m_data.Get());
	data->size		   = numKeys;
	data->alignment    = alignment;
	data->timesStride  = header_size;
	data->valuesStride = header_size + times_size + padding;

	RED_FATAL_ASSERT( red::memory::IsAligned(curveData.GetKeyValuesArray(), alignment), "Error: Key values aren't aligned" );
}

void* SingleChannelCurveBuilder::GetKeyValue( const Uint32 keyIndex )
{
	RED_FATAL_ASSERT( keyIndex < curveData.GetNumKeys(), "Error: Index [%d] is out of bounds, max num keys is [%d]", keyIndex, curveData.GetNumKeys() );
	return const_cast<void*>(curveData.GetKeyValue(keyIndex));
}

Float SingleChannelCurveBuilder::GetKeyTime( const Uint32 keyIndex ) const
{
	RED_FATAL_ASSERT( keyIndex < curveData.GetNumKeys(), "Error: Index [%d] is out of bounds, max num keys is [%d]", keyIndex, curveData.GetNumKeys() );
	return curveData.GetKeyTime(keyIndex);
}

void SingleChannelCurveBuilder::SetKeyTime( const Uint32 keyIndex, const Float time )
{
	RED_FATAL_ASSERT( keyIndex < curveData.GetNumKeys(), "Error: Index [%d] is out of bounds, max num keys is [%d]", keyIndex, curveData.GetNumKeys() );
	const_cast<Float*>(curveData.GetKeyTimesArray())[keyIndex] = time;
}

void SingleChannelCurveBuilder::SetInterpolationType( EInterpolationType newType )
{
	curveData.SetInterpolationType( newType );
}

void SingleChannelCurveBuilder::SetLinkType( ESegmentsLinkType newType )
{
	curveData.SetLinkType( newType );
}

SingleChannelCurve::SingleChannelCurve( const rtti::IType* keyType )
	: m_dataType( keyType )
	, m_name( keyType ? FormatSingleChannelCurveTypeName( keyType->GetName() ) : CName::NONE() )
	, m_interpolationType( EIT_Linear )
	, m_linkType( ESLT_Normal )
{}

SingleChannelCurve::SingleChannelCurve( const SingleChannelCurve& other )
{
	if( other.m_data.GetSize() > 0 )
	{
		void* data = RED_ALLOCATE_ALIGNED( red::PoolEngine, other.m_data.GetSize(), other.m_data.GetAlignment() );
		red::Memcpy( data, other.m_data.Get(), other.m_data.GetSize() );
		m_data = red::MakeUniqueBuffer< red::PoolEngine >(data, other.m_data.GetSize(), other.m_data.GetAlignment() );
	}

	m_dataType			= other.m_dataType;
	m_name				= other.m_name;
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

const rtti::IType* SingleChannelCurve::GetKeyType() const
{
	return m_dataType;
}

red::Uint32 SingleChannelCurve::GetNumKeys() const
{
	if ( m_data.Get() == nullptr )
	{
		return 0;
	}

	return reinterpret_cast<SingleChannelCurve::header*>(m_data.Get())->size;
}

red::Uint32 SingleChannelCurve::GetAlignment() const
{
	return reinterpret_cast<SingleChannelCurve::header*>(m_data.Get())->alignment;
}

const Float* SingleChannelCurve::GetKeyTimesArray() const
{
	const ptrdiff_t offsetToTimeArray = reinterpret_cast<SingleChannelCurve::header*>(m_data.Get())->timesStride;
	return red::OffsetPtr<const Float>(reinterpret_cast<const Float*>(m_data.Get()), offsetToTimeArray);
}

const void* SingleChannelCurve::GetKeyValuesArray() const
{
	const ptrdiff_t offsetToValueArray = reinterpret_cast<SingleChannelCurve::header*>(m_data.Get())->valuesStride;
	return red::OffsetPtr<const Float>(reinterpret_cast<const Float*>(m_data.Get()), offsetToValueArray);
}

Float SingleChannelCurve::GetKeyTime( const Uint32 keyIndex ) const
{
	return GetKeyTimesArray()[ keyIndex ];
}

const void* SingleChannelCurve::GetKeyValue( const Uint32 keyIndex ) const
{
	const Uint32 KeyValueOffset = m_dataType->GetSize() * keyIndex;
	return red::OffsetPtr<const void>(GetKeyValuesArray(), KeyValueOffset);
}

Float SingleChannelCurve::GetMinTime() const
{
	Uint32 numKeys = GetNumKeys();
	if (numKeys == 0)
	{
		return 0.0f;
	}

	return GetKeyTime(0);
}

Float SingleChannelCurve::GetMaxTime() const
{
	Uint32 numKeys = GetNumKeys();
	if ( numKeys == 0 )
	{
		return 0.0f;
	}

	return GetKeyTime( numKeys-1 );
}

void SingleChannelCurve::Serialize( IFile& file )
{
	if ( file.IsWriter() )
	{
		const SingleChannelCurve::header* header = reinterpret_cast<const SingleChannelCurve::header*>(m_data.Get());

		Uint32 numKeys = GetNumKeys();
		file << numKeys;

		for ( Uint32 i = 0; i < numKeys; ++i )
		{  
			auto time = GetKeyTime(i);
			file << time;

			auto* value = GetKeyValue(i);
			file.Serialize( (void*)value, m_dataType->GetSize() );
		}

		Uint8 interpolationType = (Uint8)m_interpolationType;
		file << interpolationType;

		Uint8 linkType = (Uint8)m_linkType;
		file << linkType;
	}
	else if ( file.IsReader() )
	{
		Float  current_time	= 0.f;
		Uint32 numKeys = 0;

		file << numKeys;

		SingleChannelCurveBuilder builder(*this);
		builder.Resize(numKeys);

		for(Uint32 i = 0; i < numKeys; ++i)
		{
			file << current_time;
			builder.SetKeyTime(i,current_time);

			void* keyValue = builder.GetKeyValue(i);
			file.Serialize(keyValue, m_dataType->GetSize());
		}

		Uint8 interpolationType, linkType;
		
		file << interpolationType;
		m_interpolationType = (EInterpolationType)interpolationType;

		file << linkType;
		m_linkType = (ESegmentsLinkType)linkType;
	}
}

Bool SingleChannelCurve::Compare( const SingleChannelCurve& other ) const
{
	if( m_dataType != other.m_dataType )
	{
		return false;
	}

	if( m_data.GetSize() != other.m_data.GetSize() )
	{
		return false;
	}

	if( m_data.GetAlignment() != other.m_data.GetAlignment() )
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

Bool SingleChannelCurve::operator==( const SingleChannelCurve& other ) const
{
	return Compare(other);
}

Bool SingleChannelCurve::operator!=( const SingleChannelCurve& other ) const
{
	return !Compare(other);
}

void SingleChannelCurve::Swap( SingleChannelCurve& other )
{
	std::swap(m_name, other.m_name);
	std::swap(m_data, other.m_data);
	std::swap(m_dataType, other.m_dataType);
	std::swap(m_interpolationType, other.m_interpolationType);
	std::swap(m_linkType, other.m_linkType);
}

CName SingleChannelCurve::GetName() const
{
	return m_name;
}

Bool SingleChannelCurve::IsValid() const
{
	return m_data.Get() && m_data.GetSize() != 0u;
}
