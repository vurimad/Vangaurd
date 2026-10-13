/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "backendMeshPreviewPanelInputListener.h"
#include "backendMeshPreviewPanel.h"
#include "meshPhysicsUtils.h"
#include "physicsObjectCreationInfo.h"
#include "physicsObjectResolver.h"
#include "physicsMesh.h"
#include "physicsBackendHelper.h"

#include "../../backendEngine/include/worldSpaceDragDropHandler.h"
#include "../../backendEngine/include/objectDragDropListener.h"
#include "../../backendEngine/include/objectHandler.h"
#include "../../backendEngine/include/previewEditMode.h"

#include "../../../common/renderDraw/include/fragmentBuilder.h"
#include "../../../common/renderData/include/renderSelectionResult.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( MarkVerticesHandlerOptions, tools );
RTTI_PARENT_TYPE( ISerializable )
RTTI_PROPERTY_CATEGORY( "Tool options" );
RTTI_PROPERTY( m_radius ).editable().range( 0.0005f, 0.2f );
RTTI_PROPERTY( m_radiusStep ).editable().range( 0.0005f, 0.02f );
RTTI_PROPERTY( m_verticeSize ).editable().range( 0.00005f, 0.01f );
RTTI_PROPERTY( m_boxForPretestSize ).editable().range( 0.005f, 2.0f );
RTTI_PROPERTY_CATEGORY( "Visualization settings" );
RTTI_PROPERTY( m_selectedVerticesColor ).editable();
RTTI_PROPERTY( m_freeVerticesColor ).editable();
RTTI_PROPERTY( m_selectedBoxSolidColor ).editable();
RTTI_PROPERTY( m_selectedBoxOutlineColor ).editable();
RTTI_PROPERTY( m_sphereSelectingColor ).editable();
RTTI_PROPERTY( m_sphereFreeingColor ).editable();

RTTI_PROPERTY( m_enableBoxOverlay ).editable();
RTTI_PROPERTY( m_renderSelectedBox ).editable();
RTTI_PROPERTY( m_renderSelectedVertices ).editable();

RTTI_END_TYPE()

namespace tools
{
	/// drag and drop handler
	class MeshEditorObjectDragDropHandler : public IWorldSpaceDragDropHandler
	{
	public:
		MeshEditorObjectDragDropHandler( ObjectHandler& handler, const ObjectCreationInfo& info, ObjectPreviewPtr previewObject, red::SharedPtr< rend::DepthResult >& depthMap, SnapManager* snapManager )
			: IWorldSpaceDragDropHandler( depthMap, nullptr, snapManager )
			, m_handler( handler )
			, m_previewObject( previewObject )
			, m_info( info )
		{}

		virtual ~MeshEditorObjectDragDropHandler()
		{
			m_previewObject->OnDestroy();
		}

		virtual void OnMouseMoved( const Vector3& position, const Quaternion& rotation )  override final
		{
			// update preview transform
			m_info.m_transform = Transform( position, rotation );

			// move the visualization
			m_previewObject->OnMove( m_info.m_transform );
		}

		virtual Bool OnFinished( const Vector3& position, const Quaternion& rotation ) override final
		{
			// update final transform
			m_info.m_transform = Transform( position, rotation );

			// destroy the visualization before creating final object
			m_previewObject->OnDestroy();

			// create final object
			// this WILL create proper undo step and all other fluff
			return m_handler.CreateObject( m_info );
		}

		virtual void OnRender( const CRenderCamera& camera, rend::IDebugDrawer& dd ) const override final
		{
			// render custom stuff
			m_previewObject->OnRender( camera, dd );
		}
	private:
		ObjectPreviewPtr	m_previewObject;

		ObjectHandler&		m_handler;
		ObjectCreationInfo	m_info;

		red::UniquePtr< rend::DepthResult >		m_depthMap;
	};

