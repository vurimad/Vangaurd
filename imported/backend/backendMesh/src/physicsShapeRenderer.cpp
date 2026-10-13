/* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.*/

#include "build.h"
#include "physicsShapeRenderer.h"

#include "../../../common/engine/include/renderFrame.h"
#include "../../../common/resourceMesh/include/mesh.h"

#include "../../../common/renderData/include/renderDataPublic.h"
#include "../../../common/renderData/include/renderer.h"
#include "../../../common/renderData/include/renderProxy.h"
#include "../../../common/renderData/include/renderProxyInitData.h"
#include "../../../common/renderDraw/include/fragmentBuilder.h"


RTTI_BEGIN_TYPE_IN_NAMESPACE( PhysicsShapeRendererOptions, physics );
RTTI_PARENT_TYPE( ISerializable )
RTTI_PROPERTY( m_jointAlpha ).editable().range( 0.f, 1.0f );
RTTI_PROPERTY( m_jointRadius ).editable().range( 0.025f, 1.0f );
RTTI_PROPERTY( m_shapeAlpha ).editable().range( 0.f, 1.f );
RTTI_PROPERTY( m_actorAlpha ).editable().range( 0.f, 1.f );
RTTI_PROPERTY( m_actorSize ).editable().range( 0.001f, 1.0f );
RTTI_PROPERTY( m_actorCOMSize ).editable().range( 0.025f, 1.0f ); 
RTTI_PROPERTY( m_simulationGroundPlaneAlpha ).editable().range( 0.f, 1.0f );
RTTI_PROPERTY( m_simulationGroundPlaneColor ).editable();
RTTI_PROPERTY( m_actors ).editable();
RTTI_PROPERTY( m_shapes ).editable();
RTTI_PROPERTY( m_joints ).editable();
RTTI_PROPERTY( m_mesh ).editable();
RTTI_PROPERTY( m_actorCOM ).editable();
RTTI_PROPERTY( m_actorNames ).editable();
RTTI_PROPERTY( m_bounds ).editable();
RTTI_PROPERTY( m_materials ).editable();
RTTI_PROPERTY( m_destructionBonds ).editable();
RTTI_END_TYPE();

namespace physics
{
	//-----------------------------

	const Color PhysicsShapeRenderer::DEFAULT_COLOR = Color::DARK_GREEN();
	const Color PhysicsShapeRenderer::SELECTED_COLOR = Color::YELLOW();

	PhysicsShapeRenderer::PhysicsShapeRenderer( Color color ) 
		: m_numPoints( 0 )
		, m_currentColor( color )
		, m_localToWorld( Matrix::IDENTITY() )
		, m_isSelected( false )
		, m_overlayEnabled( false )
		, m_vertices( red::PoolBackend() )
		, m_indices( red::PoolBackend() )
		, m_singlePoints( red::PoolBackend() )
		, m_verticesWireframe( red::PoolBackend() )
		, m_indicesWireframe( red::PoolBackend() )
		, m_wireframeRenderMode( FragmentBuilderRenderMode::Lines )
	{
	}

	PhysicsShapeRenderer::~PhysicsShapeRenderer()
	{
	}


	PhysicsShapeRenderer::PhysicsShapeRenderer( const PhysicsShapeRenderer& ) = default;
	PhysicsShapeRenderer::PhysicsShapeRenderer( PhysicsShapeRenderer&& ) = default;
	PhysicsShapeRenderer& PhysicsShapeRenderer::operator =( const PhysicsShapeRenderer& ) = default;
	PhysicsShapeRenderer& PhysicsShapeRenderer::operator =( PhysicsShapeRenderer&& ) = default;


	void PhysicsShapeRenderer::PrepareForRendering( )
	{
		Uint32 size = m_vertices.Size();
		if( size > 0 )
		{
			m_customMesh = GRender->UploadDebugMesh( m_vertices, m_indices );
		}

		size = m_verticesWireframe.Size();
		if( size > 0 )
		{
			m_customWireframeMesh = GRender->UploadDebugMesh( m_verticesWireframe, m_indicesWireframe );
		}
		m_vertices.Shrink();
		m_indices.Shrink();
		m_verticesWireframe.Shrink();
		m_indicesWireframe.Shrink();
	}

	void PhysicsShapeRenderer::Render( rend::IDebugDrawer& dd, const IRenderSelectable* selectable )
	{
		RenderCustomMesh( dd, m_customMesh, selectable );
		RenderCustomWireframeMesh( dd, m_customWireframeMesh, selectable );

		for( const auto& point : m_singlePoints )
		{
			// HACK, PhysicsShapeRenderer shouldn't use DebugVertex internally
			const Color color = Color::FromVectorLinearAccu( point.colorLinearSpace.ToVector4() );

			RenderPoint( dd, selectable, m_localToWorld, Vector3( point.x, point.y, point.z ), color );
		}				
	}


