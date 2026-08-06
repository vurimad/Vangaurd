/*
* Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
*/
#pragma once

namespace red
{
	template< typename T >
	class Optional
	{
	public:
		using ValueType = T;

		enum VariadicConstructor
		{
			VC
		};

		Optional();
		~Optional();

		Optional( const T& value );
		Optional( T&& value );

		Optional( const Optional& other );
		Optional( Optional&& other );

		template< typename... Args >
		Optional( VariadicConstructor var, Args&&... args );

		Bool HasValue() const;

		T& Value()&;
		const T& Value() const&;
		T&& Value() &&;
		const T&& Value() const&&;

		const T& ValueOr( const T& defaultValue ) const;
		T&& ValueOr( T&& defaultValue );

		void Swap( Optional& other );
		void Reset();

		Optional<T>& operator=( const Optional& other );
		Optional<T>& operator=( Optional&& other );

		Optional<T>& operator=( const T& other );
		Optional<T>& operator=( T&& other );

		explicit operator bool() const;

	private:
		union
		{
			char m_invalid;
			std::remove_const_t< T > m_value;
		};

		Bool m_hasValue;

		void DestroyValue();
	};

	template< typename T, typename... Args >
	Optional<T> MakeOptional( Args&&... args );
}

#include "../src/optional.hpp"