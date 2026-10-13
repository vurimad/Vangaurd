/*
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "scriptDataTypes.h"
#include "resourceReferenceScriptToken.h"

namespace
{
	constexpr Uint32 c_undefinedSize = 0;
}

//////////////////////////////////////////////////////////////////////////

CScriptDataTypes::BuiltInTypes CScriptDataTypes::s_builtInTypes{ red::PoolEngine() };
Bool CScriptDataTypes::s_typesInitialized = false;

//////////////////////////////////////////////////////////////////////////

Bool CScriptDataTypes::IsBuiltInType( CName name )
{
	return FindType( name ) != nullptr;
}

Bool CScriptDataTypes::IsIntegerType( CName name, Uint32* outSize /* = nullptr */ )
{
	const BuiltInType* type = FindType( name );
	if ( type != nullptr && type->m_metaType == BuiltInType::MetaType::Integer )
	{
		if ( outSize != nullptr )
		{
			RED_ASSERT( type->m_size != c_undefinedSize, "Undefined integer type size" );
			*outSize = type->m_size;
		}
		return true;
	}
	return false;
}

Bool CScriptDataTypes::IsFloatingPointType( CName name, Uint32* outSize /* = nullptr */ )
{
	const BuiltInType* type = FindType( name );
	if ( type != nullptr && type->m_metaType == BuiltInType::MetaType::FloatingPoint )
	{
		if ( outSize != nullptr )
		{
			RED_ASSERT( type->m_size != c_undefinedSize, "Undefined floating-point type size" );
			*outSize = type->m_size;
		}
		return true;
	}
	return false;
}

Uint32 CScriptDataTypes::GetIntegerTypeSize( CName name )
{
	const BuiltInType* type = FindType( name );
	if ( type != nullptr && type->m_metaType == BuiltInType::MetaType::Integer )
	{
		RED_ASSERT( type->m_size != c_undefinedSize, "Undefined integer type size" );
		return type->m_size;
	}
	return 0;
}

Uint32 CScriptDataTypes::GetFloatingPointTypeSize( CName name )
{
	const BuiltInType* type = FindType( name );
	if ( type != nullptr && type->m_metaType == BuiltInType::MetaType::FloatingPoint )
	{
		RED_ASSERT( type->m_size != c_undefinedSize, "Undefined floating-point type size" );
		return type->m_size;
	}
	return 0;
}

//////////////////////////////////////////////////////////////////////////

CName CScriptDataTypes::TranslateFallbackTypes( CName name )
{
	InitTypes();
	for ( BuiltInTypes::iterator it = s_builtInTypes.Begin(), itEnd = s_builtInTypes.End(); it != itEnd; ++it )
	{
		if ( it.Value().m_scriptAlias == name )
		{
			return it.Value().m_name;
		}
	}
	return name;
}

red::StringView CScriptDataTypes::TranslateFallbackTypes( const red::StringView name )
{
	InitTypes();
	for ( BuiltInTypes::iterator it = s_builtInTypes.Begin(), itEnd = s_builtInTypes.End(); it != itEnd; ++it )
	{
		if ( it.Value().m_scriptAliasView == name )
		{
			return it.Value().m_nameView;
		}
	}
	return name;
}

//////////////////////////////////////////////////////////////////////////

Bool CScriptDataTypes::CanParse( CName typeName, const red::String& value )
{
	const BuiltInType* type = FindType( typeName );
	if ( type != nullptr && type->m_valueParser != nullptr )
	{
		return type->m_valueParser->CanParse( value );
	}
	return false;
}

Bool CScriptDataTypes::Parse( CName typeName, const red::String& value, void* data )
{
	const BuiltInType* type = FindType( typeName );
	if ( type != nullptr && type->m_valueParser != nullptr )
	{
		return type->m_valueParser->Parse( value, data );
	}
	return false;
}

//////////////////////////////////////////////////////////////////////////

