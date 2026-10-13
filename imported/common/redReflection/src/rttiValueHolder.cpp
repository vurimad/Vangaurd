/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#include "build.h"
#include "rttiValueHolder.h"
#include "rttiSingleValueHolder.h"
#include "../../redContainers/include/string/stringBuilder.h"
#include "rttiValueParser.h"
#include "rttiValueBuilder.h"

namespace rtti
{

	ValueHolder::ValueHolder()
		: m_type( EType::Empty )
		, m_elements{ red::PoolBackend() }
	{
	}

	ValueHolder::~ValueHolder()
	{
	}

	// -- build string rep 

	String ValueHolder::ToString() const
	{
		red::StringBuilder<String> stringBuilder;
		ValueBuilder valueBuilder( stringBuilder );
		ToString( valueBuilder );
		return stringBuilder.ToString();
	}

	void ValueHolder::ToString( ValueBuilder& builder ) const
	{
		if ( m_type == EType::Empty )
		{
			// nothing
		}
		else if ( m_type == EType::Single )
		{
			// add the single value, this may add the quotes and other escapement (but that's how we store the string)
			builder.Value( m_simple->ToString().AsChar() );
		}
		else if ( m_type == EType::Array )
		{
			builder.StartArray();

			for ( Uint32 i=0; i<m_elements.Size(); ++i )
			{
				if ( i > 0 )
					builder.Separator();

				if ( m_elements[i].m_value->IsEmpty() )
				{
					builder.Value( "<>" );
				}
				else
				{
					m_elements[i].m_value->ToString( builder );
				}
			}

			builder.EndArray();
		}
		else if ( m_type == EType::Struct )
		{
			builder.StartStruct();

			for ( Uint32 i=0; i<m_elements.Size(); ++i )
			{
				if ( i > 0 )
					builder.Separator();

				builder.Ident( m_elements[i].m_name );

				builder.Equals();

				m_elements[i].m_value->ToString( builder );
			}

			builder.EndStruct();
		}
		else if ( m_type == EType::Handle )
		{
			builder.StartHandle();

			builder.Value( m_simple->ToString().AsChar() );

			builder.EndHandle();
		}
		else
		{
			RED_FATAL( "Invalid value holder type" );
		}
	}

	ValuePtr ValueHolder::Parse( const AnsiChar* valueText )
	{
		rtti::ValueParser parser( valueText );
		return Parse( parser );
	}

	ValuePtr ValueHolder::Parse( rtti::ValueParser& parser )
	{
		if ( parser.End() )
		{
			// no value
			return CreateEmpty();
		}
		else if ( parser.EatArrayStart() )
		{
			ValuePtr ret = CreateArray();

			// extract elements in the "[ ]" block
			Bool isFirst = true;
			while ( !parser.EatArrayEnd() )
			{
				// unexpected end of stream
				if ( parser.End() )
					return ValuePtr();

				// we expect separator between elements
				if ( !isFirst )
					if ( !parser.EatSeparator() )
						return ValuePtr(); // parsing error

				if ( parser.EatEmptyArrayElement() )
				{
					ret->AddArrayElement( CreateEmpty() );
				}
				else
				{
					// parse element - may fail
					auto element = Parse( parser );
					if ( !element )
						return ValuePtr();

					ret->AddArrayElement( element );
				}
				isFirst = false;
			}

			return ret;
		}
		else if ( parser.EatStructStart() )
		{
			ValuePtr ret = CreateStructure();

			// extract elements in the "{ }" block
			Bool isFirst = true;
			while ( !parser.EatStructEnd() )
			{
				// unexpected end of stream
				if ( parser.End() )
					return ValuePtr();

				// we expect separator between elements
				if ( !isFirst )
					if ( !parser.EatSeparator() )
						return ValuePtr(); // parsing error

				// structure elements are named, we need the name
				auto name = parser.EatName();
				if ( !name )
					return ValuePtr(); // parsing error

				// the name and value are separated by '='
				if ( !parser.EatEquals() )
					return ValuePtr();

				// parse element - may fail
				auto element = Parse( parser );
				if ( !element )
					return ValuePtr();

				// add as structure element, handle duplicates
				ret->SetStructElement( name, element );
				isFirst = false;
			}

			return ret;
		}
		else if ( parser.EatHandleStart() )
		{
			ValuePtr ret = CreateHandle( parser.EatValue().AsChar() );

			parser.EatHandleEnd();

			return ret;
		}
		else
		{
			// single value
			return CreateSingle( parser.EatValue().AsChar() );
		}
	}

