/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "expressionToolkit.h"
#include "rttiClassBuilder.h"
#include "../../redContainers/include/fundamentalStringConversion.h"

using red::DynArray;
using red::String;
using red::HashMap;

//***********************************************************************************************************//

RTTI_BEGIN_TYPE_IN_NAMESPACE( Expression, mathExpr );
RTTI_PARENT_TYPE(ISerializable)
RTTI_PROPERTY( m_tokenData );
RTTI_PROPERTY( m_valuesData );
RTTI_PROPERTY( m_returnVarType );
RTTI_END_TYPE();



namespace mathExpr
{
	Bool FindGlobalOperatorId( const String& op, VarType& outReturnType, Uint16& outId, Uint32 allowedOp, VarType a1 = VarInvalid, VarType a2 = VarInvalid, VarType a3 = VarInvalid, VarType a4 = VarInvalid );
	void CallGlobalOperator( Uint32 id, ExecutionContext& context );
	
	namespace ExpressionToolkit
	{
		Bool IsAlpha( AnsiChar c ) 
		{ 
			return  isalnum(c) || c == '_' || c == '$' || c == '#' || c == '@'; 
		}

		Bool IsOperator( AnsiChar c )
		{
			return ( c == '+' || c == '-' || c == '*' || c == '/' || c == '^' || c == '%' || c == '(' || c == ')' || c == ',' 
				|| c == '&' || c == '|' || c == '!'
				|| c == '>' || c == '<' || c == '=' );
		}
	
		Bool IsNumber( AnsiChar c )
		{
			return  ( c >= '0' && c <= '9' ) || c == '.';
		}

		Bool IsControlOperator( const String& str )
		{
			return str == "," || str == "(";
		}

		Int32 OpPrecenence( const String& str )
		{
			if( IsAlpha( str[0] ) )
			{
				return 6;
			}
			if ( str == "#" ) // unary minus
			{
				return 5;
			}
			if( str == "^" )
			{
				return 4;
			}
			if( str == "*" || str == "/" || str =="%" )
			{
				return 3;
			}
			if( str == "+" || str == "-" )
			{
				return 2;
			}
			else
			{
				return 0;
			}
		}

		Bool GetNextToken( const String& expr, String& out, Uint32& curPos )
		{
			while ( curPos < expr.Length() && red::IsWhiteSpace( expr[ curPos ] ) )
			{
				curPos++;
			}
			if ( curPos >= expr.Length() )
			{
				return false;
			}
			Uint32 prevPos = curPos;
			for ( ;curPos < expr.Length(); curPos++ )
			{
				if( IsNumber( expr[prevPos] ) )
				{
					if( !IsNumber( expr[curPos] ) )
					{
						break;
					}
				}				
				else if( IsAlpha( expr[curPos] ) != IsAlpha( expr[prevPos] ) )
				{
					break;
				}
				if ( IsOperator( expr[curPos] ) )
				{
					curPos++;
					break;
				}
			} 
			out.Set( expr.Begin() + prevPos, curPos - prevPos );
			return out.Length() > 0;
		}

		void ProcessValue( ParsingContext& context, Float val )
		{
			Token tok;
			tok.SetIsOperator( false );
			tok.SetValIndex( Uint16( context.m_valuesData.Size() ) );
			tok.SetType( VarScalar );
			context.m_valuesData.PushBack( val );
			context.m_tokenData.PushBack( tok );
		}

		struct OpData
		{
			const String	oper;
			Uint8				nrArgs;
		};

		void ProcessOperator( ParsingContext& context, const OpData oper )
		{
			Token token;
			token.SetIsOperator( true );
			token.SetNumOfArgs( oper.nrArgs );
			token.SetValIndex( context.m_tempData.Size() );
			context.m_tokenData.PushBack( token );
			context.m_tempData.PushBack( oper.oper );
		}

		Bool ProcessRegisteredConstant( ParsingContext& context, const String& token )
		{
			auto var = std::find_if( context.m_registeredVars.Begin(), context.m_registeredVars.End(), [ token ]( const ParsingContext::RegisteredVar & var ){ return var.name == token && var.isConst; } ); 
			if( var != context.m_registeredVars.End() )
			{
				Token tok;
				tok.SetIsOperator( false );
				tok.SetValIndex( Uint16( context.m_valuesData.Size() ) );
				tok.SetType( var->type );
				CopyVarAcrossBuffers( context.m_valuesData, context.m_constValData, var->varId, var->type );
				context.m_tokenData.PushBack( tok );					
				return true;
			}
			return false;
		}