CScriptDataTypes::BuiltInTypes::iterator CScriptDataTypes::AddType( const red::StringView name, red::StringView scriptAlias /* = {} */, ParserPtr valueParser /* = nullptr */ )
{
	if ( scriptAlias.Empty() )
	{
		scriptAlias = name;
	}

	CName realName = RED_NAME( name );
	return s_builtInTypes.Insert( realName, BuiltInType( name, scriptAlias, std::move( valueParser ) ) ).Iterator();
}

void CScriptDataTypes::InitTypes()
{
	if ( s_typesInitialized )
	{
		return;
	}

	AddType( "Bool", "bool", red::CreateUniquePtr< ValueParser< Bool > >() );
	AddType( "String", "string", red::CreateUniquePtr< StringParser >() );
	AddIntegerType< Int8 >( "Int8" );
	AddIntegerType< Uint8 >( "Uint8", "byte" );
	AddIntegerType< Int16 >( "Int16" );
	AddIntegerType< Uint16 >( "Uint16" );
	AddIntegerType< Int32 >( "Int32", "int" );
	AddIntegerType< Uint32 >( "Uint32" );
	AddIntegerType< Int64 >( "Int64" );
	AddIntegerType< Uint64 >( "Uint64" );
	AddFloatingPointType< Float >( "Float", "float" );
	AddFloatingPointType< Double >( "Double", "double" );
	AddType( "CName", "name", red::CreateUniquePtr< NameParser >() );
	AddType( "Variant", "variant" );
	AddType( "NodeRef" );
	AddType( "LocalizationString" );
	AddType( "CRUID", "ruid" );
	AddType( "CRUIDRef", "ruidref" );
	AddType( "TweakDBID", nullptr, red::CreateUniquePtr< TweakDBIDParser >() );
	AddType( "redResourceReferenceScriptToken", nullptr, red::CreateUniquePtr< ResRefParser >() );

	s_typesInitialized = true;
}

const CScriptDataTypes::BuiltInType* CScriptDataTypes::FindType( CName name )
{
	InitTypes();
	return s_builtInTypes.FindPtr( name );
}

//////////////////////////////////////////////////////////////////////////

CScriptDataTypes::BuiltInType::BuiltInType( const red::StringView name, const red::StringView scriptAlias, ParserPtr valueParser /* = nullptr */ )
	: m_name( RED_NAME( name ) )
	, m_scriptAlias( RED_NAME( scriptAlias ) )
	, m_nameView( name )
	, m_scriptAliasView( scriptAlias )
	, m_size( c_undefinedSize )
	, m_metaType( MetaType::Custom )
	, m_valueParser( std::move( valueParser ) )
{}

//////////////////////////////////////////////////////////////////////////

Bool CScriptDataTypes::StringParser::CanParse( const red::String& value ) const
{
	RED_UNUSED( value );
	return true;
}

Bool CScriptDataTypes::StringParser::Parse( const red::String& value, void* data ) const
{
	*reinterpret_cast<red::String*>( data ) = value;
	return true;
}

//////////////////////////////////////////////////////////////////////////

Bool CScriptDataTypes::NameParser::CanParse( const red::String& value ) const
{
	RED_UNUSED( value );
	return true;
}

Bool CScriptDataTypes::NameParser::Parse( const red::String& value, void* data ) const
{

	*reinterpret_cast< CName* >(data) = RED_NAME( value );
	return true;
}

//////////////////////////////////////////////////////////////////////////

Bool CScriptDataTypes::TweakDBIDParser::CanParse( const red::String& value ) const
{
	RED_UNUSED( value );
	return true;
}

Bool CScriptDataTypes::TweakDBIDParser::Parse( const red::String& value, void* data ) const
{
	*reinterpret_cast< TweakDBID* >( data ) = TweakDBID( value );
	return true;
}

Bool CScriptDataTypes::ResRefParser::CanParse( const red::String& value ) const
{
	RED_UNUSED( value );
	return true;
}

Bool CScriptDataTypes::ResRefParser::Parse( const red::String& value, void* data ) const
{

	*reinterpret_cast< red::ResourceReferenceScriptToken* >( data ) = red::ResourceReferenceScriptToken( value );
	return true;
}