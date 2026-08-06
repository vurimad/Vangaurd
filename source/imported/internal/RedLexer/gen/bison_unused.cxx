
/* A Bison parser, made by GNU Bison 2.4.1.  */

/* Skeleton implementation for Bison's Yacc-like parsers in C
   
      Copyright (C) 1984, 1989, 1990, 2000, 2001, 2002, 2003, 2004, 2005, 2006
   Free Software Foundation, Inc.
   
   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.
   
   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* C LALR(1) parser skeleton written by Richard Stallman, by
   simplifying the original so-called "semantic" parser.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output.  */
#define YYBISON 1

/* Bison version.  */
#define YYBISON_VERSION "2.4.1"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 1

/* Push parsers.  */
#define YYPUSH 0

/* Pull parsers.  */
#define YYPULL 1

/* Using locations.  */
#define YYLSP_NEEDED 0



/* Copy the first part of user declarations.  */

/* Line 189 of yacc.c  */
#line 1 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"

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



/* Line 189 of yacc.c  */
#line 220 "D:\\Temp\\redvanguard_redlexer_codegen\\bison_unused.cxx"

/* Enabling traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif

/* Enabling verbose error messages.  */
#ifdef YYERROR_VERBOSE
# undef YYERROR_VERBOSE
# define YYERROR_VERBOSE 1
#else
# define YYERROR_VERBOSE 0
#endif

/* Enabling the token table.  */
#ifndef YYTOKEN_TABLE
# define YYTOKEN_TABLE 0
#endif


/* Tokens.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
   /* Put the tokens into the symbol table, so that GDB and other debuggers
      know about them.  */
   enum yytokentype {
     TOKEN_IDENT = 258,
     TOKEN_INTEGER = 259,
     TOKEN_FLOAT = 260,
     TOKEN_NAME = 261,
     TOKEN_STRING = 262,
     TOKEN_TDBID = 263,
     TOKEN_RESREF = 264,
     TOKEN_BOOL_TRUE = 265,
     TOKEN_BOOL_FALSE = 266,
     TOKEN_NULL = 267,
     TOKEN_IMPORT = 268,
     TOKEN_IMPORTONLY = 269,
     TOKEN_HOLY = 270,
     TOKEN_ABSTRACT = 271,
     TOKEN_CONST = 272,
     TOKEN_MUTABLE = 273,
     TOKEN_EXTENDS = 274,
     TOKEN_CLASS = 275,
     TOKEN_ENUM = 276,
     TOKEN_STRUCT = 277,
     TOKEN_FUNCTION = 278,
     TOKEN_DEF = 279,
     TOKEN_EDITABLE = 280,
     TOKEN_INSTANCE_EDITABLE = 281,
     TOKEN_REPLICATED = 282,
     TOKEN_PERSISTENT = 283,
     TOKEN_FINAL = 284,
     TOKEN_VIRTUAL = 285,
     TOKEN_OVERRIDE = 286,
     TOKEN_OUT = 287,
     TOKEN_OPTIONAL = 288,
     TOKEN_SKIP = 289,
     TOKEN_INLINED = 290,
     TOKEN_ARRAY = 291,
     TOKEN_HINT = 292,
     TOKEN_HINTS = 293,
     TOKEN_DEFAULT = 294,
     TOKEN_DEFAULTS = 295,
     TOKEN_BROWSABLE = 296,
     TOKEN_VAR = 297,
     TOKEN_EXEC = 298,
     TOKEN_TIMER = 299,
     TOKEN_WEAK = 300,
     TOKEN_OPERATOR = 301,
     TOKEN_CAST = 302,
     TOKEN_IMPLICIT = 303,
     TOKEN_STATIC = 304,
     TOKEN_VISUAL = 305,
     TOKEN_CORE = 306,
     TOKEN_LOGIC = 307,
     TOKEN_MULTICAST = 308,
     TOKEN_HOST = 309,
     TOKEN_CLIENT = 310,
     TOKEN_RELIABLE = 311,
     TOKEN_QUEST = 312,
     TOKEN_REF = 313,
     TOKEN_DEBUG = 314,
     TOKEN_SYNCHRONIZED = 315,
     TOKEN_SIMPLE_TYPE = 316,
     TOKEN_CLASS_TYPE = 317,
     TOKEN_ENUM_TYPE = 318,
     TOKEN_OP_IADD = 319,
     TOKEN_OP_ISUB = 320,
     TOKEN_OP_IMUL = 321,
     TOKEN_OP_IDIV = 322,
     TOKEN_OP_IAND = 323,
     TOKEN_OP_IOR = 324,
     TOKEN_OP_LOGIC_OR = 325,
     TOKEN_OP_LOGIC_AND = 326,
     TOKEN_OP_EQUAL = 327,
     TOKEN_OP_NOTEQUAL = 328,
     TOKEN_OP_GREQ = 329,
     TOKEN_OP_LEEQ = 330,
     TOKEN_NEW = 331,
     TOKEN_IF = 332,
     TOKEN_ELSE = 333,
     TOKEN_SWITCH = 334,
     TOKEN_CASE = 335,
     TOKEN_FOR = 336,
     TOKEN_WHILE = 337,
     TOKEN_DO = 338,
     TOKEN_RETURN = 339,
     TOKEN_BREAK = 340,
     TOKEN_CONTINUE = 341,
     TOKEN_THIS = 342,
     TOKEN_SUPER = 343,
     TOKEN_PRIVATE = 344,
     TOKEN_PROTECTED = 345,
     TOKEN_PUBLIC = 346,
     TOKEN_EVENT = 347,
     TOKEN_TESTONLY = 348
   };
#endif



#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED

# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
#endif


/* Copy the second part of user declarations.  */


/* Line 264 of yacc.c  */
#line 354 "D:\\Temp\\redvanguard_redlexer_codegen\\bison_unused.cxx"

#ifdef short
# undef short
#endif

#ifdef YYTYPE_UINT8
typedef YYTYPE_UINT8 yytype_uint8;
#else
typedef unsigned char yytype_uint8;
#endif

#ifdef YYTYPE_INT8
typedef YYTYPE_INT8 yytype_int8;
#elif (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
typedef signed char yytype_int8;
#else
typedef short int yytype_int8;
#endif

#ifdef YYTYPE_UINT16
typedef YYTYPE_UINT16 yytype_uint16;
#else
typedef unsigned short int yytype_uint16;
#endif

#ifdef YYTYPE_INT16
typedef YYTYPE_INT16 yytype_int16;
#else
typedef short int yytype_int16;
#endif

#ifndef YYSIZE_T
# ifdef __SIZE_TYPE__
#  define YYSIZE_T __SIZE_TYPE__
# elif defined size_t
#  define YYSIZE_T size_t
# elif ! defined YYSIZE_T && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# else
#  define YYSIZE_T unsigned int
# endif
#endif

#define YYSIZE_MAXIMUM ((YYSIZE_T) -1)

#ifndef YY_
# if YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> /* INFRINGES ON USER NAME SPACE */
#   define YY_(msgid) dgettext ("bison-runtime", msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(msgid) msgid
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YYUSE(e) ((void) (e))
#else
# define YYUSE(e) /* empty */
#endif

/* Identity function, used to suppress warnings about constant conditions.  */
#ifndef lint
# define YYID(n) (n)
#else
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static int
YYID (int yyi)
#else
static int
YYID (yyi)
    int yyi;
#endif
{
  return yyi;
}
#endif

#if ! defined yyoverflow || YYERROR_VERBOSE

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# ifdef YYSTACK_USE_ALLOCA
#  if YYSTACK_USE_ALLOCA
#   ifdef __GNUC__
#    define YYSTACK_ALLOC __builtin_alloca
#   elif defined __BUILTIN_VA_ARG_INCR
#    include <alloca.h> /* INFRINGES ON USER NAME SPACE */
#   elif defined _AIX
#    define YYSTACK_ALLOC __alloca
#   elif defined _MSC_VER
#    include <malloc.h> /* INFRINGES ON USER NAME SPACE */
#    define alloca _alloca
#   else
#    define YYSTACK_ALLOC alloca
#    if ! defined _ALLOCA_H && ! defined _STDLIB_H && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#     ifndef _STDLIB_H
#      define _STDLIB_H 1
#     endif
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's `empty if-body' warning.  */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (YYID (0))
#  ifndef YYSTACK_ALLOC_MAXIMUM
    /* The OS might guarantee only one guard page at the bottom of the stack,
       and a page size can be as small as 4096 bytes.  So we cannot safely
       invoke alloca (N) if N exceeds 4096.  Use a slightly smaller number
       to allow for a few compiler-allocated temporary stack slots.  */
