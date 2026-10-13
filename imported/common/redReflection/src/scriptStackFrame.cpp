/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#include "build.h"
#include "scriptStackFrame.h"
#include "scriptingSystem.h"
#include "scriptable.h"
#include "rttiArrayTypesImpl.h"
#include "rttiPointerTypesImpl.h"
#include "scriptingSystemImpl.h"

#if !defined( NO_SCRIPT_DEBUG )

namespace
{
	RED_TLS CScriptStackFrame* s_scriptThreadStackFrame;

	// Index of this thread in script thread debug info array.
	RED_TLS Int32 s_scriptThreadDebugInfoIndex = -1;

	static constexpr Uint32 c_cacheLineSize = 64;

	struct RED_ALIGN( c_cacheLineSize ) ScriptThreadDebugInfo
	{
		// If of the system thread where this frame is in.
		red::ThreadId threadId;

		// Currently executed frame on the thread.
		CScriptStackFrame** stackFrame;

		// The lock used for accessing the thread.
		red::SpinLock accessLock;
	};
	static_assert( sizeof( ScriptThreadDebugInfo ) == c_cacheLineSize, "ScriptThreadDebugInfo should have cacheline size to avoid false sharing." );

	red::Atomic< Int32 > s_scriptThreadDebugInfoIndexGenerator(-1);
	constexpr Uint32 c_MaxScriptThreadDebugInfos = 64;
	ScriptThreadDebugInfo s_scriptThreadDebugInfos[ c_MaxScriptThreadDebugInfos ] = {};

	ScriptThreadDebugInfo* GetOrSetupScriptThreadDebugInfo()
	{
		ScriptThreadDebugInfo* debugInfo = nullptr;

		if( s_scriptThreadDebugInfoIndex == -1 )
		{
			s_scriptThreadDebugInfoIndex = s_scriptThreadDebugInfoIndexGenerator.Increment();
			if( s_scriptThreadDebugInfoIndex < c_MaxScriptThreadDebugInfos )
			{
				debugInfo = &s_scriptThreadDebugInfos[ s_scriptThreadDebugInfoIndex ];
				RED_SCOPE_LOCK( debugInfo->accessLock );
				debugInfo->stackFrame = &s_scriptThreadStackFrame;
				debugInfo->threadId = red::ThreadId::CurrentThread();
			}
			else
			{
				RED_LOG_ERROR( "Script thread debug infos slots depleted! (%i/%i)", s_scriptThreadDebugInfoIndex, c_MaxScriptThreadDebugInfos );
			}
		}
		else if( s_scriptThreadDebugInfoIndex < c_MaxScriptThreadDebugInfos )
		{
			debugInfo = &s_scriptThreadDebugInfos[ s_scriptThreadDebugInfoIndex ];
		}
		return debugInfo;
	}
}

const Uint32 c_maxFrameCount = 128;

CScriptStackFrame::DebugData::DebugData()
	: DebugData( -1, -1, -1, -1 )
{
}


CScriptStackFrame::DebugData::DebugData( Uint32 position, Uint16 line, Uint16 column, Uint16 length )
	: m_position( position )
	, m_line( line )
	, m_column( column )
	, m_length( length )
{
}

#endif

