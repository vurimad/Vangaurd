/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "serializable.h"

namespace rtti { class ClassType; }

// Event call result
enum EEventCallResult : Int8
{
	CR_EventNotFound	= -1,
	CR_EventFailed		= 0,
	CR_EventSucceeded	= 1
};

// Basic class for all thing that you want to add scripting support to
class RED_REFLECTION_API IScriptable : public ISerializable
{
	RTTI_DECLARE_TYPE( IScriptable );
	RED_USE_POLYMORPHIC_MEMORY_POOL( red::PoolScript );

public:
	IScriptable();
	virtual ~IScriptable();

	// Copy constructor and assignment
	IScriptable( const IScriptable& other );
	IScriptable& operator=( const IScriptable& other );

	// Get the script data buffer
	void* GetScriptPropertyData();

	// Get the script data buffer
	const void* GetScriptPropertyData() const;

	// Get the local (overridable) class, non virtual method, fastest
	const rtti::ClassType* GetLocalClass() const;

	// Assign local class.
	void BindLocalClassType( const rtti::ClassType* classType, void * scriptData ) const;

	// Find virtual function with given name in this object
	const rtti::Function* FindFunction( CName functionName ) const;

	const rtti::Function* FindFunctionByHash( Uint64 hash ) const;

	// Collect all functions from specified family
	virtual void EnumFunctionsFromFamily( IScriptable*& context, rtti::IFunctionCollector& collector ) const;

	// Called just before a capture of this object is taken prior to script compilation
	virtual void OnScriptPreCaptureSnapshot();

	// Called just after a capture of this object is taken prior to script compilation
	virtual void OnScriptPostCaptureSnapshot();

	// Called when scripts have been successfully reloaded
	virtual void OnScriptReloaded();

	//! Collect all IScriptable objects (slow, only for script reloading support)
	static void CollectAllScriptableObjects( red::DynArray< THandle< IScriptable > >& outScriptables );

	// Hiding ISerializable version. We can bypass virtual function call to GetClass here 
	//! RTTI class check
	Bool IsA( const rtti::ClassType* rttiClass ) const;

	//! RTTI exact class check
	Bool IsExactlyA( const rtti::ClassType* rttiClass ) const;

	//! RTTI class check
	template< class T >
	Bool IsA() const;

	//! RTTI class check
	template< class T >
	Bool IsExactlyA() const;

	//! Returns true if event with given name exists
	Bool FindEvent( CName functionName ) const;

	//! Call event
	template < typename... Args >
	EEventCallResult CallEvent( CName eventName, Args... args );

	//! Call event
	template < typename... Args >
	EEventCallResult CallEvent_UserContext( CName eventName, const void* userContext, Args... args );

	//! Recreate buffer with script properties
	void CreateScriptPropertiesBuffer();

	//! Release property buffer with script properties
	void ReleaseScriptPropertiesBuffer();

	//! Acquire exclusive (write) lock
	virtual void AcquireExclusiveLock() const {}

	// Release exlusive lock
	virtual void ReleaseExclusiveLock() const {}

#ifdef RED_MONITOR_SCRIPTABLE_THREAD_SAFETY
	// Only used for thread-safety debugging
	// Indicates if this object can be skipped when monitoring thread safety
	virtual Bool DEBUG_IsScriptableThreadSafe() const { return false; }
	virtual const IScriptable* DEBUG_GetParentEntity() const { return nullptr; }
#endif

	// Meant to enqueue function call for future dispatching
	// NOTES:
	// - implementation is expected to be thread safe
	// - default implementation asserts to easily detect bad uses
	// - check red::DelayedFunctionCallDispatcher for recommended usage details
	virtual void QueueFunctionCall( const rtti::Function* function, const rtti::Variant& parameter );

private:
	void funcToString( CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	void funcGetClassName( CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	void funcIsA( CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	void funcIsExactlyA( CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcDetectScriptableCycles( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );

	void CopyScriptPropertiesBuffer( const IScriptable& other );
	void ValidateScriptPropertyData() const;

	// Class of object
	mutable const rtti::ClassType* m_class;

	// Scripting data (properties)
	mutable void* m_scriptData;
};

#include "scriptable.hpp"
