%{
// Some yacc (bison) defines
#define YYDEBUG 1
#define YYERROR_VERBOSE 1

/// Disable warnings caused by Bison (disable these when editing the grammar to make sure you don't cause any yourself)
RED_DISABLE_WARNING_MSC( 4244 )	// conversion from 'int' to 'short', possible loss of data
RED_DISABLE_WARNING_MSC( 4065 )	// switch statement contains 'default' but no 'case' labels
RED_DISABLE_WARNING_MSC( 4267 )	// conversion from 'size_t' to 'int', possible loss of data
RED_DISABLE_WARNING_MSC( 4127 )	// conditional expression is constant
RED_DISABLE_WARNING_MSC( 4702 )	// Unreachable code

/// The parsing token
#define YYSTYPE YYSTYPE_File
#include "scriptFileParser_bison.cxx.h"

/// Parser interface
class CScriptFileParserWorker
{
public:
	CScriptFileParserWorker( CScriptTokenStream& tokens, CScriptFileParser& parser )
		: m_tokens( &tokens )
		, m_parser( &parser )
		, m_token( nullptr )
		, m_flags( 0 )
	{};

	int yylex( YYSTYPE* lvalp )
	{
		if( m_tokens->IsEndOfStream() )
			return 0;
	
		CScriptToken& token = m_tokens->GetReadToken();
		m_tokens->IncrementReadPosition();

		lvalp->m_context = &token.m_context;
		lvalp->m_token = token.m_text;

		m_token = &token;

		return token.m_token;
	}

	int yyerror( const char *msg, YYSTYPE* lvalp )
	{
		// Emit error
		const String& errorStringAnsi = String::Printf( "%hs, near '%hs'", msg, lvalp->m_token.ToString().AsChar() );
		m_parser->EmitError( lvalp->m_context, errorStringAnsi );

		// Continue
		return 0;
	}

	void ExtractFunctionCode()
	{
		m_tokens->ExtractFunctionTokens( m_parser->GetCodeTokens() );
	}

	void ExtractInitCode( CScriptTokenStream& initCode )
	{
		m_tokens->ExtractInitCode( initCode );
	}

	void SetGlobalFlags( const Uint64 flags )
	{
		m_flags = flags;
	}

	const Uint64 GetGlobalFlags() const
	{
		return m_flags;
	}

	void AddAttribute( const ScriptAttribute& attribute )
	{
		m_attributes.PushBack( attribute );
	}

	void ClearAttributes()
	{
		m_attributes.Clear();
	}

	const ScriptAttributes& GetAttributes() const
	{
		return m_attributes;
	}

	CScriptFileParser* GetParser()
	{
		return m_parser;
	}

	Int32 GetCurrentTokenLine() const
	{
		if( !m_token )
			return -1;

		return m_token->m_context.m_line;
	}

	// returns the line of the last token in current context
	int GetLastTokenLine() const
	{
		if ( m_tokens->IsEmpty() )
		{
			return -1;
		}

		return m_tokens->GetLastToken().m_context.m_line;
	}

private:
	CScriptTokenStream*		m_tokens;
	CScriptFileParser*		m_parser;
	CScriptToken*			m_token;

	ScriptAttributes		m_attributes;
	Uint64					m_flags;
};

// Error function
static int yyerror_thread_safe( const char *msg, void* param, YYSTYPE* lvalp )
{
	CScriptFileParserWorker* worker = (CScriptFileParserWorker*) param;
	return worker->yyerror( msg, lvalp );
}

// Token reader
int yylex_thread_safe( YYSTYPE* lvalp, void* param )
{
	CScriptFileParserWorker* worker = (CScriptFileParserWorker*) param;
	return worker->yylex( lvalp );	
}

#define GFileContext ((CScriptFileParserWorker*) param)
#define GFileParser (((CScriptFileParserWorker*) param)->GetParser())

#define YYPARSE_PARAM param 
#define YYLEX_PARAM param 

#define YYLEX yylex_thread_safe(&yylval, YYLEX_PARAM)
#define yyerror(msg) yyerror_thread_safe(msg, YYPARSE_PARAM, &yylval) 

%}

%define api.pure

/* ------------------------------------------------------------------
   Token definitions
   ------------------------------------------------------------------ */

/* Expect 0 shift/reduce conflicts */
%expect 0

