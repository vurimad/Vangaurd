/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace red { namespace err {


using DumpLineCallback = void(void* thisPtr, const char* formattedName, const char* formattedValue );

struct DumpCrashDataResult
{
	Uint32 numRegisteredCrashDatas{ 0 };
	Uint32 numEntriesWritten{ 0 };
	Uint32 numEntriesFailed{ 0 };
};

Bool DumpCrashData( void* thisPtr, DumpLineCallback* dumpLineCallback, DumpCrashDataResult& outResult );

void RegisterCrashDataImpl(const RegisterCrashDataParams& params);

} }