	//---- SINGLE ------

	ValuePtr ValueHolder::CreateEmpty()
	{
		ValuePtr ret( RED_NEW( ValueHolder ) );
		return ret;
	}

	ValuePtr ValueHolder::CreateSingle( const AnsiChar* valueText )
	{
		// empty value - alias
		if ( !valueText || !*valueText )
			return CreateEmpty();

		ValuePtr ret( RED_NEW( ValueHolder ) );
		ret->m_type = EType::Single;
		ret->m_simple = SingleValueHolder::Create( valueText );
		return ret;
	}

	ValuePtr ValueHolder::CreateHandle( const AnsiChar* valueText )
	{
		if ( !valueText || !*valueText )
			return CreateEmpty();

		ValuePtr ret( RED_NEW( ValueHolder ) );
		ret->m_type = EType::Handle;
		ret->m_simple = SingleValueHolder::Create( valueText );
		return ret;
	}

	red::SharedPtr< SingleValueHolder > ValueHolder::GetSingle() const
	{
		RED_ASSERT( IsSingle(), "Value holder is not an single value" );

		if ( IsSingle() )
			return m_simple;

		return red::SharedPtr< SingleValueHolder >();
	}

	const String& ValueHolder::GetSingleValue() const
	{
		if ( IsSingle() )
			return GetSingle()->ToString();

		return String::EMPTY();
	}

	red::SharedPtr< SingleValueHolder > ValueHolder::GetHandle() const
	{
		RED_ASSERT( IsHandle(), "Value holder is not a handle value" );

		if ( IsHandle() )
			return m_simple;

		return red::SharedPtr< SingleValueHolder >();
	}

	const String& ValueHolder::GetHandleValue() const
	{
		if ( IsHandle() )
			return GetHandle()->ToString();

		return String::EMPTY();
	}

	//----- ARRAY -------

	ValuePtr ValueHolder::CreateArray()
	{
		ValuePtr ret( RED_NEW( ValueHolder ) );
		ret->m_type = ValueHolder::EType::Array;
		return ret;
	}

	const Uint32 ValueHolder::GetNumElements() const
	{
		RED_ASSERT( IsStructure() || IsArray(), "Value holder is not an array or struct" );
		return m_elements.Size();
	}

	ValuePtr ValueHolder::GetArrayElement( Uint32 index ) const
	{
		RED_ASSERT( IsArray(), "Value holder is not an array" );
		RED_ASSERT( index < m_elements.Size(), "Out of bounds array access" );

		if ( IsArray() && (index < m_elements.Size() ) )
			return m_elements[ index ].m_value;

		return CreateEmpty();
	}

	void ValueHolder::SetArrayElement( Uint32 index, const ValuePtr& value )
	{
		RED_ASSERT( IsArray(), "Value holder is not an array" );
		RED_ASSERT( index < m_elements.Size(), "Out of bounds array access" );
		RED_ASSERT( value, "NULL value passed - will default to empty for safety" );

		if ( IsArray() && (index < m_elements.Size()) )
			m_elements[ index ].m_value = value ? value : CreateEmpty();
	}

	void ValueHolder::AddArrayElement( const ValuePtr& value )
	{
		RED_ASSERT( IsArray(), "Value holder is not an array" );
		RED_ASSERT( value, "NULL value passed - will default to empty for safety" );

		if ( IsArray() )
		{
			Element info;
			info.m_value = value ? value : CreateEmpty();
			m_elements.PushBack( info );
		}
	}

