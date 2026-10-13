#include "build.h"

#include "meshStructure.h"
#include "meshStructureVisualization.h"
#include "meshVisualization.h"

#include "../../../common/renderData/include/renderObject.h"
#include "../../../common/renderData/include/renderResource.h"
#include "../../../common/renderData/include/renderer.h"
#include "../../../common/renderData/include/renderCommandHandler.h"
#include "../../../common/redMath/include/plane.h"
#include "../../../common/resourceMaterial/include/texture.h"
#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/resourceMaterial/include/bitmapTexture.h"
#include "../../../common/renderDraw/include/fragmentBuilder.h"
#include "../../../common/resourceMesh/include/meshParamWorkspotOffsets.h"
#include "../../../common/resourceMesh/include/meshParamImportedSnapPoint.h"

namespace Config
{
	TConfigVar< Float > cvMeshEditorPointSize("MeshEditor", "PointSize", 0.0018f);
	TConfigVar< Float > cvMeshEditorVectorSize("MeshEditor", "VectorSize", 0.004f);
}

namespace tools
{

	void MeshVisualization_Box::RenderDebug( MeshVisualizationRenderContext& context )
	{
		rend::FragmentBuilder builder( *context.m_drawer );
		builder.SetColor( Color::WHITE() );
		builder.AddWireBox( m_box );
	}

	void MeshVisualization_ReferenceFrame::RenderDebug( MeshVisualizationRenderContext& context )
	{
		PC_SCOPE( MeshVisualization_ReferenceFrame_RenderDebug );

		rend::FragmentBuilder builder( *context.m_drawer );

		const auto o = m_matrix.GetTranslation();
		const auto x = m_matrix.TransformVector( Vector3::EX() );
		const auto y = m_matrix.TransformVector( Vector3::EY() );
		const auto z = m_matrix.TransformVector( Vector3::EZ() );

		builder.EnableOverlay( true );
		builder.SetColor( Color::RED() );
		builder.AddFatLine( o, o + x, 0.025f );
		builder.SetColor( Color::GREEN() );
		builder.AddFatLine( o, o + y, 0.025f );
		builder.SetColor( Color::BLUE() );
		builder.AddFatLine( o, o + z, 0.025f );
	}

	//---------------------------------------------------------------------------

	const CName MeshVisualization_Box::TYPE = RED_NAME_CONSTEXPR("Box");
	const CName MeshVisualization_BoneReference::TYPE = RED_NAME_CONSTEXPR("BoneReference");
	const CName MeshVisualization_BoneInfluence::TYPE = RED_NAME_CONSTEXPR("BoneInfluence");
	const CName MeshVisualization_Triangles::TYPE = RED_NAME_CONSTEXPR("Triangles");
	const CName MeshVisualization_ReferenceFrame::TYPE = RED_NAME_CONSTEXPR("ReferenceFrame");
	const CName MeshVisualization_StreamPositions::TYPE = RED_NAME_CONSTEXPR("StreamPositions");
	const CName MeshVisualization_StreamNormals::TYPE = RED_NAME_CONSTEXPR("StreamNormals");
	const CName MeshVisualization_StreamTangents::TYPE = RED_NAME_CONSTEXPR("StreamTangents");
	const CName MeshVisualization_StreamBitangents::TYPE = RED_NAME_CONSTEXPR("StreamBitangents");
	const CName MeshVisualization_StreamMorphOffsets::TYPE = RED_NAME_CONSTEXPR( "StreamMorphOffsets" );
	const CName MeshVisualization_StreamMorphVertexData::TYPE = RED_NAME_CONSTEXPR( "StreamMorphVertexData" );
	const CName MeshVisualization_StreamGarmentFlags::TYPE = RED_NAME_CONSTEXPR( "StreamGarmentFlags" );
	const CName MeshVisualization_StreamGarmentCaps::TYPE = RED_NAME_CONSTEXPR( "StreamGarmentCaps" );
	const CName MeshVisualization_StreamColor::TYPE = RED_NAME_CONSTEXPR("StreamColor");
	const CName MeshVisualization_StreamIndices::TYPE = RED_NAME_CONSTEXPR("StreammIndices");
	const CName MeshVisualization_StreamTexCoord0::TYPE = RED_NAME_CONSTEXPR("StreamTexCoord0");
	const CName MeshVisualization_StreamTexCoord1::TYPE = RED_NAME_CONSTEXPR("StreamTexCoord1");
	const CName MeshVisualization_Texture::TYPE = RED_NAME_CONSTEXPR("Texture");
	const CName MeshVisualization_Occluder::TYPE = RED_NAME_CONSTEXPR("Occlusion");
	const CName MeshVisualization_Workspots::TYPE = RED_NAME_CONSTEXPR( "WorkspotsOffsets" );
	const CName MeshVisualization_SnapPoints::TYPE = RED_NAME_CONSTEXPR( "ImportedSnapPoints" );

