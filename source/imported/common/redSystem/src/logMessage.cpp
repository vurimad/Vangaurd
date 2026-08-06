/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

#include "build.h"
#include "logMessage.h"
#include <algorithm>

namespace red
{
	// Time is not check because it is bound to current system time.
	// I'd have to abstract the way I get time, so I can mock it for unit test.

	bool operator==( const LoggerLine & left, const LoggerLine & right )
	{
		return left.type == left.type
			&& left.level == right.level
			&& red::Strcmp( left.buffer, right.buffer ) == 0;
	}

	bool operator==( const LoggerLineWithDataError & left, const LoggerLineWithDataError & right )
	{
		return left.line.type == left.line.type
			&& left.line.level == right.line.level
			&& red::Strcmp( left.line.buffer, right.line.buffer ) == 0
			&& red::Strcmp( left.dataErrorActions, right.dataErrorActions ) == 0
			&& left.dataErrorMessageFormatHash == right.dataErrorMessageFormatHash;
	}
}
