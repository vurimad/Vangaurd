/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/


#pragma once

class ExpressionToolkitHelpers
{
public:
	static Float AndOperation( const Float op1, const Float op2 )
	{
		const Float epsFloat = std::numeric_limits< Float >::epsilon();
		const Bool temp1 = op1 > epsFloat;
		const Bool temp2 = op2 > epsFloat;

		if( temp1 && temp2 )
		{
			return 1.f;
		}

		return 0.f;
	}
	
	static Float OrOperation( const Float op1, const Float op2 )
	{
		const Float epsFloat = std::numeric_limits< Float >::epsilon();
		const Bool temp1 = op1 > epsFloat;
		const Bool temp2 = op2 > epsFloat;

		if( temp1 || temp2 )
		{
			return 1.f;
		}

		return 0.f;
	}

	static Float XorOperation( const Float op1, const Float op2 )
	{
		const Float epsFloat = std::numeric_limits< Float >::epsilon();
		const Bool temp1 = op1 > epsFloat;
		const Bool temp2 = op2 > epsFloat;

		if( temp1 != temp2 )
		{
			return 1.f;
		}

		return 0.f;
	}
	
	static Float NotOperation( const Float op1 )
	{
		const Float epsFloat = std::numeric_limits< Float >::epsilon();
		const Bool temp = op1 > epsFloat;

		if( temp )
		{
			return 0.f;
		}

		return 1.f;
	}
	
	static Float GreaterThanOperation( const Float op1, const Float op2 )
	{
		if( op1 > op2 )
		{
			return 1.f;
		}

		return 0.f;
	}
	
	static Float GreaterThanOrEqualOperation( const Float op1, const Float op2 )
	{
		if( op1 >= op2 )
		{
			return 1.f;
		}

		return 0.f;
	}
	
	static Float LessThanOperation( const Float op1, const Float op2 )
	{
		if( op1 < op2 )
		{
			return 1.f;
		}

		return 0.f;
	}

	static Float LessThanOrEqualOperation( const Float op1, const Float op2 )
	{
		if( op1 <= op2 )
		{
			return 1.f;
		}

		return 0.f;
	}
	
	static Float EqualOperation( const Float op1, const Float op2 )
	{
		if( MAbs( op1 - op2 ) < std::numeric_limits< Float >::epsilon() )
		{
			return 1.f;
		}

		return 0.f;
	}
};