	/// input handler for handling mesh vertices selection
	class MarkMeshVerticesInputHandler : public IPreviewInputHandler
	{
	public:
		MarkMeshVerticesInputHandler( MeshPreviewPanel& panel, Int32 startX, Int32 startY, red::DynArray< Vector3 >& points, 
			red::DynArray< Bool >& selectionLookup, const THandle<MarkVerticesHandlerOptions>& options, Box& meshBounds, Bool isSelecting )
			: m_panel( panel )
			, m_currentX( startX )
			, m_currentY( startY )
			, m_isSelecting( isSelecting )
			, m_meshVertices( points )
			, m_meshVerticeSelectionLookup( selectionLookup )
			, m_hasMoved( true )
			, m_options( options )
			, m_meshBounds( meshBounds )
		{
		}

		void OnRender( const CRenderCamera& camera, rend::IDebugDrawer& dd ) const
		{
			const Float& radius = m_options->m_radius;
			rend::FragmentBuilder builder( dd );
			Color color = m_isSelecting ? m_options->m_sphereSelectingColor : m_options->m_sphereFreeingColor;
			builder.SetColor( color );
			builder.EnableTransparency( true );
			builder.AddSolidSphere( m_currentWorldSpace, radius );

			constexpr Float maxBoxSizeMultiplier = 35.f;
			Box boxForPreTest = Box( m_currentWorldSpace, std::min( m_options->m_boxForPretestSize, log( radius * maxBoxSizeMultiplier ) ) );
			builder.AddWireBox( boxForPreTest );

			Float pyramidSize = m_options->m_verticeSize;
			Vector3 axisX = Vector3::EX() * pyramidSize;
			Vector3 axisY = Vector3::EY() * pyramidSize;
			Vector3 axisZ = Vector3::EZ() * pyramidSize;

			for( auto index : m_meshVertices.Indices() )
			{
				Vector3& vertice = m_meshVertices[index];
				if( !m_meshVerticeSelectionLookup[ index ] )
				{
					if( boxForPreTest.Contains( vertice ) )
					{
						builder.SetColor( m_options->m_freeVerticesColor );
						builder.AddSolidPyramid( vertice, axisX, axisY, axisZ );
					}
				}
				
			}
		}

		Bool OnMouseDown( const InputMouseButton& evt )
		{
			evt.m_viewport.CalcRay( evt.m_x, evt.m_y, m_position, m_direction );
			return false;
		}

		Bool OnMouseUp( const InputMouseButton& evt )
		{
			return false;
		}

		Bool CheckPoint( Float& currBest, Vector3& a, Vector3& b )
		{
			Float dist = (a - b).SquareMag();
			if( dist < currBest )
			{
				currBest = dist;
				return true;
			}
			return false;
		}

		Bool OnMouseMoved( const InputMouseMove& evt )
		{
			// update tracking position
			m_currentX = evt.m_x;
			m_currentY = evt.m_y;
			m_hasMoved = true;

			evt.m_viewport.CalcRay( evt.m_x, evt.m_y, m_position, m_direction );

			// keep us active
			return true;
		}

		Bool OnUpdate( float timeDelta )
		{
			if( m_hasMoved )
			{
				const Float& radius = m_options->m_radius;

				m_hasMoved = false;
				Uint32 currentX = m_currentX;
				Uint32 currentY = m_currentY;

				// Get the world space position from the depth map
				const Rect viewportRect( currentX, currentX + 1, currentY, currentY + 1 );
				auto depthMap = red::CreateSharedPtr< rend::DepthResult >( red::PoolBackend() );
				m_panel.RenderDepthMap( viewportRect, depthMap );

				const Float maxZ = 8000.0f; // TODO no depth limit
				Float radiusSq = radius * radius;
				if( !depthMap->CalcWorldSpacePoint( currentX, currentY, m_currentWorldSpace, maxZ ) )
				{
					Vector4 entryPoint;
					if( !m_meshBounds.IntersectRay( m_position, m_direction, entryPoint ) )
					{
						Vector4 corners[ 8 ];
						m_meshBounds.CalcCorners( corners );
						Plane planes[ 6 ];
						planes[ 0 ] = Plane( corners[ 0 ], corners[ 1 ], corners[ 2 ] );
						planes[ 1 ] = Plane( corners[ 1 ], corners[ 5 ], corners[ 3 ] );
						planes[ 2 ] = Plane( corners[ 5 ], corners[ 4 ], corners[ 7 ] );
						planes[ 3 ] = Plane( corners[ 4 ], corners[ 0 ], corners[ 6 ] );
						planes[ 4 ] = Plane( corners[ 2 ], corners[ 3 ], corners[ 6 ] );
						planes[ 5 ] = Plane( corners[ 4 ], corners[ 5 ], corners[ 0 ] );

						Float distance = 0;
						Float currMinDistance = RED_FLT_MAX;
						for( Uint32 i = 0; i < 6; ++i )
						{
							auto side = planes[ i ].IntersectLine( m_position, m_direction, entryPoint, distance );

							distance = m_meshBounds.Distance( entryPoint );
							if( distance < currMinDistance )
							{
								m_currentWorldSpace = entryPoint;
								currMinDistance = distance;
							}
						}
					}
					else
					{
						m_currentWorldSpace = entryPoint;
					}
				}

				Box boxForPreTest = Box( m_currentWorldSpace, radius );
				for( Uint32 index : m_meshVertices.Indices() )
				{
					Vector3& vertice = m_meshVertices[ index ];
					if( boxForPreTest.Contains( vertice ) )
					{
						if( (vertice - m_currentWorldSpace).SquareMag() < radiusSq )
						{
							if( m_isSelecting )
							{
								m_meshVerticeSelectionLookup[ index ] = true;
							}
							else
							{
								m_meshVerticeSelectionLookup[ index ] = false;
							}
						}
					}
				}
			}

			return true;
		}

