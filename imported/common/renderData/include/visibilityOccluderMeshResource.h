/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../renderData/include/visibilityOccluderResource.h"
#include "../../redContainers/include/arraySpan.h"
#include "../../redContainers/include/dynArray.h"


namespace vis
{

	class RENDER_DATA_API OccluderMeshResource final : public IOccluderResource
	{
		RTTI_DECLARE_TYPE( OccluderMeshResource );
		RED_BASE_CLASS( IOccluderResource );

	private:
		static const Uint32 LATEST_RESOURCE_VERSION;
		Uint32					m_resourceVersion;

		DataBuffer				m_vertices;
		DataBuffer				m_indices;
		Box						m_boundingBox;

		// #TODO: Instead of mesh-wide twoSided flag, maybe it would be better to support multiple chunks here, and each
		// chunk can be two-sided or not. Or maybe in a case where we don't want the full mesh to be two-sided, that can
		// be handled by just duplicating and flipping the specific triangles.
		Bool					m_twoSided;

	public:
		struct CreateInfo
		{
			red::ArraySpan<Vector4>	vertices;
			red::ArraySpan<Uint16>	indices;
			Box						boundingBox;
			Bool					twoSided;
		};
		static THandle<IOccluderResource> Create( const CreateInfo& createInfo );

	private:
		OccluderMeshResource();

	public:
		virtual Uint32 GetVersion() const override { return m_resourceVersion; }
		virtual Bool IsVersionValid() const override { return m_resourceVersion == LATEST_RESOURCE_VERSION; }

		virtual void OnPostLoad( const PostLoadContext& context ) override;

		red::ArraySpan<const math::Vector4> GetVertices() const;
		red::ArraySpan<const Uint16> GetIndices() const;

		virtual const math::Box& GetLocalBoundingBox() const override { return m_boundingBox; }

		Bool GetTwoSided() const { return m_twoSided; }


	private:
		Uint32 CalculateResourceHash() const;
	};

}