#   define YYSTACK_ALLOC_MAXIMUM 4032 /* reasonable circa 2006 */
#  endif
# else
#  define YYSTACK_ALLOC YYMALLOC
#  define YYSTACK_FREE YYFREE
#  ifndef YYSTACK_ALLOC_MAXIMUM
#   define YYSTACK_ALLOC_MAXIMUM YYSIZE_MAXIMUM
#  endif
#  if (defined __cplusplus && ! defined _STDLIB_H \
       && ! ((defined YYMALLOC || defined malloc) \
	     && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef _STDLIB_H
#    define _STDLIB_H 1
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined _STDLIB_H && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined _STDLIB_H && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
void free (void *); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
# endif
#endif /* ! defined yyoverflow || YYERROR_VERBOSE */


#if (! defined yyoverflow \
     && (! defined __cplusplus \
	 || (defined YYSTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  yytype_int16 yyss_alloc;
  YYSTYPE yyvs_alloc;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (sizeof (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (sizeof (yytype_int16) + sizeof (YYSTYPE)) \
      + YYSTACK_GAP_MAXIMUM)

/* Copy COUNT objects from FROM to TO.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(To, From, Count) \
      __builtin_memcpy (To, From, (Count) * sizeof (*(From)))
#  else
#   define YYCOPY(To, From, Count)		\
      do					\
	{					\
	  YYSIZE_T yyi;				\
	  for (yyi = 0; yyi < (Count); yyi++)	\
	    (To)[yyi] = (From)[yyi];		\
	}					\
      while (YYID (0))
#  endif
# endif

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack_alloc, Stack)				\
    do									\
      {									\
	YYSIZE_T yynewbytes;						\
	YYCOPY (&yyptr->Stack_alloc, Stack, yysize);			\
	Stack = &yyptr->Stack_alloc;					\
	yynewbytes = yystacksize * sizeof (*Stack) + YYSTACK_GAP_MAXIMUM; \
	yyptr += yynewbytes / sizeof (*yyptr);				\
      }									\
    while (YYID (0))

#endif

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  3
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   469

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  117
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  81
/* YYNRULES -- Number of rules.  */
#define YYNRULES  210
/* YYNRULES -- Number of states.  */
#define YYNSTATES  343

/* YYTRANSLATE(YYLEX) -- Bison symbol number corresponding to YYLEX.  */
#define YYUNDEFTOK  2
#define YYMAXUTOK   348

#define YYTRANSLATE(YYX)						\
  ((unsigned int) (YYX) <= YYMAXUTOK ? yytranslate[YYX] : YYUNDEFTOK)

/* YYTRANSLATE[YYLEX] -- Bison symbol number corresponding to YYLEX.  */
static const yytype_uint8 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,   114,     2,     2,     2,   113,   106,     2,
     103,   104,   111,   110,   100,   101,   102,   112,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,    99,   116,
     108,    95,   109,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,    94,     2,    96,   107,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,    97,   105,    98,   115,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    61,    62,    63,    64,
      65,    66,    67,    68,    69,    70,    71,    72,    73,    74,
      75,    76,    77,    78,    79,    80,    81,    82,    83,    84,
      85,    86,    87,    88,    89,    90,    91,    92,    93
};

#if YYDEBUG
/* YYPRHS[YYN] -- Index of the first RHS symbol of rule number YYN in
   YYRHS.  */
static const yytype_uint16 yyprhs[] =
{
       0,     0,     3,     5,     8,     9,    13,    17,    21,    23,
      25,    27,    29,    31,    33,    36,    38,    40,    42,    45,
      48,    51,    54,    57,    60,    61,    64,    67,    68,    71,
      74,    77,    80,    83,    86,    89,    92,    95,    98,   101,
     104,   107,   108,   111,   114,   117,   120,   123,   126,   129,
     132,   133,   135,   136,   138,   140,   142,   144,   146,   147,
     154,   160,   165,   169,   174,   179,   182,   183,   185,   187,
     189,   191,   194,   198,   201,   206,   210,   213,   214,   220,
     225,   230,   233,   234,   236,   238,   240,   242,   245,   249,
     252,   257,   261,   264,   265,   271,   274,   279,   283,   285,
     287,   291,   296,   301,   304,   307,   308,   313,   318,   323,
     326,   329,   330,   335,   340,   343,   346,   347,   352,   358,
     365,   367,   369,   371,   373,   375,   377,   379,   381,   383,
     385,   387,   394,   398,   399,   401,   402,   405,   408,   411,
     414,   416,   419,   421,   423,   425,   427,   429,   431,   433,
     435,   437,   439,   441,   443,   445,   447,   449,   451,   453,
     455,   457,   459,   461,   463,   465,   467,   470,   472,   473,
     477,   479,   484,   488,   490,   493,   494,   496,   498,   500,
     502,   507,   509,   510,   515,   522,   523,   526,   528,   529,
     533,   535,   539,   540,   542,   544,   546,   548,   550,   552,
     557,   562,   567,   569,   571,   576,   578,   580,   582,   583,
     585
};

/* YYRHS -- A `-1'-separated list of the rules' RHS.  */
static const yytype_int16 yyrhs[] =
{
     118,     0,    -1,   119,    -1,   119,   120,    -1,    -1,   121,
     124,   196,    -1,   122,   125,   196,    -1,   123,   125,   196,
      -1,   130,    -1,   131,    -1,   132,    -1,   143,    -1,   150,
      -1,   135,    -1,   134,   165,    -1,   165,    -1,   143,    -1,
     135,    -1,    16,   126,    -1,    89,   126,    -1,    90,   126,
      -1,    91,   126,    -1,    29,   126,    -1,    93,   126,    -1,
      -1,    29,   127,    -1,    93,   127,    -1,    -1,    43,   128,
      -1,    44,   128,    -1,    49,   128,    -1,    53,   128,    -1,
      54,   128,    -1,    55,   128,    -1,    56,   128,    -1,    30,
     128,    -1,    31,   128,    -1,    17,   128,    -1,    57,   128,
      -1,    29,   128,    -1,    93,   128,    -1,    -1,    17,   129,
      -1,    18,   129,    -1,    25,   129,    -1,    26,   129,    -1,
      27,   129,    -1,    35,   129,    -1,    28,   129,    -1,    93,
     129,    -1,    -1,    13,    -1,    -1,    14,    -1,    15,    -1,
      89,    -1,    90,    -1,    91,    -1,    -1,    94,   194,    95,
     163,    96,   134,    -1,    94,   194,    95,   163,    96,    -1,
      94,   194,    96,   134,    -1,    94,   194,    96,    -1,   136,
      97,   137,    98,    -1,   126,    20,   194,   141,    -1,   137,
     138,    -1,    -1,   140,    -1,   154,    -1,   157,    -1,   160,
      -1,   133,   130,    -1,   134,   139,   165,    -1,   139,   165,
      -1,   134,   139,   142,   195,    -1,   139,   142,   195,    -1,
      19,   194,    -1,    -1,   129,    42,   175,    99,   187,    -1,
     144,    97,   145,    98,    -1,   127,    22,   194,   149,    -1,
     145,   146,    -1,    -1,   148,    -1,   154,    -1,   157,    -1,
     160,    -1,   133,   130,    -1,   134,   147,   165,    -1,   147,
     165,    -1,   134,   147,   142,   195,    -1,   147,   142,   195,
      -1,    19,   194,    -1,    -1,   151,    97,   152,   197,    98,
      -1,    21,   194,    -1,    21,   194,    99,   194,    -1,   152,
     100,   153,    -1,   153,    -1,   194,    -1,   194,    95,   191,
      -1,   194,    95,   101,   191,    -1,    41,    97,   155,    98,
      -1,    41,   156,    -1,   156,   155,    -1,    -1,   194,    95,
      10,   195,    -1,   194,    95,    11,   195,    -1,    38,    97,
     158,    98,    -1,    37,   159,    -1,   159,   158,    -1,    -1,
     194,    95,   192,   195,    -1,    40,    97,   161,    98,    -1,
      39,   162,    -1,   162,   161,    -1,    -1,   194,    95,   163,
     195,    -1,   194,    95,   101,   164,   195,    -1,   194,    95,
       3,   102,     3,   195,    -1,     4,    -1,     5,    -1,     7,
      -1,     6,    -1,     8,    -1,     9,    -1,    10,    -1,    11,
      -1,     4,    -1,     5,    -1,   166,    -1,   169,   103,   172,
     104,   167,   178,    -1,    99,   168,   186,    -1,    -1,    17,
      -1,    -1,   128,   170,    -1,    92,   194,    -1,    23,   194,
      -1,    46,   171,    -1,    47,    -1,    48,    47,    -1,    64,
      -1,    65,    -1,    66,    -1,    67,    -1,    68,    -1,    69,
      -1,    70,    -1,    71,    -1,   105,    -1,   106,    -1,   107,
      -1,    72,    -1,    73,    -1,   108,    -1,   109,    -1,    74,
      -1,    75,    -1,   110,    -1,   111,    -1,   112,    -1,   113,
      -1,   114,    -1,   115,    -1,   101,    -1,    94,    96,    -1,
     173,    -1,    -1,   174,   100,   173,    -1,   174,    -1,   176,
     175,    99,   186,    -1,   194,   100,   175,    -1,   194,    -1,
     177,   176,    -1,    -1,    32,    -1,    34,    -1,    33,    -1,
      17,    -1,    97,   180,   179,    98,    -1,   195,    -1,    -1,
     181,   183,   116,   180,    -1,   181,   184,    95,   185,   116,
     180,    -1,    -1,   182,    42,    -1,    17,    -1,    -1,   184,
     100,   183,    -1,   184,    -1,   175,    99,   186,    -1,    -1,
     188,    -1,   187,    -1,   189,    -1,   190,    -1,   193,    -1,
     194,    -1,    58,   108,   187,   109,    -1,    36,   108,   187,
     109,    -1,   187,    94,   191,    96,    -1,     4,    -1,     7,
      -1,    45,   108,   187,   109,    -1,     3,    -1,   116,    -1,
     116,    -1,    -1,   100,    -1,    -1
};

/* YYRLINE[YYN] -- source line where rule number YYN was defined.  */
static const yytype_uint16 yyrline[] =
{
       0,   267,   267,   275,   276,   280,   281,   282,   286,   290,
     294,   298,   299,   300,   301,   302,   306,   307,   315,   316,
     317,   318,   319,   320,   321,   325,   326,   327,   331,   332,
     333,   334,   335,   336,   337,   338,   339,   340,   341,   342,
     343,   344,   348,   349,   350,   351,   352,   353,   354,   355,
     356,   364,   365,   369,   373,   377,   378,   379,   380,   384,
     388,   392,   396,   407,   411,   415,   416,   420,   421,   422,
     423,   427,   431,   432,   433,   434,   438,   439,   447,   470,
     474,   478,   479,   483,   484,   485,   486,   490,   494,   495,
     496,   497,   501,   502,   511,   515,   516,   524,   525,   529,
     530,   531,   538,   539,   543,   544,   548,   549,   557,   558,
     562,   563,   567,   575,   576,   580,   581,   585,   586,   587,
     596,   597,   598,   599,   600,   601,   602,   603,   607,   608,
     616,   620,   624,   625,   629,   630,   634,   640,   649,   650,
     651,   652,   656,   657,   658,   659,   660,   661,   662,   663,
     664,   665,   666,   667,   668,   669,   670,   671,   672,   673,
     674,   675,   676,   677,   678,   679,   680,   684,   685,   689,
     690,   694,   706,   707,   711,   712,   716,   717,   718,   719,
     723,   724,   728,   732,   733,   734,   738,   742,   743,   747,
     748,   752,   763,   771,   772,   776,   777,   778,   779,   783,
     787,   791,   795,   809,   813,   817,   821,   825,   826,   830,
     831
};
#endif

#if YYDEBUG || YYERROR_VERBOSE || YYTOKEN_TABLE
/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "$end", "error", "$undefined", "TOKEN_IDENT", "TOKEN_INTEGER",
  "TOKEN_FLOAT", "TOKEN_NAME", "TOKEN_STRING", "TOKEN_TDBID",
  "TOKEN_RESREF", "TOKEN_BOOL_TRUE", "TOKEN_BOOL_FALSE", "TOKEN_NULL",
  "TOKEN_IMPORT", "TOKEN_IMPORTONLY", "TOKEN_HOLY", "TOKEN_ABSTRACT",
  "TOKEN_CONST", "TOKEN_MUTABLE", "TOKEN_EXTENDS", "TOKEN_CLASS",
  "TOKEN_ENUM", "TOKEN_STRUCT", "TOKEN_FUNCTION", "TOKEN_DEF",
  "TOKEN_EDITABLE", "TOKEN_INSTANCE_EDITABLE", "TOKEN_REPLICATED",
  "TOKEN_PERSISTENT", "TOKEN_FINAL", "TOKEN_VIRTUAL", "TOKEN_OVERRIDE",
  "TOKEN_OUT", "TOKEN_OPTIONAL", "TOKEN_SKIP", "TOKEN_INLINED",
  "TOKEN_ARRAY", "TOKEN_HINT", "TOKEN_HINTS", "TOKEN_DEFAULT",
  "TOKEN_DEFAULTS", "TOKEN_BROWSABLE", "TOKEN_VAR", "TOKEN_EXEC",
  "TOKEN_TIMER", "TOKEN_WEAK", "TOKEN_OPERATOR", "TOKEN_CAST",
  "TOKEN_IMPLICIT", "TOKEN_STATIC", "TOKEN_VISUAL", "TOKEN_CORE",
  "TOKEN_LOGIC", "TOKEN_MULTICAST", "TOKEN_HOST", "TOKEN_CLIENT",
  "TOKEN_RELIABLE", "TOKEN_QUEST", "TOKEN_REF", "TOKEN_DEBUG",
  "TOKEN_SYNCHRONIZED", "TOKEN_SIMPLE_TYPE", "TOKEN_CLASS_TYPE",
  "TOKEN_ENUM_TYPE", "TOKEN_OP_IADD", "TOKEN_OP_ISUB", "TOKEN_OP_IMUL",
  "TOKEN_OP_IDIV", "TOKEN_OP_IAND", "TOKEN_OP_IOR", "TOKEN_OP_LOGIC_OR",
  "TOKEN_OP_LOGIC_AND", "TOKEN_OP_EQUAL", "TOKEN_OP_NOTEQUAL",
  "TOKEN_OP_GREQ", "TOKEN_OP_LEEQ", "TOKEN_NEW", "TOKEN_IF", "TOKEN_ELSE",
  "TOKEN_SWITCH", "TOKEN_CASE", "TOKEN_FOR", "TOKEN_WHILE", "TOKEN_DO",
  "TOKEN_RETURN", "TOKEN_BREAK", "TOKEN_CONTINUE", "TOKEN_THIS",
  "TOKEN_SUPER", "TOKEN_PRIVATE", "TOKEN_PROTECTED", "TOKEN_PUBLIC",
  "TOKEN_EVENT", "TOKEN_TESTONLY", "'['", "'='", "']'", "'{'", "'}'",
  "':'", "','", "'-'", "'.'", "'('", "')'", "'|'", "'&'", "'^'", "'<'",
  "'>'", "'+'", "'*'", "'/'", "'%'", "'!'", "'~'", "';'", "$accept",
  "program_file", "file_declaration_list", "file_declaration",
  "file_import_flag", "file_importonly_flag", "file_holy_flag",
  "file_elem_declaration", "file_compound_type_declaration", "class_flags",
  "struct_flags", "function_flags", "var_flags", "import_flag",
  "importonly_flag", "holy_flag", "access_flag", "attributes", "class_dcl",
  "class_header", "class_elem_list", "class_element",
  "class_access_and_import_flags", "class_element_inner", "class_extends",
  "var_dcl", "struct_dcl", "struct_info", "struct_elem_list",
  "struct_element", "struct_access_and_import_flags",
  "struct_element_inner", "struct_extends", "enum_dcl", "enum_info",
  "enum_name_list", "enum_name", "browsable_dcl", "browsable_list",
  "browsable_val", "hints_dcl", "hints_list", "hint_val", "defaults_dcl",
  "defaults_val_list", "default_val", "default_val_const",
  "default_arithmetic_val_const", "function_dcl", "function_header",
  "function_ret_value", "function_ret_value_flag", "function_inner_header",
  "function_name", "function_op_name", "function_params",
  "function_param_list", "function_param", "function_param_ident_list",
  "function_param_flag_list", "function_param_flag", "function_tail",
  "function_body", "function_local_vars_dcl", "function_local_vars_header",
  "function_local_var_flag", "function_local_var_dcl",
  "function_local_var_single_dcl", "function_local_var_initializer",
  "obj_type", "full_type", "ref_type", "array_type", "static_array_type",
  "integer", "string", "weak_type", "ident", "semicolon",
  "optional_semicolon", "optional_comma", 0
};
#endif

# ifdef YYPRINT
/* YYTOKNUM[YYLEX-NUM] -- Internal token number corresponding to
   token YYLEX-NUM.  */
static const yytype_uint16 yytoknum[] =
{
       0,   256,   257,   258,   259,   260,   261,   262,   263,   264,
     265,   266,   267,   268,   269,   270,   271,   272,   273,   274,
     275,   276,   277,   278,   279,   280,   281,   282,   283,   284,
     285,   286,   287,   288,   289,   290,   291,   292,   293,   294,
     295,   296,   297,   298,   299,   300,   301,   302,   303,   304,
     305,   306,   307,   308,   309,   310,   311,   312,   313,   314,
     315,   316,   317,   318,   319,   320,   321,   322,   323,   324,
     325,   326,   327,   328,   329,   330,   331,   332,   333,   334,
     335,   336,   337,   338,   339,   340,   341,   342,   343,   344,
     345,   346,   347,   348,    91,    61,    93,   123,   125,    58,
      44,    45,    46,    40,    41,   124,    38,    94,    60,    62,
      43,    42,    47,    37,    33,   126,    59
};
# endif

/* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const yytype_uint8 yyr1[] =
{
       0,   117,   118,   119,   119,   120,   120,   120,   121,   122,
     123,   124,   124,   124,   124,   124,   125,   125,   126,   126,
     126,   126,   126,   126,   126,   127,   127,   127,   128,   128,
     128,   128,   128,   128,   128,   128,   128,   128,   128,   128,
     128,   128,   129,   129,   129,   129,   129,   129,   129,   129,
     129,   130,   130,   131,   132,   133,   133,   133,   133,   134,
     134,   134,   134,   135,   136,   137,   137,   138,   138,   138,
     138,   139,   140,   140,   140,   140,   141,   141,   142,   143,
     144,   145,   145,   146,   146,   146,   146,   147,   148,   148,
     148,   148,   149,   149,   150,   151,   151,   152,   152,   153,
     153,   153,   154,   154,   155,   155,   156,   156,   157,   157,
     158,   158,   159,   160,   160,   161,   161,   162,   162,   162,
     163,   163,   163,   163,   163,   163,   163,   163,   164,   164,
     165,   166,   167,   167,   168,   168,   169,   169,   170,   170,
     170,   170,   171,   171,   171,   171,   171,   171,   171,   171,
     171,   171,   171,   171,   171,   171,   171,   171,   171,   171,
     171,   171,   171,   171,   171,   171,   171,   172,   172,   173,
     173,   174,   175,   175,   176,   176,   177,   177,   177,   177,
     178,   178,   179,   180,   180,   180,   181,   182,   182,   183,
     183,   184,   185,   186,   186,   187,   187,   187,   187,   188,
     189,   190,   191,   192,   193,   194,   195,   196,   196,   197,
     197
};

/* YYR2[YYN] -- Number of symbols composing right hand side of rule YYN.  */
static const yytype_uint8 yyr2[] =
{
       0,     2,     1,     2,     0,     3,     3,     3,     1,     1,
       1,     1,     1,     1,     2,     1,     1,     1,     2,     2,
       2,     2,     2,     2,     0,     2,     2,     0,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     0,     2,     2,     2,     2,     2,     2,     2,     2,
       0,     1,     0,     1,     1,     1,     1,     1,     0,     6,
       5,     4,     3,     4,     4,     2,     0,     1,     1,     1,
       1,     2,     3,     2,     4,     3,     2,     0,     5,     4,
       4,     2,     0,     1,     1,     1,     1,     2,     3,     2,
       4,     3,     2,     0,     5,     2,     4,     3,     1,     1,
       3,     4,     4,     2,     2,     0,     4,     4,     4,     2,
       2,     0,     4,     4,     2,     2,     0,     4,     5,     6,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     6,     3,     0,     1,     0,     2,     2,     2,     2,
       1,     2,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     2,     1,     0,     3,
       1,     4,     3,     1,     2,     0,     1,     1,     1,     1,
       4,     1,     0,     4,     6,     0,     2,     1,     0,     3,
       1,     3,     0,     1,     1,     1,     1,     1,     1,     4,
       4,     4,     1,     1,     4,     1,     1,     1,     0,     1,
       0
};

/* YYDEFACT[STATE-NAME] -- Default rule to reduce with in state
   STATE-NUM when YYTABLE doesn't specify something else to do.  Zero
   means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
       4,     0,    52,     1,    51,    53,    54,     3,    41,    24,
      24,     8,     9,    10,    24,    41,     0,    41,    41,    41,
      41,    41,    41,    41,    41,    41,    41,    41,    24,    24,
      24,     0,    41,     0,   208,     0,     0,     0,    41,    13,
       0,    11,     0,    12,     0,    15,   130,     0,    24,    24,
     208,    17,    16,   208,    24,    24,    18,    41,    41,    37,
     205,    95,    22,    25,    39,    35,    36,    28,    29,    30,
      31,    32,    33,    34,    38,    19,    20,    21,   137,    23,
      26,    40,     0,   207,     5,     0,     0,     0,     0,   140,
       0,   136,    14,    66,    82,     0,   168,     6,     7,     0,
       0,    62,    77,    93,   138,   142,   143,   144,   145,   146,
     147,   148,   149,   153,   154,   157,   158,     0,   165,   150,
     151,   152,   155,   156,   159,   160,   161,   162,   163,   164,
     139,   141,    58,    58,   210,    98,    99,   179,   176,   178,
     177,     0,   167,   170,     0,   175,    96,   120,   121,   123,
     122,   124,   125,   126,   127,     0,    61,     0,    64,     0,
      80,   166,     0,     0,     0,     0,     0,    55,    56,    57,
      63,    52,    58,    65,    41,    67,    68,    69,    70,    79,
      52,    58,    81,    41,    83,    84,    85,    86,   209,     0,
       0,   133,   175,     0,   173,   174,    60,    76,    92,   109,
       0,   111,   114,     0,   116,   105,   103,     0,    71,    41,
      41,    50,    50,    50,    50,    50,    50,    41,     0,     0,
      73,    87,    41,     0,    89,    97,    94,   202,     0,   100,
     135,     0,   169,     0,     0,    59,     0,     0,   111,     0,
       0,   116,     0,   105,     0,     0,    72,    42,    50,    50,
      43,    44,    45,    46,    48,    47,    49,     0,   206,    75,
       0,    88,    91,   101,   134,     0,   185,   131,   181,     0,
       0,     0,   171,   194,   193,   195,   196,   197,   198,   172,
     203,     0,   108,   110,     0,     0,     0,   113,   115,   102,
     104,     0,     0,    74,     0,    90,   132,   187,   182,     0,
       0,     0,     0,     0,     0,   112,     0,   128,   129,     0,
     117,   106,   107,     0,     0,     0,     0,   190,   186,     0,
       0,     0,     0,     0,   118,    78,   180,     0,   185,   192,
       0,   200,   204,   199,   201,   119,   191,   183,     0,   189,
     190,   185,   184
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
      -1,     1,     2,     7,     8,     9,    10,    34,    50,    35,
      36,    37,   218,    11,    12,    13,   171,    38,    51,    40,
     132,   173,   174,   175,   158,   219,    52,    42,   133,   182,
     183,   184,   160,    43,    44,   134,   135,   176,   242,   243,
     177,   237,   238,   178,   240,   241,   155,   309,    45,    46,
     231,   265,    47,    91,   130,   141,   142,   143,   315,   144,
     145,   267,   314,   298,   299,   300,   316,   317,   338,   272,
     273,   274,   275,   276,   229,   281,   277,   278,   259,    84,
     189
};

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
#define YYPACT_NINF -284
static const yytype_int16 yypact[] =
{
    -284,    48,   168,  -284,  -284,  -284,  -284,  -284,   177,    31,
      31,  -284,  -284,  -284,     8,   122,    47,   231,   122,   122,
     122,   122,   122,   122,   122,   122,   122,   122,     8,     8,
       8,    47,   231,    47,   -61,    56,   103,    15,    37,  -284,
      34,  -284,    52,  -284,    57,  -284,  -284,    53,    31,    31,
     -61,  -284,  -284,   -61,     8,     8,  -284,   122,   122,  -284,
    -284,    41,  -284,  -284,  -284,  -284,  -284,  -284,  -284,  -284,
    -284,  -284,  -284,  -284,  -284,  -284,  -284,  -284,  -284,  -284,
    -284,  -284,     0,  -284,  -284,    47,    47,    47,   304,  -284,
     110,  -284,  -284,  -284,  -284,    47,   211,  -284,  -284,    47,
     376,    67,   143,   145,  -284,  -284,  -284,  -284,  -284,  -284,
    -284,  -284,  -284,  -284,  -284,  -284,  -284,    73,  -284,  -284,
    -284,  -284,  -284,  -284,  -284,  -284,  -284,  -284,  -284,  -284,
    -284,  -284,    69,   217,    70,  -284,    79,  -284,  -284,  -284,
    -284,    76,  -284,    84,    47,   206,  -284,  -284,  -284,  -284,
    -284,  -284,  -284,  -284,  -284,    91,  -284,    47,  -284,    47,
    -284,  -284,    47,    98,    47,   107,    19,  -284,  -284,  -284,
    -284,   187,   101,  -284,   274,  -284,  -284,  -284,  -284,  -284,
     187,   101,  -284,   274,  -284,  -284,  -284,  -284,    47,   115,
      12,   106,   206,   117,   124,  -284,    67,  -284,  -284,  -284,
     134,    47,  -284,   141,    47,    47,  -284,   142,  -284,   274,
     308,   184,   184,   184,   184,   184,   184,   308,   175,   119,
    -284,  -284,   274,   119,  -284,  -284,  -284,  -284,   238,  -284,
     229,   -57,  -284,    97,    47,  -284,   242,   152,    47,    25,
     154,    47,   161,    47,   126,   119,  -284,  -284,   184,   184,
    -284,  -284,  -284,  -284,  -284,  -284,  -284,    47,  -284,  -284,
     119,  -284,  -284,  -284,  -284,    97,     4,  -284,  -284,   155,
     156,   157,  -284,   178,  -284,  -284,  -284,  -284,  -284,  -284,
    -284,   119,  -284,  -284,   171,   140,   119,  -284,  -284,  -284,
    -284,   119,   119,  -284,   179,  -284,  -284,  -284,  -284,    47,
     234,    39,    39,    39,   238,  -284,   276,  -284,  -284,   119,
    -284,  -284,  -284,    39,   183,   190,   166,    17,  -284,   -45,
     -37,   -20,   194,   119,  -284,   178,  -284,    97,     4,  -284,
      47,  -284,  -284,  -284,  -284,  -284,  -284,  -284,   180,  -284,
     193,     4,  -284
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -284,  -284,  -284,  -284,  -284,  -284,  -284,  -284,   284,   374,
      86,   -14,   220,   -48,  -284,  -284,  -108,   -81,   287,  -284,
    -284,  -284,   125,  -284,  -284,  -144,   290,  -284,  -284,  -284,
     129,  -284,  -284,  -284,  -284,  -284,   131,   199,    99,   146,
     207,    75,   185,   208,   108,   189,   109,  -284,   -36,  -284,
    -284,  -284,  -284,  -284,  -284,  -284,   162,  -284,  -130,   210,
    -284,  -284,  -284,  -283,  -284,  -284,    28,    29,  -284,  -242,
      43,  -284,  -284,  -284,  -202,  -284,  -284,   -16,  -204,    61,
    -284
};

/* YYTABLE[YYPACT[STATE-NUM]].  What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule which
   number is the opposite.  If zero, do what YYDEFACT says.
   If YYTABLE_NINF, syntax error.  */
#define YYTABLE_NINF -189
static const yytype_int16 yytable[] =
{
      61,    59,    92,    64,    65,    66,    67,    68,    69,    70,
      71,    72,    73,    74,   193,    78,   227,    82,    81,   262,
     156,   297,    60,   296,    14,   180,   263,   268,   284,   147,
     148,   149,   150,   151,   152,   153,   154,    54,    87,   223,
     266,   293,    60,    64,    81,   337,  -188,    14,     3,   304,
      60,   172,   181,   -27,    15,    83,   295,   304,   342,   258,
      48,    88,    89,    90,   331,   245,    57,    18,    19,   102,
     103,   104,   332,   180,   304,   269,    85,   305,   260,   136,
      20,    21,   310,   146,   270,   336,    22,   311,   312,   333,
      23,    24,    25,    26,    27,   100,   101,    28,    29,    30,
      60,    55,   322,    63,   279,   324,   162,   163,   164,   165,
     166,    97,   329,   228,    98,   235,   205,   330,    80,   335,
      28,    29,    30,   208,    49,    86,   285,   294,   194,    31,
      58,    93,   221,   269,    63,    80,   291,   292,   220,    15,
      99,   197,   270,   198,   307,   308,   200,   224,   203,    94,
     207,    57,    18,    19,    95,   271,    96,   131,   167,   168,
     169,    33,   157,    33,   159,    20,    21,   170,    -2,   161,
     188,    22,   136,   246,   190,    23,    24,    25,    26,    27,
     191,     4,     5,     6,   192,   200,   261,   196,   203,   207,
     167,   168,   169,    14,    15,   201,    59,   -24,    16,   -27,
       4,   248,   211,    81,   204,   230,    17,    18,    19,   212,
     213,   214,   215,   226,  -175,    58,   233,   257,   194,   216,
      20,    21,   200,   137,   234,   203,    22,   207,   137,   236,
      23,    24,    25,    26,    27,   258,   239,   244,   138,   139,
     140,   194,   227,   138,   139,   140,   264,    14,    15,   280,
     282,   -24,   287,   -27,   162,   163,   164,   165,   166,   289,
      17,    18,    19,   301,   302,   303,    28,    29,    30,    31,
      32,    33,   304,   306,    20,    21,   318,   249,   313,   323,
      22,   326,   328,   194,    23,    24,    25,    26,    27,   327,
     334,   210,   211,   330,    53,    39,   341,   209,    41,   212,
     213,   214,   215,    57,    18,    19,   167,   168,   169,   216,
     222,    33,   206,   283,   194,   179,   -50,    20,    21,   225,
      28,    29,    30,    22,    32,   210,   211,    23,    24,    25,
      26,    27,   185,   212,   213,   214,   215,    57,    18,    19,
     186,   187,   290,   216,   319,   320,   321,   199,   286,   288,
     -50,    20,    21,   202,   232,   195,   325,    22,   339,   340,
       0,    23,    24,    25,    26,    27,    31,   217,   105,   106,
     107,   108,   109,   110,   111,   112,   113,   114,   115,   116,
     147,   148,   149,   150,   151,   152,   153,   154,    56,     0,
       0,    62,     0,     0,     0,     0,     0,     0,   117,     0,
       0,   217,    75,    76,    77,   118,    79,     0,     0,   119,
     120,   121,   122,   123,   124,   125,   126,   127,   128,   129,
       0,     0,    62,    79,     0,     0,     0,     0,    62,    79,
     247,   250,   251,   252,   253,   254,   255,   256,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,   247,   256
};

static const yytype_int16 yycheck[] =
{
      16,    15,    38,    17,    18,    19,    20,    21,    22,    23,
      24,    25,    26,    27,   144,    31,     4,    33,    32,   223,
     101,    17,     3,   265,    16,   133,   228,   231,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    29,    23,   183,
      97,   245,     3,    57,    58,   328,    42,    16,     0,    94,
       3,   132,   133,    22,    17,   116,   260,    94,   341,   116,
      29,    46,    47,    48,   109,   209,    29,    30,    31,    85,
      86,    87,   109,   181,    94,    36,    20,   281,   222,    95,
      43,    44,   286,    99,    45,   327,    49,   291,   292,   109,
      53,    54,    55,    56,    57,    95,    96,    89,    90,    91,
       3,    93,   304,    17,   234,   309,    37,    38,    39,    40,
      41,    50,    95,   101,    53,   196,    97,   100,    32,   323,
      89,    90,    91,   171,    93,    22,   101,   257,   144,    92,
      93,    97,   180,    36,    48,    49,    10,    11,   174,    17,
      99,   157,    45,   159,     4,     5,   162,   183,   164,    97,
     166,    29,    30,    31,    97,    58,   103,    47,    89,    90,
      91,    94,    19,    94,    19,    43,    44,    98,     0,    96,
     100,    49,   188,   209,    95,    53,    54,    55,    56,    57,
     104,    13,    14,    15,   100,   201,   222,    96,   204,   205,
      89,    90,    91,    16,    17,    97,   210,    20,    21,    22,
      13,    17,    18,   217,    97,    99,    29,    30,    31,    25,
      26,    27,    28,    98,     3,    93,    99,    42,   234,    35,
      43,    44,   238,    17,   100,   241,    49,   243,    17,    95,
      53,    54,    55,    56,    57,   116,    95,    95,    32,    33,
      34,   257,     4,    32,    33,    34,    17,    16,    17,     7,
      98,    20,    98,    22,    37,    38,    39,    40,    41,    98,
      29,    30,    31,   108,   108,   108,    89,    90,    91,    92,
      93,    94,    94,   102,    43,    44,    42,    93,    99,     3,
      49,    98,   116,   299,    53,    54,    55,    56,    57,    99,
      96,    17,    18,   100,    10,     8,   116,   172,     8,    25,
      26,    27,    28,    29,    30,    31,    89,    90,    91,    35,
     181,    94,   166,   238,   330,    98,    42,    43,    44,   188,
      89,    90,    91,    49,    93,    17,    18,    53,    54,    55,
      56,    57,   133,    25,    26,    27,    28,    29,    30,    31,
     133,   133,   243,    35,   301,   302,   303,   162,   239,   241,
      42,    43,    44,   164,   192,   145,   313,    49,   330,   330,
      -1,    53,    54,    55,    56,    57,    92,    93,    64,    65,
      66,    67,    68,    69,    70,    71,    72,    73,    74,    75,
       4,     5,     6,     7,     8,     9,    10,    11,    14,    -1,
      -1,    17,    -1,    -1,    -1,    -1,    -1,    -1,    94,    -1,
      -1,    93,    28,    29,    30,   101,    32,    -1,    -1,   105,
     106,   107,   108,   109,   110,   111,   112,   113,   114,   115,
      -1,    -1,    48,    49,    -1,    -1,    -1,    -1,    54,    55,
     210,   211,   212,   213,   214,   215,   216,   217,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   248,   249
};

/* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
   symbol of state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,   118,   119,     0,    13,    14,    15,   120,   121,   122,
     123,   130,   131,   132,    16,    17,    21,    29,    30,    31,
      43,    44,    49,    53,    54,    55,    56,    57,    89,    90,
      91,    92,    93,    94,   124,   126,   127,   128,   134,   135,
     136,   143,   144,   150,   151,   165,   166,   169,    29,    93,
     125,   135,   143,   125,    29,    93,   126,    29,    93,   128,
       3,   194,   126,   127,   128,   128,   128,   128,   128,   128,
     128,   128,   128,   128,   128,   126,   126,   126,   194,   126,
     127,   128,   194,   116,   196,    20,    22,    23,    46,    47,
      48,   170,   165,    97,    97,    97,   103,   196,   196,    99,
      95,    96,   194,   194,   194,    64,    65,    66,    67,    68,
      69,    70,    71,    72,    73,    74,    75,    94,   101,   105,
     106,   107,   108,   109,   110,   111,   112,   113,   114,   115,
     171,    47,   137,   145,   152,   153,   194,    17,    32,    33,
      34,   172,   173,   174,   176,   177,   194,     4,     5,     6,
       7,     8,     9,    10,    11,   163,   134,    19,   141,    19,
     149,    96,    37,    38,    39,    40,    41,    89,    90,    91,
      98,   133,   134,   138,   139,   140,   154,   157,   160,    98,
     133,   134,   146,   147,   148,   154,   157,   160,   100,   197,
      95,   104,   100,   175,   194,   176,    96,   194,   194,   159,
     194,    97,   162,   194,    97,    97,   156,   194,   130,   139,
      17,    18,    25,    26,    27,    28,    35,    93,   129,   142,
     165,   130,   147,   142,   165,   153,    98,     4,   101,   191,
      99,   167,   173,    99,   100,   134,    95,   158,   159,    95,
     161,   162,   155,   156,    95,   142,   165,   129,    17,    93,
     129,   129,   129,   129,   129,   129,   129,    42,   116,   195,
     142,   165,   195,   191,    17,   168,    97,   178,   195,    36,
      45,    58,   186,   187,   188,   189,   190,   193,   194,   175,
       7,   192,    98,   158,     3,   101,   163,    98,   161,    98,
     155,    10,    11,   195,   175,   195,   186,    17,   180,   181,
     182,   108,   108,   108,    94,   195,   102,     4,     5,   164,
     195,   195,   195,    99,   179,   175,   183,   184,    42,   187,
     187,   187,   191,     3,   195,   187,    98,    99,   116,    95,
     100,   109,   109,   109,    96,   195,   186,   180,   185,   183,
     184,   116,   180
};

#define yyerrok		(yyerrstatus = 0)
#define yyclearin	(yychar = YYEMPTY)
#define YYEMPTY		(-2)
#define YYEOF		0

#define YYACCEPT	goto yyacceptlab
#define YYABORT		goto yyabortlab
#define YYERROR		goto yyerrorlab


/* Like YYERROR except do call yyerror.  This remains here temporarily
   to ease the transition to the new meaning of YYERROR, for GCC.
   Once GCC version 2 has supplanted version 1, this can go.  */

#define YYFAIL		goto yyerrlab

#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)					\
do								\
  if (yychar == YYEMPTY && yylen == 1)				\
    {								\
      yychar = (Token);						\
      yylval = (Value);						\
      yytoken = YYTRANSLATE (yychar);				\
      YYPOPSTACK (1);						\
      goto yybackup;						\
    }								\
  else								\
    {								\
      yyerror (YY_("syntax error: cannot back up")); \
      YYERROR;							\
    }								\
while (YYID (0))


#define YYTERROR	1
#define YYERRCODE	256


/* YYLLOC_DEFAULT -- Set CURRENT to span from RHS[1] to RHS[N].
   If N is 0, then set CURRENT to the empty location which ends
   the previous symbol: RHS[0] (always defined).  */

#define YYRHSLOC(Rhs, K) ((Rhs)[K])
#ifndef YYLLOC_DEFAULT
# define YYLLOC_DEFAULT(Current, Rhs, N)				\
    do									\
      if (YYID (N))                                                    \
	{								\
	  (Current).first_line   = YYRHSLOC (Rhs, 1).first_line;	\
	  (Current).first_column = YYRHSLOC (Rhs, 1).first_column;	\
	  (Current).last_line    = YYRHSLOC (Rhs, N).last_line;		\
	  (Current).last_column  = YYRHSLOC (Rhs, N).last_column;	\
	}								\
      else								\
	{								\
	  (Current).first_line   = (Current).last_line   =		\
	    YYRHSLOC (Rhs, 0).last_line;				\
	  (Current).first_column = (Current).last_column =		\
	    YYRHSLOC (Rhs, 0).last_column;				\
	}								\
    while (YYID (0))
#endif


/* YY_LOCATION_PRINT -- Print the location on the stream.
   This macro was not mandated originally: define only if we know
   we won't break user code: when these are the locations we know.  */

#ifndef YY_LOCATION_PRINT
# if YYLTYPE_IS_TRIVIAL
#  define YY_LOCATION_PRINT(File, Loc)			\
     fprintf (File, "%d.%d-%d.%d",			\
	      (Loc).first_line, (Loc).first_column,	\
	      (Loc).last_line,  (Loc).last_column)
# else
#  define YY_LOCATION_PRINT(File, Loc) ((void) 0)
# endif
#endif


/* YYLEX -- calling `yylex' with the right arguments.  */

//dex++
#ifndef YYLEX 
//dex--
#ifdef YYLEX_PARAM
# define YYLEX yylex (&yylval, YYLEX_PARAM)
#else
# define YYLEX yylex (&yylval)
#endif
//dex++
#endif
//dex--

/* Enable debugging if requested.  */
#if YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)			\
do {						\
  if (yydebug)					\
    YYFPRINTF Args;				\
} while (YYID (0))

# define YY_SYMBOL_PRINT(Title, Type, Value, Location)			  \
do {									  \
  if (yydebug)								  \
    {									  \
      YYFPRINTF (stderr, "%s ", Title);					  \
      yy_symbol_print (stderr,						  \
		  Type, Value); \
      YYFPRINTF (stderr, "\n");						  \
    }									  \
} while (YYID (0))


/*--------------------------------.
| Print this symbol on YYOUTPUT.  |
`--------------------------------*/

/*ARGSUSED*/
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yy_symbol_value_print (FILE *yyoutput, int yytype, YYSTYPE const * const yyvaluep)
#else
static void
yy_symbol_value_print (yyoutput, yytype, yyvaluep)
    FILE *yyoutput;
    int yytype;
    YYSTYPE const * const yyvaluep;
#endif
{
  if (!yyvaluep)
    return;
# ifdef YYPRINT
  if (yytype < YYNTOKENS)
    YYPRINT (yyoutput, yytoknum[yytype], *yyvaluep);
# else
  YYUSE (yyoutput);
# endif
  switch (yytype)
    {
      default:
	break;
    }
}


/*--------------------------------.
| Print this symbol on YYOUTPUT.  |
`--------------------------------*/

#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yy_symbol_print (FILE *yyoutput, int yytype, YYSTYPE const * const yyvaluep)
#else
static void
yy_symbol_print (yyoutput, yytype, yyvaluep)
    FILE *yyoutput;
    int yytype;
    YYSTYPE const * const yyvaluep;
#endif
{
  if (yytype < YYNTOKENS)
    YYFPRINTF (yyoutput, "token %s (", yytname[yytype]);
  else
    YYFPRINTF (yyoutput, "nterm %s (", yytname[yytype]);

  yy_symbol_value_print (yyoutput, yytype, yyvaluep);
  YYFPRINTF (yyoutput, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yy_stack_print (yytype_int16 *yybottom, yytype_int16 *yytop)
#else
static void
yy_stack_print (yybottom, yytop)
    yytype_int16 *yybottom;
    yytype_int16 *yytop;
#endif
{
  YYFPRINTF (stderr, "Stack now");
  for (; yybottom <= yytop; yybottom++)
    {
      int yybot = *yybottom;
      YYFPRINTF (stderr, " %d", yybot);
    }
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)				\
do {								\
  if (yydebug)							\
    yy_stack_print ((Bottom), (Top));				\
} while (YYID (0))


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yy_reduce_print (YYSTYPE *yyvsp, int yyrule)
#else
static void
yy_reduce_print (yyvsp, yyrule)
    YYSTYPE *yyvsp;
    int yyrule;
#endif
{
  int yynrhs = yyr2[yyrule];
  int yyi;
  unsigned long int yylno = yyrline[yyrule];
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %lu):\n",
	     yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      YYFPRINTF (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr, yyrhs[yyprhs[yyrule] + yyi],
		       &(yyvsp[(yyi + 1) - (yynrhs)])
		       		       );
      YYFPRINTF (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)		\
do {					\
  if (yydebug)				\
    yy_reduce_print (yyvsp, Rule); \
} while (YYID (0))

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args)
# define YY_SYMBOL_PRINT(Title, Type, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !YYDEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef	YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   YYSTACK_ALLOC_MAXIMUM < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif



#if YYERROR_VERBOSE

# ifndef yystrlen
#  if defined __GLIBC__ && defined _STRING_H
#   define yystrlen strlen
#  else
/* Return the length of YYSTR.  */
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static YYSIZE_T
yystrlen (const char *yystr)
#else
static YYSIZE_T
yystrlen (yystr)
    const char *yystr;
#endif
{
  YYSIZE_T yylen;
  for (yylen = 0; yystr[yylen]; yylen++)
    continue;
  return yylen;
}
#  endif
# endif

# ifndef yystpcpy
#  if defined __GLIBC__ && defined _STRING_H && defined _GNU_SOURCE
#   define yystpcpy stpcpy
#  else
/* Copy YYSRC to YYDEST, returning the address of the terminating '\0' in
   YYDEST.  */
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static char *
yystpcpy (char *yydest, const char *yysrc)
#else
static char *
yystpcpy (yydest, yysrc)
    char *yydest;
    const char *yysrc;
#endif
{
  char *yyd = yydest;
  const char *yys = yysrc;

  while ((*yyd++ = *yys++) != '\0')
    continue;

  return yyd - 1;
}
#  endif
# endif

# ifndef yytnamerr
/* Copy to YYRES the contents of YYSTR after stripping away unnecessary
   quotes and backslashes, so that it's suitable for yyerror.  The
   heuristic is that double-quoting is unnecessary unless the string
   contains an apostrophe, a comma, or backslash (other than
   backslash-backslash).  YYSTR is taken from yytname.  If YYRES is
   null, do not copy; instead, return the length of what the result
   would have been.  */
static YYSIZE_T
yytnamerr (char *yyres, const char *yystr)
{
  if (*yystr == '"')
    {
      YYSIZE_T yyn = 0;
      char const *yyp = yystr;

      for (;;)
	switch (*++yyp)
	  {
	  case '\'':
	  case ',':
	    goto do_not_strip_quotes;

	  case '\\':
	    if (*++yyp != '\\')
	      goto do_not_strip_quotes;
	    /* Fall through.  */
	  default:
	    if (yyres)
	      yyres[yyn] = *yyp;
	    yyn++;
	    break;

	  case '"':
	    if (yyres)
	      yyres[yyn] = '\0';
	    return yyn;
	  }
    do_not_strip_quotes: ;
    }

  if (! yyres)
    return yystrlen (yystr);

  return yystpcpy (yyres, yystr) - yyres;
}
# endif

/* Copy into YYRESULT an error message about the unexpected token
   YYCHAR while in state YYSTATE.  Return the number of bytes copied,
   including the terminating null byte.  If YYRESULT is null, do not
   copy anything; just return the number of bytes that would be
   copied.  As a special case, return 0 if an ordinary "syntax error"
   message will do.  Return YYSIZE_MAXIMUM if overflow occurs during
   size calculation.  */
static YYSIZE_T
yysyntax_error (char *yyresult, int yystate, int yychar)
{
  int yyn = yypact[yystate];

  if (! (YYPACT_NINF < yyn && yyn <= YYLAST))
    return 0;
  else
    {
      int yytype = YYTRANSLATE (yychar);
      YYSIZE_T yysize0 = yytnamerr (0, yytname[yytype]);
      YYSIZE_T yysize = yysize0;
      YYSIZE_T yysize1;
      int yysize_overflow = 0;
      enum { YYERROR_VERBOSE_ARGS_MAXIMUM = 5 };
      char const *yyarg[YYERROR_VERBOSE_ARGS_MAXIMUM];
      int yyx;

# if 0
      /* This is so xgettext sees the translatable formats that are
	 constructed on the fly.  */
      YY_("syntax error, unexpected %s");
      YY_("syntax error, unexpected %s, expecting %s");
      YY_("syntax error, unexpected %s, expecting %s or %s");
      YY_("syntax error, unexpected %s, expecting %s or %s or %s");
      YY_("syntax error, unexpected %s, expecting %s or %s or %s or %s");
# endif
      char *yyfmt;
      char const *yyf;
      static char const yyunexpected[] = "syntax error, unexpected %s";
      static char const yyexpecting[] = ", expecting %s";
      static char const yyor[] = " or %s";
      char yyformat[sizeof yyunexpected
		    + sizeof yyexpecting - 1
		    + ((YYERROR_VERBOSE_ARGS_MAXIMUM - 2)
		       * (sizeof yyor - 1))];
      char const *yyprefix = yyexpecting;

      /* Start YYX at -YYN if negative to avoid negative indexes in
	 YYCHECK.  */
      int yyxbegin = yyn < 0 ? -yyn : 0;

      /* Stay within bounds of both yycheck and yytname.  */
      int yychecklim = YYLAST - yyn + 1;
      int yyxend = yychecklim < YYNTOKENS ? yychecklim : YYNTOKENS;
      int yycount = 1;

      yyarg[0] = yytname[yytype];
      yyfmt = yystpcpy (yyformat, yyunexpected);

      for (yyx = yyxbegin; yyx < yyxend; ++yyx)
	if (yycheck[yyx + yyn] == yyx && yyx != YYTERROR)
	  {
	    if (yycount == YYERROR_VERBOSE_ARGS_MAXIMUM)
	      {
		yycount = 1;
		yysize = yysize0;
		yyformat[sizeof yyunexpected - 1] = '\0';
		break;
	      }
	    yyarg[yycount++] = yytname[yyx];
	    yysize1 = yysize + yytnamerr (0, yytname[yyx]);
	    yysize_overflow |= (yysize1 < yysize);
	    yysize = yysize1;
	    yyfmt = yystpcpy (yyfmt, yyprefix);
	    yyprefix = yyor;
	  }

      yyf = YY_(yyformat);
      yysize1 = yysize + yystrlen (yyf);
      yysize_overflow |= (yysize1 < yysize);
      yysize = yysize1;

      if (yysize_overflow)
	return YYSIZE_MAXIMUM;

      if (yyresult)
	{
	  /* Avoid sprintf, as that infringes on the user's name space.
	     Don't have undefined behavior even if the translation
	     produced a string with the wrong number of "%s"s.  */
	  char *yyp = yyresult;
	  int yyi = 0;
	  while ((*yyp = *yyf) != '\0')
	    {
	      if (*yyp == '%' && yyf[1] == 's' && yyi < yycount)
		{
		  yyp += yytnamerr (yyp, yyarg[yyi++]);
		  yyf += 2;
		}
	      else
		{
		  yyp++;
		  yyf++;
		}
	    }
	}
      return yysize;
    }
}
#endif /* YYERROR_VERBOSE */


/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

/*ARGSUSED*/
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yydestruct (const char *yymsg, int yytype, YYSTYPE *yyvaluep)
#else
static void
yydestruct (yymsg, yytype, yyvaluep)
    const char *yymsg;
    int yytype;
    YYSTYPE *yyvaluep;
#endif
{
  YYUSE (yyvaluep);

  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yytype, yyvaluep, yylocationp);

  switch (yytype)
    {

      default:
	break;
    }
}

/* Prevent warnings from -Wmissing-prototypes.  */
#ifdef YYPARSE_PARAM
#if defined __STDC__ || defined __cplusplus
int yyparse (void *YYPARSE_PARAM);
#else
int yyparse ();
#endif
#else /* ! YYPARSE_PARAM */
#if defined __STDC__ || defined __cplusplus
int yyparse (void);
#else
int yyparse ();
#endif
#endif /* ! YYPARSE_PARAM */





/*-------------------------.
| yyparse or yypush_parse.  |
`-------------------------*/

#ifdef YYPARSE_PARAM
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
int
yyparse (void *YYPARSE_PARAM)
#else
int
yyparse (YYPARSE_PARAM)
    void *YYPARSE_PARAM;
#endif
#else /* ! YYPARSE_PARAM */
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
int
yyparse (void)
#else
int
yyparse ()

#endif
#endif
{
/* The lookahead symbol.  */
int yychar;

/* The semantic value of the lookahead symbol.  */
YYSTYPE yylval;

    /* Number of syntax errors so far.  */
    int yynerrs;

    int yystate;
    /* Number of tokens to shift before error messages enabled.  */
    int yyerrstatus;

    /* The stacks and their tools:
       `yyss': related to states.
       `yyvs': related to semantic values.

       Refer to the stacks thru separate pointers, to allow yyoverflow
       to reallocate them elsewhere.  */

    /* The state stack.  */
    yytype_int16 yyssa[YYINITDEPTH];
    yytype_int16 *yyss;
    yytype_int16 *yyssp;

    /* The semantic value stack.  */
    YYSTYPE yyvsa[YYINITDEPTH];
    YYSTYPE *yyvs;
    YYSTYPE *yyvsp;

    YYSIZE_T yystacksize;

  int yyn;
  int yyresult;
  /* Lookahead token as an internal (translated) token number.  */
  int yytoken;
  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;

#if YYERROR_VERBOSE
  /* Buffer for error messages, and its allocated size.  */
  char yymsgbuf[128];
  char *yymsg = yymsgbuf;
  YYSIZE_T yymsg_alloc = sizeof yymsgbuf;
#endif

#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N))

  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  yytoken = 0;
  yyss = yyssa;
  yyvs = yyvsa;
  yystacksize = YYINITDEPTH;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yystate = 0;
  yyerrstatus = 0;
  yynerrs = 0;
  yychar = YYEMPTY; /* Cause a token to be read.  */

  /* Initialize stack pointers.
     Waste one element of value and location stack
     so that they stay on the same level as the state stack.
     The wasted elements are never initialized.  */
  yyssp = yyss;
  yyvsp = yyvs;

  goto yysetstate;

/*------------------------------------------------------------.
| yynewstate -- Push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
 yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed.  So pushing a state here evens the stacks.  */
  yyssp++;

 yysetstate:
  *yyssp = yystate;

  if (yyss + yystacksize - 1 <= yyssp)
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYSIZE_T yysize = yyssp - yyss + 1;

#ifdef yyoverflow
      {
	/* Give user a chance to reallocate the stack.  Use copies of
	   these so that the &'s don't force the real ones into
	   memory.  */
	YYSTYPE *yyvs1 = yyvs;
	yytype_int16 *yyss1 = yyss;

	/* Each stack pointer address is followed by the size of the
	   data in use in that stack, in bytes.  This used to be a
	   conditional around just the two extra args, but that might
	   be undefined if yyoverflow is a macro.  */
	yyoverflow (YY_("memory exhausted"),
		    &yyss1, yysize * sizeof (*yyssp),
		    &yyvs1, yysize * sizeof (*yyvsp),
		    &yystacksize);

	yyss = yyss1;
	yyvs = yyvs1;
      }
#else /* no yyoverflow */
# ifndef YYSTACK_RELOCATE
      goto yyexhaustedlab;
# else
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
	goto yyexhaustedlab;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
	yystacksize = YYMAXDEPTH;

      {
	yytype_int16 *yyss1 = yyss;
	union yyalloc *yyptr =
	  (union yyalloc *) YYSTACK_ALLOC (YYSTACK_BYTES (yystacksize));
	if (! yyptr)
	  goto yyexhaustedlab;
	YYSTACK_RELOCATE (yyss_alloc, yyss);
	YYSTACK_RELOCATE (yyvs_alloc, yyvs);
#  undef YYSTACK_RELOCATE
	if (yyss1 != yyssa)
	  YYSTACK_FREE (yyss1);
      }
# endif
#endif /* no yyoverflow */

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;

      YYDPRINTF ((stderr, "Stack size increased to %lu\n",
		  (unsigned long int) yystacksize));

      if (yyss + yystacksize - 1 <= yyssp)
	YYABORT;
    }

  YYDPRINTF ((stderr, "Entering state %d\n", yystate));

  if (yystate == YYFINAL)
    YYACCEPT;

  goto yybackup;

/*-----------.
| yybackup.  |
`-----------*/
yybackup:

  /* Do appropriate processing given the current state.  Read a
     lookahead token if we need one and don't already have one.  */

  /* First try to decide what to do without reference to lookahead token.  */
  yyn = yypact[yystate];
  if (yyn == YYPACT_NINF)
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* YYCHAR is either YYEMPTY or YYEOF or a valid lookahead symbol.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token: "));
      yychar = YYLEX;
    }

  if (yychar <= YYEOF)
    {
      yychar = yytoken = YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else
    {
      yytoken = YYTRANSLATE (yychar);
      YY_SYMBOL_PRINT ("Next token is", yytoken, &yylval, &yylloc);
    }

  /* If the proper action on seeing token YYTOKEN is to reduce or to
     detect an error, take that action.  */
  yyn += yytoken;
  if (yyn < 0 || YYLAST < yyn || yycheck[yyn] != yytoken)
    goto yydefault;
  yyn = yytable[yyn];
  if (yyn <= 0)
    {
      if (yyn == 0 || yyn == YYTABLE_NINF)
	goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the lookahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);

  /* Discard the shifted token.  */
  yychar = YYEMPTY;

  yystate = yyn;
  *++yyvsp = yylval;

  goto yynewstate;


/*-----------------------------------------------------------.
| yydefault -- do the default action for the current state.  |
`-----------------------------------------------------------*/
yydefault:
  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;
  goto yyreduce;


/*-----------------------------.
| yyreduce -- Do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     `$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];


  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
        case 8:

/* Line 1461 of yacc.c  */
#line 286 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { GFileContext->SetGlobalFlags( (yyvsp[(1) - (1)].m_flags) ); ;}
    break;

  case 9:

/* Line 1461 of yacc.c  */
#line 290 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { GFileContext->SetGlobalFlags( (yyvsp[(1) - (1)].m_flags) ); ;}
    break;

  case 10:

/* Line 1461 of yacc.c  */
#line 294 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { GFileContext->SetGlobalFlags( (yyvsp[(1) - (1)].m_flags) ); ;}
    break;

  case 18:

/* Line 1461 of yacc.c  */
#line 315 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_flags) = SSF_Abstract | (yyvsp[(2) - (2)].m_flags); ;}
    break;

  case 19:

/* Line 1461 of yacc.c  */
#line 316 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_flags) = SSF_Private | (yyvsp[(2) - (2)].m_flags); ;}
    break;

  case 20:

/* Line 1461 of yacc.c  */
#line 317 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_flags) = SSF_Protected | (yyvsp[(2) - (2)].m_flags); ;}
    break;

  case 21:

/* Line 1461 of yacc.c  */
#line 318 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_flags) = SSF_Public | (yyvsp[(2) - (2)].m_flags); ;}
    break;

  case 22:

/* Line 1461 of yacc.c  */
#line 319 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_flags) = SSF_Final | (yyvsp[(2) - (2)].m_flags); ;}
    break;

  case 23:

/* Line 1461 of yacc.c  */
#line 320 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_flags) = SSF_TestOnly | (yyvsp[(2) - (2)].m_flags); ;}
    break;

  case 24:

/* Line 1461 of yacc.c  */
#line 321 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_flags) = 0; ;}
    break;

  case 25:

/* Line 1461 of yacc.c  */
#line 325 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_flags) = SSF_Final | (yyvsp[(2) - (2)].m_flags); ;}
    break;

  case 26:

/* Line 1461 of yacc.c  */
#line 326 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_flags) = SSF_TestOnly | (yyvsp[(2) - (2)].m_flags); ;}
    break;

  case 27:

/* Line 1461 of yacc.c  */
#line 327 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_flags) = 0; ;}
    break;

  case 28:

/* Line 1461 of yacc.c  */
#line 331 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_flags) = SSF_Exec | (yyvsp[(2) - (2)].m_flags); ;}
    break;

  case 29:

/* Line 1461 of yacc.c  */
#line 332 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_flags) = SSF_Timer | (yyvsp[(2) - (2)].m_flags); ;}
    break;

  case 30:

/* Line 1461 of yacc.c  */
#line 333 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_flags) = SSF_Static | (yyvsp[(2) - (2)].m_flags); ;}
    break;

  case 31:

/* Line 1461 of yacc.c  */
#line 334 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_flags) = SSF_Multicast | (yyvsp[(2) - (2)].m_flags); ;}
    break;

  case 32:

/* Line 1461 of yacc.c  */
#line 335 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_flags) = SSF_Host | (yyvsp[(2) - (2)].m_flags); ;}
    break;

  case 33:

/* Line 1461 of yacc.c  */
#line 336 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_flags) = SSF_Client | (yyvsp[(2) - (2)].m_flags); ;}
    break;

  case 34:

/* Line 1461 of yacc.c  */
#line 337 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_flags) = SSF_Reliable | (yyvsp[(2) - (2)].m_flags); ;}
    break;

  case 35:

/* Line 1461 of yacc.c  */
#line 338 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_flags) = SSF_Virtual | (yyvsp[(2) - (2)].m_flags); ;}
    break;

  case 36:

/* Line 1461 of yacc.c  */
#line 339 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_flags) = SSF_Override | (yyvsp[(2) - (2)].m_flags); ;}
    break;

  case 37:

/* Line 1461 of yacc.c  */
#line 340 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_flags) = SSF_Const | (yyvsp[(2) - (2)].m_flags); ;}
    break;

  case 38:

/* Line 1461 of yacc.c  */
#line 341 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_flags) = SSF_Quest | (yyvsp[(2) - (2)].m_flags); ;}
    break;

  case 39:

/* Line 1461 of yacc.c  */
#line 342 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_flags) = SSF_Final | (yyvsp[(2) - (2)].m_flags); ;}
    break;

  case 40:

/* Line 1461 of yacc.c  */
#line 343 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_flags) = SSF_TestOnly | (yyvsp[(2) - (2)].m_flags); ;}
    break;

  case 41:

/* Line 1461 of yacc.c  */
#line 344 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_flags) = 0; ;}
    break;

  case 42:

/* Line 1461 of yacc.c  */
#line 348 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_flags) = SSF_Const | (yyvsp[(2) - (2)].m_flags); ;}
    break;

  case 43:

/* Line 1461 of yacc.c  */
#line 349 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_flags) = SSF_Mutable | (yyvsp[(2) - (2)].m_flags); ;}
    break;

  case 44:

/* Line 1461 of yacc.c  */
#line 350 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_flags) = SSF_Editable | (yyvsp[(2) - (2)].m_flags); ;}
    break;

  case 45:

/* Line 1461 of yacc.c  */
#line 351 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_flags) = SSF_InstanceEditable | (yyvsp[(2) - (2)].m_flags); ;}
    break;

  case 46:

/* Line 1461 of yacc.c  */
#line 352 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_flags) = SSF_Replicated | (yyvsp[(2) - (2)].m_flags); ;}
    break;

  case 47:

/* Line 1461 of yacc.c  */
#line 353 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_flags) = SSF_Inlined | (yyvsp[(2) - (2)].m_flags); ;}
    break;

  case 48:

/* Line 1461 of yacc.c  */
#line 354 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_flags) = SSF_Persistent | (yyvsp[(2) - (2)].m_flags); ;}
    break;

  case 49:

/* Line 1461 of yacc.c  */
#line 355 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_flags) = SSF_TestOnly | (yyvsp[(2) - (2)].m_flags); ;}
    break;

  case 50:

/* Line 1461 of yacc.c  */
#line 356 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_flags) = 0; ;}
    break;

  case 51:

/* Line 1461 of yacc.c  */
#line 364 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_flags) = SSF_Import; ;}
    break;

  case 52:

/* Line 1461 of yacc.c  */
#line 365 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_flags) = 0; ;}
    break;

  case 53:

/* Line 1461 of yacc.c  */
#line 369 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_flags) = SSF_ImportOnly; ;}
    break;

  case 54:

/* Line 1461 of yacc.c  */
#line 373 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_flags) = SSF_ImportOnly | SSF_Final; ;}
    break;

  case 55:

/* Line 1461 of yacc.c  */
#line 377 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_flags) = SSF_Private; ;}
    break;

  case 56:

/* Line 1461 of yacc.c  */
#line 378 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_flags) = SSF_Protected; ;}
    break;

  case 57:

/* Line 1461 of yacc.c  */
#line 379 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_flags) = SSF_Public; ;}
    break;

  case 58:

/* Line 1461 of yacc.c  */
#line 380 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_flags) = 0; ;}
    break;

  case 59:

/* Line 1461 of yacc.c  */
#line 385 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    {
		GFileContext->AddAttribute( ScriptAttribute( (yyvsp[(2) - (6)].m_token), (yyvsp[(4) - (6)].m_token), (yyvsp[(1) - (6)].m_context) ) );
	;}
    break;

  case 60:

/* Line 1461 of yacc.c  */
#line 389 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    {
		GFileContext->AddAttribute( ScriptAttribute( (yyvsp[(2) - (5)].m_token), (yyvsp[(4) - (5)].m_token), (yyvsp[(1) - (5)].m_context) ) );
	;}
    break;

  case 61:

