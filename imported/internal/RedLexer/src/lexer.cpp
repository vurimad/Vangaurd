/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "lexer.h"

#include "flexSupplimentary.h"

#ifdef _WINDOWS
#	pragma warning( push )
#	pragma warning( disable : 4005 )
#		include "../gen/scripts.cxx"
#	pragma warning( pop )
#else
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmacro-redefined"
#pragma clang diagnostic ignored "-Wunneeded-internal-declaration"
#pragma clang diagnostic ignored "-Wunused-function"
#		include "../gen/scripts.cxx"
#pragma clang diagnostic pop
#endif

namespace lexer { namespace native {

void Lexer::Tokenize( const char* str, lexer::native::IListener* listener ) const
{
	InternalState lexData( str, listener );

	yyscan_t scanner;
	yylex_init_extra( &lexData, &scanner );

	YY_BUFFER_STATE buf = yy_scan_string( str, scanner );
	yylex( scanner );

	yy_delete_buffer( buf, scanner );
	yylex_destroy( scanner );
}

}} // namespace lexer { namespace native {