/* THIS LIST SHOLD MATCH EXACTLY THE OTHER LIST IN THE scriptFunctionParser.bison */
/* THIS LIST SHOLD MATCH EXACTLY THE OTHER LIST IN THE scriptFunctionParser.bison */
/* THIS LIST SHOLD MATCH EXACTLY THE OTHER LIST IN THE scriptFunctionParser.bison */
/* THIS LIST SHOLD MATCH EXACTLY THE OTHER LIST IN THE scriptFunctionParser.bison */

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

%%

////////////////////////////////
// PROGRAM
////////////////////////////////

program_file
	: file_declaration_list
	;

////////////////////////////////
// DECLARATIONS
////////////////////////////////
 
file_declaration_list
	: file_declaration_list file_declaration
	| /* empty */
	;

file_declaration
	: file_import_flag file_elem_declaration optional_semicolon
	| file_importonly_flag file_compound_type_declaration optional_semicolon
	| file_holy_flag file_compound_type_declaration optional_semicolon
	;

file_import_flag
	: import_flag		{ GFileContext->SetGlobalFlags( $<m_flags>1 ); }
	;

file_importonly_flag
	: importonly_flag	{ GFileContext->SetGlobalFlags( $<m_flags>1 ); }
	;

file_holy_flag
	: holy_flag			{ GFileContext->SetGlobalFlags( $<m_flags>1 ); }
	;

file_elem_declaration
	: struct_dcl
	| enum_dcl
	| class_dcl
	| attributes function_dcl
	| function_dcl
	;

file_compound_type_declaration
	: struct_dcl
	| class_dcl
	;

////////////////////////////////
// FLAGS
////////////////////////////////

class_flags
	: TOKEN_ABSTRACT	class_flags		{ $<m_flags>$ = SSF_Abstract | $<m_flags>2; }
	| TOKEN_PRIVATE		class_flags		{ $<m_flags>$ = SSF_Private | $<m_flags>2; }
	| TOKEN_PROTECTED	class_flags		{ $<m_flags>$ = SSF_Protected | $<m_flags>2; }
	| TOKEN_PUBLIC		class_flags		{ $<m_flags>$ = SSF_Public | $<m_flags>2; }
	| TOKEN_FINAL		class_flags		{ $<m_flags>$ = SSF_Final | $<m_flags>2; }
	| TOKEN_TESTONLY	class_flags		{ $<m_flags>$ = SSF_TestOnly | $<m_flags>2; }
	| /* empty */						{ $<m_flags>$ = 0; }
	;

struct_flags
	: TOKEN_FINAL		struct_flags	{ $<m_flags>$ = SSF_Final | $<m_flags>2; }
	| TOKEN_TESTONLY	struct_flags	{ $<m_flags>$ = SSF_TestOnly | $<m_flags>2; }
	| /* empty */						{ $<m_flags>$ = 0; }
	;

function_flags
	: TOKEN_EXEC		function_flags	{ $<m_flags>$ = SSF_Exec | $<m_flags>2; }
	| TOKEN_TIMER		function_flags	{ $<m_flags>$ = SSF_Timer | $<m_flags>2; }
	| TOKEN_STATIC		function_flags	{ $<m_flags>$ = SSF_Static | $<m_flags>2; }
	| TOKEN_MULTICAST	function_flags	{ $<m_flags>$ = SSF_Multicast | $<m_flags>2; }
	| TOKEN_HOST		function_flags	{ $<m_flags>$ = SSF_Host | $<m_flags>2; }
	| TOKEN_CLIENT		function_flags	{ $<m_flags>$ = SSF_Client | $<m_flags>2; }
	| TOKEN_RELIABLE	function_flags	{ $<m_flags>$ = SSF_Reliable | $<m_flags>2; }
	| TOKEN_VIRTUAL		function_flags	{ $<m_flags>$ = SSF_Virtual | $<m_flags>2; }
	| TOKEN_OVERRIDE	function_flags	{ $<m_flags>$ = SSF_Override | $<m_flags>2; }
	| TOKEN_CONST		function_flags	{ $<m_flags>$ = SSF_Const | $<m_flags>2; }
	| TOKEN_QUEST		function_flags	{ $<m_flags>$ = SSF_Quest | $<m_flags>2; }
	| TOKEN_FINAL		function_flags	{ $<m_flags>$ = SSF_Final | $<m_flags>2; }
	| TOKEN_TESTONLY	function_flags	{ $<m_flags>$ = SSF_TestOnly | $<m_flags>2; }
	| /* empty */						{ $<m_flags>$ = 0; }
	;