/* Line 1461 of yacc.c  */
#line 393 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    {
		GFileContext->AddAttribute( ScriptAttribute( (yyvsp[(2) - (4)].m_token), red::StringView(), (yyvsp[(1) - (4)].m_context) ) );
	;}
    break;

  case 62:

/* Line 1461 of yacc.c  */
#line 397 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    {
		GFileContext->AddAttribute( ScriptAttribute( (yyvsp[(2) - (3)].m_token), red::StringView(), (yyvsp[(1) - (3)].m_context) ) );
	;}
    break;

  case 63:

/* Line 1461 of yacc.c  */
#line 407 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { GFileParser->EndDefinition( GFileContext->GetCurrentTokenLine(), (yyvsp[(2) - (4)].m_context), (yyvsp[(4) - (4)].m_context) ); GFileParser->PopContext(); ;}
    break;

  case 64:

/* Line 1461 of yacc.c  */
#line 411 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { GFileParser->StartClass( (yyvsp[(3) - (4)].m_context), (yyvsp[(4) - (4)].m_context), (yyvsp[(3) - (4)].m_string), (yyvsp[(4) - (4)].m_string), (yyvsp[(1) - (4)].m_flags) | GFileContext->GetGlobalFlags() ); ;}
    break;

  case 71:

