/*
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#pragma once

///////////////////////////////////////////////////////////////////////////////
// IMPLEMENT_QUERY function family. Interface for auto-generation of
// QueryInterface function for IGeneralInterfaceContainer subclass.
// As macro is just implementing virtual function - there is nothing
// to stop you doing it by hand instead.
// NOTICE: there are two IMPLEMENT_QUERY_INTERFACE_BEGIN functions.
// IMPLEMENT_QUERY_INTERFACE_BEGIN is default function you should use. Put
// it in your code file and pair with DECLARE_QUERY_INTERFACE at header file.
// IMPLEMENT_QUERY_INTERFACE_BEGIN_CLASSBODY is meant to be used in class body.
// It is less constraining, as it doesn't require DECLARE_QUERY_INTERFACE to pair,
// and its easier to write. On the other hand its pushing a virtual function
// along with optimizable code into header file, so it might effect compilation
// times or even executable size.
#define DECLARE_QUERY_INTERFACE()																							\
	const Interface*							QueryInterface( InterfaceId interfaceId ) const override;

#define IMPLEMENT_QUERY_INTERFACE_BEGIN( className )																		\
	const className::Interface*						className::QueryInterface( InterfaceId interfaceId ) const				\
	{																														\
		switch( interfaceId )																								\
		{

#define IMPLEMENT_QUERY_INTERFACE_BEGIN_CLASSBODY()																			\
	const Interface*							QueryInterface( InterfaceId interfaceId ) const override					\
	{																														\
		switch( interfaceId )																								\
		{

#define IMPLEMENT_QUERY_INTERFACE_PROP( interfaceName, interfacePath )														\
		case ( interfaceName::INTERFACE_ID ): return static_cast< const interfaceName* >( interfacePath );

#define IMPLEMENT_QUERY_INTERFACE_INHERIT( interfaceName )																	\
		case ( interfaceName::INTERFACE_ID ): return static_cast< const interfaceName* >( this );


#define IMPLEMENT_QUERY_INTERFACE_END_BASE( baseClass )																		\
		default:																											\
			break;																											\
		}																													\
		return baseClass::QueryInterface( interfaceId );																	\
	}

#define IMPLEMENT_QUERY_INTERFACE_END() IMPLEMENT_QUERY_INTERFACE_END_BASE( TBaseClass )



#define IMPLEMENT_BASE_QUERY_INTERFACE()																					\
	template < class T > T* TQueryInterface()																				\
	{																														\
		const Interface* i = QueryInterface( T::INTERFACE_ID );																\
		return i ? const_cast< T* >( static_cast< const T* >( i ) ) : nullptr;												\
	}																														\
	template < class T > const T* TQueryInterface() const																	\
	{																														\
		const Interface* i = QueryInterface( T::INTERFACE_ID );																\
		return i ? static_cast< const T* >( i ) : nullptr;																	\
	}																														\
	virtual const Interface* QueryInterface( InterfaceId interfaceId ) const { return nullptr; }

