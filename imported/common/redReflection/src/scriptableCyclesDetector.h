/*
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

//////////////////////////////////////////////////////////////////////////
// Simple circular dependencies detection.
// Algorithm performs BFS through all registered IScriptable.
//
// Uncomment the following to enable IScriptable circular dependencies detection
// #define DETECT_SCRIPTABLE_CYCLES

class IScriptable;

namespace debug
{

class ScriptableCyclesDetector
{
public:

	static void Register( const IScriptable* object );
	static void Unregister( const IScriptable* object );
	static void DetectCycles();

private:

	static red::DynArray< const IScriptable* >	s_registeredObjects;
};

} // debug