/**
* Copyright (c) 2017 CDProjekt Red, Inc. All Rights Reserved.
*/

#pragma once

#include "thumbnailGenerator.h"

class CMesh;
class IMaterial;

namespace res
{
	class ThumbnailRenderingManager;

	class MaterialThumbnailGenerator: public IThumbnailGenerator
	{
		RTTI_DECLARE_TYPE( MaterialThumbnailGenerator );

	public:
		MaterialThumbnailGenerator();

		virtual void Generate( const THandle<CResource>& resource, ThumbGenContext& ctx, red::SharedPtr<rend::ColorResult>& result ) override;
		virtual void OnReset() override;

	protected:
		void SetupProxy( const THandle< CMesh >& mesh, const TRenderPtr< IRenderResource >& material, ThumbnailRenderingManager* renderingManager );
		void SetupProxy( const THandle< CMesh >& mesh, const THandle< IMaterial >& material, ThumbnailRenderingManager* renderingManager );
		void SetupDecalProxy( const THandle< IMaterial >& material, ThumbnailRenderingManager* renderingManager );
		void SetupCamera( const THandle<CMesh> mesh, ThumbnailRenderingManager* renderingManager );

		THandle< CMesh > LoadMesh( const String& path );
	};
}