		Bool InfToPre( ParsingContext& context )
		{
			String token;
			red::DynArray< OpData >  opStack{ red::PoolEngine() };	
			Float val;
			Bool prevTokenOp = true;
			while( GetNextToken( context.m_expression, token, context.m_curChar ) )
			{		
				if( IsNumber( token[0] ) && FromString( token, val ) )
				{
					ProcessValue( context, val );
					prevTokenOp = false;
				}
				else if ( token[0] == '(' )
				{
					OpData data = {token, 0};
					opStack.PushBack( data );
					prevTokenOp = true;
				}
				else if ( token[0] == ')' )
				{
					Uint8 addArgs = 0;
					while( true )
					{
						if( opStack.Empty() )
						{
							context.m_errorMessage += "Opening bracket not found \'(\'\n";
							return false; //no left bracket
						}
						OpData oldToken = opStack.PopBack();
						if( oldToken.oper[0] == '(' )
						{
							if ( addArgs > 0 )
							{
								OpData& funcToken = opStack.Back();
								if ( !IsAlpha( funcToken.oper[0] ) )
								{
									context.m_errorMessage += "Invalid function construct for function: " + funcToken.oper + "( check ',' inside brackets )\n";
									return false; // bad function construct
								}
								funcToken.nrArgs += addArgs;
							}							
							break;
						}
						else if( oldToken.oper[0] == ',' )
						{
							addArgs++;
						}
						else
						{
							ProcessOperator( context, oldToken );
						}						
					} 
					prevTokenOp = false;
				}
				else if ( IsOperator( token[0] ) )
				{
					if ( prevTokenOp && token[0] == '-' ) // Hack for unary '-'
					{
						token[0] = '#';
					}
					while( !opStack.Empty() && !IsControlOperator( opStack.Back().oper ) && OpPrecenence( opStack.Back().oper ) >= OpPrecenence( token ) )
					{
						ProcessOperator( context, opStack.PopBack() ); 
					}

					OpData tokenData = { token, Uint8( prevTokenOp ? 1 : 2 ) };
					opStack.PushBack( tokenData );
					prevTokenOp = true;
				}
				else if( IsAlpha( token[0] ) )
				{
					Uint32 indexCopy = context.m_curChar;
					String nextToken;

					if( ProcessRegisteredConstant( context, token ) )
					{
						prevTokenOp = false;
					}
					else if( GetNextToken( context.m_expression, nextToken, indexCopy ) && nextToken[0] == '(' )
					{
						while( !opStack.Empty() && OpPrecenence( opStack.Back().oper ) >= OpPrecenence( token ) )
						{
							ProcessOperator( context, opStack.PopBack() ); 
						}
						OpData tokenData = { token, 1 };
						opStack.PushBack( tokenData );
					}
					else
					{
						OpData tokenData = { token, 0 };
						ProcessOperator( context, tokenData ); // variable
						prevTokenOp = false;
					}
				}
				else
				{		
					context.m_errorMessage += "Unrecognized token used \"" + token + "\"\n";
					return false; //unrecognized token			
				}
			}			
			while( !opStack.Empty()  )
			{
				OpData tokenData = opStack.PopBack();
				if ( tokenData.oper[0] == '(' )
				{
					context.m_errorMessage += "Missing closing bracket \")\"\n";
					return false;// no closing bracket
				}		
				ProcessOperator( context, tokenData );
			}

			return true;
		}

		Bool FindRegisteredVarId( ParsingContext& context, const String& op, VarType& outReturnType, Uint16& outId )
		{
			for( const ParsingContext::RegisteredVar& var : context.m_registeredVars )
			{
				if ( var.name == op && !var.isConst )
				{
					outReturnType = var.type;
					outId = var.varId;
					return true;
				}
			}
			return false;
		}

		Bool AutoRegisterVar( ParsingContext& context, const String& op, VarType& outReturnType, Uint16& outId )
		{
			if( !context.m_autoRegisterVars )
			{
				return false;
			}

			outReturnType = op[0] == '$' ? VarVector : op[0] == '#' ? VarRotation : VarScalar;
			outId = context.RegisterVar(op, outReturnType);

			return true;
		}

