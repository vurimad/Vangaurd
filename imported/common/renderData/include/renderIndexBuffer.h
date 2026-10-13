/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/
#pragma once

namespace rend
{

	class IIndexBufferDataProvider
	{
	public:
		virtual ~IIndexBufferDataProvider() = default;

		virtual Uint32 GetChunkCount() const = 0;
		virtual Bool HasIndexData( Uint32 chunkIndex ) const = 0;
		virtual Uint32 GetIndexCount( Uint32 chunkIndex ) const = 0;
		virtual red::ArraySpan<const Uint16> GetIndexData( Uint32 chunkIndex ) const = 0;
	};

}
