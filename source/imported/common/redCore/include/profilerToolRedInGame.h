/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

//////////////////////////////////////////////////////////////////////////
// headers
#include "profiler.h"
#include "../../redSystem/include/threads.h"
#include "../../redMemory/include/linearAllocator.h"
#include "../../redMath/include/redMathPublic.h"

namespace red
{
#ifdef USE_RED_INGAME_PROFILER

	struct InstrumentationObject;


	namespace SampleType
	{
		enum Main : Uint8
		{
			Group = 0,
			Point,
			Regular,
			Count,
		};

		enum Sub : Uint8
		{
			None = 0,
			Begin,
			End,
		};
	};

	class REDCORE_API RedInGameProfilerTool
	{
	public:
		struct Sample
		{
			Uint64		m_start;		// 8
			Uint64		m_end;			// 8

			const InstrumentationObject*	m_block;		// 8
			const char*						m_scopeName;	// 8

			math::Color	m_color;			// 4

			Uint8			  m_level;		// 1
			Uint8			  m_thread;		// 1
			SampleType::Main  m_type;		// 1
			SampleType::Sub	  m_subType;	// 1
		};

	public:
		static const Uint32 kProfilerThreadInactive = 0xFFFFFFFF;

	public:
		RedInGameProfilerTool();
		~RedInGameProfilerTool();

		// common
		void Init( const Uint32 mem );
		void InitThread( const AnsiChar *name, Uint32 maxNumSamples );
		void Shutdown();
		void Update();

		// end frame markers
		void NextFrame( red::ProfilerFrameType frameType );

		// control
		void Start();
		void Stop();

		// blocks
		RED_INLINE void StartBlock( red::InstrumentationObject* block, const char* scopeName );
		RED_INLINE void StopBlock( red::InstrumentationObject* block, const char* scopeName );
		RED_INLINE void BeginGroup( red::InstrumentationObject* block );
		RED_INLINE void EndGroup ( red::InstrumentationObject* block );
		RED_INLINE void PutSyncPoint( red::InstrumentationObject* block );

		const AnsiChar* GetThreadName( Uint32 threadIndex ) const { return m_threads[threadIndex].m_name; }

		// the below 4 functions should be only called from the consuming thread
		// they are not MT safe - they are fine being called with everything else
		// running and generating samples but there are not MT safe with respect
		// to each other in any way
		void FreeFrame();
		Uint32 GetNumFramesReady() const;

		void GetLastFrame( Uint64 &startTime, Uint64 &endTime ) const;
		Uint32 CopyNewSamples( Uint64 endTime, Sample *dest0, Uint32 dest0Size, Sample *dest1, Uint32 dest1Size );

		static inline Uint32 GetProfilerThreadIndex();

	private:
		struct Samples
		{
			RED_INLINE void	Init( Sample* memory, Uint32 numSamples );
			RED_INLINE Sample& GetSample( Uint32 sampleIndex );
			RED_INLINE const Sample& GetSample( Uint32 sampleIndex ) const;

		public:
			Sample *m_samples;
			Uint32 m_numSamples;
		};

		RED_ALIGNED_STRUCT( SampleStack, 64 )
		{
			RED_INLINE void Init();

			RED_INLINE Uint32 PushSample();
			RED_INLINE void	PopSample();

			RED_INLINE Sample& GetTopSample();
			RED_INLINE Uint32 GetSize() const;

			RED_INLINE bool Overflow() const;

		private:
			static const int kMaxSampleDepth = 10;

			Uint8 m_stackMem[kMaxSampleDepth * sizeof( Sample )];
			Samples	m_stackSamples;

			Uint32 m_top;
			Uint32 m_overflowTop;
		};

		struct SampleAllocator
		{
			RED_INLINE void Init( Uint32 poolSize );

			// two stage alloc for thread safety
			// PreAllocate checks for space, returns the index of the sample to fill
			// CommitAlloc does mem barrier and advances the write ptr
			RED_INLINE Uint32 PreAllocate();
			RED_INLINE void CommitAlloc( Uint32 alloc );

			RED_INLINE void Free( const Samples &samples, Uint64 endTime );

			RED_INLINE Uint32 FindLastSample( const Samples &samples, Uint32 rangeStart, Uint32 rangeEnd, Uint64 time ) const;

			RED_INLINE Uint32 CopySamples( const Samples &samples, Uint64 time, Sample *dest0, Uint32 dest0Size, Sample *dest1, Uint32 dest1Size ) const;

			RED_INLINE Uint32 ToSampleIndex( Uint32 alloc ) const;

		private:
			Uint32 m_poolSize;
			Uint32 m_mask;

			volatile Uint32	m_writePtr;
			Uint32 m_readPtr;
		};

		struct Frame
		{
			Uint64 m_start;
			Uint64 m_end;
		};

		struct Frames
		{
			static const Uint32 kMaxFrameCount = 16;
			static_assert( red::IsPowerOf2( kMaxFrameCount ), "kMax frame count needs to be pow-2" );

			static const Uint32 kFrameIndexMask = kMaxFrameCount - 1;

			void Init();

			void NextFrame();
			Uint64 FreeFrame();

			Uint32 GetNumFrames() const;

			void GetLastFrame( Uint64 &startTime, Uint64 &endTime ) const;

		private:
			Frame m_frames[kMaxFrameCount];
			Uint32 m_readPtr;
			Uint32 m_writePtr;
		};

		// all this is local to a particular thread, no need for any synchronization
		// align to 64 bytes to avoid false sharing - unlikely anyway because of the
		// size of SampleStack, but do it regardless
		RED_ALIGNED_STRUCT( ThreadData, 64 )
		{
			AnsiChar m_name[red::g_kMaxThreadNameLength];
			Samples	m_samples;
			SampleStack	m_sampleStack;
			SampleAllocator m_samplesAllocator;
		};

	private:
		static const Uint32	kMaxThreadCount = 32;
		static const Uint32 kInvalidSampleIndex = 0xFFFFFFFF;

	private:
		Sample* AllocateThreadMem( Uint32 size );

	private:

		ThreadData m_threads[kMaxThreadCount];
		red::Atomic<Uint32> m_threadCount;

		Frames m_frames;

		void* m_memory;
		red::memory::LocklessStaticLinearAllocator m_allocator;

		bool m_initialized;
	};

	extern RedInGameProfilerTool REDCORE_API gRedInGameProfilerTool;

#endif
}

#include "profilerToolRedInGame.inl"
