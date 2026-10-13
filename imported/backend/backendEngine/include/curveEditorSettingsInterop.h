/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/
#pragma once

namespace tools
{
	class CurveSettingsInterop : public interop::utils::Object
	{
	public:
		CurveSettingsInterop( const String& xAxisPath, const Vector2& xLimits ); // if there is more parameters, make a struct
		virtual ~CurveSettingsInterop();

		interop::InteropNotifier m_curveXAxisPosChanged;

	private:
		interop::InteropNotifier m_curveZoomChanged;
		interop::InteropNotifier m_curveXAxisScrollChanged;
		interop::InteropNotifier m_curveClipboardChanged;

		Vector2 m_xAxisLimit;
		Vector4 m_curveZoom;
		Point m_curveXAxisScroll;
		String m_curveClipboard;
		String m_xAxisPath;
	};
}
