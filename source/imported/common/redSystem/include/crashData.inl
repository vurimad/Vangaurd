/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "crashDataRegistration.h"

#if defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_DURANGO )
#define DEBUG_FRAME_ADDR() reinterpret_cast<Uint64>( _AddressOfReturnAddress() )
#elif defined( RED_PLATFORM_ORBIS ) || defined( RED_PLATFORM_LINUX )
// #tbd: not exactly the same, clang Win32 emulation adds sizeof(void*)
#define DEBUG_FRAME_ADDR() reinterpret_cast< Uint64 >( __builtin_frame_address( 0 ) );
#else
# error Undefined platform!
#endif

#define RED_CRASHDATA_STATIC_INIT_ORDER_ERRMSG "Static initialization order fiasco detected. Possible fix: red::CrashData<...>& GetMyCrashData() { static red::CrashData<...> data{ \"MyGroup\", \"MyName\" }; return data; }"

namespace red
{

template< typename T, typename TCrashDataStorage >
TCrashData<T, TCrashDataStorage>::TCrashData(const char* group, const char* name)
#ifdef RED_USE_CRASHDATA
	: m_isConstructed( false )
#endif
{
#ifdef RED_USE_CRASHDATA
	RED_FATAL_ASSERT(group && group[0]);
	RED_FATAL_ASSERT(name && name[0]);

	const Uint64 thisAddr = reinterpret_cast<Uint64>(this);
	const Uint64 stackAddr = DEBUG_FRAME_ADDR();

	// #tbd: quick heuristic vs using heavier functions; try not to make too many assumptions about exact layout, but ABI should generally let us
	// Could check if in dynamic memory too
	const Uint64 pageSize = 4096;
	const Bool isThisOnStack = (thisAddr - stackAddr) < pageSize || (stackAddr - thisAddr) < pageSize;

# ifdef RED_PLATFORM_ORBIS
	if (isThisOnStack)
	{
		void* startAddr = nullptr;
		void* endAddr = nullptr;
		if (::sceKernelIsStack( this, &startAddr, &endAddr ) == SCE_OK && !startAddr && !endAddr )
		{
			RED_FATAL("sceKernelIsStack() disagrees. Fix stack check heuristic");
		}
	}
# endif

	RED_FATAL_ASSERT(!isThisOnStack, "CrashData should have static storage duration");

	err::RegisterCrashDataParams params;
	{
		params.group = group;
		params.name = name;
		params.thisPtr = this;
		params.maxThreads = static_cast< Uint8 >( TCrashDataStorage::GetMaxThreads() );
		params.callback = &PrintCallback;
	}
	err::RegisterCrashData(params);

	m_isConstructed = true;
#endif
}

template< typename T, typename TCrashDataStorage >
TCrashData<T, TCrashDataStorage>::TCrashData(const char* group, const char* name, const typename TCrashDataStorage::SetType& value)
	: TCrashData( group, name )
{
	Set( value );
}

template< typename T, typename TCrashDataStorage >
void TCrashData<T, TCrashDataStorage>::Set(const typename TCrashDataStorage::SetType& value)
{
#ifdef RED_USE_CRASHDATA
	RED_FATAL_ASSERT(m_isConstructed, RED_CRASHDATA_STATIC_INIT_ORDER_ERRMSG);
	m_data.Set(value);
#endif
}

template< typename T, typename TCrashDataStorage >
void TCrashData<T, TCrashDataStorage>::Clear(Bool allThreads /*= false*/)
{
#ifdef RED_USE_CRASHDATA
	RED_FATAL_ASSERT(m_isConstructed, RED_CRASHDATA_STATIC_INIT_ORDER_ERRMSG);
	m_data.Clear(allThreads);
#endif
}

template< typename T, typename TCrashDataStorage >
Bool TCrashData<T, TCrashDataStorage>::PrintCallback(const err::PrintCallbackParams& params, Uint64& outSequence, red::ThreadId& outThreadID)
{
#ifdef RED_USE_CRASHDATA
	auto* self = static_cast<const ThisType*>(params.thisPtr);
	return self->m_data.Print(params.tlsIndex, params.buf, params.bufSize, outSequence, outThreadID);
#else
	return false;
#endif
}

}

