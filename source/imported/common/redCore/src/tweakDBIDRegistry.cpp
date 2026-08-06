/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "tweakDBIDRegistry.h"

#include "../../redContainers/include/string/string.h"
#include "../../redSystem/include/readWriteSpinLock.h"


namespace
{
	constexpr Uint32 s_tweakDBIDRegistryInitialCapacity = 4000000; // ~100MB + strings
}

namespace game
{
	namespace data
	{
#if defined( TWEAKDB_ID_REGISTRY_ENABLED )

		class TweakDBIDRegistry
		{
			RED_USE_MEMORY_POOL( red::PoolDebug );

		public:
			TweakDBIDRegistry();

			void Initialize();
			void Reserve( const Uint32 amount );

			const red::String& GetName( const TweakDBIDHash& key ) const;

			void CreateEntry( red::String&& str, const TweakDBIDHash& hash );
			void CreateEntry( const red::String& str, const TweakDBIDHash& hash );
//#ifndef RED_CONFIGURATION_FINAL
			void CreateEntry( CName name, const TweakDBIDHash& hash );
//#endif
			TweakDBIDHash MergeEntries( const TweakDBIDHash& lhs, const TweakDBIDHash& rhs, const TweakDBIDHash& merged = TweakDBIDHash() );

		private:
			struct Storage
			{
				RED_USE_MEMORY_POOL( red::PoolDebug );

				red::String str;

				Bool operator==( const Storage& rhs ) { return str == rhs.str; }
				Bool Incomplete() { return str.BeginsWith( "." ); }
			};

			const red::String& GetName_NoLock( const TweakDBIDHash& key ) const;
			
			Bool CreateEntry( const TweakDBIDHash& key, red::String&& str );

			template< typename T >
			void CreateEntry_Internal( T&& val, const TweakDBIDHash& hash );

			Bool InsertEntry_NoLock( const TweakDBIDHash& key, red::String&& str );
			Bool InsertEntry( const TweakDBIDHash& key, red::String&& str );

			const Storage* FindEntry_NoLock( const TweakDBIDHash& key ) const;
			const Storage* FindEntry( const TweakDBIDHash& key ) const;

			using StorageMap = red::HashMap< TweakDBIDHash, Storage >;

			using MapLock = red::RWSpinLock;
			mutable MapLock m_lock;
			StorageMap m_registry;
		};

		TweakDBIDRegistry& AcquireTweakDBIDRegistry();

		TweakDBIDRegistry::TweakDBIDRegistry()
			: m_registry( s_tweakDBIDRegistryInitialCapacity, red::PoolDebug() )
		{
		}

		void TweakDBIDRegistry::Initialize()
		{
			CreateEntry( TweakDBIDHash(), "" );
		}

		void TweakDBIDRegistry::Reserve( const Uint32 amount )
		{
			m_registry.Reserve( amount );
		}

		const red::String& TweakDBIDRegistry::GetName_NoLock( const TweakDBIDHash& key ) const
		{
			const Storage* record = FindEntry_NoLock( key );
			if( record )
			{
				return record->str;
			}
			
			return red::String::EMPTY();
		}

		const red::String& TweakDBIDRegistry::GetName( const TweakDBIDHash& key ) const
		{
			const Storage* record = FindEntry( key );
			if( record )
			{
				return record->str;
			}

			return red::String::EMPTY();
		}

		namespace helper
		{
			// Make into a string only if actually register
 			red::String ExtractToString( const red::String& str )
 			{
 				return str;
 			}

			red::String ExtractToString( red::String&& str )
			{
				return std::move( str );
			}

//#ifndef RED_CONFIGURATION_FINAL
			red::String ExtractToString( const CName name )
			{
				return name.AsChar();
			}
		}
//#endif

		void TweakDBIDRegistry::CreateEntry( const red::String& str, const TweakDBIDHash& hash )
		{
			return CreateEntry_Internal( str, hash );
		}