		Bool OnMouseWheel( const InputMouseWheel& evt )
		{
			constexpr Float boxMultiplier = 3.0f;
			constexpr Float verticeMultiplier = 0.1f;

			m_options->m_radius += evt.m_delta * m_options->m_radiusStep;
			m_options->m_boxForPretestSize += evt.m_delta * m_options->m_radiusStep * boxMultiplier;
			m_options->m_verticeSize += evt.m_delta * m_options->m_radiusStep * verticeMultiplier;

			m_options->m_radius = std::max( m_options->m_radius, m_options->m_radiusStep );
			m_options->m_boxForPretestSize = std::max( m_options->m_boxForPretestSize, m_options->m_radiusStep * boxMultiplier );
			m_options->m_verticeSize = std::max( m_options->m_verticeSize, m_options->m_radiusStep * verticeMultiplier );

			m_hasMoved = true;

			m_options->NotifyPropertyChanged( rtti::AccessPath() );
			return true;
		}
	private:
		THandle<MarkVerticesHandlerOptions> m_options;
		MeshPreviewPanel& m_panel;
		Vector3 m_currentWorldSpace = Vector3::ZEROS();
		Vector3 m_position = Vector3::ZEROS();
		Vector3 m_direction = Vector3::ZEROS();
		red::DynArray< Vector3 >& m_meshVertices;
		red::DynArray< Bool >& m_meshVerticeSelectionLookup;
		Box& m_meshBounds;
		Int32 m_currentX;
		Int32 m_currentY;
		Bool m_hasMoved;
		Bool m_isSelecting;
	};

	//////////////////////////////////////////////////////////////////////////

	MeshPreviewInputListener::MeshPreviewInputListener( const res::ResourcePath& path, ObjectHandler& handler, const THandle<MarkVerticesHandlerOptions>& verticesHandlerOptions )
		: m_handler( handler )
		, m_meshVertices( red::PoolBackend() )
		, m_meshVerticeSelectionLookup( red::PoolBackend() )
		, m_resource( path )
		, m_currentMeshId( 0 )
		, m_markVerticesHandlerOptions( verticesHandlerOptions )
	{
	}

	MeshPreviewInputListener::~MeshPreviewInputListener() = default;

