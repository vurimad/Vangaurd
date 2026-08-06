/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

#include "scriptNativeFunctionMap.h"
#include "mathVector4.h"
#include "handle.h"
#include "rttiSystem.h"
#include "scriptableThreadSafetyMonitor.h"

class IScriptable;

namespace rep
{
	class ReplicationContext;
}

namespace rtti
{
	class Function;
	class Property;
	class ClassType;
}

namespace script
{
	template< typename T >
	const rtti::IType* GetRTTIType()
	{
#if defined( NO_SCRIPT_DEBUG )
		return nullptr;
#else
		return GetTypeObject< T >();
#endif
	}
}

#ifndef NO_SCRIPT_DEBUG
	#define	SCRIPT_DUMP_STACK_TO_LOG( stack ) stack.DumpToLog()
#else
	#define	SCRIPT_DUMP_STACK_TO_LOG( stack )
#endif

namespace script
{
	enum class DebugBreak : Uint8;
}

/// Stack frame for script execution
class RED_REFLECTION_API CScriptStackFrame
{
public:

#ifndef NO_SCRIPT_DEBUG
	// TODO: Move line and perfData into struct
	struct DebugData
	{
		DebugData();
		DebugData( Uint32 position, Uint16 line, Uint16 column, Uint16 length );
		DebugData( const DebugData& ) = default;
		DebugData& operator=( const DebugData& ) = default;

		RED_INLINE Uint32 GetPosition() const { return m_position; }
		RED_INLINE Uint16 GetLine() const { return m_line; }
		RED_INLINE Uint16 GetColumn() const { return m_column; }
		RED_INLINE Uint16 GetLength() const { return m_length; }

	private:
		Uint32 m_position;
		Uint16 m_line;
		Uint16 m_column;
		Uint16 m_length;
	};
#endif

#ifdef USE_PROFILER
	struct PerfData
	{
		PerfData() : instrObj( nullptr ), startExecutionTick( 0 ) {}
		PerfData( red::InstrumentationObject* obj ) : instrObj( obj ), startExecutionTick( 0 ) {}
		PerfData( const PerfData& ) = default;
		PerfData& operator=( const PerfData& ) = default;

		red::InstrumentationObject* instrObj;
		red::Timer timer;
		Uint64 startExecutionTick;
	};

#endif


public:

	CScriptStackFrame( IScriptable* context, const Uint8* code, const void* userData );
	CScriptStackFrame( CScriptStackFrame* parentFrame, IScriptable* context, const rtti::Function* function, void* locals, void* params, const void* userData );
	~CScriptStackFrame();

	CScriptStackFrame( const CScriptStackFrame& ) = delete;
	CScriptStackFrame( CScriptStackFrame&& ) = delete;

	RED_INLINE CScriptStackFrame* GetParent() const { return m_parent; }
	RED_INLINE Uint16 GetLevel() const { return m_level; }
	
	RED_INLINE IScriptable* GetContext() const { return m_context; }


	//! Execute one code token
	void Step( IScriptable* context, void* result, const rtti::IType* resultType );

public:
	//! Read embedded data from code stream
	template < typename T >
	RED_INLINE T Read()
	{
		const T *data = reinterpret_cast< const T* >( m_code );
		m_code += sizeof(T);
		return *data;
	}

	RED_INLINE void Read( red::String& result )
	{
		result = *((red::String*)m_code);
		m_code += sizeof(red::String);
	}

	RED_INLINE void Read( red::AnsiChar* buffer, Uint32 bufferSize, Uint32& readBytes )
	{
		const Uint32 size = Read< Uint32 >();
		RED_FATAL_ASSERT( size <= bufferSize );

		red::Memcpy( buffer, m_code, size );
		m_code += size;
		readBytes = size;
	}

#ifndef NO_SCRIPT_DEBUG
	//! Dump to log
	void DumpToLog() const;

	//! Dump to string
	Uint32 DumpToString( char* str, Uint32 strSize ) const;
	Uint32 DumpRawToAnsiString( AnsiChar* stf, Uint32 strSize ) const;

