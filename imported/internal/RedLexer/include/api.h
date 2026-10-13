/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#ifdef _USRDLL
#	ifdef NATIVE_LEXER_EXPORTS
#		define NATIVE_LEXER_API __declspec( dllexport )
#		define NATIVE_LEXER_API_TEMPLATE
#	else
#		define NATIVE_LEXER_API __declspec(dllimport)
#		define NATIVE_LEXER_API_TEMPLATE extern
#	endif
#else
#	define NATIVE_LEXER_API
#	define NATIVE_LEXER_API_TEMPLATE
#endif
