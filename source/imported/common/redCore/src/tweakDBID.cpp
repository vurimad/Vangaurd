/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "tweakDBID.h"
#include "tweakDBIDRegistry.h"

#include "../../redContainers/include/string/string.h"
#include "../../redSystem/include/readWriteSpinLock.h"
#include "../../redSystem/include/threads.h"


namespace game
{
	namespace data
	{
#ifdef TDBID_COLLISION_DETECTOR_ON
		Bool TweakDBID::s_collisionDetectionOn = false;
#endif // TDBID_COLLISION_DETECTOR_ON

		namespace helper
		{
			static constexpr Uint32 StringLength( const char* str )
			{
				return *str ? 1 + helper::StringLength( str + 1 ) : 0;
			}
		}

		TweakDBID::TweakDBID( const char* string )
			: m_hash( TweakDBIDHash::Create( string, helper::StringLength( string ) ) )
		{
			Register( string );
		}

		TweakDBID::TweakDBID( red::String&& string )
			: m_hash( TweakDBIDHash::Create( string ) )
		{
			Register( std::move( string ) );
		}

		TweakDBID::TweakDBID( const red::String& string )
			: m_hash( TweakDBIDHash::Create( string ) )
		{
			Register( string );
		}

//#ifndef RED_CONFIGURATION_FINAL
		TweakDBID::TweakDBID( const CName name )
			: m_hash( TweakDBIDHash::Create( name.AsStringView() ) )
		{
			Register( name.AsChar() );
		}
//#endif

		void TweakDBID::Register( red::String&& string ) const
		{
#ifdef TWEAKDB_ID_REGISTRY_FULL_MODE
			Debug_CreateRegistryEntry( std::move( string ), m_hash );
#else
			RED_UNUSED( string );
#endif // TWEAKDB_ID_REGISTRY_FULL_MODE
		}

		void TweakDBID::Register( const red::String& string ) const
		{
#ifdef TWEAKDB_ID_REGISTRY_FULL_MODE
			Debug_CreateRegistryEntry( string, m_hash );
#else
			RED_UNUSED( string );
#endif // TWEAKDB_ID_REGISTRY_FULL_MODE
		}

		void TweakDBID::Register( const CName name ) const
		{
#ifdef TWEAKDB_ID_REGISTRY_FULL_MODE
			Debug_CreateRegistryEntry( name, m_hash );
#else
			RED_UNUSED( name );
#endif // TWEAKDB_ID_REGISTRY_FULL_MODE
		}

		void TweakDBID::ReserveRegistry( const Uint32 amount )
		{
#ifdef TWEAKDB_ID_REGISTRY_ENABLED
			Debug_ReserveRegistryStorage( amount );
#else
			RED_UNUSED( amount );
#endif
		}

		void TweakDBID::ForceRegister( const red::String& string, const TweakDBIDHash hash )
		{
#ifdef TWEAKDB_ID_REGISTRY_ENABLED
			Debug_CreateRegistryEntry( string, hash );
#else
			RED_UNUSED2( string, hash );
#endif
		}

		TweakDBID& TweakDBID::operator+=( const TweakDBID& val )
		{
			return Append( val );
		}

		TweakDBID TweakDBID::operator+( const TweakDBID& val ) const
		{ 
			const TweakDBIDHash merged = TweakDBIDHash::Combine( m_hash, val.m_hash );
#ifdef TWEAKDB_ID_REGISTRY_FULL_MODE
			Debug_MergeRegistryEntries( m_hash, val.m_hash, merged );
#endif // TWEAKDB_ID_REGISTRY_FULL_MODE
			return merged;
		}

		TweakDBID TweakDBID::operator+( const char* val ) const
		{
			const TweakDBIDHash merged = TweakDBIDHash::Create( val, m_hash );
#ifdef TWEAKDB_ID_REGISTRY_FULL_MODE
			if ( IsValid() )
			{
				const TweakDBIDHash appended = TweakDBIDHash::Create( val );
				Debug_CreateRegistryEntry( val, appended );
				Debug_MergeRegistryEntries( m_hash, appended, merged );
			}
			else
			{
				Debug_CreateRegistryEntry( val, merged );
			}
#endif // TWEAKDB_ID_REGISTRY_FULL_MODE
			return merged;
		}

		Bool TweakDBID::IsValid() const
		{
			return m_hash.IsValid();
		}

		TweakDBID& TweakDBID::Prepend( const TweakDBID& val )
		{
			if( !IsValid() )
			{
				m_hash = val.m_hash;
			}
			else if( val.IsValid() )
			{
				const TweakDBIDHash merged = TweakDBIDHash::Combine( val.m_hash, m_hash );
#ifdef TWEAKDB_ID_REGISTRY_FULL_MODE
				Debug_MergeRegistryEntries( val.m_hash, m_hash, merged );
#endif // TWEAKDB_ID_REGISTRY_FULL_MODE
				m_hash = merged;
			}
			// if val is not valid then returning this is the way to go

			return *this;
		}

		TweakDBID& TweakDBID::Append( const TweakDBID& val )
		{
			if( !IsValid() )
			{
				m_hash = val.m_hash;
			}
			else if( val.IsValid() )
			{
				const TweakDBIDHash merged = TweakDBIDHash::Combine( m_hash, val.m_hash );
#ifdef TWEAKDB_ID_REGISTRY_FULL_MODE
				Debug_MergeRegistryEntries( m_hash, val.m_hash, merged );
#endif // TWEAKDB_ID_REGISTRY_FULL_MODE
				m_hash = merged;
			}
			// if val is not valid then returning this is the way to go

			return *this;
		}

		TweakDBID& TweakDBID::Append( const char* val )
		{
			const TweakDBIDHash merged = TweakDBIDHash::Create( val, m_hash );

#ifdef TWEAKDB_ID_REGISTRY_FULL_MODE
			if ( IsValid() )
			{	
				const TweakDBIDHash appended = TweakDBIDHash::Create( val );
				Debug_CreateRegistryEntry( val, appended );
				Debug_MergeRegistryEntries( m_hash, appended, merged );
			}
			else
			{
				Debug_CreateRegistryEntry( val, merged );
			}
#endif // TWEAKDB_ID_REGISTRY_FULL_MODE
			m_hash = merged;
			return *this;
		}

		Uint64 TweakDBID::ToNumber() const
		{
			union { TweakDBID id; Uint64 number; } converter { *this };
			return converter.number;
		}

		TweakDBID TweakDBID::FromNumber( const Uint64 number )
		{
			union { Uint64 number; TweakDBID id; } converter { number };
			return converter.id;
		}

		const red::String& TweakDBID::ToString() const
		{
#ifdef TWEAKDB_ID_REGISTRY_ENABLED
			return Debug_GetRegistryString( m_hash );
#else
			return String::EMPTY();
#endif
		}
	}
}
