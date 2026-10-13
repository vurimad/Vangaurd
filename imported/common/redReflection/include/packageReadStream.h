/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "packageStream.h"
#include "../../redContainers/include/blob.h"

namespace red
{
	class RED_REFLECTION_API PackageReadStream : public PackageStream
	{
	public:
		PackageReadStream();
		virtual ~PackageReadStream();

		void SetBuffer( const red::BlobView & buffer );

		RED_MOCKABLE const void * GetReadCursor() const;
		
		Uint32 GetBufferSize() const;

	private:

		virtual Uint64 OnWrite( const void * data, Uint64 size, Uint64 position ) override final;
		virtual Uint64 OnRead( void * data, Uint64 size, Uint64 position ) override final;
		virtual Uint64 OnSeek( Uint64 positionRequest ) override final;

		red::BlobView m_data;
	};

	RED_REFLECTION_API red::UniquePtr< PackageReadStream > CreateReadStream( const red::BlobView & blobView );
}
