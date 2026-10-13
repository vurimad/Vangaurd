	/* Options */
%option nounistd
%option noyywrap
%option 8bit
%option reentrant
%option nodefault
%option extra-type="InternalState*"
%option verbose

%option nounput
%option noyyget_lineno noyyset_lineno
%option noyyget_debug noyyset_debug
%option noyyget_lval noyyset_lval
%option noyyget_out noyyset_out
%option noyyget_in noyyset_in

	/* Definitions */

%s global
%x mlcomment
%x stringliteral
%x nameliteral
%x tdbidliteral
%x resrefliteral

%{

#define YY_FATAL_ERROR( msg ) { yyguts_t* yyg = (struct yyguts_t*)yyscanner; yyextra->EmitError(); }
#define YY_USER_ACTION yyextra->StoreTokenStart(); yyextra->UpdateContext( yytext, yyleng );
#include "flexSupplimentary.h"
#include "bison_tokens.h"

#define EMIT_TOKEN( id ) yyextra->EmitToken( id )
#define EMIT_SEQUENCE( id ) yyextra->EmitSequence( id )
#define EMIT_COMMENT() yyextra->EmitComment()
#define EMIT_ERROR() yyextra->EmitError()

%}

	/* https://westes.github.io/flex/manual/Numbers.html#Numbers */
dseq      ([[:digit:]]+)
dseq_opt  ([[:digit:]]*)
frac      (({dseq_opt}"."{dseq})|{dseq}".")
fsuff     [fF]
fsuff_opt ({fsuff}?)

	/* Decimal floating const */
dfc       (({frac}{fsuff_opt})|({dseq}{fsuff_opt}))

%%

	/* Rules */

	/* UTF-8 Byte Order Mark */
\xef\xbb\xbf									yyextra->m_currentContext.m_character = 0;

	/* Keywords */
"import"										EMIT_TOKEN( TOKEN_IMPORT );
"importonly"									EMIT_TOKEN( TOKEN_IMPORTONLY );
"holy"											EMIT_TOKEN( TOKEN_HOLY );
"abstract"										EMIT_TOKEN( TOKEN_ABSTRACT );
"const"											EMIT_TOKEN( TOKEN_CONST );
"mutable"										EMIT_TOKEN( TOKEN_MUTABLE );
"extends"										EMIT_TOKEN( TOKEN_EXTENDS );
"class"											EMIT_TOKEN( TOKEN_CLASS );
"enum"											EMIT_TOKEN( TOKEN_ENUM );
"struct"										EMIT_TOKEN( TOKEN_STRUCT );
"function"										EMIT_TOKEN( TOKEN_FUNCTION );
"def"											EMIT_TOKEN( TOKEN_DEF );
"editable"										EMIT_TOKEN( TOKEN_EDITABLE );
"instanceeditable"								EMIT_TOKEN( TOKEN_INSTANCE_EDITABLE );
"replicated"									EMIT_TOKEN( TOKEN_REPLICATED );
"persistent"									EMIT_TOKEN( TOKEN_PERSISTENT );
"final"											EMIT_TOKEN( TOKEN_FINAL );
"visual"										EMIT_TOKEN( TOKEN_VISUAL );
"logic"											EMIT_TOKEN( TOKEN_LOGIC );
"core"											EMIT_TOKEN( TOKEN_CORE );
"virtual"										EMIT_TOKEN( TOKEN_VIRTUAL );
"override"										EMIT_TOKEN( TOKEN_OVERRIDE );
"out"											EMIT_TOKEN( TOKEN_OUT );
"optional"										EMIT_TOKEN( TOKEN_OPTIONAL );
"skip"											EMIT_TOKEN( TOKEN_SKIP );
"inlined"										EMIT_TOKEN( TOKEN_INLINED );
"private"										EMIT_TOKEN( TOKEN_PRIVATE );
"protected"										EMIT_TOKEN( TOKEN_PROTECTED );
"public"										EMIT_TOKEN( TOKEN_PUBLIC );
"event"											EMIT_TOKEN( TOKEN_EVENT );
"timer"											EMIT_TOKEN( TOKEN_TIMER );
"array"											EMIT_TOKEN( TOKEN_ARRAY );
"hint"											EMIT_TOKEN( TOKEN_HINT );
"hints"											EMIT_TOKEN( TOKEN_HINTS );
"default"										EMIT_TOKEN( TOKEN_DEFAULT );
"defaults"										EMIT_TOKEN( TOKEN_DEFAULTS );
"browsable"										EMIT_TOKEN( TOKEN_BROWSABLE );
"true"											EMIT_TOKEN( TOKEN_BOOL_TRUE );
"false"											EMIT_TOKEN( TOKEN_BOOL_FALSE );
"NULL"											EMIT_TOKEN( TOKEN_NULL );
"var"											EMIT_TOKEN( TOKEN_VAR );
"exec"											EMIT_TOKEN( TOKEN_EXEC );
"weak"											EMIT_TOKEN( TOKEN_WEAK );
"operator"										EMIT_TOKEN( TOKEN_OPERATOR );
"cast"											EMIT_TOKEN( TOKEN_CAST );
"implicit"										EMIT_TOKEN( TOKEN_IMPLICIT );
"static"										EMIT_TOKEN( TOKEN_STATIC );
"multicast"										EMIT_TOKEN( TOKEN_MULTICAST );
"host"											EMIT_TOKEN( TOKEN_HOST );
"client"										EMIT_TOKEN( TOKEN_CLIENT );
"reliable"										EMIT_TOKEN( TOKEN_RELIABLE );
"quest"											EMIT_TOKEN( TOKEN_QUEST );
"testonly"										EMIT_TOKEN( TOKEN_TESTONLY );
"ref"											EMIT_TOKEN( TOKEN_REF );
"debug"											EMIT_TOKEN( TOKEN_DEBUG );
"synchronized"									EMIT_TOKEN( TOKEN_SYNCHRONIZED );

