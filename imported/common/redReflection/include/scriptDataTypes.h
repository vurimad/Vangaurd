/*
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redContainers/include/fundamentalStringConversion.h"

class RED_REFLECTION_API CScriptDataTypes
{
public:

	static Bool IsBuiltInType( CName name );
	static Bool IsIntegerType( CName name, Uint32* outSize = nullptr );
	static Bool IsFloatingPointType( CName name, Uint32* outSize = nullptr );
	static Uint32 GetIntegerTypeSize( CName name );
	static Uint32 GetFloatingPointTypeSize( CName name );

	static CName TranslateFallbackTypes( CName name );
	static red::StringView TranslateFallbackTypes( red::StringView name );

	static Bool CanParse( CName typeName, const red::String& value );
	static Bool Parse( CName typeName, const red::String& value, void* data );

	static void InitTypes();

private:

	class IValueParser
	{
		RED_USE_MEMORY_POOL( red::PoolEngine );

	public:
		virtual ~IValueParser() = default;
		virtual Bool CanParse( const red::String& value ) const = 0;
		virtual Bool Parse( const red::String& value, void* data ) const = 0;
	};

	typedef red::UniquePtr< IValueParser > ParserPtr;

	template < typename T >
	class ValueParser : public IValueParser
	{
	public:
		virtual Bool CanParse( const red::String& value ) const override final
		{
			T t;
			return Parse( value, &t );
		}
		virtual Bool Parse( const red::String& value, void* data ) const override final
		{
			return ::FromString( value, *reinterpret_cast<T*>( data ) );
		}
	};

	class StringParser : public IValueParser
	{
	public:
		virtual Bool CanParse( const red::String& value ) const override final;
		virtual Bool Parse( const red::String& value, void* data ) const override final;
	};

	class NameParser : public IValueParser
	{
	public:
		virtual Bool CanParse( const red::String& value ) const override final;
		virtual Bool Parse( const red::String& value, void* data ) const override final;
	};

	class TweakDBIDParser : public IValueParser
	{
	public:
		virtual Bool CanParse( const red::String& value ) const override final;
		virtual Bool Parse( const red::String& value, void* data ) const override final;
	};

	class ResRefParser : public IValueParser
	{
	public:
		virtual Bool CanParse( const red::String& value ) const override final;
		virtual Bool Parse( const red::String& value, void* data ) const override final;
	};

	struct BuiltInType
	{
		enum MetaType
		{
			Custom,
			Integer,
			FloatingPoint,
		};

		CName m_name;
		CName m_scriptAlias;
		red::StringView m_nameView;	// to avoid redundant allocations (CName -> StringView) we store names also as StringView
		red::StringView m_scriptAliasView;
		Uint32 m_size;
		MetaType m_metaType;
		ParserPtr m_valueParser;

		BuiltInType( red::StringView name, red::StringView scriptAlias, ParserPtr valueParser );
	};

	typedef red::HashMap< CName, BuiltInType > BuiltInTypes;

	static BuiltInTypes::iterator AddType( red::StringView name, red::StringView scriptAlias = {}, ParserPtr valueParser = nullptr );

	template < typename T >
	RED_INLINE static void AddIntegerType( const red::StringView name, const red::StringView scriptAlias = {} )
	{
		BuiltInTypes::iterator it = AddType( name, scriptAlias, red::CreateUniquePtr< ValueParser< T > >() );
		it.Value().m_metaType = BuiltInType::Integer;
		it.Value().m_size = sizeof( T );
	}

	template < typename T >
	RED_INLINE static void AddFloatingPointType( const red::StringView name, const red::StringView scriptAlias = {} )
	{
		BuiltInTypes::iterator it = AddType( name, scriptAlias, red::CreateUniquePtr< ValueParser< T > >() );
		it.Value().m_metaType = BuiltInType::FloatingPoint;
		it.Value().m_size = sizeof( T );
	}

	static const BuiltInType* FindType( CName name );

	static BuiltInTypes s_builtInTypes;
	static Bool s_typesInitialized;
};
