/**
* Copyright (c) 2020 CDProjekt Red, Inc. All Rights Reserved.
*/

#pragma once

#include "utility.h"

namespace red
{
	template< typename T >
	struct DefaultScopedPtrDeleter
	{
		DefaultScopedPtrDeleter();

		void operator()(T* ptr) const;
	};

	template< typename T, typename DeleterType = DefaultScopedPtrDeleter< T > >
	class ScopedPtr : red::NonCopyable
	{
	public:
		explicit ScopedPtr( T* object = nullptr );
		~ScopedPtr() noexcept;

		T* Get() const;
		void Reset( T* object = nullptr );
		void Swap( ScopedPtr< T, DeleterType >& other );

		T* operator->() const;
		T& operator*() const;
	
		explicit operator bool() const;
		bool operator!() const;
		
	private:
		T* m_ptr;
	};
}

#include "scopedPtr.hpp"