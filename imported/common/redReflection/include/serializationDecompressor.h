/*
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#pragma once

namespace red
{
class BlobView;
class BlobSpan;
}

namespace serialization
{
	class RED_REFLECTION_API Decompressor : private red::NonCopyable
	{
	public:
		static Bool DecompressData( const char* debugLogicalFileName, const red::BlobView& compressedData, red::BlobSpan outputBuffer );
		static Bool DecompressData( const char* debugLogicalFileName, const red::BlobView& compressedData, void* outputBuffer, Uint32 outputBufferSize );

		class StatsAccumulator
		{
			friend class Decompressor;

		public:
			Uint64 GetTotalBytesOutputPerSecondExclusive() const
			{
				if ( m_totalUsec > 0 )
				{
					return m_totalBytesOutput * 1000000 / m_totalUsec;
				}
				return 0;
			}

			Uint64 GetTotalBytesOutput() const
			{
				return m_totalBytesOutput;
			}

			void Reset()
			{
				m_totalBytesOutput = 0;
				m_totalUsec = 0;
			}

		private:
			Uint64 m_totalBytesOutput{ 0 };
			Uint64 m_totalUsec{ 0 };
		};

		static void FlushStats( StatsAccumulator& outStats );
	};
}