/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace game
{
	namespace data
	{
		constexpr TweakDBIDHash::TweakDBIDHash() 
			: m_hash{ 0 }
			, m_length{ 0 }
			, m_reserved{ Uint8( 0 ), Uint8( 0 ), Uint8( 0 ) }
		{}

		constexpr TweakDBIDHash::TweakDBIDHash( THash hash, TLength length )
			: m_hash{ hash }
			, m_length{ length }
			, m_reserved{ Uint8( 0 ), Uint8( 0 ), Uint8( 0 ) }
		{}

		constexpr TweakDBIDHash TweakDBIDHash::Combine( const TweakDBIDHash& lhs, const TweakDBIDHash& rhs )
		{
			return TweakDBIDHash( game::data::prv::crc32_combine( lhs.m_hash, rhs.m_hash, rhs.m_length ), lhs.m_length + rhs.m_length );
		}

		constexpr TweakDBIDHash TweakDBIDHash::Create( const char* str, const TweakDBIDHash::TLength length, const TweakDBIDHash baseHash )
		{
			return TweakDBIDHash( game::data::prv::crc32( baseHash.ToHash(), str, length ), baseHash.m_length + length );
		}

		constexpr TweakDBIDHash TweakDBIDHash::Create( const red::StringView strView, const TweakDBIDHash baseHash )
		{
			return TweakDBIDHash( game::data::prv::crc32( baseHash.ToHash(), strView.Data(), strView.Length() ), baseHash.m_length + strView.Length() );
		}

		//////////////////////////////////////////////////////////////////////////////////////////////////////
		//////////////////////////////////////////////////////////////////////////////////////////////////////
		//////////////////////////////////////////////////////////////////////////////////////////////////////

		constexpr TweakDBID::TweakDBID()
			: m_hash()
		{}

		constexpr TweakDBID::TweakDBID( const TweakDBIDHash hash )
			: m_hash( hash )
		{}

		RED_FORCE_INLINE const Bool TweakDBID::operator==( const TweakDBID& val ) const
		{
			return ( m_hash == val.m_hash );
		}

		RED_FORCE_INLINE const Bool TweakDBID::operator!=( const TweakDBID& val ) const
		{
			return !( m_hash == val.m_hash );
		}

	} //namespace data

} //namespace game