	void MeshPreviewInputListener::RegisterShortcuts( ShortcutManager& manager, MeshPreviewPanel* panel )
	{
		const CName contextName = panel->GetContextName();

		// Mark vertices - select
		{
			ShortcutBinding binding = ShortcutBinding().Key( EInputKey::IK_LeftMouse, EAction::Down ).Modifier( EInputKey::IK_Alt );

			ShortcutCallback callback = [this, panel]( const KeyEventData& data )
			{
				if( panel->IsMouseCursorOverPreviewPanel() )
					return OnMarkVerticesAction( *panel, panel->GetMouseX(), panel->GetMouseY(), true );

				return false;
			};

			ShortcutBuilder shortcut = ShortcutBuilder().IdName( "idAreaMark" ).Binding( binding ).Callback( callback ).Name( "Mark vertices in the area for further processing." ).Context( contextName );

			manager.Register( shortcut );
		}

		// Mark vertices - deselect
		{
			ShortcutBinding binding = ShortcutBinding().Key( EInputKey::IK_RightMouse, EAction::Down ).Modifier( EInputKey::IK_Alt );

			ShortcutCallback callback = [this, panel]( const KeyEventData& data )
			{
				if( panel->IsMouseCursorOverPreviewPanel() )
					return OnMarkVerticesAction( *panel, panel->GetMouseX(), panel->GetMouseY(), false );

				return false;
			};

			ShortcutBuilder shortcut = ShortcutBuilder().IdName( "idAreaUnmark" ).Binding( binding ).Callback( callback ).Name( "Unmark vertices in the area." ).Context( contextName );

			manager.Register( shortcut );
		}

		// Mark vertices - deselect all
		{
			ShortcutBinding binding = ShortcutBinding().Key( EInputKey::IK_P, EAction::Up );

			ShortcutCallback callback = [this, panel]( const KeyEventData& data )
			{
				this->ClearSelected();
				return true;
			};

			ShortcutBuilder shortcut = ShortcutBuilder().IdName( "idAreaUnmarkAll" ).Binding( binding ).Callback( callback ).Name( "Unmark all vertices." ).Context( contextName );

			manager.Register( shortcut );
		}
	}

    void MeshPreviewInputListener::UnregisterShortcuts(ShortcutManager& manager)
    {
        manager.Unregister("idAreaMark");
        manager.Unregister("idAreaUnmark");
        manager.Unregister("idAreaUnmarkAll");
    }

	void MeshPreviewInputListener::OnRender( const CRenderCamera& camera, rend::IDebugDrawer& dd ) const
	{
		rend::FragmentBuilder builder( dd );

		builder.EnableTransparency( true );
		if( m_markVerticesHandlerOptions->m_renderSelectedBox || m_markVerticesHandlerOptions->m_renderSelectedVertices )
		{
			builder.SetColor( m_markVerticesHandlerOptions->m_selectedVerticesColor );
			Box boundingBoxForPretest = Box::EMPTY();
			Float pyramidSize = m_markVerticesHandlerOptions->m_verticeSize;
			Vector3 axisX = Vector3::EX() * pyramidSize;
			Vector3 axisY = Vector3::EY() * pyramidSize;
			Vector3 axisZ = Vector3::EZ() * pyramidSize;

			for( const auto& pointIn : m_meshVertices.Indices())
			{
				if( m_meshVerticeSelectionLookup[ pointIn ] )
				{
					const Vector3& point = m_meshVertices[ pointIn ];
					if( m_markVerticesHandlerOptions->m_renderSelectedVertices )
					{
						builder.AddSolidPyramid( point, axisX, axisY, axisZ );
					}
					boundingBoxForPretest.AddPoint( point );
				}
			}

			if( m_markVerticesHandlerOptions->m_renderSelectedBox )
			{
				builder.EnableOverlay( m_markVerticesHandlerOptions->m_enableBoxOverlay );
				builder.SetColor( m_markVerticesHandlerOptions->m_selectedBoxSolidColor );
				builder.AddSolidBox( boundingBoxForPretest );
				builder.SetColor( m_markVerticesHandlerOptions->m_selectedBoxOutlineColor );
				builder.AddWireBox( boundingBoxForPretest );
			}
		}
	}

