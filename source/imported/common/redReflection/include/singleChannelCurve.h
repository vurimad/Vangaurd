/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redMemory/include/uniqueBuffer.h"

#include "rttiType.h"
#include "rttiSystem.h"
#include "reflectionPool.h"
#include "packageCustomTypeSerializer.h"

// structure of the data buffer:
// <float>Times[]  <curveType>Values[]
// 0-------------------------->

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
public:
	SingleChannelCurve( const rtti::IType* keyType = nullptr );
	SingleChannelCurve(const SingleChannelCurve& other);
	SingleChannelCurve( SingleChannelCurve&& other );
	SingleChannelCurve& operator=(const SingleChannelCurve& other);
	SingleChannelCurve& operator=( SingleChannelCurve&& other );

	const rtti::IType* GetKeyType() const;
	red::Uint32 GetNumKeys() const;
	red::Uint32 GetAlignment() const;
	const Float* GetKeyTimesArray() const;
	const void* GetKeyValuesArray() const;
	const void* GetKeyValue( const Uint32 keyIndex ) const;
	Float GetKeyTime( const Uint32 keyIndex ) const;

	Float GetMinTime() const;
	Float GetMaxTime() const;

	void Serialize( IFile& file );

	Bool Compare( const SingleChannelCurve& other ) const;

	Bool operator==( const SingleChannelCurve& other ) const;
	Bool operator!=( const SingleChannelCurve& other ) const;

	void Swap( SingleChannelCurve& other );
	CName GetName() const;

	EInterpolationType GetInterpolationType() const { return m_interpolationType; }
	void SetInterpolationType( EInterpolationType newType ) { m_interpolationType = newType; }

	ESegmentsLinkType GetLinkType() const { return m_linkType; }
	void SetLinkType( ESegmentsLinkType newType ) { m_linkType = newType; }

	Bool IsValid() const;

private:
	struct header
	{
		Uint32 size;
		Uint32 alignment;
		Uint32 timesStride;
		Uint32 valuesStride;
	};

	friend class SingleChannelCurveBuilder;

	CName					m_name;
	red::UniqueBuffer		m_data;
	const rtti::IType* 		m_dataType; 

	EInterpolationType		m_interpolationType;
	ESegmentsLinkType		m_linkType;
};

template< class T >
struct TSingleChannelCurve : public SingleChannelCurve
{
	using type	= T;

	TSingleChannelCurve() 
		: SingleChannelCurve( GetTypeObject<T>() ) 
	{}
};

class RED_REFLECTION_API SingleChannelCurveType : public rtti::IType
{
public:
	SingleChannelCurveType( const rtti::IType *innerType );
	operator SingleChannelCurve() { return m_data; }

	virtual const CName GetName() const override final;
	virtual Uint32 GetSize() const override final;
	virtual Uint32 GetAlignment() const override final;
	virtual ERTTITypeType GetType() const override final;
	virtual CName GetRefName() const override final;

	virtual void Construct( void *object ) const override final;
	virtual void Destruct( void *object ) const override final;

	virtual Bool Compare( const void* data1, const void* data2, Uint32 DEPRECATED_flags ) const override final;
	virtual void Copy( void* dest, const void* src ) const override final;

	virtual Bool Serialize( IFile& file, void* data, ISerializable* owner = nullptr ) const override final;

	virtual const Bool ReadValue( IRTTIContext& ctx, const void* data, const rtti::AccessPath& path, rtti::ValuePtr& outValue ) const override final;
	virtual const Bool WriteValue( IRTTIContext& ctx, void* data, const rtti::AccessPath& path, const rtti::ValueHolder& newValue, bool clone ) const override final;

	virtual const red::memory::Pool & GetInnerTypeMemoryPool() const override final;

	Uint32 GetKeySize() const;

private:
	SingleChannelCurve m_data;

};

// ctremblay: HACK HACK this curve should not be used. But gameplay didnt remove them yet.
class RED_REFLECTION_API PackageSingleChannelCurveSerializer : public red::PackageTypeSerializer
{
public:

	PackageSingleChannelCurveSerializer();
	virtual ~PackageSingleChannelCurveSerializer();

private:

	virtual void OnWriteValue( const WriteContext & context, const red::PackageSerializeTypeParameter & param ) const override final;
	virtual void OnReadValue( const ReadContext & context, const red::PackageSerializeTypeParameter & param ) const override final;
	virtual void OnRemapValue( const RemapContext& context, const red::PackageSerializeTypeParameter & param ) const override final;
};

class RED_REFLECTION_API SingleChannelCurveBuilder
{
public:
	SingleChannelCurveBuilder( SingleChannelCurve& data );

	void Clear();
	void Resize(const Uint32 numKeys, const red::memory::Pool& pool = red::PoolCurves());
	void* GetKeyValue( const Uint32 keyIndex );
	Float GetKeyTime( const Uint32 keyIndex ) const;
	void SetKeyTime( const Uint32 keyIndex, const Float time );
	void SetInterpolationType( EInterpolationType newType );
	void SetLinkType( ESegmentsLinkType newType );

protected:
	template< typename T >
	void DebugVerifyTypesCheck()
	{ 
		RED_FATAL_ASSERT( curveData.GetKeyType() == GetTypeObject<T>(), "Error: Type of Data for builder [%hs] is not the same type as [%hs] is being written as a value", curveData.GetKeyType()->GetName().AsChar(), GetTypeObject<T>()->GetName().AsChar() );
	}

private:
	SingleChannelCurve& curveData;
};

template< typename T >
class TSingleChannelCurveBuilder : public SingleChannelCurveBuilder
{
public:
	TSingleChannelCurveBuilder( SingleChannelCurve& data ) : SingleChannelCurveBuilder(data) {}

	void SetKeyData( Uint32 keyIndex, const Float time, const T& _value )
	{
		DebugVerifyTypesCheck<T>();
		SetKeyTime(keyIndex, time);
		T* data = reinterpret_cast<T*>(GetKeyValue(keyIndex));
		*data   = _value;
	}

	void SetKeyValue( Uint32 keyIndex, const T& _value )
	{
		DebugVerifyTypesCheck<T>();
		T* data = reinterpret_cast<T*>(GetKeyValue(keyIndex));
		*data   = _value;
	}
};

RED_FORCE_INLINE void operator<<( IFile& file, SingleChannelCurve& val )
{
	val.Serialize( file );
}
