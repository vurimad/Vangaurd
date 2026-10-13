/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/
#pragma once

class RENDER_DATA_API IShaderCompilerLogger
{
	RED_USE_MEMORY_POOL( red::PoolBackend );

public:
	static constexpr Int32 s_lenShort = 4096;
	static constexpr Int32 s_lenLong = s_lenShort * 4;

	IShaderCompilerLogger();
	virtual ~IShaderCompilerLogger();

	/// Verbose log (printed only with -verbose)
	virtual void Log( STATIC_CHECK_PRINTF_MSC const AnsiChar* txt, ... ) = 0;

	/// Forced log (printed regardless of -verbose switch - try not to use it too often)
	virtual void LogAlways( STATIC_CHECK_PRINTF_MSC const AnsiChar* txt, ... ) = 0;

	/// Spam log (lots of additional information that a regular user does not need, can be turned on with -logSpam switch)
	virtual void LogSpam( STATIC_CHECK_PRINTF_MSC const AnsiChar* txt, ... ) = 0;

	/// Warning
	virtual void Warn( STATIC_CHECK_PRINTF_MSC const AnsiChar* txt, ... ) = 0;

	/// Error
	virtual void Error( STATIC_CHECK_PRINTF_MSC const AnsiChar* txt, ... ) = 0;

	RED_FORCE_INLINE const Uint32 GetNumWarnings() const { return m_numWarnings; }
	RED_FORCE_INLINE const Uint32 GetNumErrors() const { return m_numErrors; }
	RED_FORCE_INLINE void SetVerbose( Bool verbose ) { m_verbose = verbose; }
	RED_FORCE_INLINE void SetLogSpam( Bool logSpam ) { m_logSpam = logSpam; }


protected:
	Uint32			m_numErrors;
	Uint32			m_numWarnings;
	Bool			m_verbose;
	Bool			m_logSpam;
	red::Mutex		m_logMutex;

};