		void TweakDBIDRegistry::CreateEntry( red::String&& str, const TweakDBIDHash& hash )
		{
			return CreateEntry_Internal( str, hash );
		}

//#ifndef RED_CONFIGURATION_FINAL
		void TweakDBIDRegistry::CreateEntry( const CName name, const TweakDBIDHash& hash )
		{
			return CreateEntry_Internal( name, hash );
		}
//#endif

		template< typename T >
		void TweakDBIDRegistry::CreateEntry_Internal( T&& val, const TweakDBIDHash& hash )
		{		
			bool searchResult = false;
			{
				red::ScopedSharedLock< MapLock > lock( m_lock );
				searchResult = m_registry.KeyExist( hash );
			}

#ifdef TDBID_COLLISION_DETECTOR_ON
			if ( TweakDBID::IsCollisionDetectionTurnedOn() && searchResult )
			{
				auto storage = Storage( { helper::ExtractToString( std::forward< T >( val ) ) } );

				{
					red::ScopedSharedLock< MapLock > lock( m_lock );
					auto existingVal = m_registry.Find( hash );
					auto& existingStorage = ( *existingVal ).Value();
					Bool incompleteData = existingStorage.Incomplete() || storage.Incomplete();

					if ( !incompleteData && !( existingStorage == storage ) )
					{
						RED_FATAL( "[ DO NOT MERGE ] TweakDBID Registry HASH collision:    '%s'    '%s' - resolve it manually ( name your variable differently ).", storage.str.AsChar(), existingStorage.str.AsChar() );
					}
				}
			}
#endif // TDBID_COLLISION_DETECTOR_ON

			if( !searchResult )
			{
				auto storage = Storage( { helper::ExtractToString( std::forward< T >( val ) ) } );
				red::ScopedLock< MapLock > lock( m_lock );
				m_registry.Set( hash, storage );
			}
		}

		TweakDBIDHash TweakDBIDRegistry::MergeEntries( const TweakDBIDHash& lhs, const TweakDBIDHash& rhs, const TweakDBIDHash& merged )
		{
			TweakDBIDHash hash = merged;
			if ( !merged.IsValid() )
			{
				hash = TweakDBIDHash::Combine( lhs, rhs );
			}

			red::ScopedLock< MapLock > lock( m_lock );

			// The logic behind is this:
			// This optimization works only if merged tweak DB IDs are needed more than once. This 
			// turns out to be the case in Debug/Release.
			// Ideally, in Final, any merged ID will be needed only once. So in Final, all this gives
			// is extra check (which involves a lock and hash map lookup) for each ID and no benefit.
#ifndef RED_CONFIGURATION_FINAL
			if ( FindEntry_NoLock( hash ) )
			{
				return hash;
			}
#endif
			
			// TODO : StringBuilder?
			//      - Does it matter in this case? The registry needs red::String in the end anyway.
			//        The real problem with this is that it's not needed if the resulting tweak DB ID
			//        already exists.
			const auto& strLhs = GetName_NoLock( lhs );
			const auto& strRhs = GetName_NoLock( rhs );
			red::String str = strLhs + strRhs;
			
			// newEntry will be invalid ( 0, 0 ) if it failed to add ( e.g. this hash already exists )
			Bool success = InsertEntry_NoLock( hash, std::move( str ) ); // TODO : && ?
			
			return success ? hash : TweakDBIDHash();
		}

		Bool TweakDBIDRegistry::CreateEntry( const TweakDBIDHash& key, red::String&& str )
		{
			auto record = FindEntry( key );
			if( record )
			{
				return true;
			}

			return InsertEntry( key, std::move( str ) );
		}

		Bool TweakDBIDRegistry::InsertEntry_NoLock( const TweakDBIDHash& key, red::String&& str )
		{
			return m_registry.Insert( key, Storage( { std::move( str ) } ) ).IsSuccessful();
		}

		Bool TweakDBIDRegistry::InsertEntry( const TweakDBIDHash& key, red::String&& str )
		{
			red::ScopedLock< MapLock > lock( m_lock );
			return InsertEntry_NoLock( key, std::move( str ) );
		}