namespace Helper
{
	Bool ValidateResultType( const rtti::IType* resultType, const rtti::IType* expectedResultType )
	{
		if ( !resultType || !expectedResultType )
		{
			return true;
		}

		const ERTTITypeType type = resultType->GetType();
		const ERTTITypeType expectedType = expectedResultType->GetType();

		switch ( type )
		{
		case RT_Name:
			{
				return expectedType == RT_Name;
			}
			break;

		case RT_Fundamental:
			{
				return expectedType == RT_Fundamental && resultType->GetSize() == expectedResultType->GetSize();
			}
			break;

		case RT_Simple:
			{
				return expectedType == RT_Simple && resultType->GetSize() == expectedResultType->GetSize();
			}
			break;

		case RT_Class:
			{
				if ( expectedType != RT_Class )
				{
					return false;
				}

				const rtti::ClassType* firstClassType = reinterpret_cast< const rtti::ClassType* >( resultType );
				const rtti::ClassType* secondClassType = reinterpret_cast< const rtti::ClassType* >( expectedResultType );
				return firstClassType->IsA( secondClassType ) || secondClassType->IsA( firstClassType );
			}
			break;

		case RT_Enum:
			{
				return ( expectedType == RT_Enum || expectedType == RT_BitField || expectedType == RT_Fundamental ) && resultType->GetSize() == expectedResultType->GetSize();
			}
			break;

		case RT_BitField:
			{
				return ( expectedType == RT_Enum || expectedType == RT_BitField ) && resultType->GetSize() == expectedResultType->GetSize();
			}
			break;

		case RT_Array:
			{
				if ( expectedType != RT_Array )
				{
					return false;
				}

				const rtti::ArrayType* firstArrayType = reinterpret_cast< const rtti::ArrayType* >( resultType );
				const rtti::ArrayType* secondArrayType = reinterpret_cast< const rtti::ArrayType* >( expectedResultType );
				return ValidateResultType( firstArrayType->ArrayGetInnerType(), secondArrayType->ArrayGetInnerType() );
			}
			break;

		case RT_StaticArray:
			{
				if ( expectedType != RT_StaticArray )
				{
					return false;
				}

				const rtti::StaticArrayType* firstArrayType = reinterpret_cast< const rtti::StaticArrayType* >( resultType );
				const rtti::StaticArrayType* secondArrayType = reinterpret_cast< const rtti::StaticArrayType* >( expectedResultType );
				return ValidateResultType( firstArrayType->ArrayGetInnerType(), secondArrayType->ArrayGetInnerType() );
			}
			break;

		case RT_NativeArray:
			{
				if ( expectedType != RT_NativeArray )
				{
					return false;
				}

				const rtti::NativeArrayType* firstArrayType = reinterpret_cast< const rtti::NativeArrayType* >( resultType );
				const rtti::NativeArrayType* secondArrayType = reinterpret_cast< const rtti::NativeArrayType* >( expectedResultType );
				return ValidateResultType( firstArrayType->ArrayGetInnerType(), secondArrayType->ArrayGetInnerType() );
			}
			break;

		case RT_Handle:
			{
				if ( expectedType != RT_Handle )
				{
					return false;
				}

				const rtti::HandleType* firstHandleType = reinterpret_cast< const rtti::HandleType* >( resultType );
				const rtti::HandleType* secondHandleType = reinterpret_cast< const rtti::HandleType* >( expectedResultType );
				return ValidateResultType( firstHandleType->GetPointedType(), secondHandleType->GetPointedType() );
			}
			break;

		case RT_WeakHandle:
			{
				if ( expectedType != RT_WeakHandle )
				{
					return false;
				}

				const rtti::WeakHandleType* firstHandleType = reinterpret_cast< const rtti::WeakHandleType* >( resultType );
				const rtti::WeakHandleType* secondHandleType = reinterpret_cast< const rtti::WeakHandleType* >( expectedResultType );
				return ValidateResultType( firstHandleType->GetPointedType(), secondHandleType->GetPointedType() );
			}
			break;

		case RT_ScriptReference:
			{
				if ( expectedType != RT_ScriptReference )
				{
					return false;
				}

				const rtti::ScriptedReferenceType* firstRefType = reinterpret_cast< const rtti::ScriptedReferenceType* >( resultType );
				const rtti::ScriptedReferenceType* secondRefType = reinterpret_cast< const rtti::ScriptedReferenceType* >( expectedResultType );
				return ValidateResultType( firstRefType->GetPointedType(), secondRefType->GetPointedType() );
			}
			break;

		default:
			return false;
		}
	}
}


CScriptStackFrame::CScriptStackFrame( IScriptable* context, const Uint8* code, const void* userData )
	: m_context( context )
	, m_parent( nullptr )
	, m_level( 0 )
	, m_function( nullptr )
	, m_locals( nullptr )
	, m_params( nullptr )
	, m_code( code )
	, m_parentResult( nullptr )
#ifndef NO_SCRIPT_DEBUG
	, m_isDebugging( false )
#endif
#ifdef USE_PROFILER
, m_perfData( nullptr )
#endif
	, m_repContext( nullptr )
	, m_lValuePtr( nullptr )
	, m_lValueType( nullptr )
	, m_userData( userData )
	, m_canElideCopy( false )
{
#if defined( USE_PROFILER )
	m_perfData.startExecutionTick = m_perfData.timer.GetTicks();
#endif
}

