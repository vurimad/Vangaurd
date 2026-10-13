%{

#include "scriptExpressionParserInternal.h"
#include "scriptExpressionParserPath.h"

RED_DISABLE_WARNING_MSC( 4065 );	// switch statement contains 'default' but no 'case' labels

%}

%lex-param { ExpTokenStream* stream }
%parse-param { ExpTokenStream* stream }
%parse-param { script::expression::Path* path }
%parse-param { script::expression::ErrorListener* errorListener }

%define api.pure

// %debug
%error-verbose
%verbose

/* THIS LIST SHOULD MATCH EXACTLY THE OTHER LIST IN THE scriptFunctionParser.bison */
/* THIS LIST SHOULD MATCH EXACTLY THE OTHER LIST IN THE scriptFunctionParser.bison */
/* THIS LIST SHOULD MATCH EXACTLY THE OTHER LIST IN THE scriptFunctionParser.bison */
/* THIS LIST SHOULD MATCH EXACTLY THE OTHER LIST IN THE scriptFunctionParser.bison */

/* Data tokens */
%token TOKEN_IDENT
%token TOKEN_INTEGER
%token TOKEN_FLOAT
%token TOKEN_NAME
%token TOKEN_STRING
%token TOKEN_TDBID
%token TOKEN_RESREF
%token TOKEN_BOOL_TRUE
%token TOKEN_BOOL_FALSE
%token TOKEN_NULL

/* Keywords */
%token TOKEN_IMPORT
%token TOKEN_IMPORTONLY
%token TOKEN_HOLY
%token TOKEN_ABSTRACT
%token TOKEN_CONST
%token TOKEN_MUTABLE
%token TOKEN_EXTENDS
%token TOKEN_CLASS
%token TOKEN_ENUM
%token TOKEN_STRUCT
%token TOKEN_FUNCTION
%token TOKEN_DEF
%token TOKEN_EDITABLE
%token TOKEN_INSTANCE_EDITABLE
%token TOKEN_REPLICATED
%token TOKEN_PERSISTENT
%token TOKEN_FINAL
%token TOKEN_VIRTUAL
%token TOKEN_OVERRIDE
%token TOKEN_OUT
%token TOKEN_OPTIONAL
%token TOKEN_SKIP
%token TOKEN_INLINED
%token TOKEN_ARRAY
%token TOKEN_HINT
%token TOKEN_HINTS
%token TOKEN_DEFAULT
%token TOKEN_DEFAULTS
%token TOKEN_BROWSABLE
%token TOKEN_VAR
%token TOKEN_EXEC
%token TOKEN_TIMER
%token TOKEN_WEAK
%token TOKEN_OPERATOR
%token TOKEN_CAST
%token TOKEN_IMPLICIT
%token TOKEN_STATIC
%token TOKEN_VISUAL
%token TOKEN_CORE
%token TOKEN_LOGIC
%token TOKEN_MULTICAST
%token TOKEN_HOST
%token TOKEN_CLIENT
%token TOKEN_RELIABLE
%token TOKEN_QUEST
%token TOKEN_REF
%token TOKEN_DEBUG
%token TOKEN_SYNCHRONIZED

/* Type names */
%token TOKEN_SIMPLE_TYPE
%token TOKEN_CLASS_TYPE
%token TOKEN_ENUM_TYPE

/* Function parsing tokens */
%token TOKEN_OP_IADD
%token TOKEN_OP_ISUB
%token TOKEN_OP_IMUL
%token TOKEN_OP_IDIV
%token TOKEN_OP_IAND
%token TOKEN_OP_IOR
%token TOKEN_OP_LOGIC_OR
%token TOKEN_OP_LOGIC_AND
%token TOKEN_OP_EQUAL
%token TOKEN_OP_NOTEQUAL
%token TOKEN_OP_GREQ
%token TOKEN_OP_LEEQ
%token TOKEN_NEW 
%token TOKEN_IF
%token TOKEN_ELSE
%token TOKEN_SWITCH
%token TOKEN_CASE
%token TOKEN_FOR
%token TOKEN_WHILE
%token TOKEN_DO
%token TOKEN_RETURN
%token TOKEN_BREAK
%token TOKEN_CONTINUE
%token TOKEN_THIS
%token TOKEN_SUPER
%token TOKEN_PRIVATE
%token TOKEN_PROTECTED
%token TOKEN_PUBLIC
%token TOKEN_EVENT
%token TOKEN_TESTONLY

