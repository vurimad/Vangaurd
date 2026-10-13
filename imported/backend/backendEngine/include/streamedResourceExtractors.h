/*
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../../common/redReflection/include/streamedResourceDataExtractor.h"

namespace res
{
	class BACKEND_ENGINE_API StreamingMeshDataExtractor : public IStreamedResourceDataExtractor
	{
		RTTI_DECLARE_TYPE( StreamingMeshDataExtractor );
	public:
		virtual Bool SupportsResource( const res::ResourcePath& path ) const override final;
		virtual Bool SupportsResource( const res::StreamedResource& res ) const override final;
		virtual void ExtractStreamingData( const res::ResourcePath& path, CName optionalName, const res::StreamedResource& res, StreamingData& outData ) const override final;
	};

	class BACKEND_ENGINE_API StreamingPrefabDataExtractor : public IStreamedResourceDataExtractor
	{
		RTTI_DECLARE_TYPE( StreamingPrefabDataExtractor );
	public:
		virtual Bool SupportsResource( const res::ResourcePath& path ) const override final;
		virtual Bool SupportsResource( const res::StreamedResource& res ) const override final;
		virtual void ExtractStreamingData( const res::ResourcePath& path, CName optionalName, const res::StreamedResource& res, StreamingData& outData ) const override final;
		virtual void ExtractDependencies( const res::ResourcePath& path, const res::StreamedResource& res, DependenciesList& outDependencies ) const override final;
	};

	class BACKEND_ENGINE_API StreamingParticleDataExtractor : public IStreamedResourceDataExtractor
	{
		RTTI_DECLARE_TYPE( StreamingParticleDataExtractor );
	public:
		virtual Bool SupportsResource( const res::ResourcePath& path ) const override final;
		virtual Bool SupportsResource( const res::StreamedResource& res ) const override final;
		virtual void ExtractStreamingData( const res::ResourcePath& path, CName optionalName, const res::StreamedResource& res, StreamingData& outData ) const override final;
	};

	class BACKEND_ENGINE_API StreamingEffectDataExtractor : public IStreamedResourceDataExtractor
	{
		RTTI_DECLARE_TYPE(StreamingEffectDataExtractor);
	public:
		virtual Bool SupportsResource(const res::ResourcePath& path) const override final;
		virtual Bool SupportsResource(const res::StreamedResource& res) const override final;
		virtual void ExtractStreamingData(const res::ResourcePath& path, CName optionalName, const res::StreamedResource& res, StreamingData& outData) const override final;
		virtual void ExtractDependencies(const res::ResourcePath& path, const res::StreamedResource& resource, DependenciesList& outDependencies) const override final;
	};

	class BACKEND_ENGINE_API StreamingEntityDataExtractor : public IStreamedResourceDataExtractor
	{
		RTTI_DECLARE_TYPE( StreamingEntityDataExtractor );
	public:
		virtual Bool SupportsResource( const res::ResourcePath& path ) const override final;
		virtual Bool SupportsResource( const res::StreamedResource& res ) const override final;
		virtual void ExtractStreamingData( const res::ResourcePath& path, CName optionalName, const res::StreamedResource& res, StreamingData& outData ) const override final;
		virtual void ExtractDependencies( const res::ResourcePath& path, const res::StreamedResource& res, DependenciesList& outDependencies ) const override final;
	};

	class BACKEND_ENGINE_API StreamingAppearanceDataExtractor : public IStreamedResourceDataExtractor
	{
		RTTI_DECLARE_TYPE( StreamingAppearanceDataExtractor );
	public:
		virtual Bool SupportsResource( const res::ResourcePath& path ) const override final;
		virtual Bool SupportsResource( const res::StreamedResource& res ) const override final;
		virtual void ExtractStreamingData( const res::ResourcePath& path, CName optionalName, const res::StreamedResource& res, StreamingData& outData ) const override final;
		virtual void ExtractDependencies( const res::ResourcePath& path, const res::StreamedResource& res, DependenciesList& outDependencies ) const override final;
	};

} // res
