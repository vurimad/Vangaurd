/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace red
{
	namespace err
	{
		struct PrintCallbackParams
		{
			Uint32 tlsIndex{ 0 };
			const void* thisPtr{ nullptr };
			char* buf{ nullptr };
			Uint32 bufSize{ 0 };
		};

		using PrintCallback = Bool( const PrintCallbackParams& params, Uint64& outSequence, red::ThreadId& outThreadID );

		struct RegisterCrashDataParams
		{
			const char* group{ "" };
			const char* name{ "" };
			const void* thisPtr{ nullptr };
			Uint8 maxThreads{ 0 };
			PrintCallback* callback{ nullptr };
		};
		
		REDSYSTEM_API void RegisterCrashData(const RegisterCrashDataParams& params);
		REDSYSTEM_API void DumpCrashDataForDebug();
	
		using CrashHandlerFunc = void( void* thisPtr, const RegisterCrashDataParams& params);
		struct CrashDumpHandlerParam
		{
			void* thisPtr;
			CrashHandlerFunc* registerFunc;
		};
		static_assert(std::is_pod<CrashDumpHandlerParam>::value, "");

		REDSYSTEM_API CrashDumpHandlerParam OverrideCrashHandlerRegisterFuncForUnitTests(void* thisPtr, CrashHandlerFunc* func);
	}
}
