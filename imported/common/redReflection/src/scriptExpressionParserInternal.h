/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "scriptExpressionParserToken.h"

// This file exists as the "header" for the bison for the expression parser
// So any dependencies or utilities that will only be used by Bison go here
// (Rather than type C++ directly into the .bison file which is unformatted
// and makes it harder to track down compilation errors)
namespace script
{
	namespace expression
	{
		template< typename T >
		class TokenStream
		{
		public:
			TokenStream( const red::DynArray< T >* tokens )
				: m_tokens( tokens )
				, m_index( 0 )
			{
			}

			void Next() { ++m_index; }
			Bool End() const { return m_index >= m_tokens->Size(); }

			const T& GetToken() const { return ( *m_tokens )[ m_index ]; }

		private:
			const red::DynArray< T >* m_tokens;
			Uint32 m_index;
		};

		class Path;

		int parsertime( ReadOnlyToken& lvalp, TokenStream< ReadOnlyToken >* stream );
		void errortime( const ReadOnlyToken& lvalp, const TokenStream< ReadOnlyToken >* stream, const Path* path, ErrorListener* errorListener, const char* msg );
	}
}

using ExpTokenStream = script::expression::TokenStream< script::expression::ReadOnlyToken >;
//using ExpTokenStream = script::expression::TokenStream< script::expression::Token >;

#define YYSTYPE script::expression::ReadOnlyToken

#define YYLEX script::expression::parsertime( yylval, stream )
#define yyerror( stream, path, errorListener, msg ) script::expression::errortime( yylval, stream, path, errorListener, msg )
