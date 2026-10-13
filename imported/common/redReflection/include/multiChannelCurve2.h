/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "rttiType.h"
#include "rttiSystem.h"
#include "reflectionPool.h"
#include "dataBuffer.h"
#include "singleChannelCurve2.h"

// structure of the data buffer:
// <channel_1 header> 
// <channel_2 header> 
//	...
//	<channel_N header> 
//	<channel_1 curveType>Values[]
//	<channel_2 curveType>Values[] 
//	....
//	<channel_N curveType>Values[]
// --------------------------->

namespace curve
{

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

	Uint32 GetNumChannels() const;
	Uint32 GetNumValues(Uint32 channel) const;
	Uint32 GetNumKeys(Uint32 channel) const;
	Uint32 GetAlignment() const;
	EInterpolationType GetInterpolationType(Uint32 channel) const;

	const void* GetValuesArray(Uint32 channel) const;
	const void* GetValue( Uint32 channelIndex, Uint32 keyIndex ) const;

	void SetNumChannels(Uint32 numChannels);

	void Swap( MultiChannelCurve& other );

#pragma pack(push, 1)
	struct header
	{
		Uint32 num_values;
		Uint32 num_keys;
		Uint32 values_offset;
		Uint8 interpolation_type;
	};
#pragma pack(pop)

	ESegmentsLinkType m_linkType;

private:
	const rtti::IType* m_dataType;
	DataBuffer		   m_data;
	Uint32			   m_numChannels;
	
};

template< class T >
struct TMultiChannelCurve : public MultiChannelCurve
{
	using type	= T;

	TMultiChannelCurve() 
		: MultiChannelCurve( GetTypeObject<T>() ) 
	{}
};

class RED_REFLECTION_API MultiChannelCurveBuilder
{
public:
	MultiChannelCurveBuilder(MultiChannelCurve& data);

	void Build( const red::memory::Pool& pool = red::PoolCurves() );

	MultiChannelCurveBuilder& SetNumChannels(Uint32 channels);
	MultiChannelCurveBuilder& SetLinkType( ESegmentsLinkType newType );

	MultiChannelCurveBuilder& InitConstantCurve(const Uint32 channel, const Uint32 numValues);
	MultiChannelCurveBuilder& InitLinearCurve(const Uint32 channel, const Uint32 numValues);
	MultiChannelCurveBuilder& InitQuadraticBezier(const Uint32 channel, const Uint32 numValues);
	MultiChannelCurveBuilder& InitCubicBezier(const Uint32 channel, const Uint32 numValues);
	MultiChannelCurveBuilder& InitHermite(const Uint32 channel, const Uint32 numValues);

	Uint32 GetNumChannels() const;
	Uint32 GetNumValues(Uint32 channel) const;
	void* GetValue(Uint32 channel, Uint32 key );

	EInterpolationType GetInterpolationType(Uint32 channel) const;

protected:
	template< typename T >
	void DebugVerifyTypesCheck();

	MultiChannelCurve::header* GetChannelHeader(Uint32 channel);
	MultiChannelCurve& m_curveData;

	struct channel_data
	{
		Uint8 interpolation_type;
		Uint8 values_per_channel;
	};

	red::DynArray<channel_data> m_valuesPerChannel{ red::PoolEngine() };
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

	TMultiChannelCurveBuilder& SetKeyFrame_Constant(Uint32 channelIndex, Uint32 keyIndex, const T& value);

	TMultiChannelCurveBuilder& SetInitialKeyFrame_Linear(Uint32 channelIndex, const T& p0, const T& p1 );
	TMultiChannelCurveBuilder& SetKeyFrame_Linear(Uint32 channelIndex, Uint32 keyIndex, const T& p );

	TMultiChannelCurveBuilder& SetInitialKeyFrame_QuadraticBezier(Uint32 channelIndex, const T& p0, const T& c0, const T& p1 );
	TMultiChannelCurveBuilder& SetKeyFrame_QuadraticBezier(Uint32 channelIndex, Uint32 keyIndex, const T& c, const T& p );

	TMultiChannelCurveBuilder& SetInitialKeyFrame_CubicBezier(Uint32 channelIndex, const T& p0, const T& c0, const T& c1, const T& p1 );
	TMultiChannelCurveBuilder& SetKeyFrame_CubicBezier(Uint32 channelIndex, Uint32 keyIndex, const T& c0, const T& c1, const T& p );

	TMultiChannelCurveBuilder& SetInitialKeyFrame_CubicHermite(Uint32 channelIndex, const T& p0, const T& c0, const T& c1, const T& p1 );
	TMultiChannelCurveBuilder& SetKeyFrame_CubicHermite(Uint32 channelIndex, Uint32 keyIndex, const T& c0, const T& c1, const T& p );

private:
	void SetDataInternal(Uint32 channel, Uint32 idx, const T& value);
};

}

#include "multiChannelCurve2.hpp"