/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "drawer.h"
#include "debugFilterMask.h"
#include "resolutionParams.h"
#include "renderPublicEnums.h"

#include "../../redReflection/include/scriptStackFrame.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( DebugDrawerScriptProxy, rend )
	RTTI_SCRIPT_ALIAS( "DebugDrawer" )
	RTTI_NATIVE_STATIC_FUNCTION( "TestDebugFilterMask", funcTestDebugFilterMask )
RTTI_END_TYPE()

namespace rend
{
	IDebugDrawer::RenderSettings::RenderSettings()
		: m_materialOverride( nullptr )
		, m_materialOverrideParameters( nullptr )
		, m_overlay( false )
		, m_transparent( false )
		, m_align( false )
		, m_selectableID( 0 )
		, m_color( Color::WHITE() )
		, m_resource( nullptr )
		, m_viewMask( 0xFF ) // everywhere
		, m_renderPlane( RPl_Scene )
		, m_localToWorld( math::Matrix::IDENTITY() )
	{}

	IDebugDrawer::RenderSettings::~RenderSettings()
	{

	}

	IDebugDrawer::RenderScreenSpaceSettings::RenderScreenSpaceSettings()
		: m_materialOverride( nullptr )
		, m_materialOverrideParameters( nullptr )
		, m_positionX( 0 )
		, m_positionY( 0 )
		, m_reference( RED_FLT_MAX, RED_FLT_MAX, RED_FLT_MAX )
		, m_cameraIndex( -1 )
		, m_resource( nullptr )
		, m_color( Color::WHITE() )
		, m_isText( false )
		, m_transparent( false )
		, m_selectableID( 0 )
		, m_postImGui( false )
	{}

	IDebugDrawer::RenderScreenSpaceSettings::~RenderScreenSpaceSettings()
	{

	}

	IDebugDrawer::RenderGeometry::RenderGeometry()
		: m_renderMode( DebugRenderMode::Lines )
		, m_indexType( IndexType::MAX )
		, m_vertices( nullptr )
		, m_numVertices( 0 )
		, m_indices( nullptr )
		, m_numIndices( 0 )
		, m_mesh( nullptr )
		, m_glyphBuffer( nullptr )
		, m_numGlyphs( 0 )
		, m_font( nullptr )
		, m_fontOutlineColor( math::Color::BLACK( 128 ) )
	{}

	IDebugDrawer::RenderGeometry::~RenderGeometry()
	{

	}

	IDebugDrawer::IDebugDrawer( const DebugFilterMask& mask, const Uint32 width, const Uint32 height )
		: m_highPriority( false )
		, m_mask( mask )
		, m_width( width )
		, m_height( height )
	{}

	IDebugDrawer::~IDebugDrawer()
	{}

	namespace helper
	{
		class NullDebugDrawer : public IDebugDrawer
		{
		public:
			NullDebugDrawer()
				: IDebugDrawer( DebugFilterMask(), 
								rend::dim::Preset<rend::dim::eCONSOLE_BASE>::W, 
								rend::dim::Preset<rend::dim::eCONSOLE_BASE>::H )
			{}

			virtual ~NullDebugDrawer()
			{
			}

			virtual const Bool IsNull() const 
			{
				return true;
			}

			virtual void SubmitGeometry( const RenderSettings& settings, const RenderGeometry& geometry ) override final
			{
			}

			virtual void SubmitScreenGeometry( const RenderScreenSpaceSettings& settings, const RenderGeometry& geometry ) override final
			{
			}

			virtual void SubmitDebugDrawer( RenderDebugDrawerSettings&& settings ) override final
			{
				// Just move the settings out, so it's emptied as expected
				RenderDebugDrawerSettings moved(std::move(settings));
			}

			virtual debug::RadialMenu* GetRadialMenu() const override final
			{
				return nullptr;
			}

			virtual void SetRadialMenu( debug::RadialMenu* rm ) override final
			{
			}
		};

	} // helper

	IDebugDrawer* IDebugDrawer::CreateNullDrawer()
	{
		return RED_NEW( helper::NullDebugDrawer )();
	}

	void IDebugDrawer::Collect(EFeatureFlag baseFeatureFlag, RenderStageCollector& stageCollector, const CRenderCamera* camera, Uint32 renderWidth, Uint32 renderHeight, const Vector2& screenScale, const Vector2& screenOffset) const
	{
	
	}
	
	void IDebugDrawer::Collect(EFeatureFlag baseFeatureFlag, RenderStageCollector& stageCollector, const CRenderCamera* camera, Uint32 renderWidth, Uint32 renderHeight, const Vector2& screenScale, const Vector2& screenOffset, Bool highPriorityOnly) const
	{
	
	}

	/////////////////////////////////////////////////////////////

	// As there is no abstract struct in red script we must provide default constructor
	DebugDrawerScriptProxy::DebugDrawerScriptProxy()
		: m_drawer( *static_cast< IDebugDrawer* >( reinterpret_cast< void* >( 1 ) ) ) // ugly hack, but needed for compilation reasons.
	{
		RED_HALT( "DebugDrawerScriptProxy cannot be constructed this way." );
	}

	DebugDrawerScriptProxy::DebugDrawerScriptProxy( IDebugDrawer& drawer )
		: m_drawer( drawer )
	{
	}

	void DebugDrawerScriptProxy::funcTestDebugFilterMask( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
	{
		GET_PARAMETER_SCRIPT_REF( DebugDrawerScriptProxy, self );
		GET_PARAMETER( String, debugFilterName, String() );
		FINISH_PARAMETERS;

		const rend::DebugFilterHierarchyNode* node = rend::DebugFilterHierarchyNode::FindNode( debugFilterName.AsChar() );
		if( node == nullptr )
		{
			node = rend::DebugFilterHierarchyNode::CreateNode( debugFilterName.AsChar() );
		}

		if( node )
		{
			RETURN_BOOL( self.m_drawer.GetMask().Test( node->GetFilter() ) );
		}
		else
		{
			RETURN_BOOL( false );
		}
	}

} // rend
