/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "crashDataTypeAdapter.h"

namespace red
{

namespace err
{
	REDSYSTEM_API Uint64 NextCrashValueSequence();

	enum class CrashDataState : Uint8
	{
		Cleared,
		Changing,
		Ready,
		Error,
		Truncated,
	};

	namespace helper
	{
		RED_INLINE CrashDataState GetStateFromCopyResult(CrashDataCopyResult result)
		{
			switch (result)
			{
			case CrashDataCopyResult::Error:		return CrashDataState::Error;
			case CrashDataCopyResult::Success:		return CrashDataState::Ready;
			case CrashDataCopyResult::Truncated:	return CrashDataState::Truncated;
			default:
				break;
			}

			RED_FATAL("Unexpected CrashDataCopyResult: %u", (Uint32)result);
			return CrashDataState::Error;
		}

		template< typename CrashDataTypeAdapter, typename StorageType >
		RED_INLINE Bool PrintToBuffer(CrashDataState state, const StorageType& value, char* buffer, Uint32 bufferSize)
		{
			if (state == CrashDataState::Cleared)
			{
				return false;
			}

			if (state == CrashDataState::Changing)
			{
				red::Strcpy(buffer, "#<changing>#", bufferSize);
			}
			else if (state == CrashDataState::Error)
			{
				red::Strcpy(buffer, "#<copy error>#", bufferSize);
			}
		
			if (CrashDataTypeAdapter::Print(buffer, bufferSize, value))
			{
				if (state == CrashDataState::Truncated)
				{
					red::Strcat(buffer, "...#<truncated>#", bufferSize);
				}
			}
			else
			{
				// Overwrite whatever it might have written
				red::Strcpy(buffer, "#<print error>#", bufferSize);
			}
			return true;
		}
	}

	template< typename T, Uint32 Length >
	class CrashDataStorage
	{
	public:
		using CrashDataTypeAdapter = CrashDataTypeAdapter< T, Length >;
		using StorageType = typename CrashDataTypeAdapter::StorageType;
		using SetType = typename CrashDataTypeAdapter::SetType;
		static_assert(!std::is_reference< StorageType >::value, "");
		static_assert(!std::is_reference< SetType >::value, "");

		static constexpr Uint32 GetMaxThreads() { return 1; }

		CrashDataStorage()
			: m_sequence(0)
			, m_state(CrashDataState::Cleared)
		{
		}

		void Set(const SetType& value)
		{
			m_state = CrashDataState::Changing;
			//red::Memzero(m_buffer, sizeof(m_buffer));
			// or call destructor... although shouldn't have to! but if do then should default init to sth...
			auto& storage = reinterpret_cast<StorageType&>(m_buffer);
			const auto result = CrashDataTypeAdapter::Copy(storage, value);
			m_sequence = NextCrashValueSequence();
			m_state = helper::GetStateFromCopyResult(result);
		}

		void Clear(Bool allThreads)
		{
			RED_UNUSED(allThreads);
			m_state = CrashDataState::Cleared;
		}

		Bool Print(Uint32 tlsIndex, char* buf, Uint32 bufSize, Uint64& outSequence, red::ThreadId& outThreadID) const
		{
			if ( tlsIndex >= GetMaxThreads() )
			{
				return false;
			}

			const Uint64 sequence = m_sequence;
			const auto& value = reinterpret_cast<const StorageType&>(m_buffer);
			if (helper::PrintToBuffer<CrashDataTypeAdapter>(m_state, value, buf, bufSize))
			{
				outSequence = sequence;
				outThreadID = red::ThreadId{ 0 };
				return true;
			}
			return false;
		}

	private:
		typename std::aligned_storage< sizeof(StorageType), alignof(StorageType)>::type m_buffer;
		Uint64 m_sequence;
		CrashDataState m_state;
	};

