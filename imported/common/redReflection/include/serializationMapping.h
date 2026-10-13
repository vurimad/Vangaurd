/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

#include "handle.h"
#include "resourceLoaderTypes.h"
#include "serializationLoadingToken.h"
#include "../../redContainers/include/blob.h"

class CResource;

namespace job
{
	class Builder;
}

namespace rtti
{
	class IType;
	class Property;
	class ClassType;
}

namespace res
{
	class ResourcePath;
	class ResourceLoaderThrottler;
}

namespace serialization
{
	struct AsyncSourceReadBuffer;
	class BufferHandleRO;
	class IBufferAsyncProxy;
	typedef red::UniquePtr< IBufferAsyncProxy > BufferAsyncPtr;

	struct MapperLoadBufferParam
	{
		res::ResourceLoaderThrottler* throttler{ nullptr };
		io::EAsyncPriority priority{ io::eAsyncPriority_Normal };
		void* userData{ nullptr };
		const AsyncSourceReadBuffer* inlineData{ nullptr };
	};

	// Data mapping (runtime <-> persistent) interface used by binary serialization
	// NOTE: there's only one class because the IFile is also used for both saving/loading, this can be changed once the IFile is split
	class RED_REFLECTION_API IMapper
	{
	public:
		// NOTE: changing sizes of those types requires global resave
		typedef Uint16 NameIndex;
		typedef Uint16 TypeIndex;
		typedef Uint16 PropertyIndex;
		typedef Uint16 PathIndex;
		typedef Int32 ObjectIndex; // yup, it happened 64K+ objects in one files
		typedef Uint16 BufferIndex;

		struct MapBufferFlags
		{
			Bool useCompression{ false };
			Bool useFastCompression{ false };
			Bool allowNonDefaultCompressionType{ false };
			Bool hintGPUMemory{ false };
			Bool hintAutoLoadPC{ false };
			Bool hintAutoLoadXboxOne{ false };
			Bool hintAutoLoadPS4{ false };
		};

		struct MapBufferParam
		{
			const BufferHandleRO& inMemoryData;
			const BufferHandleRO& precompressedDataForCooking;
			MapBufferFlags flags;
		};


		IMapper();
		virtual ~IMapper();

		/////////////////

		virtual void MapName( const CName& name, NameIndex& outIndex ) = 0;

		virtual void MapType( const rtti::IType* rttiType, TypeIndex& outIndex ) = 0;

		virtual void MapPointer( const THandle< ISerializable >& objectRef, ObjectIndex& outIndex ) = 0;

		virtual void MapResourceReference( const res::ResourcePath& path, PathIndex& outIndex ) = 0;

		virtual void MapResourceDeferredReference( const res::ResourcePath& path, PathIndex& outIndex ) = 0;

		virtual void MapBuffer( const MapBufferParam& param, BufferIndex& outIndex ) = 0;

		/////////////////

		virtual void UnmapName( const NameIndex index, CName& outName ) = 0;

		virtual void UnmapType( const TypeIndex index, const rtti::IType*& outTypeRef ) = 0;

		virtual void UnmapPointer( const ObjectIndex index, THandle< ISerializable >& outObjectRef ) = 0;

		virtual void UnmapResourceReference( const PathIndex index, res::ResourcePath& outPath, res::ResourceTokenHandle& token ) = 0;

		virtual void UnmapResourceDeferredReference( const PathIndex index, res::ResourcePath& outPath ) = 0;

		struct LoadBufferToken
		{
			using DispatchLoadingJobsFn = LoadingToken( const MapperLoadBufferParam& param, IBufferAsyncProxy& proxy );

			DispatchLoadingJobsFn* callback{ nullptr };
			void* userData{ nullptr };
			Bool isAutoload{ false };
		};

		virtual void UnmapBuffer( const BufferIndex index, const LoadBufferToken& token, BufferAsyncPtr& outBufferAccess ) = 0;
	
		// ctremblay: Kinda an hack. EntityTemplate use new Package Serialization system. 
		// There is currently no way to handle relink seamlessly. 
		virtual bool IsRelinking() const = 0;
		virtual res::ResourcePath RelinkPath( const res::ResourcePath& path ) const = 0;
	};
}