CScriptStackFrame::CScriptStackFrame( CScriptStackFrame* parentFrame, IScriptable* context, const rtti::Function* function, void* locals, void* params, const void* userData )
	: m_code( function->GetCode().GetCode() )
	, m_context( context )
	, m_parent( parentFrame )
	, m_level( parentFrame ? parentFrame->m_level + 1 : 0 )
	, m_function( function )
	, m_locals( reinterpret_cast< Uint8* >( locals ) )
	, m_params( reinterpret_cast< Uint8* >( params ) )
	, m_parentResult( nullptr )
#ifndef NO_SCRIPT_DEBUG
	, m_isDebugging( parentFrame ? parentFrame->m_isDebugging : false )
#endif
#ifdef USE_PROFILER
	, m_perfData( nullptr )
#endif
	, m_repContext( parentFrame ? parentFrame->m_repContext : nullptr )
	, m_lValuePtr( parentFrame ? parentFrame->m_lValuePtr : nullptr )
	, m_lValueType( parentFrame ? parentFrame->m_lValueType : nullptr )
#ifdef RED_MONITOR_SCRIPTABLE_THREAD_SAFETY
	, m_lValueProperty( parentFrame ? parentFrame->m_lValueProperty : nullptr )
	, m_lValuePropertyOwner( parentFrame ? parentFrame->m_lValuePropertyOwner : nullptr )
#endif
	, m_userData( userData ? userData : ( parentFrame ? parentFrame->m_userData : nullptr ) )
	, m_canElideCopy( false )
{
#if !defined( NO_SCRIPT_DEBUG )
	ScriptThreadDebugInfo* const info = GetOrSetupScriptThreadDebugInfo();
	if( info ) info->accessLock.Acquire();
	s_scriptThreadStackFrame = this;
	if( info ) info->accessLock.Release();
#endif

#if defined( USE_PROFILER )
	m_perfData.startExecutionTick = m_perfData.timer.GetTicks();
#endif
}

CScriptStackFrame::~CScriptStackFrame()
{
#if defined( USE_PROFILER )
	if ( auto* instrObj = m_perfData.instrObj )
	{
		gProfilers.StopBlock( instrObj, instrObj->m_name);

		if ( instrObj->m_forceChannel != PBC_NONE )
		{
			red::SwapThreadLocalPerfChannels( instrObj->m_prevChannel );
		}
	}

	if ( !m_parent )
	{
		CScriptingSystem::GetInstance().UpdateLastScriptFrameDuration( m_perfData.timer.GetTicks() - m_perfData.startExecutionTick );
	}
#endif

#if !defined( NO_SCRIPT_DEBUG )
	ScriptThreadDebugInfo* const info = GetOrSetupScriptThreadDebugInfo();
	if( info ) info->accessLock.Acquire();

	if ( s_scriptThreadStackFrame == this )
	{
		if ( CScriptStackFrame* parentFrame = s_scriptThreadStackFrame->GetParent() )
		{
			s_scriptThreadStackFrame = parentFrame;
		}
		else
		{
			s_scriptThreadStackFrame = nullptr;
		}
	}

	if( info ) info->accessLock.Release();
#endif
}

#ifndef NO_SCRIPT_DEBUG

Uint32 CScriptStackFrame::DumpRawTopToAnsiString( AnsiChar* str, Uint32 strSize ) const
{
	size_t initialLength = red::Strlen( str, strSize );

	IScriptable* contextHandle = m_context;
	if ( contextHandle && contextHandle->GetClass() && !m_function->IsStatic() )
	{
		red::Strcat( str, contextHandle->GetClass()->GetName().AsChar(), strSize );
		red::Strcat( str, "::", strSize );
	}

	red::Strcat( str, m_function->GetName().AsChar(), strSize );

	AnsiChar lineId[ 16 ];
	red::SNPrintF( lineId, RED_ARRAY_COUNT( lineId ), "(...) @%hu\n", m_debugData.GetLine() );
	red::Strcat( str, lineId, strSize );

	return static_cast< Uint32 >( red::Strlen( str, strSize ) - initialLength );
}

size_t CScriptStackFrame::DumpTopToString( char* str, Uint32 strSize ) const
{
	if ( !m_function )
	{
		return 0;
	}

	size_t initialLength = red::Strlen( str, strSize );

	if ( const rtti::ClassType* classType = m_function->GetClass() )
	{
		red::Strcat( str, classType->GetName().AsChar(), strSize );
		red::Strcat( str, "::", strSize );
	}

	red::Strcat( str, m_function->GetFamilyName().AsChar(), strSize );
	red::Strcat( str, "(...) Line ", strSize );

	char lineId[ 16 ];
	const Uint32 lineNumber = m_debugData.GetLine() + 1;
	red::SNPrintF( lineId, RED_ARRAY_COUNT( lineId ), "%u", lineNumber );

	red::Strcat( str, lineId, strSize );

	return red::Strlen( str, strSize ) - initialLength;
}

