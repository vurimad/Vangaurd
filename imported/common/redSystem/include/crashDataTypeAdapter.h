/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include <new>
#include "threads.h"
#include "typetraits.h"

namespace red
{

namespace err
{
	enum class CrashDataCopyResult
	{
		Success,
		Error,
		Truncated,
	};

	template< typename T, Uint32 Length >
	struct CrashDataTypeAdapter
	{
		static_assert(std::is_pod< T >::value || TAllowUseAsPOD< T >::Value, "You can use default CrashDataTypeAdapter only with POD types");

		static_assert(Length == 0, "Length is inapplicable; only default value allowed");

		using StorageType = T;
		using SetType = StorageType;

		static CrashDataCopyResult Copy(StorageType& storage, const SetType& value)
		{
			// #tbd: or use assignment, should be all trivial with no cleanup if POD anyway
			::new(&storage) StorageType(value);
			return CrashDataCopyResult::Success;
		}

		// Don't care how much written or if successful, crashdump handler should null buffer and verify some null terminator/length
		// don't trust external code to be correct or consistent
		static Bool Print(char* buffer, Uint32 bufferLen, const StorageType& value)
		{
			// Call ToBuffer and GetFormatString without any namespace qualification so it will use ADL
			// At least so far it's the easiest way to be consistent since clang is stricter than MSVC
			// and will often give a misleading error message if you try to use red::ToBuffer()
			// So the x::ToBuffer() should be in the namespace of type T
			Int32 written = -1;
			return ToBuffer(buffer, bufferLen, value, written, GetFormatString(value)) && written > -1;
		}
	};

	const constexpr Uint32 c_defaultCharArrayLength = 16;

	template< Uint32 Length >
	constexpr Uint32 DefaultCrashDataCharArrayLength()
	{
		using SizeType = typename std::conditional<Length == 0, std::integral_constant<Uint32, c_defaultCharArrayLength >, std::integral_constant<Uint32, Length>>::type;
		return SizeType::value;
	}

	template<Uint32 Length>
	struct CrashDataTypeAdapter< const char*, Length >
	{
		static_assert(Length <= 1024, "Length too long");
		using StorageType = char[DefaultCrashDataCharArrayLength<Length>()];
		using SetType = const char*;

		static CrashDataCopyResult Copy(StorageType& storage, const char* value)
		{
			const Uint32 destSize = RED_ARRAY_COUNT_U32(storage);
			if (!red::Strcpy(storage, value, destSize, destSize - 1))
			{
				return CrashDataCopyResult::Error;
			}
			const Uint32 srcLen = (Uint32)red::Strlen(value);
			return srcLen + 1 <= destSize ? CrashDataCopyResult::Success : CrashDataCopyResult::Truncated;
		}

		static Bool Print(char* buffer, Uint32 bufferLen, const char* value)
		{
			return red::Strcpy(buffer, value, bufferLen);
		}
	};

	template<Uint32 Length>
	struct CrashDataTypeAdapter< const wchar_t*, Length >
	{
		static_assert(Length <= 1024, "Length too long");
		using StorageType = wchar_t[DefaultCrashDataCharArrayLength<Length>()];
		using SetType = const wchar_t*;

		static CrashDataCopyResult Copy(StorageType& storage, const wchar_t* value)
		{
			const Uint32 destSize = RED_ARRAY_COUNT_U32(storage);
			if (!red::Strcpy(storage, value, destSize, destSize - 1))
			{
				return CrashDataCopyResult::Error;
			}
			const Uint32 srcLen = (Uint32)red::Strlen(value);
			return srcLen + 1 <= destSize ? CrashDataCopyResult::Success : CrashDataCopyResult::Truncated;
		}

		static Bool Print(char* buffer, Uint32 bufferLen, const wchar_t* value)
		{
			red::WideCharToStdChar_NoConv(buffer, value, bufferLen);
			return true;
		}
	};

	template< size_t N >
	using StringLiteralArray = char(&)[N];

	struct CrashDataTypeNullCopier
	{
		template <Uint32 N>
		static CrashDataCopyResult Copy( StringLiteralArray<N>& storage )
		{
			static_assert(N > 0, "");
			const char* nullTxt = "<null>";
			const Uint32 destSize = N;
			if (!red::Strcpy(storage, nullTxt, destSize, destSize-1))
			{
				return CrashDataCopyResult::Error;
			}

			const Uint32 srcLen = (Uint32)red::Strlen(nullTxt);
			return srcLen + 1 <= destSize ? CrashDataCopyResult::Success : CrashDataCopyResult::Truncated;

			return CrashDataCopyResult::Success;
		}
	};
}
} // red
