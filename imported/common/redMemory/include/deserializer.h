/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_DESERIALIZER_H_
#define _RED_MEMORY_DESERIALIZER_H_

#include "stream.h"
#include "../../redSystem/include/utility.h"

namespace red
{
namespace memory
{
	class RED_MEMORY_API Deserializer : NonCopyable
	{
	public:
		Deserializer();
		~Deserializer();

		void Initialize( Stream* stream );
		void Uninitialize();

		u32 DataAvailableToRead() const;

		template< typename T >
		void Deserialize( T & object);

		void Deserialize( void * buffer, u32 size, u32 & readSize );

		void Seek( u32 size );

	private:
		StreamView m_streamView;
	};
}
}

#include "deserializer.hpp"

#endif
