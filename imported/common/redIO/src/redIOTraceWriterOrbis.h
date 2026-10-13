/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#ifdef RED_PLATFORM_ORBIS

namespace io
{
namespace orbis
{

class Profiler;

class TraceWriter : red::NonCopyable
{
	RED_USE_MEMORY_POOL( red::PoolDebug );

public:
	void RegisterLogicalFile( Uint32 fileID, Uint64 offset, Uint32 size, const char* logicalFileName );
	void StopTrace( Profiler& profiler );

private:
	class ProfilerVisitorJSONWriter;

	const String* FindLogicalFile_NoLock( Uint32 fileID, Uint64 offset, Uint32 size );

	//#temp: if anything should store res::ResourcePaths instead, but need to move it lower so redIO can keep it alive
	struct FileOffsetSize
	{
		Uint64 offset{ 0 };
		Uint32 size{ 0 };
		Uint32 fileID{ 0 };

		Bool operator==( const FileOffsetSize& rhs ) const
		{
			return offset == rhs.offset && size == rhs.size && fileID == rhs.fileID;
		}

		Bool operator<( const FileOffsetSize& rhs ) const
		{
			if ( fileID != rhs.fileID )
			{
				return fileID < rhs.fileID;
			}

			if ( offset != rhs.offset )
			{
				return offset < rhs.offset;
			}

			return size < rhs.size;
		}
	};

	red::Map< FileOffsetSize, String > m_logicalFileNameMap{ red::PoolDebug() };
	mutable red::SpinLock m_logicalFileNameMapLock;
};

} // orbis
} // io

#endif // RED_PLATFORM_ORBIS