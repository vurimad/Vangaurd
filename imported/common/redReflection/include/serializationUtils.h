/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

class IFile;

namespace rtti
{
	class IType;
	class ClassType;
}

namespace red
{
	class ResourceReferenceScriptToken;
}

namespace serialization
{
	extern RED_REFLECTION_API void SerializeName( IFile& file, class CName &name );
	extern RED_REFLECTION_API void SerializeTypeRef( IFile& file, const rtti::IType*& typeRef );
	extern RED_REFLECTION_API void SerializePointer( IFile& file, const rtti::ClassType* pointerClass, void*& serializablePtr );
	extern RED_REFLECTION_API void SerializeGUID( IFile& file, CGUID& guid );
	extern RED_REFLECTION_API void SerializeRUID( IFile& file, CRUID& ruid );
	extern RED_REFLECTION_API void SerializeRUIDRef( IFile& file, CRUIDRef& ruidRef );
	extern RED_REFLECTION_API void SerializeTweakDBID( IFile& file, game::data::TweakDBID& tweakDBID );
}

RED_FORCE_INLINE void operator<<( IFile& file, class CName &name )
{
	serialization::SerializeName( file, name );
}

RED_FORCE_INLINE void operator<<( IFile& file, const rtti::IType*& typeRef )
{
	serialization::SerializeTypeRef( file, typeRef );
}

RED_FORCE_INLINE void operator<<( IFile& file, CGUID& guid )
{
	serialization::SerializeGUID( file, guid );
}

RED_FORCE_INLINE void operator<<( IFile& file, CRUID& ruid )
{
	serialization::SerializeRUID( file, ruid );
}

RED_FORCE_INLINE void operator<<( IFile& file, CRUIDRef& ruidRef )
{
	serialization::SerializeRUIDRef( file, ruidRef );
}

RED_FORCE_INLINE void operator<<( IFile& file, game::data::TweakDBID& tweakDBID )
{
	serialization::SerializeTweakDBID( file, tweakDBID );
}