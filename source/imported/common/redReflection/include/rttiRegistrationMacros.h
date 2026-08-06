#pragma once

#include "rttiMacrosUtils.h"
#include "rttiRegistration.h"

namespace rtti
{
	class ClassType;
}

#define RTTI_REGISTER_TYPE( _typeName )								\
{																	\
	extern const rtti::ClassType* touchType##_typeName();			\
	const rtti::ClassType *classDesc = touchType##_typeName();		\
}

#define RTTI_REGISTER_TYPE_IN_NAMESPACE( _typeName,  ... )																\
{																														\
	extern const rtti::ClassType* JOIN_TOKENS_MACRO( touchType, JOIN_TOKENS_MACRO( _typeName, __VA_ARGS__ ) )();		\
	const rtti::ClassType *classDesc = JOIN_TOKENS_MACRO( touchType, JOIN_TOKENS_MACRO( _typeName, __VA_ARGS__ ) )();	\
}

#define RTTI_REGISTER_ENUM( _enumName )			\
{												\
	RED_FORCE_LINK_THAT_FILE( _enumName );		\
}

#define RTTI_REGISTER_ENUM_IN_NAMESPACE( _enumName, ... )						\
{																				\
	RED_FORCE_LINK_THAT_FILE( JOIN_TOKENS_MACRO( _enumName, __VA_ARGS__ ) );	\
}

#define RTTI_REGISTER_BITFIELD( _bitfieldName )		\
{													\
	RED_FORCE_LINK_THAT_FILE( _bitfieldName );		\
}

#define RTTI_REGISTER_BITFIELD_IN_NAMESPACE( _bitfieldName, ... )					\
{																					\
	RED_FORCE_LINK_THAT_FILE( JOIN_TOKENS_MACRO( _bitfieldName, __VA_ARGS__ ) );	\
}

#undef RTTI_REGISTER_SIMPLE_TYPE
#define RTTI_REGISTER_SIMPLE_TYPE( _typeName )	\
{												\
	RTTIRegistrator registrator( []() {			\
		extern void RegisterType##_typeName();	\
		RegisterType##_typeName();				\
	} );										\
}
