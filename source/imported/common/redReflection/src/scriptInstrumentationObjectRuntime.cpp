/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "scriptInstrumentationObjectRuntime.h"
#include "scriptOpcodesList.h"
#include "scriptingSystemImpl.h"

RED_NO_EMPTY_FILE();

#ifdef USE_PROFILER

namespace script
{

RuntimeInstrumentationObject::RuntimeInstrumentationObject( Int32 startProfileCodeOffset, rtti::Function* function )
	: m_function( function )
	, m_startProfileCodeOffset( startProfileCodeOffset )
{
	red::Memzero( m_scopeName, c_maxScopeNameLength );

	static script::ProfilerMode profilerMode = CScriptingSystem::GetInstance().GetProfilerMode();

	if ( profilerMode != ProfilerMode_Off && m_startProfileCodeOffset != s_invalidCodeOffset && m_function )
	{
		Uint8* startProfilerInstrObjByte = SetScopeName( m_startProfileCodeOffset );
		red::InstrumentationObject* startInstrObj = GetInstrumentationObject( startProfilerInstrObjByte );

		RED_FATAL_ASSERT( !startInstrObj );

		red::InstrumentationObject* instrObj = ::new ( m_instrObj ) red::InstrumentationObject( m_scopeName, PBC_SCRIPTS );

		startInstrObj = instrObj;
		gProfilers.RegisterObject( startInstrObj );
		red::Memcpy( startProfilerInstrObjByte, &startInstrObj, sizeof( red::InstrumentationObject* ) );
	}
}

RuntimeInstrumentationObject::RuntimeInstrumentationObject()
	: RuntimeInstrumentationObject( s_invalidCodeOffset, nullptr )
{}

RuntimeInstrumentationObject::~RuntimeInstrumentationObject()
{
	Clear();
}

void RuntimeInstrumentationObject::Clear()
{
	if ( m_startProfileCodeOffset != s_invalidCodeOffset )
	{
		Uint8* startProfilerInstrObjByte = GetInstrumentationObjectByte( m_startProfileCodeOffset );
		red::InstrumentationObject* startInstrObj = GetInstrumentationObject( startProfilerInstrObjByte );

		if ( !startInstrObj )
		{
			startInstrObj = nullptr;
		}

		reinterpret_cast< red::InstrumentationObject* >( m_instrObj )->~InstrumentationObject();

		m_startProfileCodeOffset = s_invalidCodeOffset;
	}
}

const Uint8* RuntimeInstrumentationObject::GetInstrumentationObjectByte( Int32 codeOffset ) const
{
	Uint8* data = GetScopeNameByte( codeOffset );
	return SkipScopeNameBytes( data );
}

Uint8* RuntimeInstrumentationObject::SetScopeName( Int32 codeOffset )
{
	Uint8* data = GetScopeNameByte( codeOffset );
	return CopyScopeName( data );
}

Uint8* RuntimeInstrumentationObject::GetScopeNameByte( Int32 codeOffset ) const
{
	RED_FATAL_ASSERT( m_function, "Instrumentation object data missing associated function, check CScriptDataBinder::LoadOpcodes" );
	RED_FATAL_ASSERT( m_startProfileCodeOffset != s_invalidCodeOffset, "Instrumentation object data missing associated function, check CScriptDataBinder::LoadOpcodes" );

	CScriptCompiledCode& code = m_function->GetCode();
	Uint8* data = static_cast<Uint8*>( code.GetCode() ) + codeOffset;
	RED_FATAL_ASSERT( codeOffset == m_startProfileCodeOffset && *data == OP_StartProfile, "Compiled script code is invalid" );
	return ++data;
}

Uint8* RuntimeInstrumentationObject::SkipScopeNameBytes( Uint8* data ) const
{
	Uint32 functionNameLength;
	red::Memcpy( &functionNameLength, data, sizeof( functionNameLength ) );
	data += sizeof( functionNameLength );
	data += functionNameLength;
	return data;
}

Uint8* RuntimeInstrumentationObject::CopyScopeName( Uint8* data )
{
	Uint32 functionNameLength;
	red::Memcpy( &functionNameLength, data, sizeof( functionNameLength ) );
	data += sizeof( functionNameLength );

	red::Memcpy( m_scopeName, data, functionNameLength );
	data += functionNameLength;

	return data;
}

red::InstrumentationObject* RuntimeInstrumentationObject::GetInstrumentationObject( const Uint8* instrObjByte )
{
	red::InstrumentationObject* instrObj = nullptr;
	red::Memcpy( &instrObj, instrObjByte, sizeof( red::InstrumentationObject* ) );

	return instrObj;
}

}

#endif