	void PhysicsShapeRenderer::RenderPoint( rend::IDebugDrawer& dd, const IRenderSelectable* selectable, const Matrix& localToWorld, const Vector3& point, Color color, Float size )
	{
		rend::FragmentBuilder builder( dd );
		builder.EnableTransparency( true );
		builder.EnableOverlay( m_overlayEnabled );
		builder.BindTransform( localToWorld );
		builder.SetColor( color );
		builder.BindSelectable( selectable );
		builder.AddSolidCircle( point, size );
	}

	void PhysicsShapeRenderer::RenderLine( rend::IDebugDrawer& dd, const IRenderSelectable* selectable, const Matrix& localToWorld, const Vector4& start, const Vector4& end, Color color, Bool isOverlay, Float size )
	{
		if( start != end )
		{
			rend::FragmentBuilder builder( dd );
			builder.EnableTransparency( true );
			builder.BindTransform( localToWorld );
			builder.SetColor( color );
			builder.BindSelectable( selectable );
			builder.EnableOverlay( isOverlay );
			builder.AddFatLine( start, end, size );
		}
		else
		{
			RenderPoint( dd, selectable, localToWorld, start, color, size );
		}
	}

	void PhysicsShapeRenderer::RenderSprite(rend::IDebugDrawer& dd, const IRenderSelectable* selectable, const Matrix& localToWorld, Color color, Float size, Bool enableOverlay)
	{
		rend::FragmentBuilder builder( dd );
		builder.BindTransform( localToWorld );
		builder.BindSelectable( selectable );
		builder.EnableTransparency( true );
		builder.EnableOverlay( enableOverlay );
		builder.SetColor( color);
		builder.AddSprite( size );
	}

	void PhysicsShapeRenderer::RenderArrow(rend::IDebugDrawer& dd, const Vector4& a, const Vector4& b, Color color, Bool enableOverlay )
	{
		rend::FragmentBuilder builder( dd );
		builder.EnableTransparency( true );
		builder.EnableOverlay( enableOverlay );
		builder.SetColor( color );
		builder.AddArrow ( a, b );
	}

	void PhysicsShapeRenderer::RenderText( rend::IDebugDrawer& dd, const Vector4& position, const String& text, Color color, Bool enableOverlay /*= true */ )
	{
		rend::FragmentBuilder builder( dd );
		builder.EnableTransparency( true );
		builder.EnableOverlay( enableOverlay );
		builder.SetColor( color );
		builder.AddWorldText( position, 0, 0, text.AsChar() );
	}

	void PhysicsShapeRenderer::RenderCustomMesh( rend::IDebugDrawer& dd, const TRenderPtr<IRenderResource>& mesh, const IRenderSelectable* selectable )
	{
		if( mesh )
		{
			rend::FragmentBuilder builder( dd );
			builder.BindTransform( m_localToWorld );
			builder.BindSelectable( selectable );
			builder.EnableOverlay( m_overlayEnabled );
			builder.EnableTransparency( true );
			builder.SetColor( m_currentColor );
			builder.AddSolidMesh( mesh );
		}
	}

	void PhysicsShapeRenderer::RenderCustomWireframeMesh( rend::IDebugDrawer& dd, const TRenderPtr<IRenderResource>& mesh, const IRenderSelectable* selectable )
	{
		if( mesh )
		{
			rend::FragmentBuilder builder( dd );
			builder.BindTransform( m_localToWorld );
			builder.BindSelectable( selectable );
			builder.EnableOverlay( m_overlayEnabled );
			builder.EnableTransparency( true );
			builder.SetColor( m_currentColor );
			if( m_isSelected )
			{
				builder.SetColor( SELECTED_COLOR );
			}
			rend::FragmentRenderMode wireframeMode = rend::FragmentRenderMode::Lines;
			if( m_wireframeRenderMode == FragmentBuilderRenderMode::TrueWireframe )
			{
				wireframeMode = rend::FragmentRenderMode::Wireframe;
			}
			builder.AddSolidMesh( mesh, wireframeMode );
		}
	}

	void PhysicsShapeRenderer::SetLocalToWorld(const Matrix& localToWorld)
	{
		m_localToWorld = localToWorld;		
	}

	void PhysicsShapeRenderer::SetSelected(Bool isSelected)
	{
		m_isSelected = isSelected;
		PrepareForRendering();
	}

	void PhysicsShapeRenderer::Reset()
	{
		m_verticesWireframe.Clear();
		m_indicesWireframe.Clear();
		m_vertices.Clear();
		m_indices.Clear();
		m_singlePoints.Clear();
		m_numPoints = 0;
		m_customMesh = nullptr;
	}