/* Line 1461 of yacc.c  */
#line 427 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { GFileContext->SetGlobalFlags( (yyvsp[(1) - (2)].m_flags) | (yyvsp[(2) - (2)].m_flags) ); ;}
    break;

  case 76:

/* Line 1461 of yacc.c  */
#line 438 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_string) = (yyvsp[(2) - (2)].m_string); (yyval.m_context) = (yyvsp[(2) - (2)].m_context); ;}
    break;

  case 77:

/* Line 1461 of yacc.c  */
#line 439 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_string) = (""); (yyval.m_context) = nullptr; ;}
    break;

  case 78:

/* Line 1461 of yacc.c  */
#line 450 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    {
			const YYSTYPE_File::Idents& idents = (yyvsp[(3) - (5)].m_idents);
			const Uint32 identsCount = idents.Size();

			const ScriptAttributes& attributes = GFileContext->GetAttributes();

			for ( Uint32 i = 0; i < identsCount; ++i )
			{
				GFileParser->AddProperty( idents[i].m_context, (yyvsp[(5) - (5)].m_context), idents[i].m_value, (yyvsp[(1) - (5)].m_flags) | GFileContext->GetGlobalFlags(), (yyvsp[(5) - (5)].m_typeName), false, attributes );
			}

			GFileContext->ClearAttributes();
		;}
    break;

  case 79:

