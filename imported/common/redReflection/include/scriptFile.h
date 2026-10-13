#pragma once

#include "scriptBreakpointResult.h"
#include "scriptBreakpointRuntime.h"
#include "scriptInstrumentationObjectRuntime.h"

class RED_REFLECTION_API CScriptFile
{
	RED_USE_MEMORY_POOL( red::PoolScript );
public:
	CScriptFile();
	~CScriptFile() = default;
	
	RED_INLINE void SetPath( const red::String& path ) { m_path = path; }
	RED_INLINE const red::String& GetPath() const { return m_path; }

	RED_INLINE void SetHashedPath( Uint32 hash ) { m_pathHash = hash; }
	RED_INLINE Uint32 GetHashedPath() const { return m_pathHash; }

	RED_INLINE void SetCRC( Uint32 crc ) { m_sourceCRC = crc; }
	RED_INLINE Uint32 GetCRC() const { return m_sourceCRC; }

	void AddBreakpoint( const script::RuntimeBreakpoint& breakpoint );
	void SortBreakpoints();
	void ClearBreakpoints();

	script::RuntimeBreakpoint* FindBreakpoint( Uint32 position );

	script::BreakpointResult SetBreakpoint( Uint32 position, Bool isSet );

	red::DynArray< script::BreakpointResult > DisableAllBreakpoints();

#ifdef USE_PROFILER
	void AddInstrumentationObject( Int32 codeOffset, rtti::Function* function );
	void ClearInstrumentationObjects();
#endif

private:
	Uint32 m_pathHash;
	Uint32 m_sourceCRC;
	red::String m_path; //relative path to the file

	red::Map< Uint32, script::RuntimeBreakpoint > m_breakpoints;

#ifdef USE_PROFILER
	red::DynArray< red::SharedPtr< script::RuntimeInstrumentationObject, red::PoolDebug > > m_instrumentationObjects;
#endif
};