	void PhysicsShapeRenderer::AllocateLines(Uint32 amount)
	{
		m_verticesWireframe.Reserve( m_verticesWireframe.Size() + amount * 2 );
		m_indicesWireframe.Reserve( m_indicesWireframe.Size() +  amount * 2 );
	}

	void PhysicsShapeRenderer::AddLine(const Vector3& start, const Vector3& end)
	{
		AddLine( start, end, Color::WHITE() );
	}

	void PhysicsShapeRenderer::AddLine(const Vector3& start, const Vector3& end, Color color)
	{
		const auto base = m_verticesWireframe.Size();

		m_verticesWireframe.EmplaceBack( start, color );
		m_verticesWireframe.EmplaceBack( end, color );

		m_indicesWireframe.PushBack( base + 0 );
		m_indicesWireframe.PushBack( base + 1 );
	}

	void PhysicsShapeRenderer::AddTriangle(const Vector3& a, const Vector3& b, const Vector3& c)
	{
		Int32 index[3] = {-1, -1, -1 };
		Int32 size = m_vertices.Size();

		const Float eps = 0.001f;
		Bool error = false;
		for( Int32 i = 0; i < size; ++i )
		{
			Vector3 position = Vector3( m_vertices[i].x, m_vertices[ i ].y, m_vertices[ i ].z  );
			Uint32 count = 0;
			if( (position - a).Mag() < eps )
			{
				index[0] = i;
				count++;
			}
			if( (position - b).Mag() < eps )
			{
				index[1] = i;
				count++;
			}
			if( (position - c).Mag() < eps )
			{
				index[2] = i;
				count++;
			}

			if( count > 1 )
			{
				error = true; // Triangle with 2 same vertices.
				break;
			}
		}

		if( error )
		{
			// error occured, do not add this triangle
			return;
		}

		CheckAndAddVertexIfNeeded(index[0], a);
		CheckAndAddVertexIfNeeded(index[1], b);
		CheckAndAddVertexIfNeeded(index[2], c);

		m_indices.PushBack( index[0] );
		m_indices.PushBack( index[1] );
		m_indices.PushBack( index[2] );
	}

	void PhysicsShapeRenderer::AddPoint(const Vector3& a, Color color)
	{
		m_singlePoints.PushBack( DebugVertex( a, color ) );
	}

	void PhysicsShapeRenderer::AddCustomMesh( const red::DynArray< Vector3 >& vertices, const red::DynArray< Uint16 >& indices, Bool invertNormals)
	{
		Uint32 size = vertices.Size();

		if( size > 0 )
		{
			m_vertices.Clear();
			m_vertices.Reserve( size );

			for( Uint32 i = 0; i < size; ++i )
			{
				m_vertices.PushBack( DebugVertex( vertices[i], Color::WHITE() ) );
			}

			Uint32 sizeInd = indices.Size();
			m_indices.Clear();
			m_indices.Resize( sizeInd );

			for( Uint32 i = 0; i < sizeInd; i +=3 )
			{
				if( invertNormals )
				{
					m_indices[ i ] = indices[ i + 2 ];
					m_indices[ i + 2 ] = indices[ i ];
				}
				else
				{
					m_indices[ i ] = indices[ i ];
					m_indices[ i + 2 ] = indices[ i + 2 ];
				}
				m_indices[ i + 1 ] = indices[ i + 1 ];
			}
		}
	}

	void PhysicsShapeRenderer::AddCustomMesh( const red::DynArray< DebugVertex >& vertices, const red::DynArray< Uint16 >& indices, Bool invertNormals /*= false */ )
	{
		Uint32 size = vertices.Size();

		if( size > 0 )
		{
			m_vertices = vertices;	
			Uint32 sizeInd = indices.Size();
			m_indices.Clear();
			m_indices.Resize( sizeInd );

			for( Uint32 i = 0; i < sizeInd; i += 3 )
			{
				if( invertNormals )
				{
					m_indices[ i ] = indices[ i + 2 ];
					m_indices[ i + 2 ] = indices[ i ];
				}
				else
				{
					m_indices[ i ] = indices[ i ];
					m_indices[ i + 2 ] = indices[ i + 2 ];
				}
				m_indices[ i + 1 ] = indices[ i + 1 ];
			}
		}
	}

	void PhysicsShapeRenderer::AddCustomMesh( const red::DynArray< Vector3 >& vertices, const red::DynArray< Uint32 >& indices )
	{
		Uint32 size = vertices.Size();

		if( size > 0 )
		{
			m_vertices.Clear();
			m_vertices.Reserve( size );
			for( Uint32 i = 0; i < size; ++i )
			{
				m_vertices.PushBack( DebugVertex( vertices[ i ], Color::WHITE() ) );
			}			
			m_indices = indices;
		}
	}