	// Note: not using actual dynamic TLS APIs
	template< typename T, Uint32 MaxThreads, Uint32 Length >
	class CrashDataThreadLocalStorage
	{
	public:
		using CrashDataTypeAdapter = CrashDataTypeAdapter< T, Length >;
		using StorageType = typename CrashDataTypeAdapter::StorageType;
		using SetType = typename CrashDataTypeAdapter::SetType;
		static_assert(!std::is_reference< StorageType >::value, "");
		static_assert(!std::is_reference< SetType >::value, "");

		static constexpr Uint32 GetMaxThreads() { return MaxThreads; }

		CrashDataThreadLocalStorage()
			: m_threadIdIndexMap()
			, m_sequenceSlots()
			, m_stateSlots()
			, m_numOverflowThreads(0)
		{}

		void Set(const SetType& value)
		{
			const auto threadID = red::ThreadId::CurrentThread();
			Int32 tlsIndex = FindTLSIndex(threadID);
			if (tlsIndex < 0)
			{
				tlsIndex = AllocTLSIndex(threadID);
			}

			if (tlsIndex < 0)
			{
				atomic::Increment32(&m_numOverflowThreads);
				return;
			}

			m_stateSlots[tlsIndex] = CrashDataState::Changing;
			auto& storage = reinterpret_cast<StorageType&>(m_bufferSlots[tlsIndex]);
			const auto result = CrashDataTypeAdapter::Copy(storage, value);
			m_sequenceSlots[tlsIndex] = NextCrashValueSequence();
			m_stateSlots[tlsIndex] = helper::GetStateFromCopyResult(result);
		}

		void Clear(Bool allThreads)
		{
			if (allThreads)
			{
				for (Uint32 i = 0; i < RED_ARRAY_COUNT_U32(m_stateSlots); ++i)
				{
					m_stateSlots[i] = CrashDataState::Cleared;
				}
			}
			else
			{
				const auto threadID = red::ThreadId::CurrentThread();
				const Int32 tlsIndex = FindTLSIndex(threadID);
				if (tlsIndex >= 0)
				{
					m_stateSlots[tlsIndex] = CrashDataState::Cleared;
				}
			}
		}

		Bool Print(Uint32 tlsIndex, char* buf, Uint32 bufSize, Uint64& outSequence, red::ThreadId& outThreadID) const
		{
			if (tlsIndex >= GetMaxThreads())
			{
				return false;
			}

			const Uint64 sequence = m_sequenceSlots[tlsIndex];
			const auto& value = reinterpret_cast<const StorageType&>(m_bufferSlots[tlsIndex]);
			if (helper::PrintToBuffer<CrashDataTypeAdapter>(m_stateSlots[tlsIndex], value, buf, bufSize))
			{
				outSequence = sequence;
				outThreadID = m_threadIdIndexReverseMap[tlsIndex];
				return true;
			}
			return false;
		}

	private:
		Int32 FindTLSIndex(red::ThreadId threadID) const
		{
			for (Uint32 i = 0; i < RED_ARRAY_COUNT_U32(m_threadIdIndexMap); ++i)
			{
				if (m_threadIdIndexMap[i] == threadID.id)
				{
					return i;
				}
			}
			return -1;
		}

		Int32 AllocTLSIndex(red::ThreadId threadID)
		{
			for (Uint32 i = 0; i < RED_ARRAY_COUNT_U32(m_threadIdIndexMap); ++i)
			{
				if (m_threadIdIndexMap[i] == 0 && atomic::CompareExchange32(&m_threadIdIndexMap[i], threadID.id, 0) == 0)
				{
					m_threadIdIndexReverseMap[i] = threadID;
					return i;
				}
			}
			return -1;
		}

		typename std::aligned_storage< sizeof(StorageType), alignof(StorageType)>::type m_bufferSlots[MaxThreads];
		atomic::TAtomic32 m_threadIdIndexMap[MaxThreads];
		red::ThreadId m_threadIdIndexReverseMap[MaxThreads];
		Uint64 m_sequenceSlots[MaxThreads];
		CrashDataState m_stateSlots[MaxThreads];
		atomic::TAtomic32 m_numOverflowThreads;
	};
}
}