/* Line 1461 of yacc.c  */
#line 470 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { GFileParser->EndDefinition( GFileContext->GetCurrentTokenLine(), (yyvsp[(2) - (4)].m_context), (yyvsp[(4) - (4)].m_context) ); GFileParser->PopContext(); ;}
    break;

  case 80:

/* Line 1461 of yacc.c  */
#line 474 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { GFileParser->StartStruct( (yyvsp[(3) - (4)].m_context), (yyvsp[(4) - (4)].m_context), (yyvsp[(3) - (4)].m_string), (yyvsp[(4) - (4)].m_string), (yyvsp[(1) - (4)].m_flags) | GFileContext->GetGlobalFlags() ); ;}
    break;

  case 87:

/* Line 1461 of yacc.c  */
#line 490 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { GFileContext->SetGlobalFlags( (yyvsp[(1) - (2)].m_flags) | (yyvsp[(2) - (2)].m_flags) ); ;}
    break;

  case 92:

/* Line 1461 of yacc.c  */
#line 501 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_string) = (yyvsp[(2) - (2)].m_string); (yyval.m_context) = (yyvsp[(2) - (2)].m_context); ;}
    break;

  case 93:

/* Line 1461 of yacc.c  */
#line 502 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_string) = (""); (yyval.m_context) = nullptr; ;}
    break;

  case 94:

