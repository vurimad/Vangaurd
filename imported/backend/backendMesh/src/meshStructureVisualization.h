#pragma once

#include "meshStructure.h"
#include "meshPhysicsUtils.h"

#include "../../../common/renderData/include/drawer.h"
#include "../../../common/engine/include/redEnginePublic.h"
#include "../../../common/renderData/include/visibilityOccluderVisualizer.h"


class CBitmapTexture;
namespace mesh
{
	class MeshParamWorkspotOffsets;
	class MeshParamImportedSnapPoint;
}


namespace tools
{
	/// bounding box visualization
	class MeshVisualization_Box : public IMeshStructureNodeVisualization
	{
	public:
		MeshVisualization_Box(const Box& box)
			: m_box(box)
		{};

		static const CName TYPE;
		virtual CName GetType() const override final { return TYPE; }

		virtual void RenderDebug( MeshVisualizationRenderContext& context ) override final;

	private:
		Box m_box;
	};

	/// a reference frame (Axes)
	class MeshVisualization_ReferenceFrame : public IMeshStructureNodeVisualization
	{
	public:
		MeshVisualization_ReferenceFrame(const Matrix& matrix)
			: m_matrix(matrix)
		{};

		static const CName TYPE;
		virtual CName GetType() const override final { return TYPE; }

		virtual void RenderDebug( MeshVisualizationRenderContext& context ) override final;

	private:
		Matrix m_matrix;
	};

	/// mesh bone reference 
	class MeshVisualization_BoneReference : public IMeshStructureNodeVisualization
	{
	public:
		MeshVisualization_BoneReference(THandle<CMesh> mesh, const Uint32 bondIndex, const Int32 chunkIndex = -1);
		virtual ~MeshVisualization_BoneReference();

		static const CName TYPE;
		virtual CName GetType() const override final { return TYPE; }

		virtual void RenderDebug(MeshVisualizationRenderContext& context) override final;

	private:
		Vector3 m_bonePos;
		Matrix m_boneRig;
		String m_boneName;
	};

	/// mesh bone influence visualization
	class MeshVisualization_BoneInfluence : public IMeshStructureNodeVisualization
	{
	public:
		MeshVisualization_BoneInfluence(THandle<CMesh> mesh, const Uint32 bondIndex, const Int32 chunkIndex = -1, const Int32 lodIndex = -1);
		virtual ~MeshVisualization_BoneInfluence();

		static const CName TYPE;
		virtual CName GetType() const override final { return TYPE; }

		virtual void RenderDebug(MeshVisualizationRenderContext& context) override final;

	private:
		TRenderPtr<IRenderResource> m_debugMesh;

		Vector3 m_bonePos;
		String m_boneName;
	};

	/// sub-part of mesh, usually triangles from chunk(s) / material(s)
	class MeshVisualization_Triangles : public IMeshStructureNodeVisualization
	{
	public:
		MeshVisualization_Triangles(THandle<CMesh> mesh, const Int32 chunkIndex, const CName materialName);
		virtual ~MeshVisualization_Triangles();

		static const CName TYPE;
		virtual CName GetType() const override final { return TYPE; }

		virtual void RenderDebug(MeshVisualizationRenderContext& context) override final;

	private:
		TRenderPtr<IRenderResource> m_debugMesh;
	};

	/// stream data visualization
	class MeshVisualization_StreamData : public IMeshStructureNodeVisualization
	{
	public:
		MeshVisualization_StreamData( const Bool wireframe=false, const Bool screen=false, const Color color = Color::MAGENTA() );
		virtual ~MeshVisualization_StreamData();
		virtual void RenderDebug(MeshVisualizationRenderContext& context) override final;

	protected:
		TRenderPtr<IRenderResource> m_debugMesh;
		TRenderPtr<IRenderResource> m_extraDebugMesh;
		Bool m_wireframe;
		Bool m_screen;
		Color m_color;
	};

	class MeshVisualization_StreamPositions : public MeshVisualization_StreamData
	{
	public:
		MeshVisualization_StreamPositions(THandle<CMesh> mesh, const Int32 chunkIndex);

		static const CName TYPE;
		virtual CName GetType() const override final { return TYPE; }
	};

	class MeshVisualization_StreamNormals : public MeshVisualization_StreamData
	{
	public:
		MeshVisualization_StreamNormals(THandle<CMesh> mesh, const Int32 chunkIndex);

		static const CName TYPE;
		virtual CName GetType() const override final { return TYPE; }
	};

	class MeshVisualization_StreamTangents : public MeshVisualization_StreamData
	{
	public:
		MeshVisualization_StreamTangents(THandle<CMesh> mesh, const Int32 chunkIndex);

		static const CName TYPE;
		virtual CName GetType() const override final { return TYPE; }
	};

