/**
* Copyright (c) 2007-2016 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "debugFilter.h"
#include "debugFilterMask.h"
#include "renderVertices.h"
#include "renderDrawCommon.h"
#include "fontStyle.h"
#include "../../redMemory/include/uniquePtr.h"
#include "../../renderData/include/renderObjectPtr.h"

class IRenderSelectable;
class IRenderObject;
class IRenderResource;
class RenderStageCollector;

namespace debug
{
	class RadialMenu;
}

namespace rend
{
	class Font;
	struct GlyphInfo;

	class IDebugDrawer;
	typedef red::UniquePtr< IDebugDrawer > DebugDrawerPtr;

	/// an debug drawing functionality
	/// all debug drawing is camera independent (because it can be collected while the camera for a given frame is not yet known, also we may have multiple cameras)
	/// all debug drawing is maskable (per view), allows to show different debug things in a splitscreen or PIP mode
	/// on screen debug drawing may be screen "global" or relative to the sub-view (menu vs debug health bar in splitscreen)
	/// NOTE: the drawing does not do any drawing, it only collects stuff for rendering later
	/// NOTE: the filtering flags cannot be chnaged once the drawer was created
	class RENDER_DATA_API IDebugDrawer
	{
		RED_USE_MEMORY_POOL( rend::PoolRenderDebug );

	public:
		virtual ~IDebugDrawer();

		/// get the width of the master screen
		RED_FORCE_INLINE const Uint32 GetWidth() const { return m_width; }

		/// get the height of the master screen
		RED_FORCE_INLINE const Uint32 GetHeight() const { return m_height; }

		/// get the mask this drawer was created with
		RED_FORCE_INLINE const DebugFilterMask& GetMask() const { return m_mask; }

		/// generic settings for the drawer
		struct RENDER_DATA_API RenderSettings
		{
			// If material override and parameters are specified, use that instead of the default material.
			TRenderPtr<IRenderResource>	m_materialOverride;
			TRenderPtr<IRenderResource>	m_materialOverrideParameters;

			Uint8						m_viewMask;			// in which views should we render this (mask)
			Bool						m_overlay:1;		// render on top of everything else (like there was no Z buffer)
			Bool						m_transparent:1;	// render in the transparent group, alpha channel will be used in the AlphaBlend mode
			Bool						m_align:1;			// align to camera ("Billboarding"), cheap way to do sprites. Any rotation or scale applied to the sprite is discarded (as it would break the billboarding effect)
			Uint32						m_selectableID;		// allows the objects to be selected in the hit-proxy mode
			Color						m_color;			// color modulation
			math::Matrix				m_localToWorld;		// not used if m_instancesLocalToWorlds is not empty
			red::DynArray<math::Matrix>	m_instancesLocalToWorlds{ rend::PoolRenderDebug() };	// instances transforms to use, copied on submit
			TRenderPtr<IRenderObject>	m_resource;			// resource (texture, mesh, debug mesh, etc), depends,

			ERenderingPlane				m_renderPlane;

			RenderSettings();
			~RenderSettings();
		};

		/// screen space relative stuff
		struct RENDER_DATA_API RenderScreenSpaceSettings
		{
			// If material override and parameters are specified, use that instead of the default material.
			TRenderPtr<IRenderResource>	m_materialOverride;
			TRenderPtr<IRenderResource>	m_materialOverrideParameters;

			TRenderPtr<IRenderObject>	m_resource;			// resource (texture, mesh, debug mesh, etc), depends on the fragment
			Uint32						m_selectableID;		// allows the objects to be selected in the hit-proxy mode
			Int32						m_positionX;		// absolute/relative position where to render stuff, <0 - align from right
			Int32						m_positionY;		// absolute/relative position where to render stuff, <0 - align from bottom
			Vector3						m_reference;		// 3d position to render at, if not specified than global screen coordinates are used, a RED_FLT_MAX position is used to indiacate "invalid"
			Color						m_color;			// color modulation
			Int8						m_cameraIndex;		// camera index, -1 is the global screen, >= 0 - split screen (PIP) regions 
			Bool						m_isText;
			Bool						m_transparent;
			Bool						m_postImGui;

			RenderScreenSpaceSettings();
			~RenderScreenSpaceSettings();
		};

		/// nested debug drawer
		struct RENDER_DATA_API RenderDebugDrawerSettings
		{
			// #TODO: Maybe it would be useful to not use the UniquePtr, so that a sub drawer can be created once and
			// then reused multiple times. Would be best to "finalize" the sub drawer in that case, to prevent adding
			// more fragments after submission (possibly while it's being rendered).
			red::UniquePtr<IDebugDrawer> m_drawer;
			Rect m_targetRect;
		};

		enum class DebugRenderMode : Uint8
		{
			Lines,
			Wireframe,
			Solid,

			LinesBiased,
			WireframeBiased,
			SolidBiased,

			Text,

			BiasOffset = LinesBiased
		};

		/// geometry specification
		struct RENDER_DATA_API RenderGeometry
		{
			DebugRenderMode			m_renderMode;

			IndexType				m_indexType:2; // type used for indices. if m_indices is provided, this must be set accordingly.
			static_assert( Uint32(IndexType::MAX) <= 4, "Need more bits for m_indexType" );

			DebugVertexUV*			m_vertices; // NOTE: data is copied from this buffer
			Uint32					m_numVertices;

			const void*				m_indices; // NOTE: data is copied from this buffer, can be NULL for non-indexed geometry
			Uint32					m_numIndices;
			
			const Font*				m_font;
			GlyphInfo*				m_glyphBuffer;		// NOTE: data is moved from this buffer, must be set when rendering text
			FontStyle				m_fontStyle;
			Uint32					m_numGlyphs;
			math::Color				m_fontOutlineColor;

			TRenderPtr<IRenderResource>	m_mesh;	   // Custom mesh

			RenderGeometry();
			~RenderGeometry();
		};

		/// is this a null renderer ?
		virtual const Bool IsNull() const = 0;

		/// submit list of world primitives to rendering, if the index buffer is not specified we assume it's a primitive list
		/// NOTE: thread safe, use the builders for utility functions (like DrawBox, etc)
		virtual void SubmitGeometry( const RenderSettings& settings, const RenderGeometry& geometry ) = 0;

		/// submit screen space primitives to rendering, if the index buffer is not specified we assume it's a primitive list
		/// NOTE: thread safe, use the builders for utility functions (like DrawRect, etc)
		virtual void SubmitScreenGeometry( const RenderScreenSpaceSettings& settings, const RenderGeometry& geometry ) = 0;

		/// Submit a "sub debug drawer" to this one. World-space fragments (using RenderSettings) are drawn as-is.
		/// Screen-space fragments (RenderScreenSpaceSettings) are transformed to fit the given drawer's viewport into
		/// the given targetRect. This drawer will take ownership of the sub drawer (obviously, it's a UniquePtr and
		/// it's being moved).
		virtual void SubmitDebugDrawer( RenderDebugDrawerSettings&& settings ) = 0;

		/// create a NULL debug drawer, eats all rendering, cheap failsafe
		static IDebugDrawer* CreateNullDrawer();

		virtual ::debug::RadialMenu* GetRadialMenu() const = 0;
		virtual void SetRadialMenu( ::debug::RadialMenu* rm ) = 0;

		// Set whether subsequent fragments should be binned as high priority or not. High priority fragments are not
		// subject to budgeting within the renderer, and may be drawn in more situations than regular fragments.
		// NOTE: Please don't use this! It's intended just to make sure a limited set of critical fragments will always
		// be able to be drawn. Unauthorized use may be subject to removal without notice.
		RED_INLINE Bool SetHighPriority( Bool highPriority ) 
		{ 
			const Bool oldVal = m_highPriority;  
			m_highPriority = highPriority; 
			return oldVal; 
		}

		virtual void Collect( EFeatureFlag baseFeatureFlag, RenderStageCollector& stageCollector, const CRenderCamera* camera, Uint32 renderWidth, Uint32 renderHeight, const Vector2& screenScale, const Vector2& screenOffset ) const;
		virtual void Collect( EFeatureFlag baseFeatureFlag, RenderStageCollector& stageCollector, const CRenderCamera* camera, Uint32 renderWidth, Uint32 renderHeight, const Vector2& screenScale, const Vector2& screenOffset, Bool highPriorityOnly ) const;

	protected:
		IDebugDrawer( const DebugFilterMask& mask, const Uint32 width, const Uint32 height );

		Bool m_highPriority;

	private:
		DebugFilterMask m_mask;
		Uint32			m_width;
		Uint32			m_height;
	};

	class RENDER_DATA_API DebugDrawerScriptProxy
	{
		RTTI_DECLARE_TYPE( DebugDrawerScriptProxy );

	private:
		DebugDrawerScriptProxy();

	public:
		DebugDrawerScriptProxy( IDebugDrawer& drawer );

		RED_INLINE IDebugDrawer& GetDebugDrawer() const { return m_drawer; }

	private:
		static void funcTestDebugFilterMask( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );

	private:
		IDebugDrawer& m_drawer;
	};
	
} // rend