/* Line 1461 of yacc.c  */
#line 511 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { GFileParser->EndDefinition( GFileContext->GetCurrentTokenLine(), (yyvsp[(2) - (5)].m_context), (yyvsp[(5) - (5)].m_context) ); GFileParser->PopContext(); ;}
    break;

  case 95:

/* Line 1461 of yacc.c  */
#line 515 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { GFileParser->StartEnum( (yyvsp[(2) - (2)].m_context), (yyvsp[(2) - (2)].m_string), GFileContext->GetGlobalFlags() ); ;}
    break;

  case 96:

/* Line 1461 of yacc.c  */
#line 517 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    {
		(yyval.m_typeName) = CScriptTypeDummy::MakeSimple( (yyvsp[(4) - (4)].m_string) );
		GFileParser->StartEnum( (yyvsp[(2) - (4)].m_context), (yyvsp[(2) - (4)].m_string), (yyval.m_typeName), GFileContext->GetGlobalFlags() );
	;}
    break;

  case 97:

/* Line 1461 of yacc.c  */
#line 524 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    {;}
    break;

  case 98:

/* Line 1461 of yacc.c  */
#line 525 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    {;}
    break;

  case 99:

/* Line 1461 of yacc.c  */
#line 529 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { GFileParser->AddEnumOption( (yyvsp[(1) - (1)].m_context), (yyvsp[(1) - (1)].m_string) ); ;}
    break;

  case 100:

