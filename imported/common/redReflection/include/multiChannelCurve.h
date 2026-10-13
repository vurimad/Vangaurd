/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redMemory/include/uniqueBuffer.h"

#include "rttiType.h"
#include "rttiSystem.h"
#include "reflectionPool.h"

// structure of the data buffer:
// <channel_1 header> 
// <channel_2 header> 
//	...
//	<channel_N header> 
//	<channel_1 float>Times[] 
//	<channel_2 float>Times[]
//	...
//	<channel_N float>Times[]
//	<channel_1 curveType>Values[]
//	<channel_2 curveType>Values[] 
//	....
//	<channel_N curveType>Values[]
// --------------------------->

enum ESegmentsLinkType : Uint8;
enum EInterpolationType : Uint8;

class RED_REFLECTION_API MultiChannelCurve
{
	friend class MultiChannelCurveBuilder;
public:
	MultiChannelCurve( const rtti::IType* keyType );
	MultiChannelCurve( const MultiChannelCurve& other );
	MultiChannelCurve( MultiChannelCurve&& other );

	MultiChannelCurve& operator=( const MultiChannelCurve& other );
	MultiChannelCurve& operator=( MultiChannelCurve&& other );
	~MultiChannelCurve();
	
	const rtti::IType* GetKeyType() const;

	red::Uint32 GetNumChannels() const;
	red::Uint32 GetNumKeys(red::Uint32 channel) const;
	red::Uint32 GetAlignment() const;

	const Float* GetKeyTimesArray(red::Uint32 channel) const;
	const void* GetKeyValuesArray(red::Uint32 channel) const;
	const void* GetKeyValue( red::Uint32 channelIndex, Uint32 keyIndex ) const;

	Float GetKeyTime( red::Uint32 channel, const Uint32 keyIndex ) const;
	Float GetMinTime(red::Uint32 channel) const;
	Float GetMaxTime(red::Uint32 channel) const;

	void SetNumChannels(Uint32 numChannels);
	void Serialize( IFile& file );

	CName GetName() const;

	Bool Compare( const MultiChannelCurve& other ) const;
	Bool operator==(const MultiChannelCurve& other ) const;
	Bool operator!=(const MultiChannelCurve& other ) const;

	void Swap( MultiChannelCurve& other );

	EInterpolationType GetInterpolationType() const;
	void SetInterpolationType( EInterpolationType newType );

	ESegmentsLinkType GetLinkType() const;
	void SetLinkType( ESegmentsLinkType newType );

	struct header
	{
		Uint32 size;
		Uint32 timesOffset;
		Uint32 valuesOffset;
	};

private:
	CName					m_name;
	const rtti::IType* 		m_dataType;

	red::UniqueBuffer		m_data;

	Uint32					m_numChannels;
	EInterpolationType		m_interpolationType;
	ESegmentsLinkType		m_linkType;
};

template< class T >
struct TMultiChannelCurve : public MultiChannelCurve
{
	using type	= T;

	TMultiChannelCurve() 
		: MultiChannelCurve( GetTypeObject<T>() ) 
	{}
};

class RED_REFLECTION_API MultiChannelCurveType final : public rtti::IType
{
public:
	MultiChannelCurveType( const rtti::IType *innerType );

	virtual const CName GetName() const override final;
	virtual Uint32 GetSize() const override final;
	virtual Uint32 GetAlignment() const override final;
	virtual ERTTITypeType GetType() const override final;
	virtual CName GetRefName() const override final;
	
	virtual void Construct( void *object ) const override final;
	virtual void Destruct( void *object ) const override final;
	virtual Bool Compare( const void* data1, const void* data2, Uint32 ) const override final;
	virtual void Copy( void* dest, const void* src ) const override final;
	virtual Bool Serialize( IFile& file, void* data, ISerializable* owner = nullptr ) const override final;
	virtual const Bool ReadValue( IRTTIContext& ctx, const void* data, const rtti::AccessPath& path, rtti::ValuePtr& outValue ) const override final;
	virtual const Bool WriteValue( IRTTIContext& ctx, void* data, const rtti::AccessPath& path, const rtti::ValueHolder& newValue, bool clone = false ) const override final; 

private:
	MultiChannelCurve m_data;
};

class RED_REFLECTION_API MultiChannelCurveBuilder
{
public:
	MultiChannelCurveBuilder(MultiChannelCurve& data);

	void Clear();
	void Resize( const red::memory::Pool& pool = red::PoolCurves() );

	MultiChannelCurveBuilder& SetNumChannels(Uint32 channels);
	MultiChannelCurveBuilder& SetNumKeys(Uint32 channel, Uint32 numKeys);
	MultiChannelCurveBuilder& SetInterpolationType( EInterpolationType newType );
	MultiChannelCurveBuilder& SetLinkType( ESegmentsLinkType newType );

	Uint32 GetNumChannels() const;
	Uint32 GetNumKeys(Uint32 channel) const;
	Float GetKeyTime(Uint32 channel, Uint32 key );
	void* GetKeyValue(Uint32 channel, Uint32 key );
	void SetKeyTime( const Uint32 channel, const Uint32 keyIndex, const Float time );

protected:
	MultiChannelCurve::header* GetChannelHeader(Uint32 channel);

	MultiChannelCurve& m_curveData;
	red::DynArray<Uint8> m_keysPerChannel{ red::PoolEngine() };
};

template< typename T >
class TMultiChannelCurveBuilder : public MultiChannelCurveBuilder
{
public:
	TMultiChannelCurveBuilder ( MultiChannelCurve& data ) 
		: MultiChannelCurveBuilder(data) 
	{
		DebugVerifyTypesCheck<T>();
	}

	void SetKeyData( Uint32 channel, Uint32 key, const Float time, const T& value )
	{
		DebugVerifyTypesCheck<T>();
		SetKeyTime(channel, key, time);
		T* data = reinterpret_cast<T*>(GetKeyValue(channel, key));
		*data   = value;
	}

	void SetKeyValue( Uint32 channel, Uint32 key, const T& value )
	{
		DebugVerifyTypesCheck<T>();
		T* data = reinterpret_cast<T*>(GetKeyValue(channel, key));
		*data   = value;
	}

private:

	template< typename U >
	void DebugVerifyTypesCheck()
	{
		RED_FATAL_ASSERT(m_curveData.GetKeyType() == GetTypeObject<T>(), "Error: Types are incompatible");
	}
};