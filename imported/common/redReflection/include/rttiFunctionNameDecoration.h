/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once
#include "functionNameDecorator.h"
#include "rttiSystem.h"

namespace rtti
{
	class IType;

namespace utils
{

class DecoratorFunctionDescription;

class RED_REFLECTION_API DecoratorTypeDescription : public FunctionNameDecorator::TypeDescription
{
public:

	DecoratorTypeDescription();
	DecoratorTypeDescription( DecoratorFunctionDescription* parent, const rtti::IType* type );

private:

	virtual const char* GetNameData() const override;
	virtual Uint32 GetNameLength() const override;
	virtual MetaType GetMetaType() const override;
	virtual Uint32 GetSize() const override;
	virtual const FunctionNameDecorator::TypeDescription* GetInternalType() const override;

	DecoratorFunctionDescription* m_parent;
	const rtti::IType* m_type;
	red::StringView m_name;
	mutable DecoratorTypeDescription* m_internalType;
};

class RED_REFLECTION_API DecoratorFunctionDescription : public FunctionNameDecorator::FunctionDescription
{
public:

	virtual ~DecoratorFunctionDescription();

	template < typename... Args >
	RED_INLINE DecoratorFunctionDescription( CName name, const Args&... args )
		: m_name( name.AsChar() )
		, m_paramsCount( sizeof...( Args ) )
		, m_firstNonParamType( m_paramsCount )
	{
		InitParamTypes( 0, args... );
	}

	DecoratorTypeDescription* AllocTypeDescriptor( const rtti::IType* type );

private:

	virtual const char* GetNameData() const override;
	virtual Uint32 GetNameLength() const override;
	virtual Uint32 GetParamsCount() const override;
	virtual const FunctionNameDecorator::TypeDescription* GetParamType( Uint32 i ) const;

	RED_INLINE void InitParamTypes( Uint32 paramIndex )	{ RED_FATAL_ASSERT( paramIndex == m_paramsCount, "Not all params initialized." ); }
	template < typename T, typename... Args >
	void InitParamTypes( Uint32 paramIndex, const T& param, const Args&... args )
	{
		m_paramTypes[ paramIndex++ ] = DecoratorTypeDescription( this, ::GetTypeObject< T >() );
		RED_FATAL_ASSERT( paramIndex < c_maxParams, "Too many types in function signature." );
		InitParamTypes( paramIndex, args... );
	}

	static const Uint32 c_maxParams = 32;
	red::StringView m_name;
	const Uint32 m_paramsCount;
	Uint32 m_firstNonParamType;
	DecoratorTypeDescription m_paramTypes[ c_maxParams ];
};

} // utils
} // rtti