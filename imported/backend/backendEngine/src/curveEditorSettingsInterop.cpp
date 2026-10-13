/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "resourceEditor.h"
#include "curveEditorSettingsInterop.h"

namespace tools
{
	CurveSettingsInterop::CurveSettingsInterop( const String& xAxisPath, const Vector2& xLimits )
		: interop::utils::Object( "curveSettings" )
		, m_curveZoom( Vector4( 1.0f, 1.0f, 1.0f, 1.0f ) )
		, m_xAxisPath( xAxisPath )
		, m_xAxisLimit( xLimits )
	{
		RegisterNotifier( "_curveZoomChanged", m_curveZoomChanged );
		RegisterNotifier( "_curveXAxisScrollChanged", m_curveXAxisScrollChanged );
		RegisterNotifier( "_curveClipboardChanged", m_curveClipboardChanged );
		RegisterNotifier( "_curveXAxisPosChanged", m_curveXAxisPosChanged );

		RegisterValue<Vector4>( "curveZoom",
			/*GET*/ [ this ]( ::interop::IContext& ctx, Vector4& outValue ) { outValue = m_curveZoom; },
			/*SET*/ [ this ]( ::interop::IContext& ctx, const Vector4& newValue )
		{
			m_curveZoom = newValue;
			m_curveZoomChanged.Notify( newValue );
		} );

		RegisterValue<Point>( "curveXAxisScroll",
			/*GET*/ [ this ]( ::interop::IContext& ctx, Point& outValue ) { outValue = m_curveXAxisScroll; },
			/*SET*/ [ this ]( ::interop::IContext& ctx, const Point& newValue )
		{
			m_curveXAxisScroll = newValue;
			m_curveXAxisScrollChanged.Notify( newValue );
		} );
		
		RegisterValue<String>( "curveClipboard",
			/*GET*/ [ this ]( ::interop::IContext& ctx, String& outValue ) { outValue = m_curveClipboard; },
			/*SET*/ [ this ]( ::interop::IContext& ctx, const String& newValue )
		{
			m_curveClipboard = newValue;
			m_curveClipboardChanged.Notify( newValue );
		} );

		RegisterValue<String>( "xAxisPath",
			/*GET*/ [ this ]( ::interop::IContext& ctx, String& outValue ) { outValue = m_xAxisPath; },
			/*SET*/ [ this ]( ::interop::IContext& ctx, const String& newValue )
		{
		} );

		RegisterValue<Vector2>( "xAxisLimit",
			/*GET*/ [ this ]( ::interop::IContext& ctx, Vector2& outValue ) { outValue = m_xAxisLimit; },
			/*SET*/ [ this ]( ::interop::IContext& ctx, const Vector2& newValue )
		{
		} );
	}

	CurveSettingsInterop::~CurveSettingsInterop() = default;
}