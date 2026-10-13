/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once
#include "packageStream.h"
#include "../../redContainers/include/blob.h"

namespace red
{
	class RED_REFLECTION_API PackageReadWriteStream : public PackageStream
	{
	public:
		PackageReadWriteStream();
		virtual ~PackageReadWriteStream();

		void SetBuffer( const red::BlobSpan & buffer );

		void * GetCursor() const;

		Uint32 GetBufferSize() const;

	private:

		virtual Uint64 OnSeek( Uint64 requestedPosition ) override final;
		virtual Uint64 OnRead( void * data, Uint64 size, Uint64 position ) override final;
		virtual Uint64 OnWrite( const void * data, Uint64 size, Uint64 position ) override final;
		
		red::BlobSpan m_buffer;
	};

	red::UniquePtr< PackageReadWriteStream > CreatePackageReadWriteStream( const red::BlobSpan & buffer );
}
