/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#pragma once

#include "packageStream.h"
#include "../../redMemory/include/uniqueBuffer.h"
#include "../../redContainers/include/blob.h"

namespace red
{
	class RED_REFLECTION_API PackageWriteStream : public PackageStream
	{
	public:
		PackageWriteStream();
		virtual ~PackageWriteStream();

		void Initialize( Uint32 reservedSize );

		void * GetWriteCursor() const;

		red::UniqueBuffer ReleaseBuffer();

		red::BlobView GetBufferView() const;
		red::BlobSpan GetBuffer();
		red::BlobView GetRange( Uint64 start, Uint64 end ) const;   
		red::BlobSpan GetRange( Uint64 start, Uint64 end );

	private:

		virtual Uint64 OnSeek( Uint64 requestedPosition ) override final;
		virtual Uint64 OnRead( void * data, Uint64 size, Uint64 position ) override final;
		virtual Uint64 OnWrite( const void * data, Uint64 size, Uint64 position ) override final;
		
		red::UniqueBuffer m_buffer;
	};

	RED_REFLECTION_API red::UniquePtr< PackageWriteStream > CreateWriteStream( Uint32 reservedSize );
}
