/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "../../../external/oodle/include/oodle2.h"

#ifndef NO_EDITOR
# include "../../../external/oodle/include/oodle2x.h"
#endif

#include "wrapperKraken.h"

namespace compression
{
	namespace kraken
	{
		const red::Uint32 KRAKEN_MAGIC = 'KRAK';

		// Decode on stack + leeway: 
		// Worst case for OodleLZDecoder_MemorySizeNeeded returns 442552 for Kraken
		// Depending on our data raw len, it could be significantly smaller, but we should have the stack space to spare for now
		const Uint32 c_decoderMemorySizeBytes = 512 * 1024;

		const auto c_hcCompressionLevel = OodleLZ_CompressionLevel_Optimal;

		RADDEFFUNC rrbool OODLE_CALLBACK OodleDisplayAssertionCallback(const char* file, const int line, const char * function, const char * message)
		{
			::red::alwaysEnabledFatalAssert(file, line, (function ? function : "<Oodle: No Function>"), (message ? message : "<Oodle: No Message>"));
			return false;
		}

		// Note: if these are actually called, then it's unexpected but this is a fallback to crashing or calling clib malloc.
		// OodleX installs its own memory hooks, but we're only using that in the editor/cmdlets.
		RADDEFFUNC void* OODLE_CALLBACK OodleMallocAligned( SINTa bytes, S32 alignment )
		{
			ALWAYSENABLED_RED_FATAL("OodleMallocAligned called unexpectedly. Are we using an API we can pass stack memory too instead?");
			return RED_ALLOCATE_ALIGNED( red::PoolEngine, bytes, alignment );
		}

		RADDEFFUNC void OODLE_CALLBACK OodleFree( void* ptr )
		{
			RED_FREE( red::PoolEngine, ptr );
		}

		namespace helper
		{
			static red::RWSpinLock g_initOodleLock;
			static Bool g_isOodleInitialized = false;

#ifndef NO_EDITOR
			static void ShutdownOodleX()
			{
				OodleX_Shutdown();
			}
#endif

			static void EnsureOodleInit()
			{
				// Don't block if don't need to
				{
					RED_SCOPE_SHARED_LOCK(g_initOodleLock);
					if (g_isOodleInitialized)
					{
						return;
					}
				}

				RED_SCOPE_LOCK(g_initOodleLock);
				if (!g_isOodleInitialized)
				{
#ifndef NO_EDITOR
					OodleXInitOptions opts;
					if (!OodleX_Init_GetDefaults( OODLE_HEADER_VERSION, &opts))
					{
						ALWAYSENABLED_RED_FATAL( "Oodle header version mismatch");
					}

					// Can change opts here

					if (!OodleX_Init(OODLE_HEADER_VERSION, &opts))
					{
						ALWAYSENABLED_RED_FATAL( "OodleX_Init failed");
					}

					atexit( &ShutdownOodleX );
#else
					// OodleX installs its own memory plugin!
					OodleCore_Plugins_SetAllocators( &OodleMallocAligned, &OodleFree );
#endif // NO_EDITOR

					OodleCore_Plugins_SetAssertion( &OodleDisplayAssertionCallback );

					const Int32 decoderMemorySizeNeeded = OodleLZDecoder_MemorySizeNeeded(OodleLZ_Compressor_Kraken, -1);
					ALWAYSENABLED_RED_FATAL_ASSERT(c_decoderMemorySizeBytes >= decoderMemorySizeNeeded, "decoderMemorySizeBytes: %d < needed: %d", c_decoderMemorySizeBytes, decoderMemorySizeNeeded);

					g_isOodleInitialized = true;
				}
			}

			static Uint32 CompressKrakenDataFastest(const void* data, const red::Uint64 size, void* compBuf, OodleLZ_CompressionLevel level)
			{
				EnsureOodleInit();

	#ifndef NO_EDITOR
				// Just use sync. We compress on our own threads, and we can't change that now.
				const Int64 compSize = OodleLZ_Compress(OodleLZ_Compressor_Kraken, (const char*)data, (int)size, (char*)compBuf, level);
				//const Int64 compSize = OodleXLZ_Compress_AsyncAndWait(OodleXAsyncSelect_Full, OodleLZ_Compressor_Kraken, (const char*)data, (int)size, (char*)compBuf, level);			
	#else
				const Int64 compSize = OodleLZ_Compress(OodleLZ_Compressor_Kraken, (const char*)data, (int)size, (char*)compBuf, level);
	#endif
				return compSize >= 0 ? (Uint32)compSize : 0;
			}

		} // helper

		Uint32 GetMagic()
		{
			return KRAKEN_MAGIC;
		}