	tools::IPreviewDragDropHandler* MeshPreviewInputListener::OnDragEnter( PreviewPanel& panel, const String& data, const InputMouseMove& evt )
	{
		/// the depth map is going to be rendered for the whole viewport
		const auto viewportWidth = evt.m_viewport.m_viewportWidth;
		const auto viewportHeight = evt.m_viewport.m_viewportHeight;

		/// render depth map for the whole viewport
		auto depthMap = red::CreateSharedPtr<rend::DepthResult>( red::PoolBackend() );
		panel.RenderDepthMap( Rect::Make( 0, 0, viewportWidth, viewportHeight ), depthMap );

		/// setup initial object creation info
		SPhysicsBaseCreationInfo physicsCreationInfo;
		rtti::ValuePtr objectValue = rtti::ValueHolder::Parse( data.AsChar() );
		if( GetTypeObject<SPhysicsBaseCreationInfo>()->WriteValue( IRTTIContext::GetDefault(), &physicsCreationInfo, rtti::AccessPath(), *objectValue ) )
		{
			if( ((physics::EPhysicsBaseType)physicsCreationInfo.m_type) == physics::EPBT_Shape )
			{
				physicsCreationInfo.m_selectedVertices = GetSelectedVertices();
				GetTypeObject<SPhysicsBaseCreationInfo>()->ReadValue( IRTTIContext::GetDefault(), &physicsCreationInfo, rtti::AccessPath(), (rtti::ValuePtr&)objectValue );
			}
		}

		ObjectCreationInfo initInfo( objectValue );
		String reason;
		if( !m_handler.CanCreateObject( initInfo, &reason ) )
		{
			RED_REPORT_ERROR( RED_LOCATION( m_resource ), "Cannot % object from drag and drop: %", RED_NAME_CONSTEXPR( "Create" ), reason );

			return nullptr;
		}

		// Either that or we need to add complex show/hide system to the preview object
		{
			Vector3 fixedPosition = initInfo.m_transform.GetPosition3();
			fixedPosition.Z = -1000.0f;
			initInfo.m_transform.SetPosition( fixedPosition );
		}

		/// do we accept the data type ? if so, a preview object will be created
		auto objectPreview = m_handler.CreateObjectPreview( initInfo );
		if( objectPreview )
		{
			// create the drag&drop handler that will update the preview object and eventually will place it on the scene (or not)
			IPreviewDragDropHandler* handler = RED_NEW( MeshEditorObjectDragDropHandler )(m_handler, initInfo, objectPreview, std::move( depthMap ), &panel.GetSharedData()->GetSnapManager());

			// HACK: setup fake event to update the handler and preferred world position
			// this will refresh the placement of the object
			handler->OnMouseMoved( evt );
			return handler;
		}

		// drag&drop not serviced
		return nullptr;
	}

	void MeshPreviewInputListener::OnDragDrop()
	{
	}

	void MeshPreviewInputListener::OnDragLeave()
	{
	}

	red::DynArray<Vector3> MeshPreviewInputListener::GetSelectedVertices()
	{
		red::DynArray<Vector3> selectedVertices{ red::PoolBackend() };
		selectedVertices.Reserve( m_meshVertices.Size() );
		for( Uint32 pointIn : m_meshVertices.Indices() )
		{
			if( m_meshVerticeSelectionLookup[ pointIn ] )
			{
				selectedVertices.PushBack( m_meshVertices[ pointIn ] );
			}
		}
		return selectedVertices;
	}