		Uint16 BuildOpID( OperatorType type, Uint16 opId )
		{
			RED_FATAL_ASSERT( ( opId >> 14 ) == 0, "Bits for storing operator type already used" );
			opId |= type << 14;
			return opId;
		}

		OperatorType ExtractOpType( Uint16& opId )
		{
			OperatorType type = OperatorType( opId >> 14 );
			opId &= 0x3FFF;
			return type;
		}

		Bool BindOperators( ParsingContext& context, VarType& outReturnVarType )
		{
			red::DynArray<VarType> typesStack{ red::PoolEngine() };		
			for ( Token& token : context.m_tokenData )
			{				
				if ( token.IsOperator() && token.Type() == VarInvalid )
				{
					const String& op = context.m_tempData[ token.ValIndex() ];
					VarType retType = VarInvalid;
					Uint32 nrOfArgs = token.NumOfArgs();
					VarType arg4 = nrOfArgs > 3 && !typesStack.Empty() ? typesStack.PopBack() : VarInvalid;
					VarType arg3 = nrOfArgs > 2 && !typesStack.Empty() ? typesStack.PopBack() : VarInvalid;
					VarType arg2 = nrOfArgs > 1 && !typesStack.Empty() ? typesStack.PopBack() : VarInvalid;
					VarType arg1 = nrOfArgs > 0 && !typesStack.Empty() ? typesStack.PopBack() : VarInvalid; 				  
					Uint16 operatorId = 0;

					if ( FindRegisteredVarId( context, op, retType, operatorId ) || ( nrOfArgs == 0 && AutoRegisterVar( context, op, retType, operatorId ) ) )
					{
						operatorId = BuildOpID( UserRegisteredOp, operatorId );
					}
					else if( FindGlobalOperatorId( op, retType, operatorId, context.m_allowedOperations, arg1, arg2, arg3, arg4 ) )
					{
						operatorId = BuildOpID( GlobalOp, operatorId );
					}				
					else if ( context.m_projectGlobalOpBinder( op, retType, operatorId, context.m_allowedOperations, arg1, arg2, arg3, arg4 ) )
					{
						operatorId = BuildOpID( ProjectGlobalOp, operatorId );
					}

					if( operatorId != 0 )
					{
						token.SetType( retType );
						token.SetValIndex( operatorId );
						typesStack.PushBack( retType );
					}	
					else
					{
						token.m_data = 0;
						context.m_errorMessage += "Could not find matching operator for token \"" + op + "\"\n";
						return false; // Could not find operator for token with arguments 
					}
				}
				else
				{
					typesStack.PushBack( token.Type() );
				}
			}

			if( typesStack.Size() != 1 )
			{
				context.m_errorMessage += "Expression evaluates into multiple return types\n";
				return false; //Expression returns multiple values
			}
			outReturnVarType = typesStack.Back(); 
			return true;
		}
	}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	Bool CreateExpression( ParsingContext& input, Expression& out )
	{
		VarType retType = VarInvalid;
		if( !ExpressionToolkit::InfToPre( input ) ) 
		{
			return false; //Failed parsing
		}
		if( !ExpressionToolkit::BindOperators( input, retType ) ) 
		{
			return false; //Undefined operators
		}
		if( input.m_forceReturnType != VarInvalid && input.m_forceReturnType != retType )
		{
			input.m_errorMessage += "Expression evaluates into unexpected return type\n";
			return false; // expression evaluates to undesired type
		}

		out.m_returnVarType = retType;
		Uint32 tokensNum = input.m_tokenData.Size();
		out.m_tokenData.Resize( tokensNum );
		for ( Uint32 i = 0; i < tokensNum; ++i )
		{
			out.m_tokenData[i] = input.m_tokenData[i].m_data;
		}
		out.m_valuesData = input.m_valuesData;
		

		return true;
	}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	Uint32 ParsingContext::RegisterVar( const String& name, VarType type )
	{
		RegisteredVar var = { name, m_registeredVars.Size(), type, false };
		m_registeredVars.PushBack( var );
		return var.varId;
	}