		static ResultBufferPtr CompressDataInternal(const void* data, const red::Uint64 size, TCompressionAllocator allocator, OodleLZ_CompressionLevel level )
		{
			RED_FATAL_ASSERT(data != nullptr, "Invalid parameter");
			RED_FATAL_ASSERT(size != 0, "Invalid parameter");

			// estimate memory needed
			const auto headerSize = 2 * sizeof(red::Uint32);
			const auto maxSpaceRequired = headerSize + OodleLZ_GetCompressedBufferSizeNeeded((int)size);

			// Allocate a buffer for writing compressed data to
			auto ret = allocator(maxSpaceRequired);
			RED_ASSERT(ret != nullptr, "Out of memory when allocating buffer for data compression (required size: %llu bytes)", maxSpaceRequired);

			if (ret)
			{
				// compress the data
				const auto compressedSize = helper::CompressKrakenDataFastest( (const char*)data, (int)size, (char*)ret->GetData() + headerSize, level );
				RED_ASSERT(compressedSize != 0, "Internal error in Kraken compression");
				if (compressedSize != 0)
				{
					// write the data header (Oodle is not tracking size of the uncompressed data by itself)
					auto* headerPtr = (red::Uint32*) ret->GetData();
					headerPtr[0] = KRAKEN_MAGIC;
					headerPtr[1] = static_cast<Uint32>(size);

					// patch size and return the buffer
					const auto totalSize = headerSize + compressedSize;
					ret->PatchDataSize(totalSize);
					return ret;
				}
			}

			// Error condition, in case memory was allocated it will be freed
			return ResultBufferPtr();
		}

		ResultBufferPtr CompressData(const void* data, const red::Uint64 size, TCompressionAllocator allocator)
		{
			return CompressDataInternal( data , size, allocator, OodleLZ_CompressionLevel_Normal );
		}

		ResultBufferPtr CompressDataHC(const void* data, const red::Uint64 size, TCompressionAllocator allocator)
		{
			return CompressDataInternal( data, size, allocator, c_hcCompressionLevel );
		}

		ResultBufferPtr DecompressData(const void* data, const red::Uint64 size, TCompressionAllocator allocator)
		{
			ALWAYSENABLED_RED_FATAL_ASSERT(data != nullptr, "Invalid parameter");
			ALWAYSENABLED_RED_FATAL_ASSERT(size != 0, "Invalid parameter");

			// we need at least the header :)
			const auto headerSize = 2 * sizeof(red::Uint32);
			if (size >= headerSize)
			{
				// validate header
				const auto* headerPtr = (const red::Uint32*) data;
				if (headerPtr[0] == KRAKEN_MAGIC )
				{
					// make sure we have enough data in the incoming buffer
					const auto decompressedSize = headerPtr[1];

					// allocate output buffer
					auto ret = allocator(decompressedSize);
					ALWAYSENABLED_RED_FATAL_ASSERT(ret != nullptr, "Out of memory when allocating buffer for data decompression (required size: %llu bytes)", decompressedSize);

					// decompress
					// Use fuzz-safe: claimed to be as fast.
					// According to the docs older compressors weren't fuzz-safe so would fail, but Kraken ought to have it set to 'yes'.
					if (ret != nullptr)
					{
						const auto compressedSize = size - headerSize;

						helper::EnsureOodleInit();

						// Avoid dynamic allocations when decompressing. Note this is not the final output buffer.
						const Uint32 electricFenceMagic = 0xdeadbeef;
						char stackBuffer[ c_decoderMemorySizeBytes + 2 * sizeof(Uint32)];
						
						const Uint32 startElectricFenceIndex = 0;
						const Uint32 endElectricFenceIndex = sizeof(stackBuffer) - sizeof(Uint32);

						*(Uint32*)&stackBuffer[startElectricFenceIndex] = electricFenceMagic;
						*(Uint32*)&stackBuffer[endElectricFenceIndex] = electricFenceMagic;

						char* decoderBuf = stackBuffer + sizeof(Uint32);

						const auto decompressedBytesOutput = OodleLZ_Decompress(
							(const char*)data + headerSize,
							compressedSize,
							(char*)ret->GetData(),
							(int)decompressedSize,
							OodleLZ_FuzzSafe_Yes, OodleLZ_CheckCRC_Yes,
							OodleLZ_Verbosity_None, nullptr, 0, nullptr, nullptr,
							decoderBuf, c_decoderMemorySizeBytes
						);

						ALWAYSENABLED_RED_FATAL_ASSERT(*(const Uint32*)&stackBuffer[startElectricFenceIndex] == electricFenceMagic, "Buffer overrun detected!");
						ALWAYSENABLED_RED_FATAL_ASSERT(*(const Uint32*)&stackBuffer[endElectricFenceIndex] == electricFenceMagic, "Buffer overrun detected!");
							
						ALWAYSENABLED_RED_FATAL_ASSERT(decompressedBytesOutput == decompressedSize, "Interal Kraken decompression error, code: %d", decompressedBytesOutput);
						if (decompressedBytesOutput == decompressedSize)
						{
							return ret;
						}
					}
				}
			}

			// no data decompressed
			return ResultBufferPtr();
		}

	} // lz4

} // red

