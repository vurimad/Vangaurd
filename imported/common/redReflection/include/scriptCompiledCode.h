/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

#include "dataBuffer.h"
#include "../../redContainers/include/string/string.h"

class CScriptFile;

/// Compiled code block
class RED_REFLECTION_API CScriptCompiledCode
{
public:
	//! Get the line number in the source file this function was declared
	RED_INLINE Uint32 GetSourceLine() const { return m_sourceLine; }

	//! Get code buffer
	RED_INLINE Uint8* GetCode() { return static_cast< Uint8* >( m_code.Data() ); }
	RED_INLINE const Uint8* GetCode() const { return static_cast< const Uint8* >( m_code.Data() ); }

	const CScriptFile* GetScriptFile() const;
	const red::String& GetFilename() const;

	//! Get end of the code
	RED_INLINE const Uint8* GetCodeEnd() const { return GetCode() + m_code.Size(); }

public:
	CScriptCompiledCode();

	//! Initialize source code buffer
	void Initialize( const Int32 fileIndex, const Uint32 sourceLine, const void* code, const Uint32 codeSize );

protected:
	Uint32							m_sourceFileIndex; //!< Index of file info stored in CScriptingSystem
	Uint32							m_sourceLine;	//!< Line in the source file this function was defined
	DataBuffer						m_code;			//!< Compiled script code
};
