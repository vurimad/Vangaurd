/**
 * Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "../include/poolUtils.h"
#include "../include/gameMemoryPools.h"
#include "../../redSystem/include/processUtility.h"
#include "vault.h"

RED_MEMORY_DEFINE_POOL_STORAGE( red::memory::PoolRoot, RED_MEMORY_API );
RED_MEMORY_DEFINE_POOL_STORAGE( red::memory::PoolCPU, RED_MEMORY_API );
RED_MEMORY_DEFINE_POOL_STORAGE( red::memory::PoolGPU, RED_MEMORY_API );
RED_MEMORY_DEFINE_POOL_STORAGE( red::memory::PoolFlexible, RED_MEMORY_API );
RED_MEMORY_DEFINE_POOL_STORAGE( red::PoolDebug, RED_MEMORY_API );
RED_MEMORY_DEFINE_POOL_STORAGE( red::PoolEngine, RED_MEMORY_API );
RED_MEMORY_DEFINE_POOL_STORAGE( red::PoolRefCount, RED_MEMORY_API );
RED_MEMORY_DEFINE_POOL_STORAGE( red::PoolLegacyOperator, RED_MEMORY_API );
RED_MEMORY_DEFINE_POOL_STORAGE( red::PoolFrame, RED_MEMORY_API );
RED_MEMORY_DEFINE_POOL_STORAGE( red::PoolDoubleBufferedFrame, RED_MEMORY_API );
RED_MEMORY_DEFINE_POOL_STORAGE( red::PoolBackend, RED_MEMORY_API );
RED_MEMORY_DEFINE_POOL_STORAGE( red::PoolDefault, RED_MEMORY_API );

RED_MEMORY_DEFINE_POOL_STORAGE( game::PoolGMPL, RED_MEMORY_API );
RED_MEMORY_DEFINE_POOL_STORAGE( game::PoolGMPL_Abstract, RED_MEMORY_API );
RED_MEMORY_DEFINE_POOL_STORAGE( AI::PoolAI, RED_MEMORY_API );
RED_MEMORY_DEFINE_POOL_STORAGE( red::memory::PoolUI, RED_MEMORY_API );

namespace red
{
namespace memory
{
#ifdef RED_PLATFORM_CONSOLE
	const Uint32 c_frameAllocatorSize = RED_MEGA_BYTE( 1 );
	const Uint32 c_doubleBufferedFrameAllocatorSizePerFrame = RED_MEGA_BYTE( 2 );
#else
	const Uint32 c_frameAllocatorSize = RED_MEGA_BYTE( 32 );
	const Uint32 c_doubleBufferedFrameAllocatorSizePerFrame = RED_MEGA_BYTE( 16 );
#endif

	static FrameAllocatorWithFallback s_frameAllocator;
	static FrameAllocatorWithFallback s_doubleBufferedFrameAllocator;

	void RegisterPool( PoolHandle handle, const PoolParameter & param )
	{
		AcquireVault().RegisterPool( handle, param );
	}

	void SetPoolBudget( PoolHandle handle, const char* name, u64 budget )
	{
		AcquireVault().SetPoolBudget( handle, name, budget );
	}

	void RegisterAllocatorMetricsProcessor(
		PoolHandle poolHandle,
		ProxyTypeId allocatorId,
		void ( *metricsSerializer )( void * allocator, red::memory::Serializer & serializer ),
		void ( *metricsDeserializer )( ProxyTypeId proxyId, red::memory::Deserializer & deserializer ) )
	{
		AcquireVault().RegisterAllocatorMetricsProcessor( poolHandle, allocatorId, metricsSerializer, metricsDeserializer );
	}

	u64 GetPoolBudget( PoolHandle handle )
	{
		return AcquireVault().GetPoolBudget( handle );
	}

	void ValidateAllPoolBudget()
	{
		AcquireVault().ValidateAllPoolBudget();
	}

	const char * GetPoolName( PoolHandle handle )
	{
		return AcquireVault().GetPoolName( handle );
	}

	void DisableContributeToParentMetrics( PoolHandle handle )
	{
		AcquireVault().DisableContributeToParentMetrics( handle );
	}

	void SetMirroredPool( PoolHandle handle )
	{
		AcquireVault().SetMirroredPool( handle );
	}

	void VisitPoolInfos( const PoolInfoVisitor& visitor, const PoolHandle poolHandle )
	{
		AcquireVault().VisitPoolInfos( visitor, poolHandle) ;
	}

	bool IsPoolRegistered( PoolHandle handle )
	{
		return AcquireVault().IsPoolRegistered( handle );
	}

	u32 GetPoolCount()
	{
		return AcquireVault().GetPoolCount();
	}

	void InitializeRootPools( const RootPoolsSetup& setup /*= RootPoolsSetup()*/ )
	{
		SystemAllocator & systemAllocator = AcquireSystemAllocator();
		u64 systemBudget = systemAllocator.GetTotalPhysicalMemoryAvailable();
		DefaultAllocator & defaultAllocator = AcquireDefaultAllocator();
		NullAllocator & nullAllocator = AcquireNullAllocator();

#ifdef RED_PLATFORM_ORBIS
		const SystemAllocator & flexibleAllocator = AcquireFlexibleSystemAllocator();
		const u64 flexibleBudget = flexibleAllocator.GetTotalPhysicalMemoryAvailable();
		systemBudget += flexibleBudget;
		RED_INITIALIZE_MEMORY_POOL( PoolFlexible, PoolRoot, nullAllocator, flexibleBudget );
#endif

		memory::FrameAllocatorParameter param =
		{
			&systemAllocator,
			setup.frameAllocatorSizeOverride > 0 ? setup.frameAllocatorSizeOverride : c_frameAllocatorSize,
			1,
			memory::Flags_CPU_Read_Write
		};

		s_frameAllocator.Initialize( param );

		memory::FrameAllocatorParameter doubleBufferedFrameAllocatorParam =
		{
			&systemAllocator,
			c_doubleBufferedFrameAllocatorSizePerFrame,
			2,
			memory::Flags_CPU_Read_Write
		};

		s_doubleBufferedFrameAllocator.Initialize( doubleBufferedFrameAllocatorParam );

		const PoolParameter rootParam =
		{
			"PoolRoot",
			&StaticPoolStorage< PoolRoot >::storage,
			systemBudget,
			c_poolNodeInvalid
		};

		const auto globalBudget = RED_GIGA_BYTE( 4 ) + RED_MEGA_BYTE( 512 );
		const auto cpuBudget = RED_GIGA_BYTE( 1 ) + RED_MEGA_BYTE( 512 ) + c_frameAllocatorSize + c_doubleBufferedFrameAllocatorSizePerFrame * 2;
		const auto gpuBudget = globalBudget - cpuBudget;

		InitializePool< PoolRoot >( rootParam, nullAllocator );
		RegisterAllocatorMetricsProcessor(
			PoolRoot::GetHandle(),
			PoolRoot::AllocatorType::TypeId,
			&red::memory::SerializeAllocatorMetrics< PoolRoot::AllocatorType >,
			&red::memory::CoreAllocatorsMetricsLogger );

		RED_INITIALIZE_MEMORY_POOL( PoolCPU, PoolRoot, nullAllocator, cpuBudget );
			RED_INITIALIZE_MEMORY_POOL( PoolDefault, PoolCPU, defaultAllocator, RED_MEGA_BYTE( 1 ) );
			RED_INITIALIZE_MEMORY_POOL( PoolLegacyOperator, PoolCPU, defaultAllocator, RED_MEGA_BYTE( 1 ) );
			RED_INITIALIZE_MEMORY_POOL( PoolFrame, PoolCPU, s_frameAllocator, c_frameAllocatorSize );
			RED_INITIALIZE_MEMORY_POOL( PoolDoubleBufferedFrame, PoolCPU, s_doubleBufferedFrameAllocator, c_doubleBufferedFrameAllocatorSizePerFrame * 2 );
			RED_INITIALIZE_MEMORY_POOL( PoolEngine, PoolCPU, defaultAllocator, RED_MEGA_BYTE( 432 ) );
				RED_INITIALIZE_MEMORY_POOL( PoolRefCount, PoolEngine, defaultAllocator, RED_MEGA_BYTE( 16 ) );

		RED_INITIALIZE_MEMORY_POOL( PoolGPU, PoolRoot, nullAllocator, gpuBudget );
		RED_INITIALIZE_MEMORY_POOL( PoolBackend, PoolRoot, defaultAllocator, RED_MEGA_BYTE( 512 )  );

#ifdef RED_PLATFORM_CONSOLE
		// On consoles, were memory is not cheap as dirt, DefaultAllocator is much more limited in terms of how big allocations it services.
		// Also, we wanna have better visibility of that shit. Hence a separate allocator.
		ConsoleDebugAllocator& consoleDebugAllocator = AcquireVault().GetConsoleDebugAllocator();
		RED_INITIALIZE_MEMORY_POOL( PoolDebug, PoolRoot, consoleDebugAllocator, RED_MEGA_BYTE( 512 ) );
#else
		RED_INITIALIZE_MEMORY_POOL( PoolDebug, PoolRoot, defaultAllocator, RED_MEGA_BYTE( 512 ) );
#endif
	
	}

	void ResetFrameAllocators()
	{
		auto& vault = red::memory::AcquireVault();
		if ( vault.IsHandlingOOM() )
		{
			return;
		}

		s_frameAllocator.Reset();
		s_doubleBufferedFrameAllocator.Reset();
	}

