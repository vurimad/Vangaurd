/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redContainers/include/arraySpan.h"
#include "jsonFileHelper.h"

class JSONBuilderContext
{
public:
	JSONBuilderContext( JSONBuilderContext& ) = delete;
	JSONBuilderContext& operator=( JSONBuilderContext& ) = delete;
	explicit JSONBuilderContext( JSONFileHelper::JSONCompactWriter& jsonWriter )
		: m_jsonWriter( jsonWriter )
	{
		m_jsonWriter.StartArray();
		m_stack.PushBack( eArray );
	}

	~JSONBuilderContext()
	{
		Flush();
		RED_FATAL_ASSERT( m_stack.Size() == 1 && m_stack.Back() == eArray );
		m_jsonWriter.EndArray();
	}

	void Flush()
	{
		// Don't pop last element!
		while (m_stack.Size() > 1)
		{
			const EAggregateType type = m_stack.PopBack();
			if (type == eObject)
			{
				m_jsonWriter.EndObject();
			}
			else if (type == eArray)
			{
				m_jsonWriter.EndArray();
			}
			else
			{
				RED_FATAL( "Neither object nor array to pop?!" );
			}
		}
	}

	void AnonymousObject()
	{
		m_jsonWriter.StartObject();
		m_stack.PushBack( eObject );
	}

	void String( const char* value )
	{
		m_jsonWriter.String( value );
	}

	void String( const char* value, Uint32 valueSize )
	{
		m_jsonWriter.String( value, valueSize, true );
	}

	void Key( const char* key )
	{
		// hack this could be fixed by updating rapidjson
		m_jsonWriter.String( key );
	}

	void Bool( Bool value )
	{
		m_jsonWriter.Bool( value );
	}

	void Uint( Uint32 value )
	{
		m_jsonWriter.Uint( value );
	}

	void Int( Int32 value )
	{
		m_jsonWriter.Int( value );
	}

	void Uint64( Uint64 value )
	{
		m_jsonWriter.Uint64( value );
	}

	void Int64( Int64 value )
	{
		m_jsonWriter.Int64( value );
	}

	void Double( Double value )
	{
		m_jsonWriter.Double( value );
	}

	void Object( const char* name )
	{
		m_jsonWriter.String( name );
		m_jsonWriter.StartObject();
		m_stack.PushBack( eObject );
	}

	void Array( const char* name )
	{
		m_jsonWriter.String( name );
		m_jsonWriter.StartArray();
		m_stack.PushBack( eArray );
	}

	void AnonymousArray()
	{
		m_jsonWriter.StartArray();
		m_stack.PushBack( eArray );
	}

	void PopArray()
	{
		RED_FATAL_ASSERT( !m_stack.Empty() && m_stack.Back() == eArray );
		m_jsonWriter.EndArray();
		m_stack.PopBack();
	}

	void PopObject()
	{
		RED_FATAL_ASSERT( !m_stack.Empty() && m_stack.Back() == eObject );
		m_jsonWriter.EndObject();
		m_stack.PopBack();
	}

private:
	JSONFileHelper::JSONCompactWriter& m_jsonWriter;
	enum EAggregateType : Uint8
	{
		eArray,
		eObject,
	};
	red::StaticArray< EAggregateType, 64 > m_stack;
};

template< typename T >
struct RapidJSONHelper;

template<>
struct RapidJSONHelper<String>
{
	template< typename JSONWriter >
	static void WriteValue( JSONWriter& context, const String& value )
	{
		context.String( value.AsChar() );
	}
};

template<>
struct RapidJSONHelper<CName>
{
	template< typename JSONWriter >
	static void WriteValue( JSONWriter& context, CName value )
	{
		context.String( value.AsChar() );
	}
};

template<>
struct RapidJSONHelper<Uint32>
{
	static void WriteValue( JSONBuilderContext& context, Uint32 value )
	{
		context.Uint( value );
	}
};

template<>
struct RapidJSONHelper<Uint64>
{
	static void WriteValue( JSONBuilderContext& context, Uint64 value )
	{
		context.Uint64( value );
	}
};

template<>
struct RapidJSONHelper<Float>
{
	static void WriteValue( JSONBuilderContext& context, Float value )
	{
		context.Double( value );
	}
};

template<>
struct RapidJSONHelper<Double>
{
	static void WriteValue( JSONBuilderContext& context, Double value )
	{
		context.Double( value );
	}
};

