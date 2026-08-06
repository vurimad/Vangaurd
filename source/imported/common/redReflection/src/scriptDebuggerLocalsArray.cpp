/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "scriptDebuggerLocalsArray.h"
#include "scriptDebuggerLocalsProperty.h"
#include "scriptDebuggerLocalsVirtual.h"
#include "rttiArrayTypesImpl.h"
#include "../../redContainers/include/fundamentalStringConversion.h"

namespace script { namespace debug {

Array::Array( const void* data, const rtti::ArrayType* type, const String& name )
	: m_data( data )
	, m_type( type )
	, m_name( name )
{

}

Array::~Array()
{

}

red::DynArray< IVariablePtr > Array::EnumerateChildren()
{
	const Uint32 size = m_type->GetArraySize( m_data );
	const Uint32 capacity = m_type->GetArrayCapacity( m_data );

	red::DynArray< IVariablePtr > out{ red::PoolDebug() };
	out.Reserve( size + 2 );

	{
		IVariablePtr ptr( RED_NEW( Virtual )( "Size", String::Printf( "%u", size ) ) );
		out.PushBack( std::move( ptr ) );
	}

	{
		IVariablePtr ptr( RED_NEW( Virtual )( "Capacity", String::Printf( "%u", capacity ) ) );
		out.PushBack( std::move( ptr ) );
	}

	for( Uint32 i = 0; i < size; ++i )
	{
		const void* elementData = m_type->GetArrayElement( m_data, i );

		IVariablePtr ptr( RED_NEW( Property )( m_type->GetInnerType(), elementData, String::Printf( "[%u]", i ) ) );
		out.PushBack( std::move( ptr ) );
	}

	return out;
}

IVariablePtr Array::FindChild( const red::StringView& name )
{
	Uint32 index;
	if( !FromString( name.ToString(), index ) )
		return nullptr;

	const Uint32 size = m_type->GetArraySize( m_data );
	if( index >= size )
		return nullptr;

	const void* elementData = m_type->GetArrayElement( m_data, index );

	return IVariablePtr( RED_NEW( Property )( m_type->GetInnerType(), elementData, String::Printf( "[%u]", index ) ) );
}

String Array::GetName() const
{
	return m_name;
}

const rtti::IType* Array::GetType() const
{
	return m_type;
}

String Array::GetValue() const
{
	return "Array";
}

Uint32 Array::GetAttributes() const
{
	return RED_FLAG( Attributes::ReadOnly ) | RED_FLAG( Attributes::IsExpandable );
}

} } // namespace script { namespace debug {
