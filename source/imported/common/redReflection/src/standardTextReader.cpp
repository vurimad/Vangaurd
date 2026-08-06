/**
* Copyright (c) 2007-2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "standardTextReader.h"

#include "rttiClass.h"
#include "serializable.h"

#include "../../redContainers/include/fundamentalStringParser.h"


namespace text {

// NOTE: this is touchy parser. It's not totally fragile but it does not support full error handling
// It's assumed that we will consume output of the interop::TextWriter not user generated content

StandardReader::StandardReader( const AnsiChar*& str )
	: m_str( str )
{
}

StandardReader::~StandardReader()
{
}

Uint32 StandardReader::GetVersion() const
{
	return 1;
}

const Bool StandardReader::BeginObject()
{
	if ( !GParseKeyword( m_str, "{" ) )
	{
		RED_LOG_WARNING( "InteropTextReader: Expected '{' at the begining of object" );
		return false;
	}

	m_elementCount.PushBack( 0 );
	return true;
}

void StandardReader::EndObject()
{
	if ( !m_elementCount.Empty() )
	{
		if ( !GParseKeyword( m_str, "}" ) )
		{
			RED_LOG_WARNING( "InteropTextReader: Expected '}' at the end of object" );
		}

		m_elementCount.PopBack();
	}
}

const Bool StandardReader::BeginArray()
{
	if ( !GParseKeyword( m_str, "[" ) )
	{
		RED_LOG_WARNING( "InteropTextReader: Expected '[' at the begining of array" );
		return false;
	}

	m_elementCount.PushBack( 0 );
	return true;
}

void StandardReader::EndArray()
{
	if ( !m_elementCount.Empty() )
	{
		if ( !GParseKeyword( m_str, "]" ) )
		{
			RED_LOG_WARNING( "InteropTextReader: Expected ']' at the end of array" );
		}

		m_elementCount.PopBack();
	}
}

const Bool StandardReader::BeginArrayElement()
{
	if ( m_elementCount.Empty() )
		return false;

	const auto* temp = m_str;
	if ( GParseKeyword( temp, "]" ) )
		return false;

	auto& count = m_elementCount.Back();
	if ( count++ > 0 )
		if ( !GParseKeyword( m_str, "," ) )
			return false;

	return true;
}

void StandardReader::EndArrayElement()
{
	// nothing
}

const Bool StandardReader::BeginProperty( CName& outPropertyName )
{
	const auto* temp = m_str;
	if ( GParseKeyword( temp, "}" ) )
		return false;

	auto& count = m_elementCount.Back();
	if ( count++ > 0 )
		if ( !GParseKeyword( m_str, "," ) )
			return false;

	String propName;
	if ( !GParseIdentifier( m_str, propName ) )
	{
		RED_LOG_WARNING( "InteropTextReader: Expected property name in object" );
		return false;
	}

	if ( !GParseKeyword( m_str, "=" ) )
	{
		RED_LOG_WARNING( "InteropTextReader: Expected '=' after property name" );
		return false;
	}

	outPropertyName = RED_NAME( propName );
	return true;
}

void StandardReader::EndProperty()
{
}

const Bool StandardReader::ReadValue( DataBuffer& outData )
{
	RED_FATAL( "Sending byte buffers via interop is not supported yet, but it can be added" );
	return false;
}

const Bool StandardReader::ReadValue( String& outValue )
{
	return GParseEscapedString( m_str, outValue );
}

const Bool StandardReader::ReadValue( ISerializable*& outValue )
{
	if ( GParseKeyword( m_str, "null" ) )
	{
		outValue = nullptr;
		return true;
	}

	if ( !GParseKeyword( m_str, "{" ) )
	{
		RED_LOG_WARNING( "InteropTextReader: Expected '{' at the beginning of object reference" );
		return false;
	}

	if ( !GParseKeyword( m_str, "_class=" ) )
	{
		RED_LOG_WARNING( "InteropTextReader: Expected '_class' property in object reference" );
		return false;
	}

	String className;
	if ( !GParseString( m_str, className ) )
	{
		RED_LOG_WARNING( "InteropTextReader: Expected value for _class property" );
		return false;
	}

	if ( !GParseKeyword( m_str, ",_id=" ) )
	{
		RED_LOG_WARNING( "InteropTextReader: Expected '_id' property in object reference" );
		return false;
	}

	Uint32 id = 0;
	if ( !GParseUint32( m_str, id ) )
	{
		RED_LOG_WARNING( "InteropTextReader: Expected object ID for the '_id' property" );
		return false;
	}

	if ( GParseKeyword( m_str, ",_data=" ) )
	{
		if ( m_createdObjectsMap.KeyExist( id ) )
		{
			RED_LOG_WARNING( "InteropTextReader: Object with ID %d already created", id );
			return false;
		}

		const auto* classType = GetRttiSystem().FindClass( RED_NAME_NOREG( className ) );
		if ( !classType )
		{
			RED_LOG_WARNING( "InteropTextReader: Not recognized class '%hs' used, are you linked with proper projcts?", className.AsChar() );
			return false;
		}

		if ( classType->IsAbstract() || !classType->IsA< ISerializable>() )
		{
			RED_LOG_WARNING( "InteropTextReader: Class '%hs' cannot be used to create ISerializable", className.AsChar() );
			return false;
		}

		THandle<ISerializable> objectRef( classType->CreateObject<ISerializable>() );
		m_createdObjects.PushBack( objectRef );
		m_createdObjectsMap.Insert( id, objectRef );

		if ( !objectRef->OnSerializeFromText( *this ) )
		{
			RED_LOG_WARNING( "InteropTextReader: Serialization of object %d of class '%hs' failed", id, className.AsChar() );
			return false;
		}

		outValue = objectRef.Get();
	}
	else
	{
		THandle<ISerializable> objectRef;
		if ( m_createdObjectsMap.Find( id, objectRef ) )
		{
			outValue = objectRef.Get();
		}
		else
		{
			RED_LOG_WARNING( "InteropTextReader: Reference to unknown object %d of class '%hs' found", id, className.AsChar() );
			return false;
		}
	}
	
	if ( !GParseKeyword( m_str, "}" ) )
	{
		RED_LOG_WARNING( "InteropTextReader: Expected '}' at the end of object reference" );
		return false;
	}

	return true;
}

const Bool LoadFromText( const String& txt, void* data, const rtti::IType* type )
{
	if ( type == nullptr )
	{
		RED_LOG_ERROR( "Unrecognized RTTI type used in deserialization" );
		return false;
	}

	const auto* str = txt.AsChar();
	StandardReader reader( str );
	return type->SerializeFromText( reader, data );
}

} // namespace text {