	//---------------------------------------------------------------------------

	MeshVisualization_BoneInfluence::MeshVisualization_BoneInfluence(THandle<CMesh> meshData, const Uint32 boneIndex, const Int32 chunkIndex /*= -1*/, const Int32 lodIndex /*= -1*/)
	{
		const ::mesh::DataViewRO view(*meshData);

		const auto rig = view.GetBoneMatrix(boneIndex);
		m_boneName = view.GetBoneName(boneIndex).AsChar();

		const auto model = rig.FullInverted();
		m_bonePos = model.GetTranslationRef();

		m_debugMesh = CreateMeshSkinHeatMap(meshData, boneIndex, chunkIndex);
	}

	MeshVisualization_BoneInfluence::~MeshVisualization_BoneInfluence()
	{
	}

	void MeshVisualization_BoneInfluence::RenderDebug(MeshVisualizationRenderContext& context)
	{
		PC_SCOPE( MeshVisualization_BoneInfluence_RenderDebug );

		rend::FragmentBuilder builder(*context.m_drawer);
		builder.EnableTextBackground(Color(64, 64, 64, 64));
		builder.SetHorizontalTextAlignment(0);
		builder.SetVerticalTextAlignment(0);
		builder.AddWorldTextf(m_bonePos, 0, -20, "[%hs]", m_boneName.AsChar());

		builder.SetColor(Color::MAGENTA());
		builder.EnableOverlay(true);
		builder.AddWireSphere(m_bonePos, 0.01f);

		builder.SetColor(Color::WHITE());
		builder.EnableOverlay(true);
		builder.AddSolidMesh(m_debugMesh);
	}

	//---------------------------------------------------------------------------

	MeshVisualization_BoneReference::MeshVisualization_BoneReference(THandle<CMesh> meshData, const Uint32 boneIndex, const Int32 chunkIndex /*= -1*/)
	{
		const ::mesh::DataViewRO view(*meshData);

		const auto rig = view.GetBoneMatrix(boneIndex);
		m_boneName = view.GetBoneName(boneIndex).AsChar();

		m_boneRig = rig.FullInverted();
		m_bonePos = m_boneRig.GetTranslationRef();
	}

	MeshVisualization_BoneReference::~MeshVisualization_BoneReference()
	{
	}

	void MeshVisualization_BoneReference::RenderDebug(MeshVisualizationRenderContext& context)
	{
		PC_SCOPE( MeshVisualization_BoneReference_RenderDebug );

		rend::FragmentBuilder builder(*context.m_drawer);
		builder.EnableTextBackground(Color(64, 64, 64, 64));
		builder.SetHorizontalTextAlignment(0);
		builder.SetVerticalTextAlignment(0);
		builder.AddWorldTextf(m_bonePos, 0, -20, "[%hs]", m_boneName.AsChar());

		builder.SetColor( Color::RED() );
		builder.EnableOverlay( true );
		builder.AddAxes( m_boneRig, 0.1f, 0.05f );
	}

	//---------------------------------------------------------------------------

	MeshVisualization_Triangles::MeshVisualization_Triangles(THandle<CMesh> mesh, const Int32 chunkIndex, const CName materialName)
	{
		m_debugMesh = CreateMeshFiltered(mesh, chunkIndex, materialName);
	}

	MeshVisualization_Triangles::~MeshVisualization_Triangles()
	{
	}

	void MeshVisualization_Triangles::RenderDebug(MeshVisualizationRenderContext& context)
	{
		PC_SCOPE( MeshVisualization_Triangles_RenderDebug );

		rend::FragmentBuilder builder(*context.m_drawer);
		builder.SetColor(Color::WHITE());
		builder.EnableOverlay(false);
		builder.EnableTransparency(true);
		builder.AddSolidMesh(m_debugMesh);
	}

	//------

