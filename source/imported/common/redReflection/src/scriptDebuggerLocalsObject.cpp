/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "scriptDebuggerLocalsObject.h"

#include "scriptDebuggerLocalsProperty.h"
#include "serializable.h"
#include "rttiProperty.h"

#include "../../redReflection/include/event.h"
#include "../../redContainers/include/dynArrayAccessor.h"

namespace script { namespace debug {

class NativeClassBase : public NativeClassView
{
public:
	NativeClassBase( const Uint32 index, const void* const object, const String& nativeTypeName );

	virtual String GetName() const override;

private:
	const Uint32 m_index = 0;
};

class NativePrimitiveMember : public IVariable
{
public:
	NativePrimitiveMember( const String& name, const void* const object, const String& nativeTypeName );

	virtual red::DynArray< IVariablePtr > EnumerateChildren() override;
	virtual IVariablePtr FindChild( const red::StringView& name ) override;
	virtual String GetName() const override;
	virtual const rtti::IType* GetType() const override;
	virtual String GetValue() const override;
	virtual Uint32 GetAttributes() const override;
	virtual const void* GetRaw() const override;
	virtual String GetTypeName() const override;

protected:
	const String m_name;
	const void* const m_object = nullptr;
	const String m_nativeTypeName;
};

class NativeEnumMember : public NativePrimitiveMember
{
public:
	NativeEnumMember( const String &name, const void* const object, const String& nativeTypeName, const Uint32 size );

	virtual String GetValue() const override;

private:
	const Uint32 m_size = 0;
};

class NativeClassMember : public NativeClassView
{
public:
	NativeClassMember( const String& name, const void* const object, const String& nativeTypeName );

	virtual String GetName() const override;

private:
	const String m_name;
};

class NativePrimitivePointerMember : public NativePrimitiveMember
{
public:
	NativePrimitivePointerMember( const String& name, const void* const object, const String& nativeTypeName, const Uint32 pointersNumber );

	virtual String GetTypeName() const override;
	virtual String GetValue() const override;
	virtual Uint32 GetAttributes() const override;

private:
	Uint32 m_pointersNumber = 0;
};

class NativeClassPointerMember : public NativeClassMember
{
public:
	NativeClassPointerMember( const String& name, const void* const object, const String& nativeTypeName, const Uint32 pointersNumber );

	virtual String GetTypeName() const override;
	virtual String GetValue() const override;
	virtual Uint32 GetAttributes() const override;

private:
	Uint32 m_pointersNumber = 0;
};

class NativeCArrayMember : public IVariable
{
public:
	NativeCArrayMember( const String& name, const void* const object, const String& nativeTypeName, const dbgutils::TypeInfo& typeInfo );

