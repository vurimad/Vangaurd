/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "shaderCompilerLogger.h"

IShaderCompilerLogger::IShaderCompilerLogger()
	: m_numErrors( 0 )
	, m_numWarnings( 0 )
	, m_verbose( false )
	, m_logSpam( false )
{
}

IShaderCompilerLogger::~IShaderCompilerLogger()
{
}