	template<class TexCoordsType>
	static TRenderPtr<IRenderResource> CreateMeshStreamTexCoord(THandle< CMesh > meshData, Int32 chunkIndexFilter, EMeshStreamType vectorStream )
	{
		const ::mesh::DataViewRO sourceView(*meshData);

		const ::mesh::StreamViewRO<Vector3> dataPosition( sourceView.GetChunkStreamData( chunkIndexFilter, MST_Position_3F ) );
		const ::mesh::StreamViewRO<TexCoordsType> dataTexCoord( sourceView.GetChunkStreamData( chunkIndexFilter, vectorStream ) );
		const ::mesh::StreamViewRO<Uint16> dataIndices(sourceView.GetChunkStreamData(chunkIndexFilter, MST_Index_U16));

		Uint32 numSourceVertices = dataPosition.Size();
		Uint32 numSourceIndices = dataIndices.Size();

		red::DynArray< DebugVertex > debugVertices;
		red::DynArray< Uint32 > debugIndices;
		debugVertices.Reserve(numSourceVertices * 3 + 4);
		debugIndices.Reserve(numSourceVertices * 6 + 4);

		const auto colorLinear = Color::GRAY().ToVectorLinearAccu();

		const auto scale = 512.0f;
		const auto offset = 50.0f;

		// frame
		{
			const auto base = debugVertices.Size();
			debugVertices.EmplaceBack(Vector4(offset, offset, 0.0f), Vector4::ONES());
			debugVertices.EmplaceBack(Vector4(offset + scale, offset, 0.0f), Vector4::ONES());
			debugVertices.EmplaceBack(Vector4(offset + scale, offset + scale, 0.0f), Vector4::ONES());
			debugVertices.EmplaceBack(Vector4(offset, offset + scale, 0.0f), Vector4::ONES());

			debugIndices.PushBack(base + 0);
			debugIndices.PushBack(base + 1);
			debugIndices.PushBack(base + 1);
			debugIndices.PushBack(base + 2);
			debugIndices.PushBack(base + 2);
			debugIndices.PushBack(base + 3);
			debugIndices.PushBack(base + 3);
			debugIndices.PushBack(base + 0);
		}

		for (Uint32 i = 0; i < numSourceIndices; i += 3)
		{
			const auto& v0 = dataTexCoord[dataIndices[i + 0]];
			const auto& v1 = dataTexCoord[dataIndices[i + 1]];
			const auto& v2 = dataTexCoord[dataIndices[i + 2]];

			// A uv flip (1.0f - v2.Y) is performed to avoid user's confusion when checking the UV's
			// The DirectX coordinate origin space is set to top/left. In most of the 3d applications it's set to bottom left.
			// Thanks to this flip the UV's visualization will look the same as in Maya.
			const auto base = debugVertices.Size();
			debugVertices.EmplaceBack(Vector4(offset + v2.X * scale, offset + (1.0f - v2.Y) * scale, 0.1f), colorLinear);
			debugVertices.EmplaceBack(Vector4(offset + v1.X * scale, offset + (1.0f - v1.Y) * scale, 0.1f), colorLinear);
			debugVertices.EmplaceBack(Vector4(offset + v0.X * scale, offset + (1.0f - v0.Y) * scale, 0.1f), colorLinear);

			debugIndices.PushBack(base + 0);
			debugIndices.PushBack(base + 1);
			debugIndices.PushBack(base + 1);
			debugIndices.PushBack(base + 2);
			debugIndices.PushBack(base + 2);
			debugIndices.PushBack(base + 0);
		}

		// no data
		if (debugVertices.Empty() || debugIndices.Empty())
			return nullptr;

		// create debug mesh for rendering
		return GRender->UploadDebugMesh(debugVertices, debugIndices);
	}

	//---

	MeshVisualization_StreamData::MeshVisualization_StreamData(const Bool wireframe/*= false*/, const Bool screen /* false*/, const Color color /* MAGENTA */ )
		: m_color( color )
	{
		m_wireframe = wireframe;
		m_screen = screen;
		m_debugMesh = nullptr;
		m_extraDebugMesh = nullptr;
	}

	MeshVisualization_StreamData::~MeshVisualization_StreamData()
	{
	}

	void MeshVisualization_StreamData::RenderDebug(MeshVisualizationRenderContext& context)
	{
		PC_SCOPE( MeshVisualization_StreamData_RenderDebug );

		rend::FragmentBuilder builder(*context.m_drawer);
		builder.SetColor( m_color );
		builder.EnableOverlay(false);

		if (m_screen)
		{
			if (m_wireframe)
				builder.AddWireScreenMesh(m_debugMesh);
			else
				builder.AddSolidScreenMesh(m_debugMesh);
		}
		else
		{
			builder.AddSolidMesh( m_debugMesh, m_wireframe ? rend::FragmentRenderMode::Lines : rend::FragmentRenderMode::Solid );
			if ( m_extraDebugMesh != nullptr )
				builder.AddSolidMesh( m_extraDebugMesh, m_wireframe ? rend::FragmentRenderMode::Lines : rend::FragmentRenderMode::Solid );
		}
	}