	class MeshVisualization_StreamBitangents : public MeshVisualization_StreamData
	{
	public:
		MeshVisualization_StreamBitangents(THandle<CMesh> mesh, const Int32 chunkIndex);

		static const CName TYPE;
		virtual CName GetType() const override final { return TYPE; }
	};

	class MeshVisualization_StreamColor : public MeshVisualization_StreamData
	{
	public:
		MeshVisualization_StreamColor(THandle<CMesh> mesh, const Int32 chunkIndex);

		static const CName TYPE;
		virtual CName GetType() const override final { return TYPE; }
	};

	class MeshVisualization_StreamMorphOffsets : public MeshVisualization_StreamData
	{
	public:
		MeshVisualization_StreamMorphOffsets( THandle<CMesh> mesh, const Int32 chunkIndex );

		static const CName TYPE;
		virtual CName GetType() const override final { return TYPE; }
	};

	class MeshVisualization_StreamMorphVertexData : public MeshVisualization_StreamData
	{
	public:
		MeshVisualization_StreamMorphVertexData( THandle<CMesh> mesh, const Int32 chunkIndex );

		static const CName TYPE;
		virtual CName GetType() const override final { return TYPE; }
	};

	class MeshVisualization_StreamGarmentFlags : public MeshVisualization_StreamData
	{
	public:
		MeshVisualization_StreamGarmentFlags( THandle<CMesh> mesh, const Int32 chunkIndex );

		static const CName TYPE;
		virtual CName GetType() const override final { return TYPE; }
	};

	class MeshVisualization_StreamGarmentCaps : public MeshVisualization_StreamData
	{
	public:
		MeshVisualization_StreamGarmentCaps( THandle<CMesh> mesh, const Int32 chunkIndex );

		static const CName TYPE;
		virtual CName GetType() const override final { return TYPE; }
	};

	class MeshVisualization_StreamIndices : public MeshVisualization_StreamData
	{
	public:
		MeshVisualization_StreamIndices(THandle<CMesh> mesh, const Int32 chunkIndex);

		static const CName TYPE;
		virtual CName GetType() const override final { return TYPE; }
	};

	class MeshVisualization_StreamTexCoord0 : public MeshVisualization_StreamData
	{
	public:
		MeshVisualization_StreamTexCoord0(THandle<CMesh> mesh, const Int32 chunkIndex);
		virtual Int32 GetRenderOrder() { return 1; }

		static const CName TYPE;
		virtual CName GetType() const override final { return TYPE; }
	};

	class MeshVisualization_StreamTexCoord1 : public MeshVisualization_StreamData
	{
	public:
		MeshVisualization_StreamTexCoord1(THandle<CMesh> mesh, const Int32 chunkIndex);
		virtual Int32 GetRenderOrder() { return 1; }

		static const CName TYPE;
		virtual CName GetType() const override final { return TYPE; }
	};

	/// texture visualization
	class MeshVisualization_Texture : public IMeshStructureNodeVisualization
	{
	public:
		MeshVisualization_Texture(THandle<CBitmapTexture> texture);
		virtual ~MeshVisualization_Texture();

		static const CName TYPE;
		virtual CName GetType() const override final { return TYPE; }

		virtual void RenderDebug(MeshVisualizationRenderContext& context) override final;

	private:
		THandle<CBitmapTexture> m_texture;
	};


	class MeshVisualization_Occluder : public IMeshStructureNodeVisualization
	{
	public:
		MeshVisualization_Occluder(const THandle<::vis::IOccluderResource>& resource);
		virtual ~MeshVisualization_Occluder();

		static const CName TYPE;
		virtual CName GetType() const override final { return TYPE; }

		virtual void RenderDebug(MeshVisualizationRenderContext& context) override final;

	private:
		THandle<::vis::IOccluderResource> m_occluderResource;
		::vis::OccluderVisualizer m_visualizer;
	};


	class MeshVisualization_Workspots : public IMeshStructureNodeVisualization
	{
	public:
		MeshVisualization_Workspots( const THandle<mesh::MeshParamWorkspotOffsets >& resource );
		virtual ~MeshVisualization_Workspots();

		static const CName TYPE;
		virtual CName GetType() const override final { return TYPE; }

		virtual void RenderDebug( MeshVisualizationRenderContext& context ) override final;

	private:
		THandle<mesh::MeshParamWorkspotOffsets> m_offsets;
	};

	class MeshVisualization_SnapPoints : public IMeshStructureNodeVisualization
	{
	public:
		MeshVisualization_SnapPoints( const THandle< mesh::MeshParamImportedSnapPoint >& resource );
		virtual ~MeshVisualization_SnapPoints();

		static const CName TYPE;
		virtual CName GetType() const override final { return TYPE; }

		virtual void RenderDebug( MeshVisualizationRenderContext& context ) override final;

	private:
		THandle< mesh::MeshParamImportedSnapPoint > m_snapPoints;
	};

} // tools