	Bool ParsingContext::IterateUsedVars( Uint32& lastVarIndex, String& outName, VarType& outType, Uint32& outId )
	{
		// Sort to match links order in MathExpressionNodeData (float, vector and quaternion)
		red::DynArray< RegisteredVar > sortedRegisteredVars = m_registeredVars;
		std::stable_sort( sortedRegisteredVars.Begin(), sortedRegisteredVars.End(), []( const RegisteredVar& a, const RegisteredVar& b )
		{
			return a.type < b.type;
		} );

		for( ; lastVarIndex < sortedRegisteredVars.Size(); ++lastVarIndex )
		{
			RegisteredVar& var =  sortedRegisteredVars[lastVarIndex];
			if ( !var.isConst )
			{
				outName = var.name;
				outType = var.type;
				outId = var.varId;
				++lastVarIndex;
				return true;
			}
		}
		return false;
	}

	Bool NullOperatorBinder( const String& op, VarType& outReturnType, Uint16& outId, Uint32 allowedOp, VarType a1, VarType a2, VarType a3, VarType a4 )	
	{
		return false;
	}

	ParsingContext::ParsingContext() : m_curChar( 0 ), m_autoRegisterVars( false ), m_allowedOperations( ScalarOperations ), m_forceReturnType( VarInvalid ), m_projectGlobalOpBinder( NullOperatorBinder )
	{}

	void ParsingContext::SetExpression( const String& name )
	{
		m_expression = name;
		m_tokenData.Clear();
		m_valuesData.Clear();
		m_tempData.Clear();
		m_curChar = 0;
		m_errorMessage.Clear();
	}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	Bool Expression::Calculate( ExecutionContext& context ) const
	{
		for ( Uint32 intToken : m_tokenData )
		{			
			Token token( intToken );
			if( token.IsOperator() )
			{
				Uint16 operatorId = token.ValIndex();

				OperatorType type = ExpressionToolkit::ExtractOpType( operatorId );
				Bool res = true;
				switch ( type )
				{
				case GlobalOp:
					CallGlobalOperator( operatorId, context );
					break;
				case ProjectGlobalOp:
					res = context.m_projectGlobalOpExecutor( operatorId, context );
					break;
				case UserRegisteredOp:
					res = context.UseRegisteredVar( operatorId );
					break;
				}

				if( !res )
				{
					return false; //local variable used but not provided
				}
			}
			else
			{
				switch ( token.Type() )
				{
				case VarScalar:
					context.ReturnFloat( m_valuesData[ token.ValIndex() ] );
					break;
				default:
					RED_ASSERT( false, "Not supported token type." );
					break;
				};
			}
		}

		return m_returnVarType != VarType::VarInvalid && context.remainingVarType == m_returnVarType;
	}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


	Vector4 ExecutionContext::GetVector()
	{
		Vector4 vec( &data.Back() - 3 );
		data.Resize( data.Size() - 4 );
		return vec; 
	}

	Quaternion ExecutionContext::GetQuaternion()
	{
		Quaternion rot( &data.Back() - 3 );
		data.Resize( data.Size() - 4 );
		return rot; 
	}

	void ExecutionContext::ReturnVector( const Vector4& arg )
	{
		remainingVarType = VarVector;
		const Float* argData = &arg.X;
		ExpressionToolkit::CopyVarAcrossBuffers( data, argData, 0, VarVector );
	}

	void ExecutionContext::ReturnQuaternion(const Quaternion& arg)
	{
		remainingVarType = VarRotation;
        red::StaticArray<Float,4> f{arg.i, arg.j, arg.k, arg.r};
		ExpressionToolkit::CopyVarAcrossBuffers( data, f, 0, VarRotation );
	}

	Bool NullProjectGlobalExecutor( Uint16 id, ExecutionContext& context )
	{
		return false;
	}

	ExecutionContext::ExecutionContext() : m_projectGlobalOpExecutor( NullProjectGlobalExecutor )
	{}

	Bool ExecutionContext::UseRegisteredVar( Uint32 operatorId )
	{
		auto iter = std::find_if( idToIndex.Begin(), idToIndex.End(), [ operatorId ]( const ExecutionContext::RegisteredVar & var ){ return var.id == operatorId; } ); 
		if( iter != idToIndex.End() )
		{
			remainingVarType = iter->type;
			ExpressionToolkit::CopyVarAcrossBuffers( data, varData, iter->index, iter->type );		
			return true;
		}
		return false;
	}
};