	//----

	MeshVisualization_StreamPositions::MeshVisualization_StreamPositions(THandle<CMesh> mesh, const Int32 chunkIndex)
		: MeshVisualization_StreamData()
	{
		m_debugMesh = CreateMeshStreamPositions(mesh, chunkIndex);
	}

	MeshVisualization_StreamNormals::MeshVisualization_StreamNormals(THandle<CMesh> mesh, const Int32 chunkIndex)
		: MeshVisualization_StreamData(true, false, Color::WHITE() )
	{
		m_debugMesh = CreateMeshStreamTBN(mesh, chunkIndex, MST_Normal_3F, Color::BLUE());
	}

	MeshVisualization_StreamTangents::MeshVisualization_StreamTangents(THandle<CMesh> mesh, const Int32 chunkIndex)
		: MeshVisualization_StreamData(true, false, Color::WHITE() )
	{
		m_debugMesh = CreateMeshStreamTBN(mesh, chunkIndex, MST_Tangent_3F, Color::RED());
	}

	MeshVisualization_StreamBitangents::MeshVisualization_StreamBitangents(THandle<CMesh> mesh, const Int32 chunkIndex)
		: MeshVisualization_StreamData(true, false, Color::WHITE() )
	{
		m_debugMesh = CreateMeshStreamTBN(mesh, chunkIndex, MST_Binormal_3F, Color::GREEN());
	}

	MeshVisualization_StreamColor::MeshVisualization_StreamColor(THandle<CMesh> mesh, const Int32 chunkIndex)
		: MeshVisualization_StreamData(false, false, Color::WHITE() )
	{
		m_debugMesh = CreateMeshStreamColor(mesh, chunkIndex);
	}

	MeshVisualization_StreamMorphOffsets::MeshVisualization_StreamMorphOffsets( THandle<CMesh> mesh, const Int32 chunkIndex )
		: MeshVisualization_StreamData( true, false, Color::WHITE() )
	{
		m_debugMesh = CreateMeshStreamTBN( mesh, chunkIndex, MST_MorphOffset_3F, Color::YELLOW(), 1.0f );
		m_extraDebugMesh = CreateMeshStreamPositions( mesh, chunkIndex );
	}

	MeshVisualization_StreamMorphVertexData::MeshVisualization_StreamMorphVertexData( THandle<CMesh> mesh, const Int32 chunkIndex )
		: MeshVisualization_StreamData( true, true )
	{
		m_debugMesh = CreateMeshStreamTexCoord<Vector3>( mesh, chunkIndex, MST_MorphVertexData_3F );
	}

	MeshVisualization_StreamGarmentFlags::MeshVisualization_StreamGarmentFlags( THandle<CMesh> mesh, const Int32 chunkIndex )
		: MeshVisualization_StreamData( false, false, Color::WHITE() )
	{
		m_debugMesh = CreateMeshStreamGarmentFlags( mesh, chunkIndex );
	}

	MeshVisualization_StreamGarmentCaps::MeshVisualization_StreamGarmentCaps( THandle<CMesh> mesh, const Int32 chunkIndex )
		: MeshVisualization_StreamData( false, false, Color::WHITE() )
	{
		m_debugMesh = CreateMeshStreamGarmentCaps( mesh, chunkIndex );																				/// @todo DP: finish this
	}

	MeshVisualization_StreamIndices::MeshVisualization_StreamIndices(THandle<CMesh> mesh, const Int32 chunkIndex)
		: MeshVisualization_StreamData()
	{
		m_debugMesh = CreateMeshStreamIndices(mesh, chunkIndex);
	}

	MeshVisualization_StreamTexCoord0::MeshVisualization_StreamTexCoord0(THandle<CMesh> mesh, const Int32 chunkIndex)
		: MeshVisualization_StreamData(true, true)
	{
		m_debugMesh = CreateMeshStreamTexCoord<Vector2>(mesh, chunkIndex, MST_TexCoord0_2F);
	}

	MeshVisualization_StreamTexCoord1::MeshVisualization_StreamTexCoord1(THandle<CMesh> mesh, const Int32 chunkIndex)
		: MeshVisualization_StreamData(true, true)
	{
		m_debugMesh = CreateMeshStreamTexCoord<Vector2>(mesh, chunkIndex, MST_TexCoord1_2F);
	}

	//----

	MeshVisualization_Texture::MeshVisualization_Texture(THandle<CBitmapTexture> texture)
		: m_texture(texture)
	{
	}

