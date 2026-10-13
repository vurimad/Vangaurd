/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "rttiCommon.h"
#include <functional>

namespace rtti
{
	class IType;
	class Function;
}

typedef void (*RegisterFunctionCallback)(void);

class RED_REFLECTION_API RTTIRegistrator final
{
public:
	RTTIRegistrator( red::FixedSizeFunction<void()> registerFunc, red::FixedSizeFunction<void()> initFunc );
	RTTIRegistrator( RegisterFunctionCallback registerFunc, RegisterFunctionCallback initFunc, bool );
	RTTIRegistrator( red::FixedSizeFunction<void()> registerFunc );
};

RED_REFLECTION_API void RTTIRegisterType( rtti::IType *type, TypeHash hash );
RED_REFLECTION_API void RTTIRegisterTypeWrapper( TypeHash existingTypeHash, TypeHash wrappedTypeHash );
RED_REFLECTION_API void RTTIRegisterGlobalFunction( rtti::Function* function );
RED_REFLECTION_API void RTTIRegisterScriptAlias( rtti::IType *type, const char* scriptAlias );

#define RTTI_REGISTER_AUTO_TYPE( _type )													\
{																							\
	RTTIRegistrator registrator( []() {														\
		TypeHash nativeHash = GetNativeTypeHash<_type>();									\
		RTTIRegisterType( RED_NEW( _type ), nativeHash );									\
	} );																					\
}

#define RTTI_REGISTER_AUTO_TYPE_ALIAS( _type, _aliasType )									\
{																							\
	TypeHash nativeHash = GetNativeTypeHash<_type>();										\
	RTTIRegisterType( RED_NEW( _aliasType ), nativeHash );									\
}

#define RTTI_REGISTER_AUTO_TYPE_ALIAS_IN_NAMESPACE( _type, _aliasType, ... )								\
{																											\
	TypeHash nativeHash = GetNativeTypeHash<BUILD_NAMESPACE( __VA_ARGS__ )::_type>();		\
	RTTIRegisterType( RED_NEW( BUILD_NAMESPACE( __VA_ARGS__ )::_aliasType ), nativeHash );	\
}

#define RTTI_REGISTER_TYPE_WRAPPER( _type, _wrapperType )									\
{																							\
	RTTIRegistrator registrator( []() {														\
		TypeHash nativeHash = GetNativeTypeHash<_type>();									\
		TypeHash nativeWrapperHash = GetNativeTypeHash<_wrapperType>();						\
		RTTIRegisterTypeWrapper( nativeWrapperHash, nativeHash );							\
	} );																					\
}
