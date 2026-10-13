	/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../../common/redReflection/include/types.h"
#include "../../../common/redReflection/include/shapeRenderer.h"
#include "../../../common/redContainers/include/dynArray.h"
#include "../../../common/renderData/include/renderVertices.h"
#include "../../../common/renderData/include/renderScene.h"


class IRender;

namespace physics
{
	// --- SPhyiscsShapeRendererOptions ---

	struct BACKEND_MESH_API PhysicsShapeRendererOptions : public ISerializable
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
		RTTI_DECLARE_TYPE( PhysicsShapeRendererOptions )

		TRenderPtr<IRenderScene>	m_renderScene;
		Float			m_jointAlpha;
		Float			m_jointRadius;
		Float			m_shapeAlpha;
		Float			m_actorAlpha;
		Float			m_actorSize;
		Float			m_actorCOMSize;
		Float			m_simulationGroundPlaneAlpha;
		Color			m_simulationGroundPlaneColor;

		Bool m_actors = false;
		Bool m_shapes = false;
		Bool m_joints = false;
		Bool m_mesh = true;
		Bool m_actorCOM = false;
		Bool m_actorNames = false;
		Bool m_bounds = false;
		Bool m_destructionBonds = false;
		Bool m_materials = false;

		PhysicsShapeRendererOptions();
		~PhysicsShapeRendererOptions();
	};

	enum FragmentBuilderRenderMode
	{
		Lines,
		TrueWireframe
	};

	using PhysicsRendererOptionsPtr = THandle< PhysicsShapeRendererOptions >;

	// --- PhysicsRenderHelper ---

	class BACKEND_MESH_API PhysicsShapeRenderer : public red::IShapeRenderer
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

		static const Color DEFAULT_COLOR;
		static const Color SELECTED_COLOR;
	public:
		PhysicsShapeRenderer( Color defaultColor = DEFAULT_COLOR );
		~PhysicsShapeRenderer();

		PhysicsShapeRenderer( const PhysicsShapeRenderer& );
		PhysicsShapeRenderer( PhysicsShapeRenderer&& );
		PhysicsShapeRenderer& operator =( const PhysicsShapeRenderer& );
		PhysicsShapeRenderer& operator =( PhysicsShapeRenderer&& );

		virtual void							AddLine(const Vector3& start, const Vector3& end);
		void									AddLine(const Vector3& start, const Vector3& end, Color color);

		virtual void							AddQuad(const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& d)	{}

		virtual void							AddTriangle(const Vector3& a, const Vector3& b, const Vector3& c );

		virtual void							AddPoint(const Vector3& a, Color color = Color::DARK_BLUE() );

		void									AddCustomMesh( const red::DynArray< Vector3 >& vertices, const red::DynArray< Uint16 >& indices, Bool invertNormals = false );
		void									AddCustomMesh( const red::DynArray< DebugVertex >& vertices, const red::DynArray< Uint16 >& indices, Bool invertNormals = false );
		void									AddCustomMesh( const red::DynArray< Vector3 >& vertices, const red::DynArray< Uint32 >& indices );
		void									AddCustomWireframeMesh( const red::DynArray< Vector3 >& vertices, const red::DynArray< Uint16 >& indices, Bool invertNormals = false, FragmentBuilderRenderMode wireframeMode = FragmentBuilderRenderMode::Lines );
		void									AddCustomWireframeMesh( const red::DynArray< DebugVertex >& vertices, const red::DynArray< Uint16 >& indices, Bool invertNormals = false, FragmentBuilderRenderMode wireframeMode = FragmentBuilderRenderMode::Lines );
		void									AddCustomWireframeMesh( const red::DynArray< Vector3 >& vertices, const red::DynArray< Uint32 >& indices, FragmentBuilderRenderMode wireframeMode = FragmentBuilderRenderMode::Lines );

		void									CheckAndAddVertexIfNeeded(Int32& index, const Vector3& a);

		virtual void							Reserve(const Uint32 numVeritces);

		void									PrepareForRendering( );
		void									Render( rend::IDebugDrawer& dd, const IRenderSelectable* selectable );

		void									RenderPoint( rend::IDebugDrawer& dd, const IRenderSelectable* selectable, const Matrix& localToWorld, const Vector3& point, Color color, Float size = .005f );
		void									RenderLine( rend::IDebugDrawer& dd, const IRenderSelectable* selectable, const Matrix& localToWorld, const Vector4& start, const Vector4& end, Color color, Bool isOverlay = false, Float size = .005f );
		void									RenderSprite( rend::IDebugDrawer& dd, const IRenderSelectable* selectable, const Matrix& localToWorld, Color color, Float size = .25f, Bool enableOverlay = true );
		void									RenderArrow( rend::IDebugDrawer& dd, const Vector4& a, const Vector4& b, Color color, Bool enableOverlay = true );
		void									RenderText( rend::IDebugDrawer& dd, const Vector4& postion, const String& text, Color color, Bool enableOverlay = true );

		void									SetLocalToWorld( const Matrix& localToWorld );

		void									SetSelected( Bool isSelected );

		void									Reset( );
		void									AllocateLines( Uint32 amount );

		void									SetColor( const Color& color );
		void									SetOverlay( Bool isEnabled );
	private:
		void									RenderCustomMesh( rend::IDebugDrawer& renderOptions, const TRenderPtr<IRenderResource>& mesh, const IRenderSelectable* selectable );
		void									RenderCustomWireframeMesh( rend::IDebugDrawer& renderOptions, const TRenderPtr<IRenderResource>& mesh, const IRenderSelectable* selectable );

		Matrix									m_localToWorld;
		Color									m_currentColor;

		red::DynArray< DebugVertex >			m_verticesWireframe;
		red::DynArray< Uint32 >					m_indicesWireframe;

		red::DynArray< DebugVertex >			m_vertices;
		red::DynArray< Uint32 >					m_indices;
		red::DynArray< DebugVertex >			m_singlePoints;

		mutable TRenderPtr<IRenderResource>	m_customMesh;		//!< Debug mesh for drawing collision
		mutable TRenderPtr<IRenderResource>	m_customWireframeMesh;		//!< Debug mesh for drawing collision

		Uint32									m_numPoints;
		Bool									m_isSelected;
		Bool									m_overlayEnabled;
		FragmentBuilderRenderMode				m_wireframeRenderMode;

	};

	// --- !PhysicsRenderHelper ---
}
