/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_MEMORY_INTRUSIVE_SHARED_STORAGE_H_
#define _RED_MEMORY_INTRUSIVE_SHARED_STORAGE_H_

namespace red
{
namespace internal
{
	class IntrusiveSharedStorage
	{
	protected:

		IntrusiveSharedStorage();
		IntrusiveSharedStorage( void* pointer );

		IntrusiveSharedStorage( const IntrusiveSharedStorage & copyFrom );
		IntrusiveSharedStorage( IntrusiveSharedStorage && rvalue  );

		void* Get() const;

		template< typename T >
		void Destroy();

		template< typename T, typename PoolType >
		void Destroy();

		template< typename T >
		bool Release();

		template< typename T >
		void AddRef();

		void Swap( IntrusiveSharedStorage & swapWith );


	protected:
	
		~IntrusiveSharedStorage();

	private:

		void AssignRValue( IntrusiveSharedStorage && rvalue );
		
		void * m_pointee;
	};
}
}

#include "intrusiveSharedStorage.hpp"

#endif