	//! Dump top to string
	size_t DumpTopToString( char* str, Uint32 strSize ) const ;
	Uint32 DumpRawTopToAnsiString( AnsiChar* str, Uint32 strSize ) const;
#endif

public:

	const Uint8*			m_code;				//!< Code pointer
	const rtti::Function*	m_function;			//!< Function we are in
	Uint8*					m_locals;			//!< Pointer to local function variables
	Uint8*					m_params;			//!< Pointer to function params
	void*					m_parentResult;		//!< Pointer to write result of this function call
	
	const void*				m_userData;			//!< Custom data assigned by the user
	void*					m_lValuePtr;		//!< L-value data offset
	const rtti::IType*		m_lValueType;

	IScriptable*			m_context;		//!< 'this', base object context 
	CScriptStackFrame*		m_parent;			//!< Parent stack frame ( for building call stack )
	Uint16					m_level;			//!< The "height" of the callstack, up to this instance

#ifndef NO_SCRIPT_DEBUG
	DebugData				m_debugData;
	Bool					m_isDebugging;
#endif

#ifdef USE_PROFILER
	PerfData				m_perfData;
#endif

#ifdef RED_MONITOR_SCRIPTABLE_THREAD_SAFETY
	const rtti::Property*	m_lValueProperty = nullptr;
	const IScriptable*		m_lValuePropertyOwner = nullptr;
#endif

	rep::ReplicationContext*	m_repContext;		//!< Replication context; only valid for replicated functions

	Bool m_canElideCopy;
};

RED_FORCE_INLINE void CScriptStackFrame::Step( IScriptable* context, void* result, const rtti::IType* resultType )
{
	// Read native function index
	Uint8 functionIndex = *m_code++;

	// Get the function
	TNativeGlobalFunc function = CScriptNativeFunctionMap::GetGlobalNativeFunction( functionIndex );

	RED_FATAL_ASSERT( function != nullptr,
		"Calling null native function pointer at functionIndex '%u'. Possible GET_PARAMETER to script import mismatch or called GET_PARAMETER after FINISH_PARAMETERS?",
		static_cast<Uint32>(functionIndex) );

	// Execute code
	(*function)(context, *this, result, resultType);
}

using ScriptStackFrameVisitor = red::FixedSizeFunction< Bool( Uint32, const red::AnsiChar*, Uint32 ) >;
RED_REFLECTION_API void VisitScriptCallstacks( ScriptStackFrameVisitor visitor );