	void PhysicsShapeRenderer::AddCustomWireframeMesh( const red::DynArray< Vector3 >& vertices, const red::DynArray< Uint16 >& indices, Bool invertNormals /*= false */, FragmentBuilderRenderMode wireframeMode /*= FragmentBuilderRenderMode::Lines*/ )
	{
		Uint32 size = vertices.Size();

		if( size > 0 )
		{
			m_verticesWireframe.Clear();
			m_verticesWireframe.Reserve( size );

			for( Uint32 i = 0; i < size; ++i )
			{
				m_verticesWireframe.PushBack( DebugVertex( vertices[ i ], Color::WHITE() ) );
			}

			Uint32 sizeInd = indices.Size();
			m_indicesWireframe.Clear();
			m_indicesWireframe.Resize( sizeInd );

			for( Uint32 i = 0; i < sizeInd; i += 3 )
			{
				if( invertNormals )
				{
					m_indicesWireframe[ i ] = indices[ i + 2 ];
					m_indicesWireframe[ i + 2 ] = indices[ i ];
				}
				else
				{
					m_indicesWireframe[ i ] = indices[ i ];
					m_indicesWireframe[ i + 2 ] = indices[ i + 2 ];
				}
				m_indicesWireframe[ i + 1 ] = indices[ i + 1 ];
			}
		}
		m_wireframeRenderMode = wireframeMode;
	}

	void PhysicsShapeRenderer::AddCustomWireframeMesh( const red::DynArray< DebugVertex >& vertices, const red::DynArray< Uint16 >& indices, Bool invertNormals /*= false */, FragmentBuilderRenderMode wireframeMode /*= FragmentBuilderRenderMode::Lines*/ )
	{
		Uint32 size = vertices.Size();
		if( size > 0 )
		{
			m_verticesWireframe = vertices;

			Uint32 sizeInd = indices.Size();
			m_indicesWireframe.Clear();
			m_indicesWireframe.Resize( sizeInd );

			for( Uint32 i = 0; i < sizeInd; i += 3 )
			{
				if( invertNormals )
				{
					m_indicesWireframe[ i ] = indices[ i + 2 ];
					m_indicesWireframe[ i + 2 ] = indices[ i ];
				}
				else
				{
					m_indicesWireframe[ i ] = indices[ i ];
					m_indicesWireframe[ i + 2 ] = indices[ i + 2 ];
				}
				m_indicesWireframe[ i + 1 ] = indices[ i + 1 ];
			}
		}
		m_wireframeRenderMode = wireframeMode;
	}

	void PhysicsShapeRenderer::AddCustomWireframeMesh( const red::DynArray< Vector3 >& vertices, const red::DynArray< Uint32 >& indices, FragmentBuilderRenderMode wireframeMode )
	{
		Uint32 size = vertices.Size();

		if( size > 0 )
		{
			m_verticesWireframe.Clear();
			m_verticesWireframe.Reserve( size );

			for( Uint32 i = 0; i < size; ++i )
			{
				m_verticesWireframe.PushBack( DebugVertex( vertices[ i ], Color::WHITE() ) );
			}

			m_indicesWireframe = m_indices;
		}
		m_wireframeRenderMode = wireframeMode;
	}

	void PhysicsShapeRenderer::SetColor( const Color& color )
	{
		m_currentColor = color;
	}

	void PhysicsShapeRenderer::SetOverlay( Bool isEnabled )
	{
		m_overlayEnabled = isEnabled;
	}

	void PhysicsShapeRenderer::CheckAndAddVertexIfNeeded( Int32& index, const Vector3& a )
	{
		if( index == -1 )
		{
			index = m_vertices.Size();
			m_vertices.PushBack( DebugVertex( a, Color::WHITE() ) );
		}
	}

	void PhysicsShapeRenderer::Reserve(const Uint32 numVeritces)
	{
		m_vertices.Reserve( m_vertices.Size() + numVeritces);
		m_verticesWireframe.Reserve(m_verticesWireframe.Size() + numVeritces);
		m_indices.Reserve(m_indices.Size() + numVeritces);
		m_indicesWireframe.Reserve(m_indicesWireframe.Size() + numVeritces);
	}

	//-----------------------------

	PhysicsShapeRendererOptions::PhysicsShapeRendererOptions()
		: m_renderScene( nullptr )
		, m_jointAlpha( 0.25f )
		, m_shapeAlpha( 0.5f )
		, m_actorAlpha( 0.5f )
		, m_jointRadius( 0.25f )
		, m_actorSize( 0.025f )
		, m_actorCOMSize( 0.1f )
		, m_simulationGroundPlaneAlpha( 0.5f )
		, m_simulationGroundPlaneColor( Color::GRAY() )
	{

	}

	PhysicsShapeRendererOptions::~PhysicsShapeRendererOptions()
	{

	}

}

