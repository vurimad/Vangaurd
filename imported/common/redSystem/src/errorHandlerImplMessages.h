/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace red { namespace err
{
	void PrintErrorMessage( Uint32 errFlags, red::EErrorReason errorReason, const red::ErrorMessage& customErrorMsg, char* scratchBuffer, Uint32 bufferSize );
	void PrintErrorMessage( Uint32 errFlags, red::EErrorReason errorReason, char* scratchBuffer, Uint32 bufferSize );

#if defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_DURANGO )
	void PrintErrorMessage( Uint32 errFlags, _EXCEPTION_POINTERS* exceptionInfo, char* scratchBuffer, Uint32 bufferSize );
#endif

} } // red/err
