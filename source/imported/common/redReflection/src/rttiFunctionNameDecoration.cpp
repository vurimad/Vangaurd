/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "rttiFunctionNameDecoration.h"
#include "rttiSystem.h"
#include "rttiType.h"
#include "rttiArrayTypesImpl.h"
#include "rttiPointerTypes.h"
#include "rttiClass.h"
#include "rttiPointerTypesImpl.h"

namespace rtti
{
namespace utils
{

//////////////////////////////////////////////////////////////////////////
// type description

DecoratorTypeDescription::DecoratorTypeDescription()
	: m_parent( nullptr )
	, m_type( nullptr )
	, m_internalType( nullptr )
{}

DecoratorFunctionDescription::~DecoratorFunctionDescription()
{}

DecoratorTypeDescription::DecoratorTypeDescription( DecoratorFunctionDescription* parent, const rtti::IType* type )
	: m_parent( parent )
	, m_type( type )
	, m_internalType( nullptr )
{
	if ( GetMetaType() == MetaType::Named )
	{
		m_name = GetRttiSystem().NativeNameToScriptAlias( m_type->GetName() ).AsStringView();
	}
}

const char* DecoratorTypeDescription::GetNameData() const
{
	return m_name.Data();
}

Uint32 DecoratorTypeDescription::GetNameLength() const
{
	return m_name.Length();
}

DecoratorTypeDescription::MetaType DecoratorTypeDescription::GetMetaType() const
{
	RED_FATAL_ASSERT( m_type != nullptr, "Descriptor has no valid type assigned." );

	switch ( m_type->GetType() )
	{
	case ERTTITypeType::RT_Array:
		return MetaType::DynArray;
	case ERTTITypeType::RT_StaticArray:
		return MetaType::StaticArray;
	case ERTTITypeType::RT_Handle:
		return MetaType::Handle;
	case ERTTITypeType::RT_WeakHandle:
		return MetaType::WeakHandle;
	case ERTTITypeType::RT_ScriptReference:
		return MetaType::Reference;
	default:
		return MetaType::Named;
	}
}

Uint32 DecoratorTypeDescription::GetSize() const
{
	RED_FATAL_ASSERT( m_type != nullptr, "Descriptor has no valid type assigned." );

	if ( m_type->GetType() == ERTTITypeType::RT_StaticArray )
	{
		const rtti::StaticArrayType* arrType = static_cast< const rtti::StaticArrayType* >( m_type );
		return arrType->ArrayGetArrayCapacity( nullptr ); // can be nullptr as capacity doesn't depend on instance
	}
	return 0;
}

const FunctionNameDecorator::TypeDescription* DecoratorTypeDescription::GetInternalType() const
{
	RED_FATAL_ASSERT( m_type != nullptr, "Descriptor has no valid type assigned." );

	if ( m_internalType != nullptr )
	{
		return m_internalType;
	}
	const ERTTITypeType metaType = m_type->GetType();
	if ( metaType == ERTTITypeType::RT_Array || metaType == ERTTITypeType::RT_StaticArray )
	{
		const rtti::IBaseArrayType* arrType = static_cast< const rtti::IBaseArrayType* >( m_type );
		m_internalType = m_parent->AllocTypeDescriptor( arrType->ArrayGetInnerType() );
	}
	else if ( metaType == ERTTITypeType::RT_Handle || metaType == ERTTITypeType::RT_WeakHandle )
	{
		const rtti::IBasePointerType* ptrType = static_cast< const rtti::IBasePointerType* >( m_type );
		m_internalType = m_parent->AllocTypeDescriptor( ptrType->GetPointedType() );
	}
	else if ( metaType == ERTTITypeType::RT_ScriptReference )
	{
		const rtti::ScriptedReferenceType* referenceType = static_cast< const rtti::ScriptedReferenceType* >( m_type );
		m_internalType = m_parent->AllocTypeDescriptor( referenceType->GetPointedType() );
	}
	return m_internalType;
}

//////////////////////////////////////////////////////////////////////////
// function description

DecoratorTypeDescription* DecoratorFunctionDescription::AllocTypeDescriptor( const rtti::IType* type )
{
	RED_FATAL_ASSERT( m_firstNonParamType < c_maxParams, "Too many types in function signature" );

	m_paramTypes[ m_firstNonParamType ] = DecoratorTypeDescription( this, type );
	return &m_paramTypes[ m_firstNonParamType++ ];
}

const char* DecoratorFunctionDescription::GetNameData() const
{
	return m_name.Data();
}

Uint32 DecoratorFunctionDescription::GetNameLength() const
{
	return m_name.Length();
}

Uint32 DecoratorFunctionDescription::GetParamsCount() const
{
	return m_paramsCount;
}

const FunctionNameDecorator::TypeDescription* DecoratorFunctionDescription::GetParamType( Uint32 i ) const
{
	RED_FATAL_ASSERT( i < m_paramsCount, "Param index out of bounds." );
	return &m_paramTypes[ i ];
}

} // utils
} // rtti