class JSONPoints2DBuilder
{
public:
	explicit JSONPoints2DBuilder( JSONBuilderContext& context )
		: m_context( context )
	{
		m_context.Flush();
		m_context.AnonymousObject();
		m_context.Object( "points2d" );
	}

	~JSONPoints2DBuilder()
	{
		m_context.Flush();
	}

	JSONPoints2DBuilder( const JSONPoints2DBuilder& ) = delete;
	JSONPoints2DBuilder& operator=( const JSONPoints2DBuilder& ) = delete;
	JSONPoints2DBuilder( JSONPoints2DBuilder&& ) = default;

	JSONPoints2DBuilder& Caption( const char* caption )
	{
		m_context.String( "caption" );
		m_context.String( caption );
		return *this;
	}

	// #tbd:array *slice*
	template< typename T, typename U >
	void ZipData( const T pointsX[], Uint32 numPointsX, const U pointsY[], Uint32 numPointsY )
	{
		RED_FATAL_ASSERT( numPointsX == numPointsY );

		m_context.Array( "data" );
		for (Uint32 i = 0; i < numPointsX; ++i)
		{
			m_context.AnonymousArray();
			RapidJSONHelper<T>::WriteValue( m_context, pointsX[ i ] );
			RapidJSONHelper<U>::WriteValue( m_context, pointsY[ i ] );
			m_context.PopArray();
		}
		m_context.PopArray();
	}

	// Flat (x,y) list
	// template< typename T>
// 		void PointsFlat( red::ArraySpan<T> points )
// 		{
//			assert points.Size() even
// 		}

private:
	JSONBuilderContext& m_context;
};

class JSONTableBuilder
{
public:
	explicit JSONTableBuilder( JSONBuilderContext& context )
		: m_context( context )
	{
		m_context.Flush();
		m_context.AnonymousObject();
		m_context.Object( "table" );
	}

	~JSONTableBuilder()
	{
		m_context.Flush();
	}

	JSONTableBuilder( const JSONTableBuilder& ) = delete;
	JSONTableBuilder& operator=( const JSONTableBuilder& ) = delete;
	JSONTableBuilder( JSONTableBuilder&& ) = default;

	JSONTableBuilder& Autolink( const char* value )
	{
		m_context.String( "autolink" );
		m_context.String( value );
		return *this;
	}

	JSONTableBuilder& Caption( const char* caption )
	{
		m_context.String( "caption" );
		m_context.String( caption );
		return *this;
	}

	JSONTableBuilder& Headers( red::ArraySpan< const char* > headers )
	{
		m_context.Array( "headers" );
		for (const char* hdr : headers)
		{
			m_context.String( hdr );
		}
		m_context.PopArray();
		return *this;
	}

	JSONTableBuilder& Headers( const char* header1, const char* header2, const char* header3 = nullptr, const char* header4 = nullptr )
	{
		m_context.Array( "headers" );
		m_context.String( header1 );
		m_context.String( header2 );
		if (header3)
		{
			m_context.String( header3 );
		}
		if (header4)
		{
			m_context.String( header4 );
		}
		m_context.PopArray();
		return *this;
	}

	JSONTableBuilder& Header( const char* header )
	{
		m_context.String( "headers" );
		m_context.String( header );
		return *this;
	}

	template<typename T>
	void Rows( const red::DynArray<T> /*red::ArraySpan< T >*/ rows )
	{
		m_context.Array( "rows" );
		for (const auto& r : rows)
		{
			RapidJSONHelper<T>::WriteValue( m_context, r );
		}
		m_context.PopArray();
	}

	template<typename T, typename U>
	void Rows( const red::DynArray<T>/*red::ArraySpan< T >*/ col1, const red::DynArray<T>/*red::ArraySpan< T > */col2 )
	{
		RED_FATAL_ASSERT( col1.Size() == col2.Size() );

		m_context.Array( "rows" );

		for (Uint32 i = 0; i < col1.Size(); ++i)
		{
			m_context.AnonymousArray();
			RapidJSONHelper<T>::WriteValue( m_context, col1[ i ] );
			RapidJSONHelper<U>::WriteValue( m_context, col2[ i ] );
			m_context.PopArray();
		}

		m_context.PopArray();
	}

private:
	JSONBuilderContext& m_context;
};