#ifdef RED_MEMORY_DEBUG_ALLOCATOR_ENABLED
	class PoolDebugAllocatorSettings
	{
	public:

		static PoolDebugAllocatorSettings& Get()
		{
			static PoolDebugAllocatorSettings instance;
			return instance;
		}

		Bool ShouldPoolUseDebugAllocator( PoolHandle handle )
		{
			// We use the fact here that pool handle is actually the hash of its name.
			const red::THash32 nameHash = handle;
			if( IsPoolInList( m_poolsWithDisabledDebugAllocator, m_poolsWithDisabledDebugAllocatorCount, nameHash ) )
			{
				return false;
			}

			if( m_enableDebugAllocatorForAllPools )
			{
				return true;
			}
			return IsPoolInList( m_poolsWithEnabledDebugAllocator, m_poolsWithEnabledDebugAllocatorCount, nameHash );
		}

		void Reset()
		{
			m_enableDebugAllocatorForAllPools = false;
			m_poolsWithEnabledDebugAllocatorCount = 0;
			red::Memset( m_poolsWithEnabledDebugAllocator, 0, sizeof( m_poolsWithEnabledDebugAllocator ) );
			m_poolsWithDisabledDebugAllocatorCount = 0;
			red::Memset( m_poolsWithDisabledDebugAllocator, 0, sizeof( m_poolsWithDisabledDebugAllocator ) );
		}

		/// Unit test API

		Bool ForceDebugAllocatorOnPool( PoolHandle handle )
		{
			return AddPool( static_cast< THash32 >( handle ) );
		}

		void ForceDebugAllocatorOnAllPools()
		{
			m_enableDebugAllocatorForAllPools = true;
		}
	private:
		PoolDebugAllocatorSettings()
		{
			RED_WARNING_PUSH()
			RED_DISABLE_WARNING_MSC( 4996 )
			FILE* file = fopen( "debugPools.list", "r" );
			RED_WARNING_POP()
			if( !file )
			{
				return;
			}

			AnsiChar currentProcessName[ 256 ] = {};
			const Bool processNameGetResult = GetProcessName( currentProcessName );
			RED_ASSERT( processNameGetResult );
			RED_UNUSED( processNameGetResult );

			const THash32 c_initialHashValue = RED_FNV_OFFSET_BASIS32;
			const Int32 c_processNameLengthMax = 256;
			struct ParserContext
			{
				enum class State
				{
					Decision,
					ParsePoolName,
					ParsePoolNameExclusion,
					ParseProcessName,
					SkipUntilNextProcessNameOrEof,
					Done,
				};
				State state = State::Decision;


				THash32 poolNameHash = c_initialHashValue;


				AnsiChar processName[ c_processNameLengthMax ] = {};
				Int32 processNameCharacterIdx = 0;
			} parserContext;

			Bool isEOF{};

			while( parserContext.state != ParserContext::State::Done )
			{
				switch( parserContext.state )
				{
				case ParserContext::State::Decision:
					{
						const AnsiChar ch = static_cast< AnsiChar >( fgetc( file ) );
						isEOF = feof( file ) || ch == EOF;

						if ( isEOF )
						{
							parserContext.state = ParserContext::State::Done;
						}
						else
						{
							if( ch == '!' )
							{
								parserContext.state = ParserContext::State::ParsePoolNameExclusion;
							}
							else if( ch == '.' )
							{
								parserContext.state = ParserContext::State::ParseProcessName;
							}
							else if( IsEol( ch ) )
							{
								// Handle empty lines
								isEOF = EatNextLine( file, ch );
							}
							else
							{
								ungetc( ch, file );
								isEOF = feof( file );
								parserContext.state = ParserContext::State::ParsePoolName;
							}
						}
					}
					break;

				case ParserContext::State::ParsePoolName:
				case ParserContext::State::ParsePoolNameExclusion:
					{
						const AnsiChar ch = static_cast< AnsiChar >( fgetc( file ) );
						isEOF = feof( file ) || ch == EOF;

						if ( !isEOF )
						{
							if( !IsEol( ch ) )
							{
								parserContext.poolNameHash = CalculateHash32( &ch, 1, parserContext.poolNameHash );
								continue;
							}

							isEOF = EatNextLine( file, ch );
						}

						const THash32 entryHash = parserContext.poolNameHash;
						parserContext.poolNameHash = c_initialHashValue;

						if( entryHash == c_allHash )
						{
							m_enableDebugAllocatorForAllPools = true;
						}
						else
						{
							if( parserContext.state == ParserContext::State::ParsePoolName )
							{
								const Bool poolAdded = AddPool( entryHash );
								RED_ASSERT( poolAdded, "Debug pool allocator slots depleted! (max: %u)", c_maxPools );
								RED_UNUSED( poolAdded );
							}
							else
							{
								const Bool poolAdded = AddExcludedPool( entryHash );
								RED_ASSERT( poolAdded, "Excluded debug pool allocator slots depleted! (max: %u)", c_maxPools );
								RED_UNUSED( poolAdded );
							}
						}

						parserContext.state = ParserContext::State::Decision;
					}
					break;
				case ParserContext::State::ParseProcessName:
					{
						const AnsiChar ch = static_cast< AnsiChar >( fgetc( file ) );
						isEOF = feof( file ) || ch == EOF;

						if ( !isEOF )
						{
							if( !IsEol( ch ) )
							{
								RED_ASSERT( parserContext.processNameCharacterIdx < c_processNameLengthMax, "Too long process name! Max %u characters.", c_processNameLengthMax );
								parserContext.processName[ parserContext.processNameCharacterIdx++ ] = ch;
								continue;
							}

							isEOF = EatNextLine( file, ch );
						}

						const Bool isListForCurrentProcess = !red::Strcmp( parserContext.processName, currentProcessName );
						red::Memset( parserContext.processName, 0, sizeof( parserContext.processName ) );
						parserContext.processNameCharacterIdx = 0;
						if( isListForCurrentProcess )
						{
							parserContext.state = ParserContext::State::Decision;
						}
						else
						{
							parserContext.state = ParserContext::State::SkipUntilNextProcessNameOrEof;
						}
					}
					break;
				case ParserContext::State::SkipUntilNextProcessNameOrEof:
					{
						const AnsiChar ch = static_cast<AnsiChar>( fgetc( file ) );
						isEOF = feof( file ) || ch == EOF;

						if ( isEOF )
						{
							parserContext.state = ParserContext::State::Decision;
						}
						else
						{
							if ( ch == '.' )
							{
								ungetc( ch, file );
								parserContext.state = ParserContext::State::Decision;
							}
						}
					}
					break;
				}
			}

			fclose( file );
		}

		bool IsEol( const AnsiChar ch ) const
		{
			return ( ch == '\r' ) || ( ch == '\n' );
		}

		Bool EatNextLine( FILE* fp, AnsiChar ch ) const
		{
			Bool isEOF = feof( fp );
			if( isEOF )
			{
				return true;
			}

			if ( !IsEol( ch ) )
			{
				return isEOF;
			}

			if( ch == '\r' )
			{
				ch = static_cast< AnsiChar >( fgetc( fp ) );
				isEOF = feof( fp ) || ch == EOF;

				if( ch != '\n' )
				{
					ungetc( ch, fp );
					isEOF = feof( fp );
				}
			}

			return isEOF;
		}

		bool AddPool( THash32 poolHash )
		{
			if( m_poolsWithEnabledDebugAllocatorCount < c_maxPools )
			{
				m_poolsWithEnabledDebugAllocator[ m_poolsWithEnabledDebugAllocatorCount++ ] = poolHash;
				return true;
			}
			return false;
		}

		bool AddExcludedPool( THash32 poolHash )
		{
			if( m_poolsWithDisabledDebugAllocatorCount < c_maxPools )
			{
				m_poolsWithDisabledDebugAllocator[ m_poolsWithDisabledDebugAllocatorCount++ ] = poolHash;
				return true;
			}
			return false;
		}

		Bool IsPoolInList( const THash32* const list, const Uint32 count, const THash32 poolNameHash ) const
		{
			for( Uint32 i = 0; i < count; ++i )
			{
				if( list[ i ] == poolNameHash )
				{
					return true;
				}
			}
			return false;

		}

		PoolDebugAllocatorSettings( const PoolDebugAllocatorSettings& ) = delete;
		PoolDebugAllocatorSettings& operator=( const PoolDebugAllocatorSettings& ) = delete;

		static constexpr THash32 c_allHash = CalculateHash32( "ALL" );
		static constexpr Uint32 c_maxPools = c_poolMaxCount;
		red::THash32 m_poolsWithEnabledDebugAllocator[ c_maxPools ];
		Uint32 m_poolsWithEnabledDebugAllocatorCount = 0;
		red::THash32 m_poolsWithDisabledDebugAllocator[ c_maxPools ];
		Uint32 m_poolsWithDisabledDebugAllocatorCount = 0;

#ifdef RED_MEMORY_FORCE_DEBUG_ALLOCATOR
		Bool m_enableDebugAllocatorForAllPools = true;
#else
		Bool m_enableDebugAllocatorForAllPools = false;
#endif

	};

	Bool ShouldPoolUseDebugAllocator( PoolHandle handle )
	{
		return PoolDebugAllocatorSettings::Get().ShouldPoolUseDebugAllocator( handle );
	}

	Bool UnitTestForceDebugAllocatorOnPool( PoolHandle handle )
	{
		return PoolDebugAllocatorSettings::Get().ForceDebugAllocatorOnPool( handle );
	}

	void UnitTestForceDebugAllocatorOnAllPools()
	{
		return PoolDebugAllocatorSettings::Get().ForceDebugAllocatorOnAllPools();
	}

	void UnitTestResetDebugPoolAllocatorSettings()
	{
		return PoolDebugAllocatorSettings::Get().Reset();
	}