/* Line 1461 of yacc.c  */
#line 530 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { GFileParser->AddEnumOption( (yyvsp[(1) - (3)].m_context), (yyvsp[(1) - (3)].m_string), (yyvsp[(3) - (3)].m_integer) ); ;}
    break;

  case 101:

/* Line 1461 of yacc.c  */
#line 531 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { GFileParser->AddEnumOption( (yyvsp[(1) - (4)].m_context), (yyvsp[(1) - (4)].m_string), -(yyvsp[(4) - (4)].m_integer) ); ;}
    break;

  case 106:

/* Line 1461 of yacc.c  */
#line 548 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { GFileParser->SetBrowsable( (yyvsp[(1) - (4)].m_context), (yyvsp[(1) - (4)].m_string), true ); ;}
    break;

  case 107:

/* Line 1461 of yacc.c  */
#line 549 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { GFileParser->SetBrowsable( (yyvsp[(1) - (4)].m_context), (yyvsp[(1) - (4)].m_string), false ); ;}
    break;

  case 112:

/* Line 1461 of yacc.c  */
#line 567 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { GFileParser->AddHint( (yyvsp[(1) - (4)].m_context), (yyvsp[(1) - (4)].m_string), (yyvsp[(3) - (4)].m_string) ); ;}
    break;

  case 117:

/* Line 1461 of yacc.c  */
#line 585 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { GFileParser->AddDefaultValue( (yyvsp[(1) - (4)].m_context), (yyvsp[(1) - (4)].m_string), (yyvsp[(3) - (4)].m_token), false ); ;}
    break;

  case 118:

/* Line 1461 of yacc.c  */
#line 586 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { GFileParser->AddDefaultValue( (yyvsp[(1) - (5)].m_context), (yyvsp[(1) - (5)].m_string), (yyvsp[(4) - (5)].m_token), true ); ;}
    break;

  case 119:

/* Line 1461 of yacc.c  */
#line 588 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    {
		red::String tmp = (yyvsp[(3) - (6)].m_token).ToString().TrimLeft().TrimRight() + (yyvsp[(4) - (6)].m_token).ToString() + (yyvsp[(5) - (6)].m_token).ToString().TrimRight();
		red::StringView view{ tmp };
		GFileParser->AddDefaultValue( (yyvsp[(1) - (6)].m_context), (yyvsp[(1) - (6)].m_string), view, false );
	;}
    break;

  case 120:

/* Line 1461 of yacc.c  */
#line 596 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_token) = (yyvsp[(1) - (1)].m_token); ;}
    break;

  case 121:

/* Line 1461 of yacc.c  */
#line 597 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_token) = (yyvsp[(1) - (1)].m_token); ;}
    break;

  case 122:

/* Line 1461 of yacc.c  */
#line 598 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_token) = (yyvsp[(1) - (1)].m_token); ;}
    break;

  case 123:

/* Line 1461 of yacc.c  */
#line 599 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_token) = (yyvsp[(1) - (1)].m_token); ;}
    break;

  case 124:

/* Line 1461 of yacc.c  */
#line 600 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_token) = (yyvsp[(1) - (1)].m_token); ;}
    break;

  case 125:

/* Line 1461 of yacc.c  */
#line 601 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_token) = (yyvsp[(1) - (1)].m_token); ;}
    break;

  case 126:

/* Line 1461 of yacc.c  */
#line 602 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_token) = (yyvsp[(1) - (1)].m_token); ;}
    break;

  case 127:

/* Line 1461 of yacc.c  */
#line 603 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_token) = (yyvsp[(1) - (1)].m_token); ;}
    break;

  case 128:

/* Line 1461 of yacc.c  */
#line 607 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_token) = (yyvsp[(1) - (1)].m_token); ;}
    break;

  case 129:

/* Line 1461 of yacc.c  */
#line 608 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_token) = (yyvsp[(1) - (1)].m_token); ;}
    break;

  case 132:

/* Line 1461 of yacc.c  */
#line 624 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { GFileParser->SetReturnValue( (yyvsp[(3) - (3)].m_context), (yyvsp[(3) - (3)].m_typeName), (yyvsp[(2) - (3)].m_flags) ); ;}
    break;

  case 134:

/* Line 1461 of yacc.c  */
#line 629 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_flags) = SSF_Const; ;}
    break;

  case 135:

/* Line 1461 of yacc.c  */
#line 630 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_flags) = 0; ;}
    break;

  case 136:

/* Line 1461 of yacc.c  */
#line 635 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    {
		const ScriptAttributes& attributes = GFileContext->GetAttributes();
		GFileParser->StartFunction( (yyvsp[(2) - (2)].m_context), (yyvsp[(2) - (2)].m_string), (yyvsp[(1) - (2)].m_flags) | (yyvsp[(2) - (2)].m_flags) | GFileContext->GetGlobalFlags(), attributes );
		GFileContext->ClearAttributes();
	;}
    break;

  case 137:

/* Line 1461 of yacc.c  */
#line 641 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    {
		const ScriptAttributes& attributes = GFileContext->GetAttributes();
		GFileParser->StartFunction( (yyvsp[(2) - (2)].m_context), (yyvsp[(2) - (2)].m_string), SSF_Event | SSF_Virtual | SSF_Protected, attributes );
		GFileContext->ClearAttributes();
	;}
    break;

  case 138:

/* Line 1461 of yacc.c  */
#line 649 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_string) = (yyvsp[(2) - (2)].m_string); (yyval.m_context) = (yyvsp[(2) - (2)].m_context); ;}
    break;

  case 139:

/* Line 1461 of yacc.c  */
#line 650 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_string) = (yyvsp[(2) - (2)].m_string); (yyval.m_flags) = SSF_Operator; (yyval.m_context) = (yyvsp[(1) - (2)].m_context); ;}
    break;

  case 140:

/* Line 1461 of yacc.c  */
#line 651 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_string) = ("Cast"); (yyval.m_flags) = SSF_Cast; (yyval.m_context) = (yyvsp[(1) - (1)].m_context); ;}
    break;

  case 141:

/* Line 1461 of yacc.c  */
#line 652 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_string) = ("Cast"); (yyval.m_flags) = SSF_Cast | SSF_Implicit; (yyval.m_context) = (yyvsp[(1) - (2)].m_context); ;}
    break;

  case 142:

/* Line 1461 of yacc.c  */
#line 656 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_string) = red::StringView( ("OperatorAssignAdd" ) ); ;}
    break;

  case 143:

/* Line 1461 of yacc.c  */
#line 657 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_string) = red::StringView( ("OperatorAssignSubtract") ); ;}
    break;

  case 144:

/* Line 1461 of yacc.c  */
#line 658 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_string) = red::StringView( ("OperatorAssignMultiply") ); ;}
    break;

  case 145:

/* Line 1461 of yacc.c  */
#line 659 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_string) = red::StringView( ("OperatorAssignDivide") ); ;}
    break;

  case 146:

/* Line 1461 of yacc.c  */
#line 660 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_string) = red::StringView( ("OperatorAssignAnd") ); ;}
    break;

  case 147:

/* Line 1461 of yacc.c  */
#line 661 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_string) = red::StringView( ("OperatorAssignOr") ); ;}
    break;

  case 148:

/* Line 1461 of yacc.c  */
#line 662 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_string) = red::StringView( ("OperatorLogicOr") ); ;}
    break;

  case 149:

/* Line 1461 of yacc.c  */
#line 663 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_string) = red::StringView( ("OperatorLogicAnd") ); ;}
    break;

  case 150:

/* Line 1461 of yacc.c  */
#line 664 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_string) = red::StringView( ("OperatorOr") ); ;}
    break;

  case 151:

/* Line 1461 of yacc.c  */
#line 665 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_string) = red::StringView( ("OperatorAnd") ); ;}
    break;

  case 152:

/* Line 1461 of yacc.c  */
#line 666 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_string) = red::StringView( ("OperatorXor") ); ;}
    break;

  case 153:

/* Line 1461 of yacc.c  */
#line 667 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_string) = red::StringView( ("OperatorEqual") ); ;}
    break;

  case 154:

/* Line 1461 of yacc.c  */
#line 668 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_string) = red::StringView( ("OperatorNotEqual") ); ;}
    break;

  case 155:

/* Line 1461 of yacc.c  */
#line 669 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_string) = red::StringView( ("OperatorLess") ); ;}
    break;

  case 156:

/* Line 1461 of yacc.c  */
#line 670 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_string) = red::StringView( ("OperatorGreater") ); ;}
    break;

  case 157:

