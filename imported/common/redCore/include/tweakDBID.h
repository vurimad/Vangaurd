/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "names.h"
#include "tweakDBIDHash.h"
#include "../../redContainers/include/dynArray.h"
#include "../../redContainers/include/string/stringView.h"

#if defined( RED_PLATFORM_WINPC )
	#if !defined( RED_CONFIGURATION_FINAL )
		#define TDBID_COLLISION_DETECTOR_ON
	#endif // RED_CONFIGURATION_FINAL
#endif // RED_PLATFORM_WINPC

#if !defined( RED_CONFIGURATION_FINAL )
#define TWEAKDB_ID_REGISTRY_ENABLED
#endif

#if defined( TWEAKDB_ID_REGISTRY_ENABLED ) && !defined( RED_PLATFORM_CONSOLE )
#define TWEAKDB_ID_REGISTRY_FULL_MODE
#endif

namespace red
{
	class String;
}

namespace game
{
	namespace data
	{
		union TDBIDNumber;
		class TweakDBIDRegistry;
		class DataSerializerInterface;

		struct TweakDBIDHash
		{
			using THash = Uint32;
			using TLength = Uint8;

		public:
			constexpr TweakDBIDHash();
			constexpr TweakDBIDHash( THash hash, TLength length );

			constexpr Bool IsValid() const { return ( m_hash != 0 && m_length != 0 ); }
			
			constexpr const Bool operator==( const TweakDBIDHash& val ) const { return ( val.m_hash == m_hash && val.m_length == m_length ); }
			constexpr const Bool operator<( const TweakDBIDHash& val ) const { return ( m_hash < val.m_hash || ( m_hash == val.m_hash && m_length < val.m_length ) ); }

			constexpr red::THash32 ToHash() const { return m_hash; }
			constexpr red::THash32 CalcHash() const { return m_hash; }

			static constexpr TweakDBIDHash Combine( const TweakDBIDHash& lhs, const TweakDBIDHash& rhs );
			static constexpr TweakDBIDHash Create( const char* str, const TLength length, const TweakDBIDHash baseHash = TweakDBIDHash() );
			static constexpr TweakDBIDHash Create( const red::StringView strView, const TweakDBIDHash baseHash = TweakDBIDHash() );

		private:
			THash m_hash;
			TLength m_length;
			Uint8 m_reserved[ 3 ];
		};
		
		static_assert( sizeof( TweakDBIDHash ) == sizeof( Uint64 ), "Size of TweakDBIDHash is not Uint64" );

		//////////////////////////////////////////////////////////////////////////////////////////////////////
		//////////////////////////////////////////////////////////////////////////////////////////////////////
		//////////////////////////////////////////////////////////////////////////////////////////////////////

		struct REDCORE_API TweakDBID
		{
		public:
			constexpr TweakDBID();

			// Does not register
			constexpr TweakDBID( TweakDBIDHash hash );

			/*constexpr*/ TweakDBID( const char* string );
			TweakDBID( red::String&& string );
			/*explicit*/ TweakDBID( const red::String& string );
//#ifndef RED_CONFIGURATION_FINAL
			explicit TweakDBID( const CName name );
//#endif

		public:
			const Bool		operator==( const TweakDBID& val ) const;
			const Bool		operator!=( const TweakDBID& val ) const;

			TweakDBID&		operator+=( const TweakDBID& val );
			TweakDBID		operator+( const TweakDBID& val ) const;
			TweakDBID		operator+( const char* val ) const;

			constexpr Bool	operator<( const TweakDBID& val ) const { return ( m_hash < val.m_hash ); }

			Bool			IsValid() const;
			Bool			Empty() const { return !IsValid(); } // s.ortiz: temp, to remove once TweakDB changes stabilize

			TweakDBID&		Prepend( const TweakDBID& val );
			TweakDBID&		Append( const TweakDBID& val );
			TweakDBID&		Append( const char* val );

			Uint64			ToNumber() const;
			static TweakDBID FromNumber( const Uint64 number );

			constexpr static TweakDBID NONE() { return TweakDBID(); }

