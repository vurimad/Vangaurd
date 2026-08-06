/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "crashDataStorage.h"

#ifdef RED_USE_ERRORHANDLER
# define RED_USE_CRASHDATA
#endif

namespace red
{

namespace err { 
	struct PrintCallbackParams;
	static const Uint32 c_defaultMaxThreads = 16;
}

template< typename T, typename TCrashDataStorage >
class TCrashData final : red::NonCopyable
{
	using ThisType = TCrashData< T, TCrashDataStorage >;

public:
	// Creates crash data with unset value (won't show up in crash report)
	TCrashData(const char* group, const char* name);
	
	// Creates crash data with initial value. Convenience constructor which simply calls Set().
	TCrashData(const char* group, const char* name, const typename TCrashDataStorage::SetType& value );

	~TCrashData() = default;

	TCrashData(TCrashData&&) = delete;
	TCrashData& operator=(TCrashData&&) = delete;

	void Set( const typename TCrashDataStorage::SetType& value);
	
	void Clear(Bool allThreads = false);

private:
	static Bool PrintCallback(const err::PrintCallbackParams& params, Uint64& outSequence, red::ThreadId& outThreadID);

#ifdef RED_USE_CRASHDATA
	TCrashDataStorage m_data;
	Bool m_isConstructed; // keep this so static init to zero means uninitialized. Help catch any static order init fiasco.
#endif
};

template< typename T, Uint32 Length = 0 >
using CrashData = TCrashData< T, err::CrashDataStorage< T, Length > >;

template< typename T, Uint32 Length = 0 >
using CrashDataThreadLocal = TCrashData< T, err::CrashDataThreadLocalStorage< T, err::c_defaultMaxThreads, Length > >;

template< typename TCrashData >
struct ScopedCrashData : red::NonCopyable
{
	ScopedCrashData(TCrashData& data)
		: m_data( data )
	{}

	~ScopedCrashData()
	{
		m_data.Clear();
	}

	TCrashData& m_data;
};

} // red

#define RED_SET_SCOPED_CRASH_DATA(data, value)\
	::red::ScopedCrashData< decltype(data) > RED_CONCATENATE2( scopedCrashData, __LINE__ ){ data };\
	data.Set(value)

#include "crashData.inl"