var_flags
	: TOKEN_CONST				var_flags	{ $<m_flags>$ = SSF_Const | $<m_flags>2; }
	| TOKEN_MUTABLE				var_flags	{ $<m_flags>$ = SSF_Mutable | $<m_flags>2; }
	| TOKEN_EDITABLE			var_flags	{ $<m_flags>$ = SSF_Editable | $<m_flags>2; }
	| TOKEN_INSTANCE_EDITABLE	var_flags	{ $<m_flags>$ = SSF_InstanceEditable | $<m_flags>2; }
	| TOKEN_REPLICATED			var_flags	{ $<m_flags>$ = SSF_Replicated | $<m_flags>2; }
	| TOKEN_INLINED				var_flags	{ $<m_flags>$ = SSF_Inlined | $<m_flags>2; }
	| TOKEN_PERSISTENT			var_flags	{ $<m_flags>$ = SSF_Persistent | $<m_flags>2; }
	| TOKEN_TESTONLY			var_flags	{ $<m_flags>$ = SSF_TestOnly | $<m_flags>2; }
	| /* empty */							{ $<m_flags>$ = 0; }
	;

////////////////////////////////
// COMMON
////////////////////////////////

import_flag
	: TOKEN_IMPORT			{ $<m_flags>$ = SSF_Import; }
	| /* empty */			{ $<m_flags>$ = 0; }
	;

importonly_flag
	: TOKEN_IMPORTONLY		{ $<m_flags>$ = SSF_ImportOnly; }
	;

holy_flag
	: TOKEN_HOLY			{ $<m_flags>$ = SSF_ImportOnly | SSF_Final; }
	;

access_flag
	: TOKEN_PRIVATE			{ $<m_flags>$ = SSF_Private; }
	| TOKEN_PROTECTED		{ $<m_flags>$ = SSF_Protected; }
	| TOKEN_PUBLIC			{ $<m_flags>$ = SSF_Public; }
	| /* empty */			{ $<m_flags>$ = 0; }
	;

attributes
	: '[' ident '=' default_val_const ']' attributes
	{
		GFileContext->AddAttribute( ScriptAttribute( $<m_token>2, $<m_token>4, $<m_context>1 ) );
	}
	| '[' ident '=' default_val_const ']'
	{
		GFileContext->AddAttribute( ScriptAttribute( $<m_token>2, $<m_token>4, $<m_context>1 ) );
	}
	| '[' ident ']' attributes
	{
		GFileContext->AddAttribute( ScriptAttribute( $<m_token>2, red::StringView(), $<m_context>1 ) );
	}
	| '[' ident ']'
	{
		GFileContext->AddAttribute( ScriptAttribute( $<m_token>2, red::StringView(), $<m_context>1 ) );
	}
	;

////////////////////////////////
// CLASS
////////////////////////////////

class_dcl
	: class_header '{' class_elem_list '}' { GFileParser->EndDefinition( GFileContext->GetCurrentTokenLine(), $<m_context>2, $<m_context>4 ); GFileParser->PopContext(); }
	;

class_header
	: class_flags TOKEN_CLASS ident class_extends { GFileParser->StartClass( $<m_context>3, $<m_context>4, $<m_string>3, $<m_string>4, $<m_flags>1 | GFileContext->GetGlobalFlags() ); }
	;

class_elem_list
	: class_elem_list class_element
	| /* empty */
	;

class_element
	: class_element_inner
	| browsable_dcl
	| hints_dcl
	| defaults_dcl
	;

class_access_and_import_flags
	: access_flag import_flag		{ GFileContext->SetGlobalFlags( $<m_flags>1 | $<m_flags>2 ); }
	;

class_element_inner
	: attributes class_access_and_import_flags function_dcl
	| class_access_and_import_flags function_dcl
	| attributes class_access_and_import_flags var_dcl semicolon
	| class_access_and_import_flags var_dcl semicolon
	;

