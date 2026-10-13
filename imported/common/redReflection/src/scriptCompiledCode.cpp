/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#include "build.h"
#include "scriptCompiledCode.h"

#include "scriptingSystem.h"
#include "scriptingSystemImpl.h"
#include "dataBuffer.h"
#include "scriptFile.h"

CScriptCompiledCode::CScriptCompiledCode()
	: m_code()
	, m_sourceLine( 0 )
{
}

void CScriptCompiledCode::Initialize( const Int32 fileIndex, const Uint32 sourceLine, const void* code, const Uint32 codeSize )
{
	// Code location information
	m_sourceLine = sourceLine;
	m_sourceFileIndex = fileIndex;

	// Initialize source code
	m_code = DataBuffer::Copy( code, codeSize, red::PoolScript() );
}

const CScriptFile* CScriptCompiledCode::GetScriptFile() const
{
	return static_cast<CScriptingSystem&>( IScriptingSystem::GetInstance() ).GetScriptFile( m_sourceFileIndex );
}

const red::String& CScriptCompiledCode::GetFilename() const
{
	static const red::String unknownPath( "UnknownFile" );

	auto* file = GetScriptFile();
	return file ? file->GetPath() : unknownPath;
}
