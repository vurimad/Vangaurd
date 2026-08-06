/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "rttiFunctionCalling.h"
#include "rttiClassBuilderUtils.h"

template < typename... Args >
RED_INLINE EEventCallResult IScriptable::CallEvent( CName eventName, Args... args )
{
	Bool result = false;
	Bool found = rtti::CallFunctionRet< Bool >( this, rtti::RawFunctionName( eventName ), result, args... );
	return found ? ( result ? CR_EventSucceeded : CR_EventFailed ) : CR_EventNotFound;
}

template < typename... Args >
RED_INLINE EEventCallResult IScriptable::CallEvent_UserContext( CName eventName, const void* userContext, Args... args )
{
	Bool result = false;
	Bool found = rtti::CallFunctionRet_UserContext< Bool >( this, userContext, rtti::RawFunctionName( eventName ), result, args... );
	return found ? ( result ? CR_EventSucceeded : CR_EventFailed ) : CR_EventNotFound;
}

template< class T >
RED_INLINE Bool IScriptable::IsA() const
{
	return GetLocalClass()->IsA( ClassID< T >() );
}

template< class T >
RED_INLINE Bool IScriptable::IsExactlyA() const
{
	return GetLocalClass() == ClassID< T >();
}

RED_INLINE Bool IScriptable::IsA( const rtti::ClassType* rttiClass ) const
{
	return GetLocalClass()->IsA( rttiClass );
}

RED_INLINE Bool IScriptable::IsExactlyA( const rtti::ClassType* rttiClass ) const
{
	return GetLocalClass() == rttiClass;
}

RED_FORCE_INLINE const rtti::ClassType* IScriptable::GetLocalClass() const
{
	// ctremblay, if m_class is null, class was allocated with new operator. Until GC is removed, I can't cache GetNativeClass to m_class 
	// Also I'd need to make sure GetLocalClass is not called from any ctor.
	return m_class ? m_class : GetNativeClass();
}

RED_FORCE_INLINE const rtti::ClassType * rtti::ScriptableClassExtrator::Extract( const IScriptable * scriptable )
{
	return scriptable->GetLocalClass();
}