	Bool MeshPreviewInputListener::OnMarkVerticesAction( MeshPreviewPanel& panel, Int32 mouseX, Int32 mouseY, Bool isSelecting )
	{
		// Ignore input if the mouse is outside the bounds of the preview panel
		if( mouseX < 0 )
			return false;
		else if( mouseX > static_cast<Int32>(panel.GetWidth()) )
			return false;
		else if( mouseY < 0 )
			return false;
		else if( mouseY > static_cast<Int32>(panel.GetHeight()) )
			return false;

		// start area selection action
		PhysicsObjectResolver* resolver = m_handler.FindObjectResolver< PhysicsObjectResolver >();
		RED_ASSERT( resolver, "Expected to have Physics Object Resolver" );
		physics::PhysicsEditorScene* editorScene = resolver->GetPhysicsEditorScene();
		if( editorScene )
		{
			physics::PhysicsMeshPtr meshPtr = nullptr;
			if( physics::PhysicsBackendHelper::FindPhysicsMesh( editorScene, meshPtr ) )
			{
				if( meshPtr->GetPhysicsObjectID() != m_currentMeshId || m_meshVertices.Empty() )
				{
					ClearSelected();
					m_currentMeshId = meshPtr->GetPhysicsObjectID();
					red::DynArray< Matrix > bonePoses = { red::PoolBackend() };
					meshPtr->GenerateBonePoses( bonePoses );
					const THandle< CMesh >& meshHandle = meshPtr->GetMesh();
					m_meshBounds = Box::EMPTY();
					if( bonePoses.Size() > 1 )
					{
						const mesh::DataViewRO view( *meshHandle );
						const Matrix& meshTransform = meshPtr->GetLocalToWorld();
						Uint32 numChunks = view.GetNumChunks();
						Uint32 currentIndex = 0;
						for( Uint32 c = 0; c < numChunks; ++c )
						{
							const mesh::StreamViewRO<Vector3> dataPositions( view.GetChunkStreamData( c, MST_Position_3F ) );
							const mesh::StreamViewRO<Color> dataBoneIndices( view.GetChunkStreamData( c, MST_SkinningIndices_4U8 ) );
							const mesh::StreamViewRO<Vector4> dataBoneWeights( view.GetChunkStreamData( c, MST_SkinningWeights_4F ) );							
							const mesh::StreamViewRO<Color> dataBoneIndicesExt( view.GetChunkStreamData( c, MST_SkinningIndicesExt_4U8 ) );
							const mesh::StreamViewRO<Vector4> dataBoneWeightsExt( view.GetChunkStreamData( c, MST_SkinningWeightsExt_4F ) );

							if( dataPositions.IsValid() && dataBoneIndices.IsValid() && dataBoneWeights.IsValid() )
							{
								m_meshVertices.Resize( m_meshVertices.Size() + dataPositions.Size() );
								m_meshVerticeSelectionLookup.Resize( m_meshVerticeSelectionLookup.Size() + dataPositions.Size() );
								for( Uint16 i = 0; i < dataPositions.Size(); ++i )
								{
									const auto& p0 = dataPositions[i];
									m_meshVertices[currentIndex] = Vector3::ZEROS();
									if( dataBoneIndicesExt.IsValid() && dataBoneWeightsExt.IsValid() )
									{
										for( Uint32 j = 0; j < 4; ++j )
										{
											m_meshVertices[currentIndex] += bonePoses[dataBoneIndices[i].RGBA[j]].TransformPoint( p0 ) * dataBoneWeights[i][j];
											m_meshVertices[currentIndex] += bonePoses[dataBoneIndicesExt[i].RGBA[j]].TransformPoint( p0 ) * dataBoneWeightsExt[i][j];
										}
									}
									else
									{
										for( Uint32 j = 0; j < 4; ++j )
										{
											m_meshVertices[currentIndex] += bonePoses[dataBoneIndices[i].RGBA[j]].TransformPoint( p0 ) * dataBoneWeights[i][j];
										}
									}
									m_meshVerticeSelectionLookup[ currentIndex ] = false;
									m_meshBounds.AddPoint( m_meshVertices[ currentIndex ] );
									++currentIndex;
								}
							}
						}

						for( auto index : m_meshVertices.Indices() )
						{
							m_meshVertices[index] = meshTransform.TransformPoint( m_meshVertices[index] );
						}
					}
					else
					{
						ExtractVerticesFromMesh( m_meshVertices, *meshHandle );
						m_meshVerticeSelectionLookup.Resize( m_meshVertices.Size() );
						for( auto index : m_meshVertices.Indices() )
						{
							m_meshVertices[index] = bonePoses[0].TransformPoint( m_meshVertices[index] );
							m_meshBounds.AddPoint( m_meshVertices[ index ] );
							m_meshVerticeSelectionLookup[ index ] = false;
						}
					}
				}				

				panel.AttachInputHandler( red::CreateUniquePtr< MarkMeshVerticesInputHandler >( panel, mouseX, mouseY, 
					m_meshVertices, m_meshVerticeSelectionLookup, m_markVerticesHandlerOptions, m_meshBounds, isSelecting ) );
				return true;
			}			
		}	
		return false;
	}


	void MeshPreviewInputListener::ClearSelected()
	{
		m_meshVertices.Clear();
		m_meshVerticeSelectionLookup.Clear();
		m_currentMeshId = 0;
	}
}