	MeshVisualization_Texture::~MeshVisualization_Texture()
	{
	}

	void MeshVisualization_Texture::RenderDebug(MeshVisualizationRenderContext& context)
	{
		//FIXME this doesn't work; the textures displayed are all black
		//and the rect placement logic is not correct

		//PC_SCOPE( MeshVisualization_Texture_RenderDebug );

		//Int32 margin = 50;
		//Int32 width = context.m_drawer->GetWidth();

		//// calculate size per texture
		//const auto spareWidth = std::max<Int32>(64, width - 2*margin - (margin * (context.m_texturesCount -1)));
		//const auto textureWidth = std::min<Int32>(512, spareWidth);

		//// calculate texture rect
		//Rect rect;
		//rect.m_left = margin;
		//rect.m_top = margin;
		//rect.m_bottom = margin + textureWidth;
		//rect.m_right = margin + textureWidth;

		//GRender->GetCommands()->SendDebugTexturePreview( m_texture->GetRenderResource(), rect );
	}

	//----

	MeshVisualization_Occluder::MeshVisualization_Occluder(const THandle<::vis::IOccluderResource>& resource)
		: m_occluderResource(resource)
	{
		m_visualizer.SetResource( m_occluderResource );
	}

	MeshVisualization_Occluder::~MeshVisualization_Occluder()
	{
	}

	void MeshVisualization_Occluder::RenderDebug(MeshVisualizationRenderContext& context)
	{
		PC_SCOPE( MeshVisualization_Occluder_RenderDebug );

		rend::FragmentBuilder builder(*context.m_drawer);
		builder.SetColor(Color(96, 96, 128, 64));

		builder.EnableOverlay(false);
		builder.EnableTransparency(true);

		if ( const auto& solidMesh = m_visualizer.GetSolidMesh() )
		{
			builder.AddSolidMesh( solidMesh );
		}
		if ( const auto& wireMesh = m_visualizer.GetWireMesh() )
		{
			builder.AddSolidMesh( wireMesh, rend::FragmentRenderMode::Lines );
		}
	}

	MeshVisualization_Workspots::MeshVisualization_Workspots( const THandle<mesh::MeshParamWorkspotOffsets >& resource )
		: m_offsets(resource)
	{
	}

	MeshVisualization_Workspots::~MeshVisualization_Workspots()
	{
	}

	void MeshVisualization_Workspots::RenderDebug( MeshVisualizationRenderContext& context )
	{
		PC_SCOPE( MeshVisualization_Workspots_RenderDebug );
		rend::FragmentBuilder builder( *context.m_drawer );
		static math::Vector4 lower( 0.0f, 0.0f, -.01f );
		
		for( const auto& name : m_offsets->GetOffsetsNames() )
		{
			const math::Matrix& offset = m_offsets->GetOffset( name );
			math::Vector4 position = offset.GetTranslation();
			builder.SetColor( Color::LIGHT_BLUE() );
			builder.AddSolidCircle( position, .11f );
			builder.SetColor( Color::DARK_BLUE() );
			builder.AddSolidCircle( position + lower, .15f );
			builder.AddFatArrow( position, position + ( offset.GetAxisY() * .4 ) );
			builder.SetColor( Color::WHITE() );
			builder.AddWorldText( position, 0, 0, name.AsChar() );
		}
	}

	MeshVisualization_SnapPoints::MeshVisualization_SnapPoints( const THandle< mesh::MeshParamImportedSnapPoint >& resource )
		: m_snapPoints( resource )
	{
	}

	MeshVisualization_SnapPoints::~MeshVisualization_SnapPoints()
	{
	}

	void MeshVisualization_SnapPoints::RenderDebug( MeshVisualizationRenderContext& context )
	{
		PC_SCOPE( MeshVisualization_Workspots_RenderDebug );
		rend::FragmentBuilder builder( *context.m_drawer );
		Vector4 cornerOffset( .05, .05, .05 );
		for( const auto& point: m_snapPoints->GetSnapPoints() )
		{
			const math::Matrix& snapPoint = point->GetTransform();
			math::Vector4 position = snapPoint.GetTranslation();
			builder.SetColor( Color::DARK_BLUE() );
			
			builder.AddSolidBox( position - ( cornerOffset * point->GetRange() * 2.0f ), position + ( cornerOffset * point->GetRange() * 2.0f ) );

			builder.SetColor( Color::LIGHT_GRAY() );
			builder.AddWireSphere( position, point->GetRange() );
		}
	}

} // tools