	void ValueHolder::InsertArrayElement( const Uint32 index, const ValuePtr& value )
	{
		RED_ASSERT( IsArray(), "Value holder is not an array" );
		RED_ASSERT( index <= m_elements.Size(), "Out of bounds array access" );
		RED_ASSERT( value, "NULL value passed - will default to empty for safety" );

		if ( IsArray() && ( index <= m_elements.Size() ) )
		{
			Element info;
			info.m_value = value ? value : CreateEmpty();
			m_elements.InsertAt( index, info );
		}
	}

	void ValueHolder::RemoveArrayElement( const Uint32 index )
	{
		RED_ASSERT( IsArray(), "Value holder is not an array" );
		RED_ASSERT( index < m_elements.Size(), "Out of bounds array access" );

		if ( IsArray() && (index < m_elements.Size()) )
			m_elements.RemoveAt( index );
	}

	//---- STRUCTURE -----

	ValuePtr ValueHolder::CreateStructure()
	{
		ValuePtr ret( RED_NEW( ValueHolder ) );
		ret->m_type = ValueHolder::EType::Struct;
		return ret;
	}

	void ValueHolder::SetStructElement( const CName name, const ValuePtr& value )
	{
		RED_ASSERT( IsStructure(), "Value holder is not a structure" );
		RED_ASSERT( name, "Empty structure element names are not allowed" );
		//RED_WARNING( value, "NULL value passed - will default to empty for safety" );

		if ( IsStructure() && name )
		{
			// patch up existing
			for ( auto& element : m_elements )
			{
				if ( element.m_name == name )
				{
					element.m_value = value ? value : CreateEmpty();
					return;
				}
			}

			// add new one
			Element info;
			info.m_name = name;
			info.m_value = value ? value : CreateEmpty();
			m_elements.PushBack( info );
		}
	}

	void ValueHolder::RemoveStructElement( const CName name )
	{
		RED_ASSERT( IsStructure(), "Value holder is not a structure" );
		RED_ASSERT( name, "Empty structure element names are not allowed" );

		if ( IsStructure() && name )
		{
			// patch up existing
			for ( Uint32 i=0; i<m_elements.Size(); ++i )
			{
				if ( m_elements[i].m_name == name )
				{
					m_elements.RemoveAt( i );
					return;
				}
			}
		}
	}

	ValuePtr ValueHolder::GetStructElement( const CName name ) const
	{
		RED_ASSERT( IsStructure(), "Value holder is not a structure" );
		RED_ASSERT( name, "Empty structure element names are not allowed" );

		if ( IsStructure() && name )
		{
			for ( auto& element : m_elements )
				if ( element.m_name == name )
					return element.m_value;
		}

		// empty value, never return a null
		return CreateEmpty();
	}

	ValuePtr ValueHolder::GetStructElement( const Uint32 index ) const
	{
		RED_ASSERT( IsStructure(), "Value holder is not a structure" );
		RED_ASSERT( index < m_elements.Size(), "Out of bounds array access" );

		if ( IsStructure() && (index < m_elements.Size() ) )
			return m_elements[ index ].m_value;

		return CreateEmpty();
	}

	const CName ValueHolder::GetStructElementName( const Uint32 index ) const
	{
		RED_ASSERT( IsStructure(), "Value holder is not a structure" );
		RED_ASSERT( index < m_elements.Size(), "Out of bounds array access" );

		if ( IsStructure() && (index < m_elements.Size() ) )
			return m_elements[ index ].m_name;

		return CName::NONE();
	}

	Bool ValueHolder::HasStructElement( const CName name ) const
	{
		RED_ASSERT( IsStructure(), "Value holder is not a structure" );
		RED_ASSERT( name, "Empty structure element names are not allowed" );

		if ( IsStructure() && name )
		{
			for ( auto& element : m_elements )
				if ( element.m_name == name )
					return true;
		}

		// struct element doesn't exist
		return false;
	}

} // rtti