/* Line 1461 of yacc.c  */
#line 671 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_string) = red::StringView( ("OperatorGreaterEqual") ); ;}
    break;

  case 158:

/* Line 1461 of yacc.c  */
#line 672 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_string) = red::StringView( ("OperatorLessEqual") ); ;}
    break;

  case 159:

/* Line 1461 of yacc.c  */
#line 673 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_string) = red::StringView( ("OperatorAdd") ); ;}
    break;

  case 160:

/* Line 1461 of yacc.c  */
#line 674 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_string) = red::StringView( ("OperatorMultiply") ); ;}
    break;

  case 161:

/* Line 1461 of yacc.c  */
#line 675 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_string) = red::StringView( ("OperatorDivide") ); ;}
    break;

  case 162:

/* Line 1461 of yacc.c  */
#line 676 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_string) = red::StringView( ("OperatorModulo") ); ;}
    break;

  case 163:

/* Line 1461 of yacc.c  */
#line 677 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_string) = red::StringView( ("OperatorLogicNot") ); ;}
    break;

  case 164:

/* Line 1461 of yacc.c  */
#line 678 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_string) = red::StringView( ("OperatorBitNot") ); ;}
    break;

  case 165:

/* Line 1461 of yacc.c  */
#line 679 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_string) = red::StringView( ("OperatorNeg") ); ;}
    break;

  case 166:

/* Line 1461 of yacc.c  */
#line 680 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_string) = red::StringView( ("OperatorArray") ); ;}
    break;

  case 171:

/* Line 1461 of yacc.c  */
#line 695 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    {
			const YYSTYPE_File::Idents& idents = (yyvsp[(2) - (4)].m_idents);
			const Uint32 identsCount = idents.Size();
			for ( Uint32 i = 0; i < identsCount; ++i )
			{
				GFileParser->AddProperty( idents[i].m_context, (yyvsp[(4) - (4)].m_context), idents[i].m_value, (yyvsp[(1) - (4)].m_flags), (yyvsp[(4) - (4)].m_typeName), true );
			}
		;}
    break;

  case 172:

/* Line 1461 of yacc.c  */
#line 706 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_idents).PushBack( YYSTYPE_File::Ident( (yyvsp[(1) - (3)].m_string), (yyvsp[(1) - (3)].m_context) ) ); (yyval.m_idents).PushBack( (yyvsp[(3) - (3)].m_idents) );	;}
    break;

  case 173:

/* Line 1461 of yacc.c  */
#line 707 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_idents).PushBack( YYSTYPE_File::Ident( (yyvsp[(1) - (1)].m_string), (yyvsp[(1) - (1)].m_context) ) ); ;}
    break;

  case 174:

/* Line 1461 of yacc.c  */
#line 711 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_flags) = (yyvsp[(1) - (2)].m_flags) | (yyvsp[(2) - (2)].m_flags); ;}
    break;

  case 175:

/* Line 1461 of yacc.c  */
#line 712 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_flags) = 0; ;}
    break;

  case 176:

/* Line 1461 of yacc.c  */
#line 716 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_flags) = SSF_Out; ;}
    break;

  case 177:

/* Line 1461 of yacc.c  */
#line 717 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_flags) = SSF_Skipable; ;}
    break;

  case 178:

/* Line 1461 of yacc.c  */
#line 718 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_flags) = SSF_Optional; ;}
    break;

  case 179:

/* Line 1461 of yacc.c  */
#line 719 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_flags) = SSF_Const; ;}
    break;

  case 180:

/* Line 1461 of yacc.c  */
#line 723 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { GFileParser->EndDefinition( GFileContext->GetCurrentTokenLine(), (yyvsp[(1) - (4)].m_context), (yyvsp[(4) - (4)].m_context) ); GFileParser->PopContext(); ;}
    break;

  case 181:

/* Line 1461 of yacc.c  */
#line 724 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { GFileParser->SetFunctionUndefined(); GFileParser->PopContext(); ;}
    break;

  case 182:

/* Line 1461 of yacc.c  */
#line 728 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { GFileContext->ExtractFunctionCode();  yyclearin; ;}
    break;

  case 186:

/* Line 1461 of yacc.c  */
#line 738 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { GFileParser->ResetFunctionPropertyList(); GFileParser->SetFunctionLocalParamFlags( (yyvsp[(1) - (2)].m_flags) ); ;}
    break;

  case 187:

/* Line 1461 of yacc.c  */
#line 742 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_flags) = SSF_Const; ;}
    break;

  case 188:

/* Line 1461 of yacc.c  */
#line 743 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_flags) = 0; ;}
    break;

  case 191:

/* Line 1461 of yacc.c  */
#line 753 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    {
		const YYSTYPE_File::Idents& idents = (yyvsp[(1) - (3)].m_idents);
		const Uint32 identsCount = idents.Size();
		for( Uint32 i = 0; i < identsCount; ++i )
		{
			GFileParser->AddProperty( idents[ i ].m_context, (yyvsp[(3) - (3)].m_context), idents[ i ].m_value, GFileParser->GetFunctionLocalParamFlags(), (yyvsp[(3) - (3)].m_typeName), false );
		}
	;}
    break;

  case 192:

/* Line 1461 of yacc.c  */
#line 763 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { CScriptTokenStream initCode; GFileContext->ExtractInitCode( initCode ); GFileParser->SetLastFunctionPropertyInitCode( initCode ); yyclearin; ;}
    break;

  case 195:

/* Line 1461 of yacc.c  */
#line 776 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_typeName) = (yyvsp[(1) - (1)].m_typeName); ;}
    break;

  case 196:

/* Line 1461 of yacc.c  */
#line 777 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_typeName) = (yyvsp[(1) - (1)].m_typeName); ;}
    break;

  case 197:

/* Line 1461 of yacc.c  */
#line 778 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_typeName) = (yyvsp[(1) - (1)].m_typeName); ;}
    break;

  case 198:

/* Line 1461 of yacc.c  */
#line 779 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_typeName) = CScriptTypeDummy::MakeSimple( (yyvsp[(1) - (1)].m_string) ); ;}
    break;

  case 199:

/* Line 1461 of yacc.c  */
#line 783 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_typeName) = CScriptTypeDummy::MakeReference( (yyvsp[(3) - (4)].m_typeName) ); ;}
    break;

  case 200:

/* Line 1461 of yacc.c  */
#line 787 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_typeName) = CScriptTypeDummy::MakeDynamicArray( (yyvsp[(3) - (4)].m_typeName) ); ;}
    break;

  case 201:

/* Line 1461 of yacc.c  */
#line 791 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_typeName) = CScriptTypeDummy::MakeStaticArray( (yyvsp[(1) - (4)].m_typeName), (yyvsp[(3) - (4)].m_integer) ); ;}
    break;

  case 202:

/* Line 1461 of yacc.c  */
#line 796 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    {
		if ( !red::StringToInt( (yyval.m_integer), (yyvsp[(1) - (1)].m_token).ToString().AsChar(), nullptr, red::BaseAuto ) )
		{
			yyerror( "Could not convert script token into integer" );
		}
		else
		{
			(yyval.m_string) = (yyvsp[(1) - (1)].m_token); (yyval.m_context) = (yyvsp[(1) - (1)].m_context);
		}
	;}
    break;

  case 203:

/* Line 1461 of yacc.c  */
#line 809 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_string) = (yyvsp[(1) - (1)].m_token); (yyval.m_context) = (yyvsp[(1) - (1)].m_context); ;}
    break;

  case 204:

/* Line 1461 of yacc.c  */
#line 813 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_typeName) = CScriptTypeDummy::MakeWeakHandle( (yyvsp[(3) - (4)].m_typeName) ); ;}
    break;

  case 205:

/* Line 1461 of yacc.c  */
#line 817 "D:\\Temp\\redvanguard_redlexer_codegen\\scriptFileParser.bison"
    { (yyval.m_string) = (yyvsp[(1) - (1)].m_token); (yyval.m_context) = (yyvsp[(1) - (1)].m_context); ;}
    break;



/* Line 1461 of yacc.c  */
#line 3029 "D:\\Temp\\redvanguard_redlexer_codegen\\bison_unused.cxx"
      default: break;
    }
  YY_SYMBOL_PRINT ("-> $$ =", yyr1[yyn], &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);

  *++yyvsp = yyval;

  /* Now `shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */

  yyn = yyr1[yyn];

  yystate = yypgoto[yyn - YYNTOKENS] + *yyssp;
  if (0 <= yystate && yystate <= YYLAST && yycheck[yystate] == *yyssp)
    yystate = yytable[yystate];
  else
    yystate = yydefgoto[yyn - YYNTOKENS];

  goto yynewstate;


/*------------------------------------.
| yyerrlab -- here on detecting error |
`------------------------------------*/
yyerrlab:
  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
#if ! YYERROR_VERBOSE
      yyerror (YY_("syntax error"));
#else
      {
	YYSIZE_T yysize = yysyntax_error (0, yystate, yychar);
	if (yymsg_alloc < yysize && yymsg_alloc < YYSTACK_ALLOC_MAXIMUM)
	  {
	    YYSIZE_T yyalloc = 2 * yysize;
	    if (! (yysize <= yyalloc && yyalloc <= YYSTACK_ALLOC_MAXIMUM))
	      yyalloc = YYSTACK_ALLOC_MAXIMUM;
	    if (yymsg != yymsgbuf)
	      YYSTACK_FREE (yymsg);
	    yymsg = (char *) YYSTACK_ALLOC (yyalloc);
	    if (yymsg)
	      yymsg_alloc = yyalloc;
	    else
	      {
		yymsg = yymsgbuf;
		yymsg_alloc = sizeof yymsgbuf;
	      }
	  }

	if (0 < yysize && yysize <= yymsg_alloc)
	  {
	    (void) yysyntax_error (yymsg, yystate, yychar);
	    yyerror (yymsg);
	  }
	else
	  {
	    yyerror (YY_("syntax error"));
	    if (yysize != 0)
	      goto yyexhaustedlab;
	  }
      }
#endif
    }



  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
	 error, discard it.  */

      if (yychar <= YYEOF)
	{
	  /* Return failure if at end of input.  */
	  if (yychar == YYEOF)
	    YYABORT;
	}
      else
	{
	  yydestruct ("Error: discarding",
		      yytoken, &yylval);
	  yychar = YYEMPTY;
	}
    }

  /* Else will try to reuse lookahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*---------------------------------------------------.
| yyerrorlab -- error raised explicitly by YYERROR.  |
`---------------------------------------------------*/
yyerrorlab:

  /* Pacify compilers like GCC when the user code never invokes
     YYERROR and the label yyerrorlab therefore never appears in user
     code.  */
  if (/*CONSTCOND*/ 0)
     goto yyerrorlab;

  /* Do not reclaim the symbols of the rule which action triggered
     this YYERROR.  */
  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);
  yystate = *yyssp;
  goto yyerrlab1;


/*-------------------------------------------------------------.
| yyerrlab1 -- common code for both syntax error and YYERROR.  |
`-------------------------------------------------------------*/
yyerrlab1:
  yyerrstatus = 3;	/* Each real token shifted decrements this.  */

  for (;;)
    {
      yyn = yypact[yystate];
      if (yyn != YYPACT_NINF)
	{
	  yyn += YYTERROR;
	  if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYTERROR)
	    {
	      yyn = yytable[yyn];
	      if (0 < yyn)
		break;
	    }
	}

      /* Pop the current state because it cannot handle the error token.  */
      if (yyssp == yyss)
	YYABORT;


      yydestruct ("Error: popping",
		  yystos[yystate], yyvsp);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  *++yyvsp = yylval;


  /* Shift the error token.  */
  YY_SYMBOL_PRINT ("Shifting", yystos[yyn], yyvsp, yylsp);

  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturn;

/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturn;

#if !defined(yyoverflow) || YYERROR_VERBOSE
/*-------------------------------------------------.
| yyexhaustedlab -- memory exhaustion comes here.  |
`-------------------------------------------------*/
yyexhaustedlab:
  yyerror (YY_("memory exhausted"));
  yyresult = 2;
  /* Fall through.  */
#endif

yyreturn:
  if (yychar != YYEMPTY)
     yydestruct ("Cleanup: discarding lookahead",
		 yytoken, &yylval);
  /* Do not reclaim the symbols of the rule which action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
		  yystos[*yyssp], yyvsp);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif
#if YYERROR_VERBOSE
  if (yymsg != yymsgbuf)
    YYSTACK_FREE (yymsg);
#endif
  /* Make sure YYID is used.  */
  return YYID (yyresult);
}



