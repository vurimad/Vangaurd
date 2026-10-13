/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "rttiRegistration.h"
#include "rttiSystem.h"
#include "rttiType.h"

RTTIRegistrator::RTTIRegistrator( red::FixedSizeFunction<void()> registerFunc, red::FixedSizeFunction<void()> initFunc )
{
	GetRttiSystem().AddPreRegistrationFunc( registerFunc );
	GetRttiSystem().AddRegistrationFunc( initFunc );
}

RTTIRegistrator::RTTIRegistrator( RegisterFunctionCallback registerFunc, RegisterFunctionCallback initFunc, bool )
{
	GetRttiSystem().AddPreRegistrationFunc( registerFunc );
	GetRttiSystem().AddRegistrationFunc( initFunc );
}

RTTIRegistrator::RTTIRegistrator( red::FixedSizeFunction<void()>  registerFunc )
{
	GetRttiSystem().AddPreRegistrationFunc( registerFunc );
}

void RTTIRegisterType( rtti::IType *type, TypeHash hash )
{
	GetRttiSystem().RegisterType( type, hash );
}

void RTTIRegisterTypeWrapper( TypeHash existingTypeHash, TypeHash wrappedTypeHash )
{
	GetRttiSystem().RegisterTypeWrapper( existingTypeHash, wrappedTypeHash );
}

void RTTIRegisterGlobalFunction( rtti::Function* function )
{
	GetRttiSystem().RegisterGlobalFunction( function );
}

void RTTIRegisterScriptAlias( rtti::IType *type, const char* scriptAlias )
{
	GetRttiSystem().RegisterScriptAlias( type->GetName(), RED_NAME( scriptAlias ) );
}
