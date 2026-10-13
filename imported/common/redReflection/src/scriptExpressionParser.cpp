/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "scriptExpressionParser.h"

#include "scriptExpressionParserToken.h"

#include "../../../internal/RedLexer/include/listener.h"
#include "../../../internal/RedLexer/include/lexer.h"

#include "scriptExpressionParser_bison.cxx.h"
#include "scriptExpressionParser_bison.cxx"

namespace script
{
	namespace expression
	{
		class Listener final : public lexer::native::IListener
		{
		public:
			Listener() = default;
			~Listener() = default;

			virtual void Comment( const lexer::native::State& ) override {}
			virtual void Error( const lexer::native::State& lexerState ) override {}

			virtual void Token( const lexer::native::State& lexerState, unsigned int id ) override
			{
				const AnsiChar* text = lexerState.m_source + lexerState.m_tokenStart.m_byte;
				const Uint32 length = lexerState.m_currentContext.m_byte - lexerState.m_tokenStart.m_byte;

				m_tokens.PushBack( script::expression::ReadOnlyToken { red::StringView( text, length ), id } );
			}

			virtual void Sequence( const lexer::native::State& lexerState, unsigned int id ) override
			{
				// Sequences include the surrounding quote marks, so we need to reduce the size of the token text respectively
				const AnsiChar* text = lexerState.m_source + lexerState.m_sequenceStart.m_byte + 1;
				const Uint32 length = lexerState.m_currentContext.m_byte - lexerState.m_sequenceStart.m_byte - 2;

				m_tokens.PushBack( script::expression::ReadOnlyToken { red::StringView( text, length ), id } );
			}

			const red::DynArray< script::expression::ReadOnlyToken >& GetTokens() const { return m_tokens; }

		private:
			red::DynArray< script::expression::ReadOnlyToken > m_tokens{ red::PoolScript() };
		};

		Parser::Parser() = default;
		Parser::~Parser() = default;

		Bool Parser::Parse( const AnsiChar* expression, Path& path )
		{
			Listener listener;

			lexer::native::Lexer lexer;
			lexer.Tokenize( expression, &listener );

			auto& tokens = listener.GetTokens();
			TokenStream< ReadOnlyToken > stream( &tokens );

			ErrorListener errorListener;
			yyparse( &stream, &path, &errorListener );

			return !errorListener.HasError();
		}
	}
}
