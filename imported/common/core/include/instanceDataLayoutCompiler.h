/**
* Copyright (c) 2007-16 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "coreApi.h"
#include "../../../common/redContainers/include/redContainersPublic.h"

#ifdef RED_ASSERTS_ENABLED
#define INST_BUFFER_DEBUG( x ) x
#else
#define INST_BUFFER_DEBUG( ... )
#endif

class InstanceVar;
class InstanceDataLayout;

// Array of instanced variables with their offsets
typedef red::DynArray< std::pair< InstanceVar*, Uint32 > > TInstanceVarList;

/// Compiles the layout of data buffer
class RED_CORE_API InstanceDataLayoutCompiler
{
protected:
	Uint32				m_currentSize;		//!< Current size
	TInstanceVarList	m_variables;		//!< Allocated variables with their offsets
	InstanceDataLayout* m_dataLayout;		//<! Used OMLY in game for direct building of variables list

public:
	//! Get the size so far
	RED_INLINE Uint32 GetCurrentSize() const { return m_currentSize; }

	//! Get the variable map
	RED_INLINE const TInstanceVarList& GetVariables() const { return m_variables; }

public:
	InstanceDataLayoutCompiler( InstanceDataLayout& dataLayout );
	
	//! Add the variable to the compiled layout
	Uint32 AddVar( InstanceVar& var, Uint32 numElem = 1 );
};