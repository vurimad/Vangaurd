/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace tools
{
	class MeshPackingSource;

	class IMeshCompilationSource
	{
	public:
		virtual ~IMeshCompilationSource() {}

		virtual red::String GetMeshName() const = 0;

		virtual Uint64 GetResourceHash() const = 0; 

		virtual Vector4 GetQuantizationScale() const = 0;

		virtual Vector4 GetQuantizationOffset() const = 0;

		virtual void GetBonePositions( red::DynArray< Vector4 >& outPositions ) const = 0;

		virtual Uint32 GetCustomData( red::DynArray< Uint8 >& outData ) const = 0;

		virtual Uint32 GetTopologyData( Uint32 chunkIndex, red::DynArray< Uint8 >& outData ) const = 0;

		virtual Uint32 GetTopologyMetadata( Uint32 chunkIndex, red::DynArray< Uint8 >& outData ) const = 0;

		virtual Uint32 GetNumMeshLods() const = 0;

		virtual Bool ValidateMeshData() const = 0;

		virtual Bool GetPackingSource( MeshPackingSource* source ) const = 0;

		virtual void GetWindParams( void** windParams ) const = 0;
	};
}