	virtual red::DynArray< IVariablePtr > EnumerateChildren() override;
	virtual IVariablePtr FindChild( const red::StringView& name ) override;
	virtual String GetName() const override;
	virtual const rtti::IType* GetType() const override;
	virtual String GetValue() const override;
	virtual Uint32 GetAttributes() const override;
	virtual const void* GetRaw() const override;
	virtual String GetTypeName() const override;

private:
	const String m_name;
	const void* const m_object = nullptr;
	const String m_nativeTypeName;
	const Uint32 m_elementSize = 0;
	const Uint32 m_elementsCount = 0;
	dbgutils::TypeInfo m_elementTypeInfo;
};

IVariablePtr CreateVariable( const String& name, const void* const variableObject, const Uint32 offset, const dbgutils::TypeInfo& typeInfo )
{
	if( typeInfo.m_pointerCount > 0 )
	{
		const void* object = red::OffsetPtr( variableObject, offset );
		for( Uint32 i = 0; i < typeInfo.m_pointerCount; ++i )
		{
			if( object ) { object = *static_cast< const void* const* >( object ); }
		}
		if( typeInfo.m_primitiveType )
		{
			return red::CreateUniquePtr< NativePrimitivePointerMember >( name, object, typeInfo.m_name, typeInfo.m_pointerCount );
		}
		else if( typeInfo.m_enumType )
		{
			// TODO: Handle pointer to enum
			return nullptr;
		}
		else
		{
			return red::CreateUniquePtr< NativeClassPointerMember >( name, object, typeInfo.m_name, typeInfo.m_pointerCount );
		}
	}
	else if( typeInfo.m_arrayType )
	{
		return red::CreateUniquePtr< NativeCArrayMember >( name, red::OffsetPtr( variableObject, offset ), typeInfo.m_name, typeInfo );
	}
	else if( typeInfo.m_enumType )
	{
		return red::CreateUniquePtr< NativeEnumMember >( name, red::OffsetPtr( variableObject, offset ),typeInfo.m_name, typeInfo.m_size );
	}
	else if( typeInfo.m_primitiveType )
	{
		return red::CreateUniquePtr< NativePrimitiveMember >( name, red::OffsetPtr( variableObject, offset ), typeInfo.m_name );
	}
	else
	{
		return red::CreateUniquePtr< NativeClassMember >( name, red::OffsetPtr( variableObject, offset ), typeInfo.m_name );
	}
}

// ==========================================================================================================================

Object::Object( const void* object, const rtti::ClassType* classType )
	: m_object( object )
	, m_class( classType )
{
}

Object::Object( const ISerializable* object )
	: Object( object, object->GetClass() )
{
#ifndef RED_CONFIGURATION_FINAL
	m_nativeTypeName = object->GetNativeTypeName();
#endif
}

red::DynArray< IVariablePtr > Object::EnumerateChildren()
{
	red::DynArray< IVariablePtr > children{ red::PoolDebug() };
	children.Reserve( m_class->GetLocalProperties().Size() + 1 );

	if( m_nativeTypeName )
	{
		children.PushBack( red::CreateUniquePtr< NativeClassView >( m_object, m_nativeTypeName ) );
	}

	AddBaseClass( children );
	EnumProperties( children );

	return children;
}

IVariablePtr Object::FindChild( const red::StringView& name )
{
	if( m_nativeTypeName && name == "__native__" )
	{
		return red::CreateUniquePtr< NativeClassView >( m_object, m_nativeTypeName );
	}

	if( name == "base" )
	{
		const rtti::ClassType* baseClass = GetBaseClass();

		if ( !baseClass )
			return nullptr;

		return red::CreateUniquePtr< AliasedObject >( m_object, baseClass, "base" );
	}

	const rtti::Property* property = m_class->FindProperty( RED_NAME_NOREG( name ) );

	if( property )
	{
		const void* data = property->GetOffsetPtr( m_object );
		return red::CreateUniquePtr< Property >( property, data );
	}

	return nullptr;
}

const rtti::IType* Object::GetType() const
{
	return m_class;
}

String Object::GetValue() const
{
	return String::Printf( "0x%p", m_object );
}

Uint32 Object::GetAttributes() const
{
	const rtti::ClassType* baseClass = GetBaseClass();
	Bool hasBaseClass = baseClass != nullptr;

	const Uint32 isExpandableFlag = ( hasBaseClass || m_class->GetLocalProperties().Size() > 0 ) ? RED_FLAG( Attributes::IsExpandable ) : 0;
	const Uint32 isBaseClassFlag = !hasBaseClass ? RED_FLAG( Attributes::TypeBaseClass ) : 0;
	const Uint32 isMostDerivedClassFlag = ( m_class->IsA( ISerializable::GetStaticClass() ) && static_cast< const ISerializable* >( m_object )->GetClass() == m_class ) ? RED_FLAG( Attributes::TypeMostDerivedClass ) : 0;
	const Uint32 isInnerClassFlag = ( isBaseClassFlag + isMostDerivedClassFlag == 0 ) ? RED_FLAG( Attributes::TypeInnerClass ) : 0;
	const Uint32 isAbstractFlag = m_class->IsAbstract() ? RED_FLAG( Attributes::Abstract ) : 0;
	return RED_FLAG( Attributes::ReadOnly ) | isExpandableFlag | isBaseClassFlag | isMostDerivedClassFlag | isInnerClassFlag | isAbstractFlag;
}

const void* Object::GetRaw() const
{
	return m_object;
}

String Object::GetTypeName() const
{
	if( m_class )
	{
		// Make sure we are displaying script alias. The function below returns the native name if no script alias is available.
		return rtti::ITypeSystem::GetInstance().NativeNameToScriptAlias( m_class->GetName() ).AsChar();
	}
	return TBaseClass::GetTypeName();
}

const rtti::ClassType* Object::GetBaseClass() const
{
	return m_class ? m_class->GetBaseClass() : nullptr;
}

void Object::AddBaseClass( red::DynArray< IVariablePtr >& children ) const
{
	const rtti::ClassType* baseClassType = GetBaseClass();

	if( baseClassType )
	{
		IVariablePtr ptr = red::CreateUniquePtr< AliasedObject >( m_object, baseClassType, "base" );
		children.PushBack( std::move( ptr ) );
	}
}

void Object::EnumProperties( red::DynArray< IVariablePtr >& children ) const
{
	const red::DynArray< const rtti::Property* >& properties = m_class->GetLocalProperties();

	for ( const rtti::Property* property : properties )
	{
		const void* data = property->GetOffsetPtr( m_object );

		IVariablePtr ptr = red::CreateUniquePtr< Property >( property, data );
		children.PushBack( std::move( ptr ) );
	}
}

AliasedObject::AliasedObject( const ISerializable* object, const AnsiChar* alias )
	: Object( object )
	, m_alias( alias )
{
}

AliasedObject::AliasedObject( const ISerializable* object, const String& alias )
	: Object( object )
	, m_alias( alias )
{
}

AliasedObject::AliasedObject( const void* object, const rtti::ClassType* classType, const AnsiChar* alias )
	: Object( object, classType )
	, m_alias( alias )
{
}

AliasedObject::AliasedObject( const void* object, const rtti::ClassType* classType, const String& alias )
	: Object( object, classType )
	, m_alias( alias )
{
}

String AliasedObject::GetName() const
{
	return m_alias;
}

// ==========================================================================================================================

NativeClassView::NativeClassView( const void* const object, const String& nativeTypeName )
	: m_object( object )
	, m_nativeTypeName( nativeTypeName )
{
}

red::DynArray< IVariablePtr > NativeClassView::EnumerateChildren()
{
	red::DynArray< IVariablePtr > children = { red::PoolDebug() };

#ifndef RED_CONFIGURATION_FINAL

	if( m_object == nullptr )
	{
		return children;
	}

	// dbgutils::ClassInfo is about 500 KB heavy, it is not good idea to put it on stack (crash in __chkstk)
	red::UniquePtr< dbgutils::ClassInfo, red::PoolDebug > classInfo = red::CreateUniquePtr< dbgutils::ClassInfo, red::PoolDebug >();
	dbgutils::GetClassInformation( m_nativeTypeName.AsChar(), *classInfo );

	for( Uint32 i = 0; i < classInfo->m_baseClassesCount; ++i )
	{
		const dbgutils::ClassInfo::BaseClassInfo& baseClass = classInfo->m_baseClasses[ i ];
		children.PushBack( CreateBaseClass( i, baseClass ) );
	}

	for( Uint32 i = 0; i < classInfo->m_membersCount; ++i )
	{
		const dbgutils::ClassInfo::MemberInfo& member = classInfo->m_members[ i ];
		children.PushBack( CreateMember( member ) );
	}

	if( m_nativeTypeName.BeginsWith( "THandle<" ) && m_nativeTypeName.EndsWith( ">" ) )
	{
		const auto data = static_cast< const THandle< ISerializable >* >( m_object );
		if( data->Get() )
		{
			children.PushBack( red::CreateUniquePtr< NativeClassMember >( "__object__", data->Get(), data->Get()->GetNativeTypeName() ) );
		}
	}

	if( m_nativeTypeName.BeginsWith( "WeakHandle<" ) && m_nativeTypeName.EndsWith( ">" ) )
	{
		const auto data = static_cast< const WeakHandle< ISerializable >* >( m_object );
		if( data->ToHandle().Get() )
		{
			children.PushBack( red::CreateUniquePtr< NativeClassMember >( "__object__", data->ToHandle().Get(), data->ToHandle().Get()->GetNativeTypeName() ) );
		}
	}

	if( m_nativeTypeName.BeginsWith( "red::DynArray<" ) && m_nativeTypeName.EndsWith( ">" ) )
	{
		const auto data = static_cast< const red::DynArrayAccessor* >( m_object );

		String elementNativeTypeName = m_nativeTypeName.MidString( String( "red::DynArray<" ).Length() );
		elementNativeTypeName = elementNativeTypeName.MidString( 0, elementNativeTypeName.Length() - 1 ).TrimRight( ' ' );

		dbgutils::TypeInfo elementTypeInfo;
		dbgutils::GetTypeInformation( elementNativeTypeName.AsChar(), elementTypeInfo );

		if( elementTypeInfo.m_size != 0 )
		{
			for( Uint32 i = 0; i < data->Size(); ++i )
			{
				children.PushBack( CreateVariable( String::Printf( "[%u]", i ), data->Data(), i * elementTypeInfo.m_size, elementTypeInfo ) );
			}
		}
	}

#ifdef RED_EVENT_SENDER_INFO_AVAILABLE
	if( m_nativeTypeName == "red::Event::SenderInfo::CodeStacktrace" )
	{
		class CodeStacktraceEntry : public IVariable
		{
		public:
			CodeStacktraceEntry( const Uint32 index, const String& value ) : m_index( index ), m_value( value ) {}

			virtual String GetName() const override { return String::Printf( "[%u]", m_index ); }
			virtual const rtti::IType* GetType() const override { return nullptr; };
			virtual String GetValue() const override { return m_value; };

		private:
			Uint32 m_index = 0;
			String m_value;
		};

		const auto data = static_cast< const red::Event::SenderInfo::CodeStacktrace* >( m_object );

		for( Uint32 currentFrame = 0; currentFrame < data->m_framesCount; ++currentFrame )
		{
			dbgutils::Address address;
			dbgutils::LineInfo lineInfo = { { 0 }  };
			dbgutils::SymbolInfo symbolInfo = { { 0 } };
			address.m_absoluteVirtualAddress = reinterpret_cast< Uint64 >( data->m_frames[ currentFrame ] );
			if( dbgutils::GetSymbolInfo( address, symbolInfo ) && dbgutils::GetLineInfo( address, lineInfo ) )
			{
				const String value = String::Printf( "(C++) %s(...) Line %u", symbolInfo.m_name, lineInfo.m_lineNumber );
				children.PushBack( red::CreateUniquePtr< CodeStacktraceEntry >( currentFrame, value ) );
			}
		}
	}
#endif // #ifdef RED_EVENT_SENDER_INFO_AVAILABLE

#endif // #ifndef RED_CONFIGURATION_FINAL

	return children;
}

IVariablePtr NativeClassView::FindChild( const red::StringView& name )
{
#ifndef RED_CONFIGURATION_FINAL

	if( m_object == nullptr )
	{
		return nullptr;
	}

	// dbgutils::ClassInfo is about 500 KB heavy, it is not good idea to put it on stack (crash in __chkstk)
	red::UniquePtr< dbgutils::ClassInfo, red::PoolDebug > classInfo = red::CreateUniquePtr< dbgutils::ClassInfo, red::PoolDebug >();
	dbgutils::GetClassInformation( m_nativeTypeName.AsChar(), *classInfo );

	if( name.StartsWith( "__base_" ) )
	{
		const Uint32 baseIndex = name[ sizeof( "__base_" ) - 1 ] - '0'; // TODO: Handle more than 10 base classes
		const dbgutils::ClassInfo::BaseClassInfo& baseClass = classInfo->m_baseClasses[ baseIndex ];
		return CreateBaseClass( baseIndex, baseClass );
	}

	for( Uint32 i = 0; i < classInfo->m_membersCount; ++i )
	{
		const dbgutils::ClassInfo::MemberInfo& member = classInfo->m_members[ i ];
		if( name == member.m_name )
		{
			return CreateMember( member );
		}
	}

	if( m_nativeTypeName.BeginsWith( "THandle<" ) && m_nativeTypeName.EndsWith( ">" ) )
	{
		const auto data = static_cast< const THandle< ISerializable >* >( m_object );
		if( data->Get() )
		{
			return red::CreateUniquePtr< NativeClassMember >( "__object__", data->Get(), data->Get()->GetNativeTypeName() );
		}
	}

	if( m_nativeTypeName.BeginsWith( "WeakHandle<" ) && m_nativeTypeName.EndsWith( ">" ) )
	{
		const auto data = static_cast< const WeakHandle< ISerializable >* >( m_object );
		if( data->ToHandle().Get() )
		{
			return red::CreateUniquePtr< NativeClassMember >( "__object__", data->ToHandle().Get(), data->ToHandle().Get()->GetNativeTypeName() );
		}
	}

	if( m_nativeTypeName.BeginsWith( "red::DynArray<" ) && m_nativeTypeName.EndsWith( ">" ) )
	{
		const auto data = static_cast< const red::DynArrayAccessor* >( m_object );

		Uint32 index = 0;
		if( red::StringToInt( index, name.ToString().AsChar(), nullptr, red::BaseTen ) )
		{
			if( index >= data->Size() )
			{
				return nullptr;
			}

			String elementNativeTypeName = m_nativeTypeName.MidString( String( "red::DynArray<" ).Length() );
			elementNativeTypeName = elementNativeTypeName.MidString( 0, elementNativeTypeName.Length() - 1 ).TrimRight( ' ' );

			dbgutils::TypeInfo elementTypeInfo;
			dbgutils::GetTypeInformation( elementNativeTypeName.AsChar(), elementTypeInfo );

			if( elementTypeInfo.m_size != 0 )
			{
				return CreateVariable( String::Printf( "[%u]", index ), data->Data(), index * elementTypeInfo.m_size, elementTypeInfo );
			}
		}
	}

#endif // #ifndef RED_CONFIGURATION_FINAL

	return nullptr;
}

IVariablePtr NativeClassView::CreateBaseClass( const Uint32 index, const dbgutils::ClassInfo::BaseClassInfo& baseClass )
{
	return red::CreateUniquePtr< NativeClassBase >( index, red::OffsetPtr( m_object, baseClass.m_offset ), baseClass.m_name );
}

IVariablePtr NativeClassView::CreateMember( const dbgutils::ClassInfo::MemberInfo& member )
{
	return CreateVariable( member.m_name, m_object, member.m_offset, member.m_typeInfo );
}

String NativeClassView::GetName() const
{
	return "__native__";
}

const rtti::IType* NativeClassView::GetType() const
{
	return nullptr;
}

String NativeClassView::GetValue() const
{
	if( m_nativeTypeName == "red::String" )
	{
		const auto data = static_cast< const red::String* >( m_object );
		return "\"" + ( *data ) + "\"";
	}
	else if( m_nativeTypeName == "CName" )
	{
		const auto data = static_cast< const CName* >( m_object );
		return "'" + String( data->AsChar() ) + "'";
	}
	else if( m_nativeTypeName == "game::data::TweakDBID" )
	{
		const auto data = static_cast< const game::data::TweakDBID* >( m_object );
		return "'" + data->ToString() + "'";
	}
	else if( m_nativeTypeName == "Vector4" || m_nativeTypeName == "math::Vector4" )
	{
		const auto data = static_cast< const math::Vector4* >( m_object );
		return String::Printf( "{ x: %f, y: %f, z: %f, w: %f }", data->X, data->Y, data->Z, data->W );
	}
	else if( m_nativeTypeName == "Vector3" || m_nativeTypeName == "math::Vector3" )
	{
		const auto data = static_cast< const math::Vector3* >( m_object );
		return String::Printf( "{ x: %f, y: %f, z: %f }", data->X, data->Y, data->Z );
	}
	else if( m_nativeTypeName == "Vector2" || m_nativeTypeName == "math::Vector2" )
	{
		const auto data = static_cast< const math::Vector2* >( m_object );
		return String::Printf( "{ x: %f, y: %f }", data->X, data->Y );
	}
	else if( m_nativeTypeName == "Quaternion" || m_nativeTypeName == "math::Quaternion" )
	{
		const auto data = static_cast< const math::Quaternion* >( m_object );
		return String::Printf( "{ i: %f, j: %f, k: %f, r: %f }", data->i, data->j, data->k, data->r );
	}
#ifdef RED_EVENT_SENDER_INFO_AVAILABLE
	else if( m_nativeTypeName == "red::Event::SenderInfo::ScriptsStacktrace" )
	{
		const auto data = static_cast< const red::Event::SenderInfo::ScriptsStacktrace* >( m_object );
		if( data->m_class && data->m_function )
		{
			return String::Printf( "(Script) %s::%s(...) Line: %u", data->m_class, data->m_function, data->m_line );
		}
		else if( data->m_function )
		{
			return String::Printf( "(Script) %s(...) Line: %u", data->m_function, data->m_line );
		}
		else
		{
			return String();
		}
	}
#endif // #ifdef RED_EVENT_SENDER_INFO_AVAILABLE

	return String::Printf( "0x%p", m_object );
}

Uint32 NativeClassView::GetAttributes() const
{
	return RED_FLAG( Attributes::ReadOnly ) | RED_FLAG( Attributes::IsExpandable );
}

const void* NativeClassView::GetRaw() const
{
	return nullptr;
}

String NativeClassView::GetTypeName() const
{
	return m_nativeTypeName;
}

// ==========================================================================================================================

NativeClassBase::NativeClassBase( const Uint32 index, const void* const object, const String& nativeTypeName )
	: NativeClassView( object, nativeTypeName )
	, m_index( index )
{

}

String NativeClassBase::GetName() const
{
	return String::Printf( "__base_%d__", m_index );
}

// ==========================================================================================================================

NativeEnumMember::NativeEnumMember( const String &name, const void* const object, const String& nativeTypeName, const Uint32 size )
	: NativePrimitiveMember( name, object, nativeTypeName )
	, m_size( size )
{
}

String NativeEnumMember::GetValue() const
{
	if( m_size == 1 ) return String::Printf( "%u", *static_cast< const Uint8* >( m_object ) );
	if( m_size == 2 ) return String::Printf( "%u", *static_cast< const Uint16* >( m_object ) );
	if( m_size == 4 ) return String::Printf( "%u", *static_cast< const Uint32* >( m_object ) );
	if( m_size == 8 ) return String::Printf( "%llu", *static_cast< const Uint64* >( m_object ) );
	return "Invalid enum type";
}

// ==========================================================================================================================

NativeClassMember::NativeClassMember( const String& name, const void* const object, const String& nativeTypeName )
	: NativeClassView( object, nativeTypeName )
	, m_name( name )
{
}

String NativeClassMember::GetName() const
{
	return m_name;
}

// ==========================================================================================================================

NativePrimitivePointerMember::NativePrimitivePointerMember( const String& name, const void* const object, const String& nativeTypeName, const Uint32 pointersNumber )
	: NativePrimitiveMember( name, object, nativeTypeName )
	, m_pointersNumber( pointersNumber )
{
}

String NativePrimitivePointerMember::GetTypeName() const
{
	String result = NativePrimitiveMember::GetTypeName();
	for( Uint32 i = 0; i < m_pointersNumber; ++i )
	{
		result += '*';
	}
	return result;
}

String NativePrimitivePointerMember::GetValue() const
{
	if( m_object )
	{
		return String::Printf( "0x%p", m_object );
	}
	else
	{
		return "nullptr";
	}
}

Uint32 NativePrimitivePointerMember::GetAttributes() const
{
	if( m_object )
	{
		return RED_FLAG( Attributes::ReadOnly ) | RED_FLAG( Attributes::IsExpandable );
	}
	else
	{
		return RED_FLAG( Attributes::ReadOnly );
	}
}

// ==========================================================================================================================

NativeClassPointerMember::NativeClassPointerMember( const String& name, const void* const object, const String& nativeTypeName, const Uint32 pointersNumber )
	: NativeClassMember( name, object, nativeTypeName )
	, m_pointersNumber( pointersNumber )
{
}

String NativeClassPointerMember::GetTypeName() const
{
	String result = NativeClassView::GetTypeName();
	for( Uint32 i = 0; i < m_pointersNumber; ++i )
	{
		result += '*';
	}
	return result;
}

String NativeClassPointerMember::GetValue() const
{
	if( m_object )
	{
		return String::Printf( "0x%p", m_object );
	}
	else
	{
		return "nullptr";
	}
}

Uint32 NativeClassPointerMember::GetAttributes() const
{
	if( m_object )
	{
		return RED_FLAG( Attributes::ReadOnly ) | RED_FLAG( Attributes::IsExpandable );
	}
	else
	{
		return RED_FLAG( Attributes::ReadOnly );
	}
}

// ==========================================================================================================================


NativePrimitiveMember::NativePrimitiveMember( const String& name, const void* const object, const String& nativeTypeName )
	: m_name( name )
	, m_object( object )
	, m_nativeTypeName( nativeTypeName )
{
}

red::DynArray< IVariablePtr > NativePrimitiveMember::EnumerateChildren()
{
	return red::DynArray< IVariablePtr >{ red::PoolDebug() };
}

IVariablePtr NativePrimitiveMember::FindChild( const red::StringView& name )
{
	return nullptr;
}

String NativePrimitiveMember::GetName() const
{
	return m_name;
}

const rtti::IType* NativePrimitiveMember::GetType() const
{
	return nullptr;
}

String NativePrimitiveMember::GetValue() const
{
	if( m_nativeTypeName == "char" )
	{
		return String::Printf( "%c (%d)", *static_cast< const char* >( m_object ), static_cast< int >( *static_cast< const char* >( m_object ) ) );
	}
	else if( m_nativeTypeName == "wchar_t" )
	{
		return String::Printf( "(%d)", static_cast< int >( *static_cast< const wchar_t* >( m_object ) ) );
	}
	else if( m_nativeTypeName == "int" )
	{
		return String::Printf( "%d", *static_cast< const int* >( m_object ) );
	}
	else if( m_nativeTypeName == "unsigned int" )
	{
		return String::Printf( "%u", *static_cast< const unsigned int* >( m_object ) );
	}
	else if( m_nativeTypeName == "float" )
	{
		return String::Printf( "%f", *static_cast< const float* >( m_object ) );
	}
	else if( m_nativeTypeName == "double" )
	{
		return String::Printf( "%f", *static_cast< const double* >( m_object ) );
	}
	else if( m_nativeTypeName == "bool" )
	{
		return String::Printf( "%s", ( *static_cast< const bool* >( m_object ) ) ? "true" : "false" );
	}
	else if( m_nativeTypeName == "long" )
	{
		return String::Printf( "%ld", *static_cast< const long* >( m_object ) );
	}
	else if( m_nativeTypeName == "unsigned long" )
	{
		return String::Printf( "%lu", *static_cast< const unsigned long* >( m_object ) );
	}

	return "unknown type";
}

Uint32 NativePrimitiveMember::GetAttributes() const
{
	return 0;
}

const void* NativePrimitiveMember::GetRaw() const
{
	return nullptr;
}

String NativePrimitiveMember::GetTypeName() const
{
	return m_nativeTypeName;
}

// ==============

NativeCArrayMember::NativeCArrayMember( const String& name, const void* const object, const String& nativeTypeName, const dbgutils::TypeInfo& typeInfo )
	: m_name( name )
	, m_object( object )
	, m_nativeTypeName( nativeTypeName )
	, m_elementSize( typeInfo.m_size )
	, m_elementsCount( typeInfo.m_arrayLength / typeInfo.m_size )
{
	m_elementTypeInfo = typeInfo;
	m_elementTypeInfo.m_arrayType = false;
	m_elementTypeInfo.m_arrayLength = 0;
}

red::DynArray< IVariablePtr > NativeCArrayMember::EnumerateChildren()
{
	red::DynArray< IVariablePtr > children = { red::PoolDebug() };
	for( Uint32 i = 0; i < m_elementsCount; ++i )
	{
		children.PushBack( CreateVariable( String::Printf( "[%u]", i ), red::OffsetPtr( m_object, m_elementSize * i ), 0, m_elementTypeInfo ) );
	}
	return children;
}

IVariablePtr NativeCArrayMember::FindChild( const red::StringView& name )
{ 
	Uint32 index = 0;
	if( red::StringToInt( index, name.ToString().AsChar(), nullptr, red::BaseTen ) )
	{
		if( index >= m_elementsCount )
		{
			return nullptr;
		}
		return CreateVariable( String::Printf( "[%u]", index ), red::OffsetPtr( m_object, m_elementSize * index ), 0, m_elementTypeInfo );
	}
	return nullptr;
}

String NativeCArrayMember::GetName() const
{
	return m_name;
}

const rtti::IType* NativeCArrayMember::GetType() const
{
	return nullptr;
}

String NativeCArrayMember::GetValue() const
{
	return String::Printf( "0x%p", m_object );
}

Uint32 NativeCArrayMember::GetAttributes() const
{
	return RED_FLAG( Attributes::ReadOnly ) | RED_FLAG( Attributes::IsExpandable );
}

const void* NativeCArrayMember::GetRaw() const
{
	return nullptr;
}

String NativeCArrayMember::GetTypeName() const
{
	return String::Printf( "%s[%u]", m_nativeTypeName.AsChar(), m_elementsCount );
}

} } // namespace script { namespace debug {
