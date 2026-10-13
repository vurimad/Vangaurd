
/* A Bison parser, made by GNU Bison 2.4.1.  */

/* Skeleton interface for Bison's Yacc-like parsers in C
   
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