class_extends
	: TOKEN_EXTENDS ident	{ $<m_string>$ = $<m_string>2; $<m_context>$ = $<m_context>2; }
	| /* empty */			{ $<m_string>$ = (""); $<m_context>$ = nullptr; }
	;

////////////////////////////////
// VARIABLES
////////////////////////////////

var_dcl
	:	var_flags
		TOKEN_VAR
		function_param_ident_list ':' full_type
		{
			const YYSTYPE_File::Idents& idents = $<m_idents>3;
			const Uint32 identsCount = idents.Size();

			const ScriptAttributes& attributes = GFileContext->GetAttributes();

			for ( Uint32 i = 0; i < identsCount; ++i )
			{
				GFileParser->AddProperty( idents[i].m_context, $<m_context>5, idents[i].m_value, $<m_flags>1 | GFileContext->GetGlobalFlags(), $<m_typeName>5, false, attributes );
			}

			GFileContext->ClearAttributes();
		}
	;

////////////////////////////////
// STRUCT
////////////////////////////////

struct_dcl
	:  struct_info '{' struct_elem_list '}'		{ GFileParser->EndDefinition( GFileContext->GetCurrentTokenLine(), $<m_context>2, $<m_context>4 ); GFileParser->PopContext(); }
	;

struct_info
	: struct_flags TOKEN_STRUCT ident struct_extends { GFileParser->StartStruct( $<m_context>3, $<m_context>4, $<m_string>3, $<m_string>4, $<m_flags>1 | GFileContext->GetGlobalFlags() ); }
	;

struct_elem_list
	: struct_elem_list struct_element
	| /* empty */
	;

struct_element 
	: struct_element_inner
	| browsable_dcl
	| hints_dcl
	| defaults_dcl
	;

struct_access_and_import_flags
	: access_flag import_flag		{ GFileContext->SetGlobalFlags( $<m_flags>1 | $<m_flags>2 ); }
	;

struct_element_inner
	: attributes struct_access_and_import_flags function_dcl
	| struct_access_and_import_flags function_dcl
	| attributes struct_access_and_import_flags var_dcl semicolon
	| struct_access_and_import_flags var_dcl semicolon
	;

struct_extends
	: TOKEN_EXTENDS ident	{ $<m_string>$ = $<m_string>2; $<m_context>$ = $<m_context>2; }
	| /* empty */			{ $<m_string>$ = (""); $<m_context>$ = nullptr; }
	;


////////////////////////////////
// ENUM
////////////////////////////////

enum_dcl
	:  enum_info '{' enum_name_list optional_comma '}'	{ GFileParser->EndDefinition( GFileContext->GetCurrentTokenLine(), $<m_context>2, $<m_context>5 ); GFileParser->PopContext(); }
	;

enum_info
	: TOKEN_ENUM ident { GFileParser->StartEnum( $<m_context>2, $<m_string>2, GFileContext->GetGlobalFlags() ); }
	| TOKEN_ENUM ident ':' ident
	{
		$<m_typeName>$ = CScriptTypeDummy::MakeSimple( $<m_string>4 );
		GFileParser->StartEnum( $<m_context>2, $<m_string>2, $<m_typeName>$, GFileContext->GetGlobalFlags() );
	}
	;
 
enum_name_list 
	: enum_name_list ',' enum_name	{}
	| enum_name						{}
	;
 
enum_name
	: ident						{ GFileParser->AddEnumOption( $<m_context>1, $<m_string>1 ); }
	| ident	'=' integer			{ GFileParser->AddEnumOption( $<m_context>1, $<m_string>1, $<m_integer>3 ); }
	| ident	'=' '-' integer		{ GFileParser->AddEnumOption( $<m_context>1, $<m_string>1, -$<m_integer>4 ); }
	;

////////////////////////////////
// BROWSABLE
////////////////////////////////
browsable_dcl
	: TOKEN_BROWSABLE '{' browsable_list '}'
	| TOKEN_BROWSABLE browsable_val
	;

browsable_list
	: browsable_val browsable_list
	| /* empty */
	;

browsable_val
	: ident '=' TOKEN_BOOL_TRUE semicolon			{ GFileParser->SetBrowsable( $<m_context>1, $<m_string>1, true ); }
	| ident '=' TOKEN_BOOL_FALSE semicolon			{ GFileParser->SetBrowsable( $<m_context>1, $<m_string>1, false ); }
	;

