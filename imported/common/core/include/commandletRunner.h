/*
 * Copyright (c) 2010 CD Projekt Red. All Rights Reserved.
 */
#pragma once

class CommandLine;

namespace red
{
	// NOTE: Only for main() type functions!
	RED_CORE_API int RunCommandlet( const String& commandletName, const CommandLine& commandLine );
} // red