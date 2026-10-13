/**
* Copyright (c) 2007-2018 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../redMemory/include/sharedPtr.h"

namespace GpuWrapApi
{
	enum eHDRMode : Uint8;
}

enum EViewportWindowMode : Uint8
{
	VWM_Windowed,		// Regular windowed mode with title, border and such
	VWM_Borderless,		// "Borderless fullscreen windowed" mode, basically a borderless window that covers the screen
	VWM_Fullscreen,		// Classic fullscreen mode
	VWM_EditorPreview,	// for internal use of RenderViewport only
};

enum class EAspectRatio : Uint8
{
	FR_NONE,
	FR_21_9,
	FR_16_9,
	FR_16_10,
	FR_4_3,
};

enum EMouseMode
{
	MM_Normal,			// Normal mouse mode
	MM_Clip,			// Mouse is clipped within the window bounds
	MM_Capture,			// Mouse is captured by the window and is invisible
	MM_ClipAndCapture,	// Mouse is captured by the window and clipped within the window bounds
};

using ViewportHandle = red::SharedPtr< IViewport >;
struct IViewportWindowMessageSink;
struct IViewportWindowActionListener;

/// Base class for rendering viewport
class RENDER_DATA_API IViewport : public red::NonCopyable
{
	RED_USE_MEMORY_POOL( rend::PoolRenderData );

protected:
	Uint32				m_width;
	Uint32				m_height;
	Uint32				m_x;
	Uint32				m_y;
	Uint32				m_fullWidth;
	Uint32				m_fullHeight;
	EViewportWindowMode	m_windowMode;
	EMouseMode			m_mouseMode;
	EAspectRatio	    m_cachetAspectRatio;

public:
	IViewport( Uint32 width, Uint32 height, EViewportWindowMode windowMode );
	virtual ~IViewport();
	virtual bool IsNull() = 0;
	// Getters
	// Get width of rendering canvas
	Uint32 GetWidth() const { return m_width; }

	// Get height of rendering canvas
	Uint32 GetHeight() const { return m_height; }

	// Get aspect ratio of rendering canvas
	Float GetAspectRatio() const { return m_height ? ((Float)m_width / (Float)m_height) : 1.0f; }

	// Get X offset of rendering canvas
	Uint32 GetX() const { return m_x; }

	// Get Y offset of rendering canvas
	Uint32 GetY() const { return m_y; }

	EViewportWindowMode GetViewportWindowMode() const { return m_windowMode; }
	Bool IsMinimized() const { return m_width < 1 && m_height < 1; }

	// Get width of window
	Uint32 GetFullWidth() const { return m_fullWidth; }

	// Get height of window
	Uint32 GetFullHeight() const { return m_fullHeight; }

	// Get aspect ratio of window
	Float GetFullAspectRatio() const { return m_fullHeight ? ((Float)m_fullWidth / (Float)m_fullHeight) : 1.0f; }

	EMouseMode GetMouseMode() const { return m_mouseMode; }

	// Rendering canvas and window sizes are different so it is cachet scenario
	Bool IsCachet() const { return m_cachetAspectRatio != EAspectRatio::FR_NONE; }

	virtual void SetFocus() {}
	virtual void SetForegroundWindow() {}

	//
	virtual Uint32 GetRendererWidth() const { return m_width; }
	virtual Uint32 GetRendererHeight()  const { return m_height; }

	virtual GpuWrapApi::eHDRMode GetHDRMode() const = 0;
	virtual Bool IsFullscreenMode() const = 0;

	// interface
	virtual Bool AdjustSize( Uint32 width, Uint32 height );
	virtual Bool AdjustSize( Uint32 x, Uint32 y, Uint32 width, Uint32 height );
	virtual void AdjustSizeWithCachets( EAspectRatio cachetAspectRatio );
	virtual void RestoreSize();

	virtual Uint64 GetViewportID() const = 0;
	virtual void AttachMessageSink(IViewportWindowMessageSink*)=0;

	virtual void RegisterActionListener( IViewportWindowActionListener* listener ) = 0;
	virtual void UnregisterActionListener( IViewportWindowActionListener* listener ) = 0;

	virtual void Activate() {}
	virtual void Deactivate() {}

	virtual void RequestWindowMode( EViewportWindowMode windowMode )=0;
	virtual void RequestWindowSize( Uint32 width, Uint32 height )=0;
	virtual void RequestWindowPosition( Int32 left, Int32 top ) {}
	virtual void RequestOutputMonitor( Uint32 outputMonitorIndex)=0;
	virtual void RequestHDRMode( /*GpuWrapApi::eHDRMode*/ Uint8 mode )=0;
	virtual void RequestHDRMaxBrightness( Float maxBrightness )=0;
	virtual void RequestHDRPaperWhite( Float paperWhite )=0;
	virtual void RequestHDRTonemappingMidpoint( Float tonemappingMidpoint )=0;
	virtual void RequestGamma( Float gamma ) = 0;
	virtual void RequestVSyncInterval( Uint32 ) = 0;
	virtual void RequestFPSClamp( Int32 ) = 0;
	virtual Int32 GetFPSClamp() const = 0;

#ifdef RED_PLATFORM_WINPC
	virtual void RequestDLSS( Bool enabled ) = 0;
	virtual Bool GetDLSS() const = 0;
	virtual void RequestDLSSQualityPreset( Int32 quality ) = 0;
	virtual Int32 GetDLSSQualityPreset() const = 0;
	virtual void RequestDRS( Bool enabled ) = 0;
	virtual void RequestDRSTargetFPS( Uint32 fps ) = 0;
	virtual void RequestDRSMaximalResolution( Uint32 maxResPercentage ) = 0;
	virtual void RequestDRSMinimalResolution( Uint32 minResPercentage ) = 0;
	virtual Bool GetDRS() const = 0;
	virtual Uint32 GetDRSTargetFPS() const = 0;
	virtual Uint32 GetDRSMaximalResolution() const = 0;
	virtual Uint32 GetDRSMinimalResolution() const = 0;
	virtual void RequestSRS( Bool enabled ) = 0;
	virtual void RequestSRSResolution( Uint32 resPercentage ) = 0;
	virtual Bool GetSRS() const = 0;
	virtual Uint32 GetSRSResolution() const = 0;
#endif

	virtual Float GetHDRMaxMonitorBrightness() const = 0;
	virtual Float GetHDRPaperWhite() const = 0;
	virtual Float GetHDRTonemappingMidpoint() const = 0;
	virtual Float GetGamma() const = 0;
	virtual Uint32 GetVSyncInterval() const = 0;
	virtual Uint32 GetOutputMonitor() const = 0;
	RED_INLINE EViewportWindowMode GetWindowMode() const { return m_windowMode; };
	virtual void UpdateViewportProperties() = 0;

protected:
	void GetSizeAdjustedToCachet( const EAspectRatio aspectRatio, const Uint32 inFullWidth, const Uint32 inFullHeight, Int32& outWidth, Int32& outHeight ) const;
};
