#ifndef _SCE_CALLSTACK_H_
#define _SCE_CALLSTACK_H_

#include "libDbg.h"

namespace sce
{
	namespace callstack
	{

		// Maximum section headers to search for elf file parsing. This is used to find the symtab and strtab sections
		const uint32_t MAX_SECTION_HEADERS = 64;
		// You may need to bump the maximum symbol name up if you are noticing that your symbol names are clipped
		const uint32_t MAX_SYMBOL_NAME = 128;
		// This is the maximum amount of runtime modules, that includes system prxs. You probably don't need 256, but this is a safe number
		const uint32_t MAX_MODULES = 256;
		// Symbol window size, For a complete description look at the documention or in the source code for how it's used
		const uint32_t SYMBOL_WINDOW_SIZE = 2731; // ends up with a buffer a little bigger than 64K
		// The maximum amount of calls reported, since we go from top down, it'll include most of the context for the call, but if you need more you can bump this number up
		const uint32_t MAX_CALLSTACK_DEPTH = 32;

		enum SCE_CALLSTACK_SYMBOL_LOCATION
		{
			SCE_CALLSTACK_SYMBOLS_LOCATION_None,
			SCE_CALLSTACK_SYMBOLS_LOCATION_Map,
			SCE_CALLSTACK_SYMBOLS_LOCATION_Elf
		};

		/**  Outputs the current callstack to tty

		@param[in]	user_channel	TTY channel to output to

		@param[in]	location		Symbols search location.

		@param[in]	numToSkip		number of stack levels to skip

		@param[in]	maxLevels		maximum number of stack levels to add

		*/
		void DumpCallstackToTTY(SceDbgUserChannel user_channel, SCE_CALLSTACK_SYMBOL_LOCATION location = SCE_CALLSTACK_SYMBOLS_LOCATION_Elf, uint32_t numToSkip=0, uint32_t maxLevels=MAX_CALLSTACK_DEPTH);


		/**  Outputs the current callstack to a buffer

		@param[in]	pBuffer			buffer to receive the text

		@param[in]	bufSize			size of the buffer

		@param[in]	location		Symbols search location.

		@param[in]	numToSkip		number of stack levels to skip

		@param[in]	maxLevels		maximum number of stack levels to add

		@return						number of bytes written to the buffer

		*/
		int DumpCallstackToBuffer(char* pBuffer, uint32_t bufSize, SCE_CALLSTACK_SYMBOL_LOCATION location = SCE_CALLSTACK_SYMBOLS_LOCATION_Elf, uint32_t numToSkip=0, uint32_t maxLevels=MAX_CALLSTACK_DEPTH);


		int GetCallstackStaticDataSize();
	}
}
#endif //_SCE_CALLSTACK_H_