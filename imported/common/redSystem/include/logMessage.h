/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */
#ifndef _RED_SYSTEM_LOG_LINE_H_
#define _RED_SYSTEM_LOG_LINE_H_

#include <chrono>

namespace red
{
	const Uint32 c_loggerLineLength = 3072;
	const Uint32 c_loggerDateLength = 21;
	const Uint32 c_maxDataErrorActionsSize = 2048;
	const Uint32 c_maxDataErrorMessageFormatSize = 1024;

	enum LoggerLineType : Uint8
	{
		LoggerLineType_Log,
		LoggerLineType_Flush,
		LoggerLineType_DataError
	};

	enum LoggerLevel : Uint8;

	struct LoggerLine
	{
		Uint64 frame;
		std::chrono::system_clock::time_point time;
		const char* prefix;
		Uint32 threadId;
		Uint32 threadContextId;
		Uint32 netPeerId; //< Using raw integer type insted of net::PeerID to avoid cyclic dependencies with networkCore project.
		LoggerLineType type;
		LoggerLevel level;
		LoggerCategory category;
		char buffer[ c_loggerLineLength ];
	};

	struct LoggerLineWithDataError
	{
		LoggerLine line;
		char dataErrorActions[ c_maxDataErrorActionsSize ];
		Uint32 dataErrorMessageFormatHash;
	};

	REDSYSTEM_API bool operator==( const LoggerLine & left, const LoggerLine & right );
	REDSYSTEM_API bool operator==( const LoggerLineWithDataError & left, const LoggerLineWithDataError & right );
}

#endif
