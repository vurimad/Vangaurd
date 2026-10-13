/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "renderDataApi.h"
#include "renderResource.h"


class IDebugDrawer;

namespace vis
{

	class IOccluderResource;

	class RENDER_DATA_API OccluderVisualizer
	{
	public:
		OccluderVisualizer();
		~OccluderVisualizer();

		void SetResource( const THandle<IOccluderResource>& resource );
		Bool HasResource() const { return m_resource != nullptr; }

		const TRenderPtr<IRenderResource>& GetSolidMesh();
		const TRenderPtr<IRenderResource>& GetWireMesh();

	private:
		void BuildMeshes();

		THandle<IOccluderResource> m_resource;

		TRenderPtr<IRenderResource> m_solidMesh;
		TRenderPtr<IRenderResource> m_wireMesh;
	};

}
