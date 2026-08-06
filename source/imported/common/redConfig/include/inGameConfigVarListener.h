/**
 * Copyright (c) 2019-2020 CD Projekt Red. All Rights Reserved.
 */

#pragma once

namespace InGameConfig
{
	class Var;

	enum class ChangeReason : Int8;
	enum class NotificationType : Uint8;
	enum class Source : Int8;
	enum class VarType : Uint8;

	template<typename T, typename OpCallReturnType = void>
	class TCallback
	{
	public:
		using Functor = red::FixedSizeFunction<T>;
		using ID = Uint64;

		explicit TCallback( Functor &&callback, Uint8 priority = 0 ) noexcept
			: m_callback{ std::move( callback ) }
			, m_id{ GenerateID() }
			, m_priority( priority )
		{}

		explicit TCallback( const Functor &callback, Uint8 priority = 0 ) noexcept
			: m_callback{ callback }
			, m_id{ GenerateID() }
			, m_priority( priority )
		{}

		ID GetID() const noexcept
		{
			return m_id;
		}

		Bool operator==( const TCallback &other ) const noexcept
		{
			return m_id == other.m_id;
		}

		Bool operator<( const TCallback& other ) const noexcept
		{
			// higher number means earlier in a sorted array with default compare (std::less)
			return m_priority > other.m_priority;
		}

		template<typename... Args>
		OpCallReturnType operator()( Args... args ) const noexcept
		{
			return m_callback( std::forward<Args>( args )... );
		}

	private:
		static ID GenerateID() noexcept
		{
			static red::Atomic< Uint64 > s_id{ 0 };
			return s_id.Increment();
		}

		Functor m_callback;
		ID m_id;
		Uint8 m_priority;
	};

	using VarListener = TCallback<void( CName, CName, VarType, ChangeReason )>;
	using VarValidator = TCallback<Bool( CName, CName, VarType, Source ), Bool>;
	using NotificationHandler = TCallback<void( NotificationType )>;

} // namespace InGameConfig
