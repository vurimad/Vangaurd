/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "errorHandlerImplCrashDumpData.h"
#include "../include/scopedPtr.h"

namespace red { namespace err {

static const Uint32 c_maxCrashDumpDataEntries = 65536;
static const Uint32 c_maxValueBufferSize = 1024 + 64; // extra for metadata like "...#<truncated>#, but requires to keep within the 1k limit normally
static const Uint32 c_maxNameBufferSize = 128;

struct CrashDumpDataEntry
{
	const char* group;
	const char* name;
	const void* thisPtr;
	PrintCallback* callback;
	Uint8 maxThreads;
};

static_assert(std::is_pod< CrashDumpDataEntry >::value, "");

struct CrashDumpDataTable
{
	CrashDumpDataEntry m_entries[c_maxCrashDumpDataEntries];
	atomic::TAtomic32 m_numOverflowEntriesForDebug;
};

static_assert(std::is_pod< CrashDumpDataEntry >::value, "");

static atomic::TAtomic32 gCrashDumpTableIndexAllocator = 0;
static atomic::TAtomic32 gCrashDumpTableInitializedIndex = -1;

static red::ScopedPtr< CrashDumpDataTable > gCrashDumpDataTable;

void RegisterCrashDataImpl(const RegisterCrashDataParams& params)
{
	if ( !gCrashDumpDataTable )
	{
		void* ptr = ::malloc( sizeof( CrashDumpDataTable ) );
		gCrashDumpDataTable.Reset( ::new ( ptr ) CrashDumpDataTable );
	}

	const Int32 index = atomic::Increment32(&gCrashDumpTableIndexAllocator) - 1;
	if (index < c_maxCrashDumpDataEntries)
	{
		gCrashDumpDataTable->m_entries[index] = { params.group, params.name, params.thisPtr, params.callback, params.maxThreads };
		const Int32 prevIndex = index - 1;
		while (atomic::CompareExchange32(&gCrashDumpTableInitializedIndex, index, prevIndex ) != prevIndex )
		{
			red::YieldCurrentThread();
		}
	}
}

Bool DumpCrashData(void* thisPtr, DumpLineCallback* dumpLineCallback, DumpCrashDataResult& outResult)
{
	outResult = DumpCrashDataResult{};

	if (!thisPtr || !dumpLineCallback)
	{
		return false;
	}

	const red::ProfileTimer timer;

	const auto initializedIndexSnapshot = const_cast<volatile atomic::TAtomic32 &>(gCrashDumpTableInitializedIndex);

	char nameBuf[c_maxNameBufferSize] = "";
	char valueBuf[c_maxValueBufferSize] = "";
	const Uint32 nameBufSize = RED_ARRAY_COUNT_U32(nameBuf);
	const Uint32 valueBufSize = RED_ARRAY_COUNT_U32(valueBuf);

	if (gCrashDumpDataTable->m_numOverflowEntriesForDebug > 0)
	{
		valueBuf[0] = '\0';
		red::SNPrintFUnsafe(valueBuf, RED_ARRAY_COUNT_U32(valueBuf), "%u", gCrashDumpDataTable->m_numOverflowEntriesForDebug);
		valueBuf[valueBufSize - 1] = '\0';
		dumpLineCallback(thisPtr, "##CrashDump##/NumCrashDumpTableOverflowEntriesForDebug", valueBuf);
	}

	outResult.numRegisteredCrashDatas = initializedIndexSnapshot + 1;

	for (Int32 i = 0; i <= initializedIndexSnapshot; ++i)
	{
		// #tbd: optimize: could register some "skip control" pointer to check if no threads are ready
		const auto& entry = gCrashDumpDataTable->m_entries[i];
		for (Uint32 tlsIndex = 0; tlsIndex < entry.maxThreads; ++tlsIndex)
		{
			// Don't trust the callback; ensure empty if callback returned an empty string
			nameBuf[0] = '\0';
			valueBuf[0] = '\0';

			PrintCallbackParams dataParams;
			{
				dataParams.tlsIndex = tlsIndex;
				dataParams.buf = valueBuf;
				dataParams.bufSize = valueBufSize;
				dataParams.thisPtr = entry.thisPtr;
			}

			Uint64 sequence = 0;
			red::ThreadId threadID;
			if (entry.callback(dataParams, sequence, threadID ))
			{
				// Don't trust the callback; ensure always null terminated
				nameBuf[nameBufSize-1] = '\0';
				valueBuf[valueBufSize- 1] = '\0';

				red::SNPrintFUnsafe(nameBuf, RED_ARRAY_COUNT_U32(nameBuf), "%s/%s@%llu#TID=%u", entry.group, entry.name, sequence, threadID);
				dumpLineCallback(thisPtr, nameBuf, valueBuf);
				outResult.numEntriesWritten += 1;
			}
			else
			{
				// #todo: not so much failed, but supposedly had no data... need to distinguish later
				outResult.numEntriesFailed += 1;
			}
		}
	}

	{
		const Uint64 seconds = timer.GetDeltaSec();
		valueBuf[0] = '\0';
		red::SNPrintFUnsafe(valueBuf, RED_ARRAY_COUNT_U32(valueBuf), "%llu", seconds);
		valueBuf[valueBufSize - 1] = '\0';
		dumpLineCallback(thisPtr, "##CrashDump##/DumpCrashDataSeconds", valueBuf);
	}

	return true;
}

} }
