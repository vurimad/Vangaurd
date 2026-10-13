/**
* Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "rttiClassBuilderUtils.h"
#include "rttiClass.h"
#include "rttiPropertyBuilder.h"
#include "rttiFunctionParamBuilder.h"
#include "rttiPropertyOverrideBuilder.h"

RED_DISABLE_WARNING_CLANG( "-Winvalid-offsetof" )

namespace rtti
{
	void PropertyBinder::AddPropertyToClass( const rtti::ClassType* classType, rtti::PropertyBuilder*& builder )
	{
		RED_FATAL_ASSERT( classType, "No class" );
		if( builder )
		{
			builder->AddPropertyToClass();
			RED_DELETE( builder );
			builder = nullptr;
		}
	}


	void PropertyBinder::AddPropertyOverrideToClass( const rtti::ClassType* classType, rtti::PropertyOverrideBuilder*& builder )
	{
		RED_FATAL_ASSERT( classType, "No class" );
		if( builder )
		{
			builder->AddPropertyOverrideToClass();
			RED_DELETE( builder );
			builder = nullptr;
		}
	}

	void FunctionParamBinder::AddParamToFunction( const rtti::Function* function, rtti::FunctionParamBuilder*& builder )
	{
		RED_FATAL_ASSERT( function, "No function" );
		if( builder )
		{
			builder->AddParamToFunction();
			RED_DELETE( builder );
			builder = nullptr;
		}
	}
}

