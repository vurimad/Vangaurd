/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "expressionToolkit.h"
#include "expressionToolkitHelper.h"
#include "math.h"
#include "rttiMacrosUtils.h"
#include "mathVector3.h"

#define REGISTER_OPERATOR5( op, fun, id, ret, _1,_2,_3,_4 )	REGISTER_OPERATOR_INTERNAL( op, fun, id, ret, _1, _2, _3, _4 )
#define REGISTER_OPERATOR4( op, fun, id, ret, _1,_2,_3 )	REGISTER_OPERATOR_INTERNAL( op, fun, id, ret, _1, _2, _3, VarInvalid )
#define REGISTER_OPERATOR3( op, fun, id, ret, _1,_2 )		REGISTER_OPERATOR_INTERNAL( op, fun, id, ret, _1, _2, VarInvalid, VarInvalid )
#define REGISTER_OPERATOR2( op, fun, id, ret, _1 )			REGISTER_OPERATOR_INTERNAL( op, fun, id, ret, _1, VarInvalid, VarInvalid, VarInvalid )
#define REGISTER_OPERATOR1( op, fun, id, ret )				REGISTER_OPERATOR_INTERNAL( op, fun, id, ret, VarInvalid, VarInvalid, VarInvalid, VarInvalid )
#define REGISTER_OPERATOR_INLINE( op, fun, id, ...) INDIRECT_EXPAND( GET_MACRO_FOR_ARG_NR5(__VA_ARGS__, REGISTER_OPERATOR5, REGISTER_OPERATOR4, REGISTER_OPERATOR3, REGISTER_OPERATOR2, REGISTER_OPERATOR1 )( op, fun, id, __VA_ARGS__) )
#define REGISTER_OPERATOR( op, fun, id, ...) REGISTER_OPERATOR_INLINE( op, fun##( context );, id, __VA_ARGS__ )


namespace mathExpr
{
	Bool FindGlobalOperatorId( const String& op, VarType& outReturnType, Uint16& outId, Uint32 allowedOp, VarType a1 = VarInvalid, VarType a2 = VarInvalid, VarType a3 = VarInvalid, VarType a4 = VarInvalid )
	{
		{
#define OPERATOR_GROUP( group ) } if( ( allowedOp & Uint32( group ) ) == Uint32( group ) ){
#define REGISTER_OPERATOR_INTERNAL( oper, fun, id, ret, arg1, arg2, arg3, arg4 ) if( op == #oper && arg1 == a1 && arg2 == a2 && arg3 == a3 && arg4 == a4 ) { outReturnType = ret; outId = id; return true; }
		#include "expressionToolkit_opRegistry.h"
#undef REGISTER_OPERATOR_INTERNAL
#undef OPERATOR_GROUP
		}
		return false;
	}	

	void CallGlobalOperator( Uint32 id, ExecutionContext& context )
	{
		switch( id )
		{
		case 0: RED_LOG( "MathExpression: Executing math expression with invalid data" ); return;
#define OPERATOR_GROUP( ... )
#define REGISTER_OPERATOR_INTERNAL( oper, fun, id, ret, arg1, arg2, arg3, arg4 ) case id: fun; return;
			#include "expressionToolkit_opRegistry.h"
#undef REGISTER_OPERATOR_INTERNAL
		}
	}
}