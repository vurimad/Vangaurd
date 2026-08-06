/**
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "deferredDataBuffer.h"
#include "serializationMapping.h"
#include "bufferAsyncProxy.h"
#include "dataBuffer.h"

#include "../../redJobs2/include/jobRunner.h"
#include "../../redJobs2/include/jobDeferral.h"
#include "../../redFileSystem/include/file.h"
#include "resourceLoader.h"
#include "../../redCore/include/globalModeInfo.h"
#include "../../redJobs2/include/jobCounterOwner.h"
#include "../../redJobs2/include/jobBuilder.h"

namespace serialization
{
	const Bool ToString( String& outTxt, const serialization::DeferredDataBuffer& val )
	{
		return false;
	}

	const Bool FromString( const String& txt, serialization::DeferredDataBuffer& outVal )
	{
		return false;
	}

	bool operator==( const DeferredDataBuffer & left, const DeferredDataBuffer & right )
	{
		if ( left.GetSize() == 0 && right.GetSize() == 0 )
		{
			return true;
		}

		return false;
	} 

	//---------------------------------------------------------
	
	const compression::ECompressionType DeferredDataBuffer::c_defaultCompression = compression::c_defaultCompressionType;
	
	DeferredDataBuffer::MovableData::MovableData()
		: pendingDataBuffer()
		, memoryBuffer()
		, bufferAccess()
		, flags( 0 )
#ifndef NO_EDITOR
		, editorIsCookerWriting( false )
#endif
	{
	}

	DeferredDataBuffer::DeferredDataBuffer( const red::memory::Pool& memoryPool, Uint32 alignment, Uint8 flags )
		: m_numCreateLoadingJobsInFlight( 0 )
		, m_loadingState( LoadingState::Cleared )
		, m_numClearingLocks( 0 )
	{
		RED_FATAL_ASSERT( memoryPool.GetHandle() != red::PoolFrame::GetHandle(), "Frame pool for latent I/O forbidden" );
		RED_FATAL_ASSERT( memoryPool.GetHandle() != red::PoolDoubleBufferedFrame::GetHandle(), "Frame pool for latent I/O forbidden" );

		RED_FATAL_ASSERT( alignment != 0 && red::IsPowerOf2( alignment ), "Alignment %u should be a power of two!", alignment );
		RED_FATAL_ASSERT( alignment <= 65536, "Alignment %u is too large!", alignment );

		m_data.pendingDataBuffer = red::MakeEmptyUniqueBuffer(memoryPool, alignment);
		m_data.flags = flags;
	}

	DeferredDataBuffer::DeferredDataBuffer( Uint8 flags )
		: DeferredDataBuffer( red::PoolEngine(), c_defaultAlignment, flags )
	{
	}
	
 	DeferredDataBuffer::DeferredDataBuffer( DeferredDataBuffer&& rhs )
 		: DeferredDataBuffer()
 	{
		// only need to lock rhs since this is being constructed, so can't started being used yet
		auto& lockRHS = rhs.m_loadingLock;
		RED_SCOPE_LOCK( lockRHS );

		RED_FATAL_ASSERT( rhs.m_loadingState != DeferredDataBuffer::LoadingState::Pending, "Can't swap while loads still pending. Will crash on callback!" );
		RED_FATAL_ASSERT( rhs.m_numCreateLoadingJobsInFlight == 0, "Can't swap while loads still pending. Will crash on callback!" );
		RED_FATAL_ASSERT( rhs.m_loadingCounter == nullptr );
		RED_FATAL_ASSERT( rhs.m_waitCallbackComplete == nullptr );

		m_loadingState = rhs.m_loadingState;
		rhs.m_loadingState = LoadingState::Moved;
		::Swap( m_data, rhs.m_data );
 	}

	DeferredDataBuffer::~DeferredDataBuffer()
	{
		RED_SCOPE_LOCK( m_loadingLock );

		RED_FATAL_ASSERT( m_numCreateLoadingJobsInFlight == 0, "Destructed while CreateLoadingJobs() still pending. Will crash!" );
		RED_FATAL_ASSERT( m_loadingState != LoadingState::Pending, "Destructed while load still pending. Will crash on callback!" );
		RED_FATAL_ASSERT( m_loadingCounter == nullptr );
		RED_FATAL_ASSERT( m_waitCallbackComplete == nullptr );
	}

	void DeferredDataBuffer::Serialize( IFile& file )
	{
		if (file.IsMapper())
		{
			return;
		}

#ifndef NO_EDITOR
		// Just in case. Right now terrain nodes (not instances) have DDBs, and when saving animation stuff.
		if ( file.IsCloner() || file.IsWriter() )
		{
			constexpr Double timeoutSeconds = 1.;
			red::Timer waitTimer;
			m_loadingLock.Acquire();
			while (m_loadingState == LoadingState::Pending)
			{
				m_loadingLock.Release();
				red::SleepOnCurrentThread(1);
				m_loadingLock.Acquire();

				if (waitTimer.GetSeconds() > timeoutSeconds )
				{
					RED_LOG_ERROR("Spinning for cloner while DDB in pending state: timed out after %f seconds. Likely will fatal assert.", timeoutSeconds);
					break;
				}
			}
			m_loadingLock.Release();
		}
#endif

		RED_FATAL_ASSERT( m_loadingState != LoadingState::Pending, "Pending asynchronous load!" );

		if ( !file.HasMapper() )
		{
			// For now make it required; instead of supporting some other format where we serialize the size as well
			// #tbd: support DDB->databuffer conversion
			RED_FATAL( "Requires mapper for serialization");
			return;
		}

		// Mapped write
		if ( file.IsWriter() )
		{
#ifndef NO_EDITOR
			IMapper::BufferIndex index = 0;

			if ( !m_data.memoryBuffer.Data() )
			{
				if ( m_data.bufferAccess )
				{
					m_data.memoryBuffer = m_data.bufferAccess->Editor_LoadContentDirect();
				}
			}

			BufferHandleRO precompressedBuffer;
			if (file.IsCooker())
			{
				// Try to get the compressed data, so the cooker can avoid recompressing it if nothing changed
				// Editor_SetContentDirect clears the buffer access, so it's still possible to pre-modify the buffer during cooking
				// But the modification has to be before we start writing out the file

				m_data.editorIsCookerWriting = true;

				// #tbd: if need to ever cook from an async source, can convert it into a memory buffer instead of fatal assert here
				//RED_FATAL_ASSERT( !m_data.bufferAccess || m_data.bufferAccess->IsMemoryFileBuffer(), "Cooker should be using a memory file buffer!" );
				if (m_data.bufferAccess )
				{
					Uint32 sizeInMemory = 0;
					precompressedBuffer = m_data.bufferAccess->Editor_LoadCompressedDataForCookingDirect( sizeInMemory );
					if ( precompressedBuffer.Data() ) // empty if data wasn't compressed
					{
						RED_FATAL_ASSERT(sizeInMemory == m_data.memoryBuffer.GetSize(), "SizeInMemory mismatch: %u vs %u", sizeInMemory, m_data.memoryBuffer.GetSize());
					}
				}
			}
			else
			{
				// Must drop file buffer access so can be saved without access share violations on Win32
				// DeferredBufferLoader is used to do this before even really trying to save
				// Then the resource should be reloaded in the editor, reestablishing the source; if in a commandlet, then have to be more careful and not call clear if the data
				// will still be needed after
				m_data.bufferAccess = nullptr;
			}
			
			IMapper::MapBufferFlags flags;
			flags.useCompression = ( m_data.flags & DDBF_SaveNoCompression) == 0;
			flags.useFastCompression = (m_data.flags & DDBF_Editor_SaveFastCompression) != 0;
			flags.allowNonDefaultCompressionType = file.IsCooker();
			
			// #todo: check pools or have a DDB flag to explicitly say
			// we don't have any direct loading into Garlic bus yet, so currently not much point of this
			
			flags.hintGPUMemory = false;
			//flags.hintGPUMemory = reinterpret_cast<red::memory::Pool&>(m_data.memoryPoolVtable).GetHandle() == rend::PoolRender???::GetHandle();
			
			// #tbd: allow to override this
			flags.hintAutoLoadPC = (m_data.flags & DDBF_AutoLoadPC ) != 0;
			flags.hintAutoLoadXboxOne = (m_data.flags & DDBF_AutoLoadXboxOne ) != 0;
			flags.hintAutoLoadPS4 = (m_data.flags & DDBF_AutoLoadPS4 ) != 0;

			file.GetMapper()->MapBuffer({ m_data.memoryBuffer, precompressedBuffer, flags }, index);

			file << index;

			RED_SCOPE_LOCK( m_loadingLock );

			// Now the editor just needs to reload this resource before it can do anything but be write-serialized
			m_loadingState = LoadingState::Loaded;

#else
			// #tbd: No-op in the game: usually some CNullWriter used to map and find pointers.
			// Game with editor-compiled in should probably also check if is game or not too...
			//RED_FATAL( "Writing not supported without editor!");
#endif
		}
		else if ( file.IsReader() )
		{
			IMapper::BufferIndex index = 0;
			file << index;

			// #tbd: better way to check if default pool, could compare vtable ptrs directly
			if ( GetPool().GetHandle() == red::PoolEngine::GetHandle() )
			{
				// Only use the file's pool if we're using the default pool as default
				m_data.pendingDataBuffer = red::MakeEmptyUniqueBuffer(file.GetInternalMemoryPool(), GetAlignment());
			}

			Bool isAutoload = false;
#if defined( RED_PLATFORM_WINPC )
			isAutoload = (m_data.flags & DDBF_AutoLoadPC) != 0;
#elif defined( RED_PLATFORM_DURANGO )
			isAutoload = (m_data.flags & DDBF_AutoLoadXboxOne) != 0;
#elif defined( RED_PLATFORM_ORBIS )
			isAutoload = (m_data.flags & DDBF_AutoLoadPS4) != 0;
#else
			// No autoload for you... unless Linux wants it
			isAutoload = false;
#endif

			IMapper::LoadBufferToken token;
			token.isAutoload = isAutoload;
			token.callback = &DeferredDataBuffer::StaticDispatchLoadingJobs;
			token.userData = this;
			file.GetMapper()->UnmapBuffer( index, token, m_data.bufferAccess );
		}	
	}

#ifndef NO_EDITOR
	const BufferHandleRO& DeferredDataBuffer::Editor_SetContentDirect( const void* data, Uint32 size, Uint32 alignment )
	{
		RED_SCOPE_LOCK( m_loadingLock );
		RED_FATAL_ASSERT( !m_data.editorIsCookerWriting, "Too late for modification. Already being cooked" );
		RED_FATAL_ASSERT( m_loadingState != LoadingState::Pending, "Set while result still pending!" );
		m_data.bufferAccess = nullptr; // buffer access no longer valid
		m_data.memoryBuffer = BufferHandleRO( data, size, alignment );
		m_loadingState = LoadingState::Loaded;
		return m_data.memoryBuffer;
	}
#endif

#ifndef NO_EDITOR
	const BufferHandleRO& DeferredDataBuffer::Editor_SetContentDirect( Uint32 size )
	{
		RED_SCOPE_LOCK( m_loadingLock );
		RED_FATAL_ASSERT( !m_data.editorIsCookerWriting, "Too late for modification. Already being cooked" );
		RED_FATAL_ASSERT( m_loadingState != LoadingState::Pending, "Set while result still pending!" );
		m_data.bufferAccess = nullptr; // buffer access no longer valid
		m_data.memoryBuffer = BufferHandleRO( size, c_defaultAlignment );
		m_loadingState = LoadingState::Loaded;
		return m_data.memoryBuffer;
	}
#endif

#ifndef NO_EDITOR
	const BufferHandleRO& DeferredDataBuffer::Editor_SetContentDirect( const BufferHandleRO& data )
	{
		RED_SCOPE_LOCK( m_loadingLock );
		RED_FATAL_ASSERT( !m_data.editorIsCookerWriting, "Too late for modification. Already being cooked" );
		RED_FATAL_ASSERT( m_loadingState != LoadingState::Pending, "Set while result still pending!" );
		m_data.bufferAccess = nullptr; // buffer access no longer valid
		m_data.memoryBuffer = data;
		m_loadingState = LoadingState::Loaded;
		return m_data.memoryBuffer;
	}
#endif

#ifndef NO_EDITOR
	const BufferHandleRO& DeferredDataBuffer::Editor_SetContentDirect( red::UniqueBuffer&& data )
	{
		RED_SCOPE_LOCK( m_loadingLock );
		RED_FATAL_ASSERT( !m_data.editorIsCookerWriting, "Too late for modification. Already being cooked" );
		RED_FATAL_ASSERT( m_loadingState != LoadingState::Pending, "Set while result still pending!" );
		m_data.bufferAccess = nullptr; // buffer access no longer valid
		m_data.memoryBuffer = BufferHandleRO( std::move( data ) );
		m_loadingState = LoadingState::Loaded;
		return m_data.memoryBuffer;
	}
#endif

#ifndef NO_EDITOR
	void DeferredDataBuffer::Editor_Abandon()
	{
		RED_SCOPE_LOCK( m_loadingLock );
		RED_FATAL_ASSERT( m_loadingState != LoadingState::Pending, "Abandoned while result still pending!" );

		m_data.bufferAccess = nullptr; // buffer access no longer valid
		m_data.memoryBuffer = nullptr;
		m_loadingState = LoadingState::Abandoned;
	}
#endif

	void DeferredDataBuffer::LockClearing()
	{
		RED_SCOPE_LOCK( m_loadingLock );
		m_numClearingLocks += 1;
		RED_FATAL_ASSERT( m_numClearingLocks, "Clearing lock overflow!" );
	}

	void DeferredDataBuffer::UnlockAndClear()
	{
		RED_SCOPE_LOCK( m_loadingLock );
		RED_FATAL_ASSERT( m_loadingState != LoadingState::Pending, "Cleared while result still pending!" );
		RED_FATAL_ASSERT( m_numClearingLocks > 0, "Lock/Unlock calls mismatch!" );

		m_numClearingLocks -= 1;
		if( !m_numClearingLocks )
		{
			m_data.memoryBuffer = nullptr;
			// Clear but keep buffer access so can reload async
			m_loadingState = LoadingState::Cleared;
		}
	}

	Uint16 DeferredDataBuffer::GetNumClearingLocks() const
	{
		return m_numClearingLocks;
	}

	void DeferredDataBuffer::Clear()
	{
		RED_SCOPE_LOCK( m_loadingLock );
		RED_FATAL_ASSERT( m_loadingState != LoadingState::Pending, "Cleared while result still pending!" );

		RED_FATAL_ASSERT( !m_numClearingLocks, "Buffer is locked for clearing! Call UnlockAndClear() instead." );

		m_data.memoryBuffer = nullptr;
		// Clear but keep buffer access so can reload async
		m_loadingState = LoadingState::Cleared;
	}

#if 1//ndef NO_EDITOR
	BufferHandleRO DeferredDataBuffer::Editor_LoadContentDirect() const
	{
		//RED_FATAL_ASSERT( !::IsGameMode() );

		RED_SCOPE_LOCK( m_loadingLock );

		RED_FATAL_ASSERT( m_loadingState != LoadingState::Pending );

		if ( !m_data.memoryBuffer.Data() )
		{
			if (m_data.bufferAccess )
			{
				const_cast<BufferHandleRO&>( m_data.memoryBuffer ) = m_data.bufferAccess->Editor_LoadContentDirect();
			}
		}
		
		return m_data.memoryBuffer;
	}
#endif

	LoadingToken DeferredDataBuffer::StaticDispatchLoadingJobs( const MapperLoadBufferParam& param, IBufferAsyncProxy& proxy )
	{
		auto self = static_cast< DeferredDataBuffer* >( param.userData );
		RED_FATAL_ASSERT( self );

		auto& loadingLock = self->m_loadingLock;
		RED_SCOPE_LOCK( loadingLock );
		return self->DispatchLoadingJobsInternal_NoLock( param.throttler, param.priority, proxy, param.inlineData );
	}

	LoadingToken DeferredDataBuffer::DispatchLoadingJobsInternal_NoLock( res::ResourceLoaderThrottler* throttler, io::EAsyncPriority priority, IBufferAsyncProxy& proxy, const AsyncSourceReadBuffer* inlineData )
	{
		RED_FATAL_ASSERT( m_loadingState != LoadingState::Moved, "Trying to load a buffer that's been moved and left a zombie!" );
		RED_FATAL_ASSERT( m_loadingState != LoadingState::Abandoned, "Trying to load a buffer that's been abandoned!" );

		job::Counter tokenCounter{ job::Priority::Latent };

		if ( m_loadingState == LoadingState::Cleared )
		{
			if (proxy.IsMemoryFileBuffer())
			{
#ifdef NO_EDITOR
				RED_FATAL("Editor_LoadContentDirect called during game");
#endif
				// Can hit here during undo. Better would just be to have proxy.DispatchLoadingJobs return immediately
				// but then we'd currently deadlock... Meh.
				m_data.memoryBuffer = proxy.Editor_LoadContentDirect();
				m_loadingState = LoadingState::Loaded;
			}
			else
			{
				m_loadingState = LoadingState::Pending;

				RED_FATAL_ASSERT( !m_loadingCounter );

				IBufferAsyncProxy::BufferLoadingContext bufferContext;
				bufferContext.target = this;
				bufferContext.callback = &DeferredDataBuffer::OnLoadedDataCallback;
				bufferContext.reentrantCallback = &DeferredDataBuffer::OnLoadedDataCallback_NoLock;
				bufferContext.destinationBuffer = &m_data.pendingDataBuffer;
				bufferContext.alignment = GetAlignment();
				bufferContext.priority = priority;
				bufferContext.throttler = throttler;
				bufferContext.inlineData = inlineData;

				// #tbd: preferably outside of loadingLock, but then would still need to prevent races
				// from multiple calls to CreateLoadingJobs() before kicked off loading jobs
				proxy.DispatchLoadingJobs( bufferContext );

				// Check if DispatchLoadingJobs finished reentrantly from inlined data (archives only for now)
				if (m_loadingState != LoadingState::Loaded)
				{
					// The loadingLock isn't enough to otherwise prevent a race where the loading finishes, but OnLoadedDataCallback hasn't been called yet
					// Although the LoadingState would have asserted if the buffer was accessed
					m_loadingCounter = red::CreateUniquePtr< job::Counter >( job::Priority::Latent, "DeferredDataBuffer" );
					m_waitCallbackComplete = red::CreateUniquePtr< job::CompletionDeferral >( m_loadingCounter->CreateDeferral( this, "DeferredDataBuffer" ) );
				
					tokenCounter += *m_loadingCounter;
				}
			}
		}
		else if ( m_loadingState != LoadingState::Loaded )
		{
			RED_FATAL_ASSERT( m_loadingCounter );
			
			tokenCounter += *m_loadingCounter;
		}
		else
		{
			RED_FATAL_ASSERT( m_loadingState == LoadingState::Loaded );
			RED_FATAL_ASSERT( m_loadingCounter == nullptr );
		}

		return LoadingToken(std::move(tokenCounter));
	}

	void DeferredDataBuffer::OnLoadedDataCallback( void* target )
	{	
		auto* self = static_cast<DeferredDataBuffer*>(target);
		auto& lock = self->m_loadingLock;
		RED_SCOPE_LOCK( lock );

		OnLoadedDataCallback_NoLock(self);
	}

	void DeferredDataBuffer::OnLoadedDataCallback_NoLock( void* target )
	{
		auto* self = static_cast<DeferredDataBuffer*>(target);

		self->m_data.memoryBuffer = BufferHandleRO(std::move(self->m_data.pendingDataBuffer));
		self->m_loadingState = LoadingState::Loaded;

		if (self->m_waitCallbackComplete)
		{
			self->m_waitCallbackComplete->FinishDeferral();
		}
		self->m_waitCallbackComplete = nullptr;
		self->m_loadingCounter = nullptr;
	}

	const BufferHandleRO& DeferredDataBuffer::Data() const
	{
		RED_FATAL_ASSERT( m_loadingState == LoadingState::Loaded );
		return m_data.memoryBuffer;
	}

	Uint32 DeferredDataBuffer::GetSize() const
	{
		RED_SCOPE_LOCK( m_loadingLock );

		if ( m_loadingState == LoadingState::Loaded )
		{
			return m_data.memoryBuffer.GetSize();
		}
		return m_data.bufferAccess ? m_data.bufferAccess->GetSize() : 0;
	}

	const red::memory::Pool& DeferredDataBuffer::GetPool() const
	{
		return m_data.pendingDataBuffer.GetPool();
	}

	Uint32 DeferredDataBuffer::GetAlignment() const
	{
		return m_data.pendingDataBuffer.GetAlignment();
	}

	const serialization::BufferHandleRO& DeferredDataBuffer::Convert_SetContentDirect(const DataBuffer& dataBuffer)
	{
		RED_SCOPE_LOCK( m_loadingLock );
		RED_FATAL_ASSERT( m_loadingState != LoadingState::Pending, "Set while result still pending!" );
		m_data.bufferAccess = nullptr; // buffer access no longer valid
		m_data.memoryBuffer = BufferHandleRO( dataBuffer.Data(), dataBuffer.Size(), c_defaultAlignment );
		m_loadingState = LoadingState::Loaded;
		return m_data.memoryBuffer;
	}

	LoadingToken DeferredDataBuffer::IssueLoadingJobs(io::EAsyncPriority priority)
	{
		const io::EAsyncPriority ioPriorityReadFromTLS = io::GetThreadLocalIOPriority();
		if ( ioPriorityReadFromTLS != io::eAsyncPriority_INVALID )
		{
			// this takes precedence over manually set priority
			priority = ioPriorityReadFromTLS;
		}

		job::Builder builder{ job::Priority::Latent };

		{
			RED_SCOPE_LOCK( m_loadingLock );
			RED_FATAL_ASSERT( m_loadingState != LoadingState::Moved, "Trying to load a buffer that's been moved and left a zombie!" );
			RED_FATAL_ASSERT( m_loadingState != LoadingState::Abandoned, "Trying to load a buffer that's been abandoned!" );

			m_numCreateLoadingJobsInFlight += 1;
		}

		// Defer to a job even though could start loading DDB now
		// So it behaves like scheduling other jobs in a chain of sync's, especially if the DDB is Clear()'d at the end after loading
		builder.DispatchJob( "DeferredDataBuffer/CreateLoadingJobs", [this, priority]( const job::RunContext& context )
		{
			RED_SCOPE_LOCK( m_loadingLock );

			job::Builder depBuilder{ context };

			if ( m_data.bufferAccess )
			{
				auto* throttler = GResourceLoader ? GResourceLoader->GetThrottler() : nullptr;
				auto token = DispatchLoadingJobsInternal_NoLock( throttler, priority, *m_data.bufferAccess, nullptr );
				depBuilder.DispatchWait(token.GetWaitCounter());
			}
			else
			{
				if ( m_loadingState == LoadingState::Cleared )
				{
					m_loadingState = LoadingState::Loaded;
				}
			}

			// Don't have to wait for depBuilder, this counter is just until we can start dispatching loads, then the loading state
			// can be asserted on
			RED_FATAL_ASSERT( m_numCreateLoadingJobsInFlight > 0 );
			m_numCreateLoadingJobsInFlight -= 1;
		} );

		return LoadingToken(builder.ExtractWaitCounter());
	}

	namespace helper
	{
		struct NonMutableLoadingContext
		{
			RED_USE_MEMORY_POOL(red::PoolResource);

			red::WeakPtr< LoadingTokenImmutable > tokenWeakPtr;
			job::CompletionDeferral loadingDeferral;
			red::UniqueBuffer pendingDataBuffer;

			static void OnLoadedDataCallback(void* target)
			{
				auto* context = reinterpret_cast<NonMutableLoadingContext*>(target);
				RED_FATAL_ASSERT(context);

				context->OnDataLoaded();

				RED_DELETE(context);
			}

			void OnDataLoaded()
			{
				// note: the token should be in scope of the 'if', or at least the strong reference should be released before loadingDeferral.FinishDeferral() is called.
				// Otherwise, the strong reference will be held till the end of function, so when FinishDeferral unblocks post-load jobs to execute, these jobs might see
				// the token as strong reference while expecting it to be weak (because of loading cancellation). This is the case with animBufferDataState post-load job
				// and a condition if(!token) doing early exit. Otherwise, if the reference in that condition is strong, it will pass and set invalid state to animation.
				if (auto token = tokenWeakPtr.Lock())
				{
					token->Internal_OnDataLoaded(std::move(pendingDataBuffer));
				}
				loadingDeferral.FinishDeferral();
			}
		};
	}

	red::SharedPtr< LoadingTokenImmutable > DeferredDataBuffer::IssueLoadingJobs_Immutable(io::EAsyncPriority priority /*= io::eAsyncPriority_Normal*/)
	{
		// #tbd: shouldn't even need this lock or Loaded state check if never use other API.
		// #tbd: but if autoloaded? Try to avoid redundantly loading it (still shouldn't need the lock unless cleared then)
		RED_SCOPE_LOCK(m_loadingLock);

		RED_FATAL_ASSERT(m_loadingState != LoadingState::Pending, "Mixing mutable/immutable API is not supported currently. Might work, but no guarantees. Shouldn't mix it bottom line.");

		RED_FATAL_ASSERT(m_loadingState != LoadingState::Moved, "Trying to load a buffer that's been moved and left a zombie!");
		RED_FATAL_ASSERT(m_loadingState != LoadingState::Abandoned, "Trying to load a buffer that's been abandoned!");

		//TBD:

		if (m_loadingState == LoadingState::Loaded) // maybe autoloaded buffer
		{
			auto token = red::CreateSharedPtr< LoadingTokenImmutable >();
			token->Internal_OnDataLoaded(m_data.memoryBuffer);
			return token;
		}

		RED_FATAL_ASSERT(m_data.bufferAccess); // allow if empty DDB...?
		RED_FATAL_ASSERT(!m_data.bufferAccess->IsMemoryFileBuffer(), "Not supported currently. Probably just need to make  MemoryFileBufferAsyncProxy::DispatchLoadingJobs call the 'reentrant' callback");

		auto token = red::CreateSharedPtr< LoadingTokenImmutable >();

		auto* context = RED_NEW(helper::NonMutableLoadingContext)();
		context->tokenWeakPtr = token;
		context->loadingDeferral = token->Internal_CreateDeferral();
		context->pendingDataBuffer = red::MakeEmptyUniqueBuffer(GetPool(), GetAlignment());

		auto* throttler = GResourceLoader ? GResourceLoader->GetThrottler() : nullptr;
		IBufferAsyncProxy::BufferLoadingContext bufferContext;
		bufferContext.target = context;
		bufferContext.callback = &helper::NonMutableLoadingContext::OnLoadedDataCallback;
		bufferContext.reentrantCallback = &helper::NonMutableLoadingContext::OnLoadedDataCallback;
		bufferContext.destinationBuffer = &context->pendingDataBuffer;
		bufferContext.alignment = GetAlignment();
		bufferContext.priority = priority;
		bufferContext.throttler = throttler;
		bufferContext.inlineData = nullptr;
		bufferContext.ioContext = token->Internal_GetIOContext();

		m_data.bufferAccess->DispatchLoadingJobs(bufferContext);

		return token;
	}

} // serialization