// Grab function parameter from code stream
#define GET_PARAMETER( _type, _var, _def )								\
	const rtti::IType* _var ## Type = ::script::GetRTTIType< _type >();	\
	_type _var = _def;													\
	stack.m_lValuePtr = nullptr;										\
	stack.m_lValueType = nullptr;										\
	stack.Step( stack.GetContext(), &_var, _var ## Type );

// Grab optional function parameter from code stream
#define GET_PARAMETER_OPT( _type, _var, _def )							\
	const rtti::IType* _var ## Type = ::script::GetRTTIType< _type >();	\
	_type _var = _def;													\
	stack.m_lValuePtr = nullptr;										\
	stack.m_lValueType = nullptr;										\
	stack.Step( stack.GetContext(), &_var, _var ## Type );

// Grab reference to function parameter from code stream
#define GET_PARAMETER_REF( _type, _var, _def )									\
	const rtti::IType* _var ## Type = ::script::GetRTTIType< _type >();			\
	_type _var##T = _def;														\
	stack.m_canElideCopy = true;												\
	stack.m_lValuePtr = nullptr;												\
	stack.m_lValueType = nullptr;												\
	stack.Step( stack.GetContext(), &_var##T, _var ## Type );					\
	stack.m_canElideCopy = false;												\
	RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_ON_WRITE_PROPERTY();					\
	_type& _var = stack.m_lValuePtr ? *(_type*)stack.m_lValuePtr : _var##T;

#define GET_PARAMETER_SCRIPT_REF( _type, _var )																\
	rtti::ScriptedReferenceType _var##T;																	\
	stack.m_lValuePtr = nullptr;																			\
	stack.m_lValueType = nullptr;																			\
	stack.Step( stack.GetContext(), &_var##T, ::script::GetRTTIType< rtti::ScriptedReferenceType >() );		\
	RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_ON_WRITE_PROPERTY();												\
	RED_FATAL_ASSERT( _var##T.GetReferencedObject(), "Trying to derefence non-existing reference" );		\
	_type& _var = *(_type*)_var##T.GetReferencedObject()

#define FINISH_PARAMETERS							\
	stack.m_code++;

namespace Helper
{
	RED_REFLECTION_API Bool ValidateResultType( const rtti::IType* resultType, const rtti::IType* expectedResultType );

	template< typename T >
	RED_FORCE_INLINE void WriteFunc( void* ptr, const rtti::IType* ptrType, const T& val )
	{
		if ( ptr != nullptr )
		{
			RED_ASSERT( ValidateResultType( ptrType, script::GetRTTIType< T >() ), "Invalid result type: '%s', expected '%s'", ptrType->GetName().AsChar(), script::GetRTTIType< T >()->GetName().AsChar() );
			*(T*)ptr = val;
		}
	}

	template< typename T >
	RED_FORCE_INLINE void MoveFunc( void* ptr, const rtti::IType* ptrType, T&& val )
	{
		if ( ptr != nullptr )
		{
			RED_ASSERT( ValidateResultType( ptrType, script::GetRTTIType< T >() ), "Invalid result type: '%s', expected '%s'", ptrType->GetName().AsChar(), script::GetRTTIType< T >()->GetName().AsChar() );
			*(T*)ptr = std::move( val );
		}
	}
}

#define RETURN_INT(_val)	\
Helper::WriteFunc<Int32>( result, resultType, _val );

#define RETURN_INT64(_val)	\
Helper::WriteFunc<Int64>( result, resultType, _val );

#define RETURN_UINT(_val) \
Helper::WriteFunc<Uint32>( result, resultType, _val );

#define RETURN_UINT64(_val) \
Helper::WriteFunc<Uint64>( result, resultType, _val );

#define RETURN_ENUM(_type, _val)	\
Helper::WriteFunc< _type >( result, resultType, static_cast< _type >( _val ) );

#define RETURN_BYTE(_val) \
Helper::WriteFunc<Uint8>( result, resultType, _val );

#define RETURN_BOOL(_val) \
Helper::WriteFunc<Bool>( result, resultType, _val );

#define RETURN_FLOAT(_val) \
Helper::WriteFunc<Float>( result, resultType, _val );

#define RETURN_SHORT(_val) \
Helper::WriteFunc<Int16>( result, resultType, _val );

#define RETURN_STRING(_val) \
Helper::WriteFunc<String>( result, resultType, _val );

#define RETURN_UTF16STRING(_val) \
Helper::WriteFunc<Utf16String>( result, resultType, _val );

#define RETURN_NAME(_val) \
Helper::WriteFunc<CName>( result, resultType, _val );

#define RETURN_OBJECT(_val) \
{ Helper::WriteFunc< THandle<IScriptable> >( result, resultType, _val ); }

#define RETURN_WEAK_OBJECT(_val) \
{ Helper::WriteFunc< WeakHandle<IScriptable> >( result, resultType, _val ); }

#define RETURN_HANDLE(_type, _val) \
{ Helper::WriteFunc< THandle<_type> >( result, resultType, _val ); }

#define RETURN_WEAK_HANDLE(_type, _val) \
{ Helper::WriteFunc< WeakHandle<_type> >( result, resultType, _val ); }

#define RETURN_STRUCT(_type, _val) \
{ Helper::WriteFunc< _type >( result, resultType, _val ); }

#define RETURN_MOVE(_val) \
{ Helper::MoveFunc( result, resultType, _val ); }

#define RETURN_AUTO(_val) \
{ Helper::WriteFunc( result, resultType, _val ); }

#define RETURN_VOID() \
{ RED_UNUSED( result ); }
