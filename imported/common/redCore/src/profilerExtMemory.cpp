/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
//////////////////////////////////////////////////////////////////////////
// headers
#include "build.h"
#include "../../redMemory/include/memoryStream.h"
#include "../../redMemory/include/uniqueBuffer.h"
#include "../../redSystem/include/timer.h"
#include "../../redSystem/include/threads.h"
#include "../../redSystem/include/redThreadsThread.h"
#include "../../redSystem/include/clock.h"
#include "../../redIO/include/redIOPublic.h"
#include "../../redMemory/include/metricsUtils.h"
#include "profilerExtMemory.h"


//#ifdef USE_RED_PROFILER

//////////////////////////////////////////////////////////////////////////
// usings
using red::DynArray;
using red::HashMap;
using red::String;


namespace red
{
	MemoryProfilerExtension::MemoryProfilerExtension() :
		m_stored(false),
		m_init(false),
		m_isRunning(false),
		m_oom(false),
		m_reservedMem( PROFILER_DEFAULT_VMEM )
	{
	}

	MemoryProfilerExtension::~MemoryProfilerExtension()
	{
	}

	//////////////////////////////////////////////////////////////////////////
	//
	// todo - return internal memory fill factor
	Float MemoryProfilerExtension::GetFilledBufferFactor()
	{
		red::memory::Stream& outputStream = red::memory::GetMemoryCaptureStream();
		return static_cast<Float>( outputStream.DataAvailableToRead() / ( static_cast< double >( m_reservedMem ) ) );
	}

	Bool MemoryProfilerExtension::Init( const Uint32 bufferSize )
	{
		m_reservedMem = bufferSize;
		m_oom.SetValue(false);
		m_stored = false;

		RED_LOG( "Profiler Memory Extension: %d bytes buffer reserved..", m_reservedMem );

		m_init.SetValue( true );
		return true;
	}

	void MemoryProfilerExtension::Start( red::memory::OutOfProfilerMemoryCallback outOfProfilerMemoryCallback )
	{
		RED_ASSERT( m_isRunning.GetValue() == false, "Memory profiler is already running" );
		m_isRunning.SetValue( true );
		red::memory::ResetMemoryCapture();
		red::memory::StartMemoryCapture( m_reservedMem, std::move( outOfProfilerMemoryCallback ) );
	}

	void MemoryProfilerExtension::Stop()
	{
		red::memory::StopMemoryCapture();
		m_oom.SetValue( false );
		m_isRunning.SetValue( false );
	}

	void MemoryProfilerExtension::NextFrame( red::ProfilerFrameType frameType )
	{
		RED_ASSERT( m_isRunning.GetValue() == true, "Memory profiler doesn't work" );
		red::memory::WriteCurrentFrameTick();
	}

	Bool MemoryProfilerExtension::Store( const red::String& _filename )
	{
		// write profile data
		red::memory::Stream& stream = red::memory::GetMemoryCaptureStream();
		if ( stream.DataAvailableToRead() > 0 )
		{
			// init
			red::AbsolutePath outputPath = red::AbsolutePath::CreateFilePath( _filename );
			outputPath.AppendFilePath( ".rmm" );

			// alloc saving buffer
			red::UniqueBuffer mem = red::CreateUniqueBuffer< red::PoolDebug >( _savingBufferSize, 8 );
			red::Memset( mem.Get(), 0, _savingBufferSize );

			io::NativeFileHandle writer;
			writer.Open( outputPath.AsChar(), io::eOpenFlag_WriteNew );
			if ( writer.IsValid() )
			{
				Uint32 readSize = 0;
				Uint32 writtenSize = 0;

				stream.ReadBuffer( mem.Get(), _savingBufferSize, readSize );

				while ( readSize != 0 && readSize == _savingBufferSize )
				{
					writer.Write( mem.Get(), readSize, writtenSize );
					stream.ReadBuffer( mem.Get(), _savingBufferSize, readSize );
				}

				if ( readSize != 0 )
				{
					writer.Write( mem.Get(), readSize, writtenSize );
				}

				writer.Flush();
				writer.Close();

				m_stored = true;
			}
			else
			{
				RED_LOG_WARNING( "Cannot create memory profile file: '%hs'", outputPath.AsChar() );
			}
		}
		else
		{
			RED_LOG_WARNING( "Memory capture stream is empty" );
		}

		// free resources
		red::memory::ResetMemoryCapture();

		return m_stored;
	}

	void MemoryProfilerExtension::Shutdown()
	{
	}

	red::memory::Stream& MemoryProfilerExtension::GetOutputStream()
	{
		return red::memory::GetMemoryCaptureStream();
	}

	Bool MemoryProfilerExtension::IsRunning() const
	{
		return m_isRunning.GetValue();
	}

	Bool MemoryProfilerExtension::IsOutOfMemory() const
	{
		return m_oom.GetValue();
	}

	void MemoryProfilerExtension::SetOutOfMemory()
	{
		m_oom.SetValue( true );
	}

	void MemoryProfilerExtension::Update()
	{
		RED_LOG( "memory profiler ext: %0.4f", GetFilledBufferFactor() );
	}
}

//#else 
//RED_NO_EMPTY_FILE();
//#endif