//----

namespace serialization
{
	class DeferredDataBufferType : public rtti::IType
	{
	public:
		virtual const CName GetName() const override final
		{
			return GetTypeName< serialization::DeferredDataBuffer >();
		}

		virtual Uint32 GetSize() const override final
		{
			return sizeof( serialization::DeferredDataBuffer );
		}

		virtual Uint32 GetAlignment() const override final
		{
			return 4;
		}

		virtual ERTTITypeType GetType() const override final
		{
			return RT_Simple;
		}

		virtual CName GetRefName() const override final
		{
			return rtti::FormatScriptedReferenceTypeName( GetTypeName< serialization::DeferredDataBuffer >() );
		}

		virtual void Construct( void *object ) const override final
		{
			new ( object ) serialization::DeferredDataBuffer();
		}

		virtual void Destruct( void *object ) const override final
		{
			( ( serialization::DeferredDataBuffer* )object )->~DeferredDataBuffer();
		}

		virtual Bool Compare( const void* data1, const void* data2, Uint32 DEPRECATED_flags ) const override final
		{
			RED_FATAL_ASSERT( data1 != nullptr && data2 != nullptr );
			const serialization::DeferredDataBuffer& left = *static_cast<const serialization::DeferredDataBuffer*>( data1 );
			const serialization::DeferredDataBuffer& right = *static_cast<const serialization::DeferredDataBuffer*>( data2 );
			return left == right;
		}

		virtual void Copy( void* dest, const void* src ) const override final
		{
			// Don't want this copying be default
			//(*(serialization::DeferredDataBuffer*)dest) = serialization::DeferredDataBuffer::Copy( (*(const serialization::DeferredDataBuffer*) src) );
		}

		virtual Bool Serialize( IFile& file, void* data, ISerializable* owner = nullptr ) const override final
		{
			( ( serialization::DeferredDataBuffer* )data )->Serialize( file );
			return true;
		}

		virtual Bool NeedsCleaning() const override final
		{
			return true;
		}

		virtual const Bool SerializeToText( text::ITextWriter& writer, const void* data ) const override final
		{
			return false;
		}

		virtual const Bool SerializeFromText( text::ITextReader& reader, void* data ) const override final
		{
			return false;
		}

		virtual const red::memory::Pool & GetInnerTypeMemoryPool() const override final
		{
			return red::PoolEngine::GetInstance();
		}
	};
}

void RegisterTypeDeferredDataBuffer()
{
	RTTI_REGISTER_AUTO_TYPE_ALIAS_IN_NAMESPACE( DeferredDataBuffer, DeferredDataBufferType, serialization );
}