void CScriptStackFrame::DumpToLog() const
{
	char top[512] = { '\0' };
	DumpTopToString( top, RED_ARRAY_COUNT( top ) );

	RED_LOG( " Script Frame: %hs", top );

	if ( m_parent )
	{
		m_parent->DumpToLog();
	}
}

Uint32 CScriptStackFrame::DumpToString( char* str, Uint32 strSize ) const
{
	size_t bufferUsed = 0;

	red::Strcat( str, "     ", strSize );

	bufferUsed += DumpTopToString( str, strSize );

	red::Strcat( str, "\n", strSize );

	if ( m_parent )
	{
		bufferUsed += m_parent->DumpToString( str, strSize );
	}

	return static_cast< Uint32 >( bufferUsed );
}

Uint32 CScriptStackFrame::DumpRawToAnsiString( AnsiChar* stf, Uint32 strSize ) const
{
	Uint32 charsWritten = DumpRawTopToAnsiString( stf, strSize );
	if( m_parent )
	{
		charsWritten += m_parent->DumpRawToAnsiString( stf, strSize );
	}

	return charsWritten;
}

void VisitScriptCallstack( const red::ThreadId threadId, const CScriptStackFrame& frame, ScriptStackFrameVisitor visitor )
{
	const CScriptStackFrame* framePtr = &frame;
	Uint32 frameIndex = 0;
	for( ; framePtr != nullptr && frameIndex < c_maxFrameCount; framePtr = framePtr->GetParent(), ++frameIndex )
	{
		if( !framePtr->m_function )
		{
			continue;
		}

		red::AnsiChar buffer[ RED_KILO_BYTE( 1 ) ] = { '\0' };
		const Uint32 bufferSize = RED_ARRAY_COUNT( buffer );

		framePtr->DumpTopToString( buffer, bufferSize );
		if( !visitor( threadId.AsNumber(), buffer, bufferSize ) )
		{
			// in most cases it would mean that we are out of memory
			break;
		}
	}
}
#endif // NO_SCRIPT_DEBUG

void VisitScriptCallstacks( ScriptStackFrameVisitor visitor )
{
#if !defined( NO_SCRIPT_DEBUG )
	const red::ThreadId currentThreadId = red::ThreadId::CurrentThread();

	// First, visit stack frame of current thread.
	if( s_scriptThreadStackFrame )
	{
		// It is safe now, we are reading current thread, we did not crash in middle of modifying callstack we can just visit it.
		VisitScriptCallstack( currentThreadId, *s_scriptThreadStackFrame, visitor );
	}

	// Next, if available, print callstack from all available threads.
	struct ReleaseLockOnScopeExitHelper
	{
		red::SpinLock& lock;

		~ReleaseLockOnScopeExitHelper()
		{
			lock.Release();
		}
	};
	for( Uint32 i = 0; i < c_MaxScriptThreadDebugInfos; ++i )
	{
		ScriptThreadDebugInfo& scriptThreadDebugInfo = s_scriptThreadDebugInfos[ i ];

		if( !scriptThreadDebugInfo.accessLock.TryAcquire() )
		{
			// We have failed to acquire the access lock, meaning that most likely the crash happened
			// when we were in the middle of modifying the stack information for that thread.
			// Let's skip it as we may never release that lock.
			continue;
		}
		ReleaseLockOnScopeExitHelper releaseHelper{ scriptThreadDebugInfo.accessLock };

		red::ThreadId threadId = scriptThreadDebugInfo.threadId;
		if( !threadId.IsValid() )
		{
			continue;
		}

		const Bool isCurrentThread = threadId == currentThreadId;
		if( isCurrentThread )
		{
			continue;
		}

		if( scriptThreadDebugInfo.stackFrame )
		{
			CScriptStackFrame* threadStackFrame = *scriptThreadDebugInfo.stackFrame;
			if( threadStackFrame )
			{
				VisitScriptCallstack( threadId, *threadStackFrame, visitor );
			}
		}
	}
#else
	RED_TOUCH( visitor );
#endif
}
