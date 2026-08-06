/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_SERIALIZER_H_
#define _RED_MEMORY_SERIALIZER_H_

#include "stream.h"
#include "../../redSystem/include/utility.h"

namespace red
{
namespace memory
{
	class RED_MEMORY_API Serializer : NonCopyable
	{
	public:
		Serializer();
		~Serializer();

		void Initialize( Stream* stream );
		void Uninitialize();

		u32 DataAvailableToWrite() const;

		template< typename T >
		Bool Serialize( const T & object );

		Bool Serialize( const void * buffer, u32 size );

	private:
		StreamView m_streamView;
	};
}
}

#include "serializer.hpp"

#endif