			const red::String& ToString() const;

			constexpr red::THash32 CalcHash() const { return m_hash.CalcHash(); }

		private:
			void Register( red::String&& string ) const;
			void Register( const red::String& string ) const;
			void Register( const CName name ) const;

			static void ReserveRegistry( const Uint32 amount );
			static void ForceRegister( const red::String& string, const TweakDBIDHash hash );

			TweakDBIDHash	m_hash;

			friend class TDBIDHashFunc;
			friend struct TDBIDEqualFunc;
			friend class DataSerializerInterface;

#ifdef TDBID_COLLISION_DETECTOR_ON
			static Bool s_collisionDetectionOn;
		public:
			static void TurnCollisionDetection( Bool on ) { s_collisionDetectionOn = on; }
			static Bool IsCollisionDetectionTurnedOn() { return s_collisionDetectionOn; }
#endif // TDBID_COLLISION_DETECTOR_ON
		};

		static_assert( sizeof( TweakDBID ) == 8, "Size of TweakDBID is not 8B" );

		//////////////////////////////////////////////////////////////////////////////////////////////////////
		//////////////////////////////////////////////////////////////////////////////////////////////////////
		//////////////////////////////////////////////////////////////////////////////////////////////////////

		using RecordID = TweakDBID;
		using RecordIDList = red::DynArray< RecordID >;
		REDCORE_API constexpr RecordID INVALID_ID() { return RecordID(); }

		class TDBIDHashFunc
		{
		public:
			static RED_INLINE red::THash32 GetHash( const TweakDBID& key ) { return key.m_hash.ToHash(); }
		};

		struct TDBIDEqualFunc
		{
			template < typename U >
			static RED_INLINE Bool Equal( const TweakDBID& a, const U& b )
			{
				return a == b;
			}
		};

		struct TDBIDLessFunc
		{
			constexpr Bool operator()( const TweakDBID& a, const TweakDBID& b ) const
			{
				return a < b;
			}
		};

		struct TDBIDHashPolicy
		{
			using HashFunc = TDBIDHashFunc;
			using EqualFunc = TDBIDEqualFunc;
		};

	} //namespace data
} //namespace game

#define TDBID( str ) \
	game::data::TweakDBID( str )

#define TDBID_CONSTEXPR_NOREG( str ) \
	std::constant_integral< TweakDBID, TweakDBID{ TweakDBIDHash::Create( str ) } >::value

RED_ALLOW_TYPE_AS_POD( game::data::TweakDBID );

namespace red
{
	namespace err
	{
		struct TweakDBIDStorage
		{
			char m_name[64]; // bump if insufficient
			Uint64 m_number;
		};
	}

	template<Uint32 Length>
	struct err::CrashDataTypeAdapter< ::game::data::TweakDBID, Length >
	{
		// Bump as neeeded to not truncate path
		static_assert(Length == 0, "Length should already be set for longest resource path");
		using StorageType = TweakDBIDStorage;
		using SetType = ::game::data::TweakDBID;

		static CrashDataCopyResult Copy(StorageType& storage, const SetType& value)
		{
			storage.m_number = value.ToNumber();

			// Note: our strcpy wrapper doesn't have the same behavior on Orbis, so manually truncate
			const Uint32 destSize = RED_ARRAY_COUNT_U32(storage.m_name);
			if (!red::Strcpy(storage.m_name, value.ToString().AsChar(), destSize, destSize - 1))
			{
				return CrashDataCopyResult::Error;
			}
			
			const Uint32 srcLen = value.ToString().Length();
			return srcLen + 1 <= destSize ? CrashDataCopyResult::Success : CrashDataCopyResult::Truncated;
		}

		static Bool Print(char* buffer, Uint32 bufferLen, const StorageType& val)
		{
			static_assert(sizeof(val.m_number) == sizeof(Uint64), "");
			const Int32 ret = red::SNPrintFUnsafe(buffer, bufferLen, "%s<0x%016llX>", val.m_name, val.m_number);
			return ret > -1;
		}
	};
}

#include "tweakDBID.hpp"