////////////////////////////////
// HINT
////////////////////////////////

hints_dcl
	: TOKEN_HINTS '{' hints_list '}'
	| TOKEN_HINT hint_val	
	;

hints_list
	: hint_val hints_list 
	| /* empty */
	;

hint_val
	: ident '=' string semicolon					{ GFileParser->AddHint( $<m_context>1, $<m_string>1, $<m_string>3 ); }
	;

////////////////////////////////
// DEFAULTS
////////////////////////////////

defaults_dcl
	: TOKEN_DEFAULTS '{' defaults_val_list '}'
	| TOKEN_DEFAULT default_val	
	;
	
defaults_val_list
	: default_val defaults_val_list 
	| /* empty */
	;
	
default_val
	: ident '=' default_val_const semicolon					{ GFileParser->AddDefaultValue( $<m_context>1, $<m_string>1, $<m_token>3, false ); }
	| ident '=' '-' default_arithmetic_val_const semicolon	{ GFileParser->AddDefaultValue( $<m_context>1, $<m_string>1, $<m_token>4, true ); }
	| ident '=' TOKEN_IDENT '.' TOKEN_IDENT semicolon
	{
		red::String tmp = $<m_token>3.ToString().TrimLeft().TrimRight() + $<m_token>4.ToString() + $<m_token>5.ToString().TrimRight();
		red::StringView view{ tmp };
		GFileParser->AddDefaultValue( $<m_context>1, $<m_string>1, view, false );
	}
	;

default_val_const
	: TOKEN_INTEGER									{ $<m_token>$ = $<m_token>1; }
	| TOKEN_FLOAT									{ $<m_token>$ = $<m_token>1; }
	| TOKEN_STRING									{ $<m_token>$ = $<m_token>1; }
	| TOKEN_NAME									{ $<m_token>$ = $<m_token>1; }
	| TOKEN_TDBID									{ $<m_token>$ = $<m_token>1; }
	| TOKEN_RESREF									{ $<m_token>$ = $<m_token>1; }
	| TOKEN_BOOL_TRUE								{ $<m_token>$ = $<m_token>1; }
	| TOKEN_BOOL_FALSE								{ $<m_token>$ = $<m_token>1; }
	;

default_arithmetic_val_const
	: TOKEN_INTEGER									{ $<m_token>$ = $<m_token>1; }
	| TOKEN_FLOAT									{ $<m_token>$ = $<m_token>1; }
	;

////////////////////////////////
// FUNCTION
////////////////////////////////

function_dcl
	: function_header
	;

function_header
	: function_inner_header '(' function_params ')' function_ret_value function_tail 
	;

function_ret_value
	: ':' function_ret_value_flag obj_type		{ GFileParser->SetReturnValue( $<m_context>3, $<m_typeName>3, $<m_flags>2 ); }
	| /* empty */
	;

function_ret_value_flag
	: TOKEN_CONST			{ $<m_flags>$ = SSF_Const; }
	| /* empty */			{ $<m_flags>$ = 0; } 
	;

function_inner_header
	: function_flags function_name
	{
		const ScriptAttributes& attributes = GFileContext->GetAttributes();
		GFileParser->StartFunction( $<m_context>2, $<m_string>2, $<m_flags>1 | $<m_flags>2 | GFileContext->GetGlobalFlags(), attributes );
		GFileContext->ClearAttributes();
	}
	| TOKEN_EVENT ident
	{
		const ScriptAttributes& attributes = GFileContext->GetAttributes();
		GFileParser->StartFunction( $<m_context>2, $<m_string>2, SSF_Event | SSF_Virtual | SSF_Protected, attributes );
		GFileContext->ClearAttributes();
	}
	;

function_name
    : TOKEN_FUNCTION ident 				{ $<m_string>$ = $<m_string>2; $<m_context>$ = $<m_context>2; }
	| TOKEN_OPERATOR function_op_name	{ $<m_string>$ = $<m_string>2; $<m_flags>$ = SSF_Operator; $<m_context>$ = $<m_context>1; }
	| TOKEN_CAST						{ $<m_string>$ = ("Cast"); $<m_flags>$ = SSF_Cast; $<m_context>$ = $<m_context>1; }
	| TOKEN_IMPLICIT TOKEN_CAST			{ $<m_string>$ = ("Cast"); $<m_flags>$ = SSF_Cast | SSF_Implicit; $<m_context>$ = $<m_context>1; }
	;

