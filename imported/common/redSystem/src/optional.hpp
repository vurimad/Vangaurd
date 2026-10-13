/*
* Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
*/

namespace red
{
	template <typename T>
	Optional<T>::Optional()
		: m_hasValue( false )
	{}

	template <typename T>
	Optional<T>::~Optional()
	{
		DestroyValue();
		m_hasValue = false;
	}

	template <typename T>
	Optional<T>::Optional( const T& value )
		: m_value( value )
		, m_hasValue( true )
	{}

	template <typename T>
	Optional<T>::Optional( T&& value )
		: m_value( std::forward< T >( value ) )
		, m_hasValue( true )
	{}

	template< typename T >
	Optional<T>::Optional( const Optional& other )
		: m_hasValue( other.m_hasValue )
	{
		if ( m_hasValue )
		{
			::new(&m_value) T( other.m_value );
		}
	}

	template< typename T >
	Optional<T>::Optional( Optional&& other )
		: m_hasValue( other.m_hasValue )
	{
		if ( m_hasValue )
		{
			::new( &m_value ) T( std::move( other.m_value ) );
			other.Reset();
		}
	}


	template< typename T >
	template< typename... Args >
	Optional<T>::Optional( VariadicConstructor, Args&&... args )
		: m_value( std::forward< Args >( args )... )
		, m_hasValue( true )
	{}


	template <typename T>
	Bool Optional<T>::HasValue() const
	{
		return m_hasValue;
	}

	template <typename T>
	T& Optional<T>::Value()&
	{
		RED_FATAL_ASSERT( m_hasValue, "Called Value() on Optional that has no value." );
		return m_value;
	}

	template <typename T>
	const T& Optional<T>::Value() const&
	{
		RED_FATAL_ASSERT( m_hasValue, "Called Value() on Optional that has no value." );
		return m_value;
	}

	template <typename T>
	T&& Optional<T>::Value() &&
	{
		RED_FATAL_ASSERT( m_hasValue, "Called Value() on Optional that has no value." );
		return std::move( m_value );
	}

	template <typename T>
	const T&& Optional<T>::Value() const&&
	{
		RED_FATAL_ASSERT( m_hasValue, "Called Value() on Optional that has no value." );
		return std::move( m_value );
	}

	template <typename T>
	const T& Optional<T>::ValueOr( const T& defaultValue ) const
	{
		if ( !m_hasValue )
		{
			return defaultValue;
		}
		return m_value;
	}

	template <typename T>
	T&& Optional<T>::ValueOr( T&& defaultValue )
	{
		if ( !m_hasValue )
		{
			return std::move( defaultValue );
		}
		return std::move( m_value );
	}

	template <typename T>
	void Optional<T>::Swap( Optional& other )
	{
		if ( HasValue() && other.HasValue() )
		{
			std::swap( m_value, other.m_value );
		}
		else if ( HasValue() && !other.HasValue() )
		{
			other = MakeOptional< T >( std::move( m_value ) );
			Reset();
		}
		else if ( !HasValue() && other.HasValue() )
		{
			::new(&m_value) T( std::move( other.m_value ) );
			m_hasValue = true;
			other.Reset();
		}
	}


	template <typename T>
	void Optional<T>::Reset()
	{
		DestroyValue();
	}

	template <typename T>
	Optional<T>& Optional<T>::operator=( const Optional& other )
	{
		if ( &other == this )
			return *this;

		if ( other.m_hasValue )
		{
			if ( m_hasValue )
				m_value = other.m_value;
			else
				::new(&m_value) T( other.m_value );
			m_hasValue = true;
		}
		else
		{
			DestroyValue();
			m_hasValue = false;
		}
			
		return *this;
	}

	template <typename T>
	Optional<T>& Optional<T>::operator=( Optional&& other )
	{
		if ( &other == this )
			return *this;

		if ( other.m_hasValue )
		{
			if ( m_hasValue )
			{
				m_value = std::move( other.m_value );
				other.Reset();
			}
			else
			{
				::new(&m_value) T( std::move( other.m_value ) );
				other.Reset();
			}
			m_hasValue = true;
		}
		else
		{
			DestroyValue();
			m_hasValue = false;
		}

		return *this;
	}

	template <typename T>
	Optional<T>& Optional<T>::operator=( const T& other )
	{
		Reset();
		::new(&m_value) T( other );
		m_hasValue = true;

		return *this;
	}

	template <typename T>
	Optional<T>& Optional<T>::operator=( T&& other )
	{
		Reset();
		::new(&m_value) T( std::move( other ) );
		m_hasValue = true;

		return *this;
	}

	template <typename T>
	Optional<T>::operator bool() const
	{
		return HasValue();
	}

	template< typename T >
	void Optional<T>::DestroyValue()
	{
		if ( m_hasValue )
			m_value.~T();
		m_hasValue = false;
	}


//////////////////////////////////////////////////////////////////////////

	template< typename T, typename... Args >
	Optional<T> MakeOptional( Args&&... args )
	{
		return Optional<T>( Optional<T>::VC, std::forward< Args >( args )... );
	}

	template< typename T >
	Optional<T> MakeEmptyOptional()
	{
		return Optional<T>();
	}
}