/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#pragma once

#include "enumBuilder.h"

//////////////////////////////////////////////////////////////////////////
// Cooking platform
enum ECookingPlatform : Uint8
{
	PLATFORM_None,			//!< No cooking
	PLATFORM_PC,			//!< PC cooking
	PLATFORM_XboxOne,		//!< XboxOne cooking
	PLATFORM_PS4,			//!< PS4 cooking
	PLATFORM_WindowsServer,	//!< Windows server cooking
	PLATFORM_LinuxServer,	//!< Linux server cooking


	PLATFORM_All			//!< Cook for all platform
};

//////////////////////////////////////////////////////////////////////////
// Enum used for any form of comparison
enum class EComparisonType
{
	Greater,
	GreaterOrEqual,
	Equal,
	NotEqual,
	Less,
	LessOrEqual,
};

template< typename T >
static Bool	CompareValues( const T& lhs, const T& rhs, EComparisonType comparisonOperator )
{
	switch ( comparisonOperator )
	{
	case EComparisonType::Greater:
		return lhs > rhs;

	case EComparisonType::GreaterOrEqual:
		return lhs >= rhs;

	case EComparisonType::Equal:
		return lhs == rhs;

	case EComparisonType::NotEqual:
		return lhs != rhs;

	case EComparisonType::Less:
		return lhs < rhs;

	case EComparisonType::LessOrEqual:
		return lhs <= rhs;

	default:
		return lhs <= rhs;
	}
}

RTTI_DECLARE_ENUM( EComparisonType );
RTTI_DECLARE_ENUM( ECookingPlatform );