"new"											EMIT_TOKEN( TOKEN_NEW );
"if"											EMIT_TOKEN( TOKEN_IF );
"else"											EMIT_TOKEN( TOKEN_ELSE );
"switch"										EMIT_TOKEN( TOKEN_SWITCH );
"case"											EMIT_TOKEN( TOKEN_CASE );
"for"											EMIT_TOKEN( TOKEN_FOR );
"while"											EMIT_TOKEN( TOKEN_WHILE );
"do"											EMIT_TOKEN( TOKEN_DO );
"return"										EMIT_TOKEN( TOKEN_RETURN );
"break"											EMIT_TOKEN( TOKEN_BREAK );
"continue"										EMIT_TOKEN( TOKEN_CONTINUE );
"this"											EMIT_TOKEN( TOKEN_THIS );
"super"											EMIT_TOKEN( TOKEN_SUPER );

	/* Syntax */
"+="											EMIT_TOKEN( TOKEN_OP_IADD );
"-="											EMIT_TOKEN( TOKEN_OP_ISUB );
"*="											EMIT_TOKEN( TOKEN_OP_IMUL );
"/="											EMIT_TOKEN( TOKEN_OP_IDIV );
"&="											EMIT_TOKEN( TOKEN_OP_IAND );
"|="											EMIT_TOKEN( TOKEN_OP_IOR );
"||"											EMIT_TOKEN( TOKEN_OP_LOGIC_OR );
"&&"											EMIT_TOKEN( TOKEN_OP_LOGIC_AND );
"=="											EMIT_TOKEN( TOKEN_OP_EQUAL );
"!="											EMIT_TOKEN( TOKEN_OP_NOTEQUAL );
">="											EMIT_TOKEN( TOKEN_OP_GREQ );
"<="											EMIT_TOKEN( TOKEN_OP_LEEQ );

"("												EMIT_TOKEN( '(' ); 
")"												EMIT_TOKEN( ')' );
"{"												{ EMIT_TOKEN( '{' ); ++yyextra->m_scopeLevel; }
"}"												{ EMIT_TOKEN( '}' ); --yyextra->m_scopeLevel; }
"["												EMIT_TOKEN( '[' );
"]"												EMIT_TOKEN( ']' );
"<"												EMIT_TOKEN( '<' );
">"												EMIT_TOKEN( '>' );
"&"												EMIT_TOKEN( '&' );
"|"												EMIT_TOKEN( '|' );
"^"												EMIT_TOKEN( '^' );
"~"												EMIT_TOKEN( '~' );
"+"												EMIT_TOKEN( '+' );
"-"												EMIT_TOKEN( '-' );
"*"												EMIT_TOKEN( '*' );
"/"												EMIT_TOKEN( '/' );
"%"												EMIT_TOKEN( '%' );
"!"												EMIT_TOKEN( '!' );
"."												EMIT_TOKEN( '.' );
";"												EMIT_TOKEN( ';' );
"?"												EMIT_TOKEN( '?' );
"="												EMIT_TOKEN( '=' );
","												EMIT_TOKEN( ',' );
":"												EMIT_TOKEN( ':' );

	/* Identifier */
[a-zA-Z_]+[a-zA-Z_0-9]*							EMIT_TOKEN( TOKEN_IDENT );

	/* Invalid Unicode chars */
[^\x00-\x7F]*									EMIT_ERROR();

	/* Whitespace */
[ \t\r]

	/* Integer */
[0-9]+											EMIT_TOKEN( TOKEN_INTEGER );

	/* Hexadecimal Integer */
"0"[Xx][0-9a-fA-F]+								EMIT_TOKEN( TOKEN_INTEGER );

	/* float */
{dfc}											EMIT_TOKEN( TOKEN_FLOAT );

	/* Strings */
\"												{ yyextra->StoreSequenceStart(); BEGIN( stringliteral ); }

<stringliteral>
{
	[^"]*
	\"											{ EMIT_SEQUENCE( TOKEN_STRING ), BEGIN( global ); }
}

	/* Names */
'												{ yyextra->StoreSequenceStart(); BEGIN( nameliteral ); }

<nameliteral>
{
	[^']*
	'											{ EMIT_SEQUENCE( TOKEN_NAME ), BEGIN( global ); }
}

	/* TweakDBID */
"T\""											{ yyextra->StoreSequenceStart( 1 /*skip \" character*/ ); BEGIN( tdbidliteral ); }
<tdbidliteral>
{
	[^"]*
	\"											{ EMIT_SEQUENCE( TOKEN_TDBID ), BEGIN( global ); }
}

	/* ResRef */
"R\""											{ yyextra->StoreSequenceStart( 1 /*skip \" character*/ ); BEGIN( resrefliteral ); }
<resrefliteral>
{
	[^"]*
	\"											{ EMIT_SEQUENCE( TOKEN_RESREF ), BEGIN( global ); }
}

	/* Comments */
"//"[^\r\n]*									{ yyextra->StoreSequenceStart(); EMIT_COMMENT(); }
"/*"											{ yyextra->StoreSequenceStart(); BEGIN( mlcomment ); }

<mlcomment>
{
	[^*\n]+
	"*"
	"*/"										{ EMIT_COMMENT(); BEGIN( global ); }
}

<*>\n											yyextra->NextLine();

	/* Default Rule */
.|\n											EMIT_ERROR();

%%

/* User code */