		const game::data::TweakDBIDRegistry::Storage* TweakDBIDRegistry::FindEntry_NoLock( const TweakDBIDHash& key ) const
		{
			return m_registry.FindPtr( key );
		}

		const game::data::TweakDBIDRegistry::Storage* TweakDBIDRegistry::FindEntry( const TweakDBIDHash& key ) const
		{
			red::ScopedSharedLock< MapLock > scopedLock( m_lock );
			return FindEntry_NoLock( key );
		}

		TweakDBIDRegistry* s_tweakDBIDRegistryDebugger;

		TweakDBIDRegistry& AcquireTweakDBIDRegistry()
		{
			struct TweakDBIDRegistryProxy
			{
				TweakDBIDRegistryProxy()
				{
					registry.Initialize();
					s_tweakDBIDRegistryDebugger = &registry;
				}

				TweakDBIDRegistry registry;
			};

			static TweakDBIDRegistryProxy s_nameRegistryProxy;
			return s_nameRegistryProxy.registry;
		}

#endif // TWEAKDB_ID_REGISTRY_ENABLED

		void Debug_ReserveRegistryStorage( const Uint32 amount )
		{
#if defined( TWEAKDB_ID_REGISTRY_ENABLED )
			AcquireTweakDBIDRegistry().Reserve( amount );
#else
			RED_UNUSED( amount );
#endif // TWEAKDB_ID_REGISTRY_ENABLED
		}

		void Debug_CreateRegistryEntry( const red::String& str, const TweakDBIDHash& hash )
		{
#if defined( TWEAKDB_ID_REGISTRY_ENABLED )
			AcquireTweakDBIDRegistry().CreateEntry( str, hash );
#else
			RED_UNUSED2( str, hash );
#endif // TWEAKDB_ID_REGISTRY_ENABLED
		}

		void Debug_CreateRegistryEntry( red::String&& str, const TweakDBIDHash& hash )
		{
#if defined( TWEAKDB_ID_REGISTRY_ENABLED )
			AcquireTweakDBIDRegistry().CreateEntry( str, hash );
#else
			RED_UNUSED2( str, hash );
#endif // TWEAKDB_ID_REGISTRY_ENABLED
		}

		void Debug_CreateRegistryEntry( const CName name, const TweakDBIDHash& hash )
		{
#if defined( TWEAKDB_ID_REGISTRY_ENABLED )
			AcquireTweakDBIDRegistry().CreateEntry( name, hash );
#else
			RED_UNUSED2( name, hash );
#endif // TWEAKDB_ID_REGISTRY_ENABLED
		}

		TweakDBIDHash Debug_MergeRegistryEntries( const TweakDBIDHash& lhs, const TweakDBIDHash& rhs, const TweakDBIDHash& merged )
		{
#if defined( TWEAKDB_ID_REGISTRY_ENABLED )
			return AcquireTweakDBIDRegistry().MergeEntries( lhs, rhs, merged );
#else
			RED_UNUSED3( lhs, rhs, merged );
			return {};
#endif // TWEAKDB_ID_REGISTRY_ENABLED
		}

		const red::String& Debug_GetRegistryString( const TweakDBIDHash hash )
		{
#if defined( TWEAKDB_ID_REGISTRY_ENABLED )
			return AcquireTweakDBIDRegistry().GetName( hash );
#else
			RED_UNUSED( hash );
			return red::String::EMPTY();
#endif // TWEAKDB_ID_REGISTRY_ENABLED
		}

		red::StringView Debug_GetRegistryStringView( const TweakDBIDHash hash )
		{
#if defined( TWEAKDB_ID_REGISTRY_ENABLED )
			return AcquireTweakDBIDRegistry().GetName( hash );
#else
			RED_UNUSED( hash );
			return { "" };
#endif // TWEAKDB_ID_REGISTRY_ENABLED
		}
	}
}