function_op_name   
	: TOKEN_OP_IADD				{ $<m_string>$ = red::StringView( ("OperatorAssignAdd" ) ); }
	| TOKEN_OP_ISUB				{ $<m_string>$ = red::StringView( ("OperatorAssignSubtract") ); }
	| TOKEN_OP_IMUL				{ $<m_string>$ = red::StringView( ("OperatorAssignMultiply") ); }
	| TOKEN_OP_IDIV				{ $<m_string>$ = red::StringView( ("OperatorAssignDivide") ); }
	| TOKEN_OP_IAND				{ $<m_string>$ = red::StringView( ("OperatorAssignAnd") ); }
	| TOKEN_OP_IOR				{ $<m_string>$ = red::StringView( ("OperatorAssignOr") ); }
	| TOKEN_OP_LOGIC_OR			{ $<m_string>$ = red::StringView( ("OperatorLogicOr") ); }
	| TOKEN_OP_LOGIC_AND		{ $<m_string>$ = red::StringView( ("OperatorLogicAnd") ); }
	| '|'						{ $<m_string>$ = red::StringView( ("OperatorOr") ); }
	| '&'						{ $<m_string>$ = red::StringView( ("OperatorAnd") ); }
	| '^'						{ $<m_string>$ = red::StringView( ("OperatorXor") ); }
	| TOKEN_OP_EQUAL   			{ $<m_string>$ = red::StringView( ("OperatorEqual") ); }
	| TOKEN_OP_NOTEQUAL			{ $<m_string>$ = red::StringView( ("OperatorNotEqual") ); }
	| '<'						{ $<m_string>$ = red::StringView( ("OperatorLess") ); }
	| '>'						{ $<m_string>$ = red::StringView( ("OperatorGreater") ); }
	| TOKEN_OP_GREQ				{ $<m_string>$ = red::StringView( ("OperatorGreaterEqual") ); }
	| TOKEN_OP_LEEQ				{ $<m_string>$ = red::StringView( ("OperatorLessEqual") ); }
	| '+'						{ $<m_string>$ = red::StringView( ("OperatorAdd") ); }
	| '*'						{ $<m_string>$ = red::StringView( ("OperatorMultiply") ); }
	| '/'						{ $<m_string>$ = red::StringView( ("OperatorDivide") ); }
	| '%'						{ $<m_string>$ = red::StringView( ("OperatorModulo") ); }
	| '!'						{ $<m_string>$ = red::StringView( ("OperatorLogicNot") ); }
	| '~'						{ $<m_string>$ = red::StringView( ("OperatorBitNot") ); }
	| '-'						{ $<m_string>$ = red::StringView( ("OperatorNeg") ); }
	| '[' ']'					{ $<m_string>$ = red::StringView( ("OperatorArray") ); }
	;	

function_params
	: function_param_list 
	| /* empty */
	;

function_param_list
	: function_param ',' function_param_list
	| function_param
	;

function_param
	: function_param_flag_list function_param_ident_list ':' obj_type
		{
			const YYSTYPE_File::Idents& idents = $<m_idents>2;
			const Uint32 identsCount = idents.Size();
			for ( Uint32 i = 0; i < identsCount; ++i )
			{
				GFileParser->AddProperty( idents[i].m_context, $<m_context>4, idents[i].m_value, $<m_flags>1, $<m_typeName>4, true );
			}
		}
	;

function_param_ident_list
	: ident ',' function_param_ident_list	{ $<m_idents>$.PushBack( YYSTYPE_File::Ident( $<m_string>1, $<m_context>1 ) ); $<m_idents>$.PushBack( $<m_idents>3 );	}
	| ident									{ $<m_idents>$.PushBack( YYSTYPE_File::Ident( $<m_string>1, $<m_context>1 ) ); }
	; 

function_param_flag_list
	: function_param_flag function_param_flag_list     { $<m_flags>$ = $<m_flags>1 | $<m_flags>2; }
	| /* empty */                                      { $<m_flags>$ = 0; } 
	;

