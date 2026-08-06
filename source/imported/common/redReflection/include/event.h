/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "scriptable.h"

namespace red
{
	class RED_REFLECTION_API Event : public IScriptable
	{
		RTTI_DECLARE_TYPE( Event );
		RED_USE_MEMORY_POOL( red::PoolEvent );

	public:
		Event();
		virtual ~Event();

		Uint16 GetEventTypeID() const;

#ifdef RED_EVENT_SENDER_INFO_AVAILABLE
	public:
		struct SenderInfo
		{
			struct CodeStacktrace
			{
				static const Uint32 c_framesSize = 64;
				const void* m_frames[ c_framesSize ];
				Uint32 m_framesCount = 0;
			};
			CodeStacktrace m_codeStacktrace;

			struct ScriptsStacktrace
			{
				const AnsiChar* m_class;
				const AnsiChar* m_function;
				Uint32 m_line;
			};
			static const Uint32 c_scriptsStacktraceSize = 16;
			ScriptsStacktrace m_scriptsStacktrace[ c_scriptsStacktraceSize ];
		};

		void DEBUG_CaptureCodeStacktrace();
		void DEBUG_CaptureScriptsStacktrace( CScriptStackFrame& stack );

	private:
		SenderInfo m_senderInfo;
#endif
	};
}