%left '<'
%left '>'
%left TOKEN_OP_GREQ 
%left TOKEN_OP_LEEQ 
%left TOKEN_OP_EQUAL
%left TOKEN_OP_NOTEQUAL
%left TOKEN_OP_LOGIC_OR
%left TOKEN_OP_LOGIC_AND

%left '&'
%left '|'
%left '^'

%left '-'
%left '+'
%left '*'
%left '/'
%left '%'

%token TOKEN_UNARY_LOGICAL_NOT
%token TOKEN_UNARY_BITWISE_NOT

%%

statement
	: expression
	;

access
	: basic_access
	| array
	;

basic_access
	: TOKEN_IDENT						{ path->Add( $<text>1, $<id>1 ); }
	| TOKEN_THIS						{ path->Add( $<text>1, $<id>1 ); }
	| basic_access '.' TOKEN_IDENT		{ path->Add( $<text>3, $<id>3 ); path->Add( $<text>2, $<id>2 ); }
	| array '.' TOKEN_IDENT				{ path->Add( $<text>3, $<id>3 ); path->Add( $<text>2, $<id>2 ); }
	;

array_index
	: TOKEN_INTEGER						{ path->Add( $<text>1, $<id>1 ); }
	| basic_access
	| array
	;

array_start
	: '['
	;
	
array_end
	: ']'								{ path->Add( ".", '.' ); }
	;

array
	: basic_access array_start array_index array_end
	;

literal_token_base
	: TOKEN_BOOL_TRUE
	| TOKEN_BOOL_FALSE
	| TOKEN_INTEGER
	| TOKEN_FLOAT
	| TOKEN_NULL
	| TOKEN_STRING
	| TOKEN_NAME
	;

literal_token
	: literal_token_base { path->Add( $<text>1, $<id>1 ); }
	;

basic_expression_component
	: literal_token
	| access
	;

expression_component
	: basic_expression_component
	| unary_expression
	| binary_expression
	| '(' expression_component ')'
	;

unary_expression
	: '-' { path->Add( "0", TOKEN_INTEGER ); } basic_expression_component	{ path->Add( $<text>1, $<id>1 ); }
	| '-' { path->Add( "0", TOKEN_INTEGER ); } '(' expression_component ')'	{ path->Add( $<text>1, $<id>1 ); }

	| '!' basic_expression_component	{ path->Add( $<text>1, TOKEN_UNARY_LOGICAL_NOT ); }
	| '!' '(' expression_component ')'	{ path->Add( $<text>1, TOKEN_UNARY_LOGICAL_NOT ); }

	| '~' basic_expression_component	{ path->Add( $<text>1, TOKEN_UNARY_BITWISE_NOT ); }
	| '~' '(' expression_component ')'	{ path->Add( $<text>1, TOKEN_UNARY_BITWISE_NOT ); }
	;

binary_expression
	: expression_component '-' expression_component	{ path->Add( $<text>2, $<id>2 ); }
	| expression_component '+' expression_component	{ path->Add( $<text>2, $<id>2 ); }
	| expression_component '*' expression_component	{ path->Add( $<text>2, $<id>2 ); }
	| expression_component '/' expression_component	{ path->Add( $<text>2, $<id>2 ); }
	| expression_component '%' expression_component	{ path->Add( $<text>2, $<id>2 ); }
	
	| expression_component '&' expression_component	{ path->Add( $<text>2, $<id>2 ); }
	| expression_component '|' expression_component	{ path->Add( $<text>2, $<id>2 ); }
	| expression_component '^' expression_component	{ path->Add( $<text>2, $<id>2 ); }

	| expression_component '>'					expression_component	{ path->Add( $<text>2, $<id>2 ); }
	| expression_component '<'					expression_component	{ path->Add( $<text>2, $<id>2 ); }
	| expression_component TOKEN_OP_GREQ		expression_component	{ path->Add( $<text>2, $<id>2 ); }
	| expression_component TOKEN_OP_LEEQ		expression_component	{ path->Add( $<text>2, $<id>2 ); }
	| expression_component TOKEN_OP_EQUAL		expression_component	{ path->Add( $<text>2, $<id>2 ); }
	| expression_component TOKEN_OP_NOTEQUAL	expression_component	{ path->Add( $<text>2, $<id>2 ); }
	| expression_component TOKEN_OP_LOGIC_OR	expression_component	{ path->Add( $<text>2, $<id>2 ); }
	| expression_component TOKEN_OP_LOGIC_AND	expression_component	{ path->Add( $<text>2, $<id>2 ); }
	;

expression
	: expression_component
	;