#else
	Bool ShouldPoolUseDebugAllocator( PoolHandle )
	{
		return false;
	}

	Bool UnitTestForceDebugAllocatorOnPool( PoolHandle )
	{
		return false;
	}

	void UnitTestForceDebugAllocatorOnAllPools()
	{
	}

	void UnitTestResetDebugPoolAllocatorSettings()
	{
	}
#endif

	u64 MakeAllocatorStorage( void* allocator, const PoolHandle handle )
	{
		u64 result = reinterpret_cast< u64 >( allocator );
#ifdef RED_MEMORY_DEBUG_ALLOCATOR_ENABLED
		if( ShouldPoolUseDebugAllocator( handle ) )
		{
			RED_ASSERT( IsAligned( result, 8 ) );
			result |= internal::c_AllocatorPointerFlag_UsingDebugAllocator;
		}
#else
		RED_UNUSED( handle );
#endif // #ifdef RED_MEMORY_DEBUG_ALLOCATOR_ENABLED
		return result;
	}

	Bool IsDebugAllocatorEnabled( const PoolStorage& storage )
	{
		RED_UNUSED( storage );

		Bool debugAllocatorEnabled = false;

#ifdef RED_MEMORY_DEBUG_ALLOCATOR_ENABLED
		debugAllocatorEnabled = ( storage.allocatorStorage & internal::c_AllocatorPointerFlag_UsingDebugAllocator ) != 0;
		RED_ASSERT( !debugAllocatorEnabled || ( ( storage.allocatorStorage & internal::c_AllocatorPointerFlag_NeverUseDebugAllocator ) == 0 ) );

		// If null allocator is used, all these pools should use no debug allocator.
		if( debugAllocatorEnabled && ( DecodeAllocatorPointer( storage.allocatorStorage ) == &AcquireNullAllocator() ) )
		{
			debugAllocatorEnabled = false;
		}
#endif// #ifdef RED_MEMORY_DEBUG_ALLOCATOR_ENABLED
		return debugAllocatorEnabled;
	}

}
}