function_param_flag
	: TOKEN_OUT                { $<m_flags>$ = SSF_Out; }
	| TOKEN_SKIP               { $<m_flags>$ = SSF_Skipable; }
	| TOKEN_OPTIONAL           { $<m_flags>$ = SSF_Optional; }
	| TOKEN_CONST			   { $<m_flags>$ = SSF_Const; }
	;

function_tail
	: '{' function_local_vars_dcl function_body '}'		{ GFileParser->EndDefinition( GFileContext->GetCurrentTokenLine(), $<m_context>1, $<m_context>4 ); GFileParser->PopContext(); }
	| semicolon											{ GFileParser->SetFunctionUndefined(); GFileParser->PopContext(); }
	;

function_body
	: /* empty */ { GFileContext->ExtractFunctionCode();  yyclearin; }
	;

function_local_vars_dcl
	: function_local_vars_header function_local_var_dcl ';' function_local_vars_dcl
	| function_local_vars_header function_local_var_single_dcl '=' function_local_var_initializer ';' function_local_vars_dcl
	| /* empty */
	;

function_local_vars_header
	: function_local_var_flag TOKEN_VAR	{ GFileParser->ResetFunctionPropertyList(); GFileParser->SetFunctionLocalParamFlags( $<m_flags>1 ); }
	;

function_local_var_flag
	: TOKEN_CONST			{ $<m_flags>$ = SSF_Const; }
	| /* empty */			{ $<m_flags>$ = 0; } 
	;

function_local_var_dcl
	: function_local_var_single_dcl ',' function_local_var_dcl
	| function_local_var_single_dcl
	;

function_local_var_single_dcl
	: function_param_ident_list ':' obj_type
	{
		const YYSTYPE_File::Idents& idents = $<m_idents>1;
		const Uint32 identsCount = idents.Size();
		for( Uint32 i = 0; i < identsCount; ++i )
		{
			GFileParser->AddProperty( idents[ i ].m_context, $<m_context>3, idents[ i ].m_value, GFileParser->GetFunctionLocalParamFlags(), $<m_typeName>3, false );
		}
	};

function_local_var_initializer
	: /* empty */ { CScriptTokenStream initCode; GFileContext->ExtractInitCode( initCode ); GFileParser->SetLastFunctionPropertyInitCode( initCode ); yyclearin; }
	;

////////////////////////////////
// TERMINALS
//////////////////////////////// 

obj_type
	: ref_type
	| full_type
	;

full_type
	: array_type		{ $<m_typeName>$ = $<m_typeName>1; }
	| static_array_type	{ $<m_typeName>$ = $<m_typeName>1; }
	| weak_type			{ $<m_typeName>$ = $<m_typeName>1; }
	| ident				{ $<m_typeName>$ = CScriptTypeDummy::MakeSimple( $<m_string>1 ); }
	;

ref_type
	: TOKEN_REF '<' full_type '>'	{ $<m_typeName>$ = CScriptTypeDummy::MakeReference( $<m_typeName>3 ); }
	;

array_type
	: TOKEN_ARRAY '<' full_type '>'	{ $<m_typeName>$ = CScriptTypeDummy::MakeDynamicArray( $<m_typeName>3 ); }
	;

static_array_type
	: full_type '[' integer ']'		{ $<m_typeName>$ = CScriptTypeDummy::MakeStaticArray( $<m_typeName>1, $<m_integer>3 ); }
	;

integer
	: TOKEN_INTEGER
	{
		if ( !red::StringToInt( $<m_integer>$, $<m_token>1.ToString().AsChar(), nullptr, red::BaseAuto ) )
		{
			yyerror( "Could not convert script token into integer" );
		}
		else
		{
			$<m_string>$ = $<m_token>1; $<m_context>$ = $<m_context>1;
		}
	}
	;

string
	: TOKEN_STRING		{ $<m_string>$ = $<m_token>1; $<m_context>$ = $<m_context>1; }
	;

weak_type
	: TOKEN_WEAK '<' full_type '>' { $<m_typeName>$ = CScriptTypeDummy::MakeWeakHandle( $<m_typeName>3 ); }
	;

ident
	: TOKEN_IDENT		{ $<m_string>$ = $<m_token>1; $<m_context>$ = $<m_context>1; }
	; 

semicolon
	: ';'
	;

optional_semicolon
	: ';'
	| /* empty */
	;

optional_comma
	: ','
	| /* empty */
	;