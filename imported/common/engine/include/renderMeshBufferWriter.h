/**
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "redEngineApi.h"
#include "../../redReflection/include/mathUtils.h"
#include "../../core/include/memoryBufferFile.h"
#include "../../redMath/include/float16compressor.h"
#include "../../renderData/include/gpuWrapApiVertexPacking.h"

// Pointer based data buffer writer
class RED_ENGINE_API CRenderMeshBufferWriter : public GpuWrapApi::VertexPacking::IBufferWriter
{
public:
	CRenderMeshBufferWriter() : m_buffer( nullptr ) {}
	explicit CRenderMeshBufferWriter( GrowableMemoryBuffer& buffer );

	virtual Uint32 GetOffset() const override;
	virtual void Align( Uint32 size ) override;
	virtual void WriteFloat( Float data ) override;
	virtual void WriteFloats( const Float* data, Uint32 numFloats ) override;
	virtual void WriteUnsignedByte( Uint8 data ) override;
	virtual void WriteUnsignedByteN( Float data ) override;
	virtual void WriteSignedByteN( Float data ) override;
	virtual void WriteUnsignedShort( Uint16 data ) override;
	virtual void WriteUnsignedShorts( const Uint16* data, Uint32 numShorts ) override;
	virtual void WriteUnsignedShortN( Float data ) override;
	virtual void WriteUnsignedShortsN( const Float* data, Uint32 numFloats ) override;
	virtual void WriteSignedShortN( Float data ) override;
	virtual void WriteSignedShortsN( const Float* data, Uint32 numFloats ) override;
	virtual void WriteUnsignedInt( Uint32 data ) override;
	virtual void WriteFloat16( Float f ) override;
	virtual void WriteFloats16( const Float* data, Uint32 numFloats ) override;
	virtual void WriteColor( Uint32 data ) override;
	virtual void WriteDec3N( Float x, Float y, Float z ) override;
	virtual void WriteDec4N( Float x, Float y, Float z, Float w ) override;

private:
	RED_INLINE void Write( const void* data, Uint32 size );

	GrowableMemoryBuffer* m_buffer;
};
