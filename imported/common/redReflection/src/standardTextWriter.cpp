/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "standardTextWriter.h"

#include "rttiType.h"
#include "rttiValueBuilder.h"
#include "serializable.h"

namespace text {

StandardWriter::StandardWriter( red::StringBuilder<String>& builder )
	: m_builder( builder )
	, m_objectIDAllocator( 1 )
{
}

StandardWriter::~StandardWriter()
{
}

Uint32 StandardWriter::GetVersion() const
{
	return 1;
}

void StandardWriter::BeginObject( const rtti::IType* type, const void* data )
{
	RED_FATAL_ASSERT( type->GetType() == RT_Class, "Only class types can be saved like objects" );

	m_builder.Append( "{" );
	m_itemCounts.PushBack( 0 );
}

void StandardWriter::EndObject()
{
	m_builder.Append( "}" );
	m_itemCounts.PopBack();
}

void StandardWriter::BeginArray()
{
	m_builder.Append( "[" );
	m_itemCounts.PushBack( 0 );
}

void StandardWriter::EndArray()
{
	m_builder.Append( "]" );
	m_itemCounts.PopBack();
}

void StandardWriter::BeginArrayElement()
{
	auto& count = m_itemCounts.Back();
	if ( count++ > 0 )
		m_builder.Append( "," );
}

void StandardWriter::EndArrayElement()
{
	// nothing
}

void StandardWriter::BeginProperty( const CName& name )
{
	auto& count = m_itemCounts.Back();
	if ( count++ > 0 )
		m_builder.Append( "," );

	m_builder.Append( name.AsChar() );
	m_builder.Append( "=" );
}

void StandardWriter::EndProperty()
{
	// nothing
}

void StandardWriter::WriteValue( const void* data, Uint32 size )
{
	RED_FATAL( "Sending raw buffers via interop is not supported, although it could be" );
}

void StandardWriter::WriteValue( const String& str )
{
	if ( !str.Empty() )
	{
		red::StringBuilder<String> textBuilder;
		rtti::ValueBuilder valueBuilder( textBuilder );
		valueBuilder.Value( str.AsChar() );
		m_builder.Append( textBuilder.ToString() );
	}
}

void StandardWriter::WriteValue( const ISerializable* object )
{
	if ( !object )
	{
		m_builder.Append( "null" );
		return;
	}

	Uint32 id = 0;
	if ( m_mappedObjects.Find( object, id ) )
	{
		m_builder.Appendf( R"({_class="%hs",_id=%d})", object->GetClass()->GetName().AsChar(), id );
	}
	else
	{
		id = m_objectIDAllocator++;
		m_mappedObjects.Insert( object, id );

		// pointer based type
		m_builder.Appendf( R"({_class="%hs",_id=%d,_data=)", object->GetClass()->GetName().AsChar(), id );
		object->OnSerializeToText( *this );
		m_builder.Append( "}" );
	}
}

const Bool SaveToText( const void* data, const rtti::IType* type, String& outText )
{
	if ( type == nullptr )
	{
		RED_LOG_ERROR( "Unrecognized RTTI type used in serialization" );
		return false;
	}

	red::StringBuilder<String> builder;
	StandardWriter writer( builder );
	if ( !type->SerializeToText( writer, data ) )
		return false;

	outText = builder.ToString();
	return true;
}

} // namespace text {
