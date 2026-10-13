/**
* Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
*/
#pragma once

namespace red
{
	enum class TOMLValueType
	{
		INVALID = 0,
		Float,
		Bool,
		String,
		Int64,
		Transform
	};

#define DECLARE_TOML_VALUE_TYPE( type_name )																\
	struct TOMLArray##type_name;																	\
	template<> RED_RESOURCE_API Optional< type_name > TOMLKeyValue::GetValue< type_name >() const;	\
	template<> struct TOMLArrayDerived< type_name > { using Derived = TOMLArray##type_name; };

#define DEFINE_TOML_VALUE_TYPE( typeName, valueFieldName )				\
	template<>															\
	Optional< typeName > TOMLKeyValue::GetValue< typeName >() const		\
	{																	\
		if ( type == TOMLValueType::typeName )							\
		{																\
			return MakeOptional< typeName >( value.valueFieldName );	\
		}																\
		return {};														\
	}																	\
	struct TOMLArray##typeName : public TOMLArray< typeName >			\
	{																	\
		RTTI_DECLARE_TYPE( TOMLArray##typeName );						\
	};																	\
	RTTI_BEGIN_TYPE_IN_NAMESPACE( TOMLArray##typeName, red );			\
	RTTI_PARENT_TYPE( red::TOMLBaseValue );								\
	RTTI_END_TYPE();


	struct RED_RESOURCE_API TOMLBaseValue : public ISerializable
	{
		RTTI_DECLARE_TYPE( TOMLBaseValue );
		RED_USE_MEMORY_POOL( PoolEngine );

		TOMLBaseValue();
		TOMLBaseValue( StringView key );
		virtual ~TOMLBaseValue();

		StringView GetKey() const { return { key }; }

	protected:
		String key;
	};

	struct RED_RESOURCE_API TOMLKeyValue : public TOMLBaseValue
	{
		RTTI_DECLARE_TYPE( TOMLKeyValue );

		TOMLKeyValue();
		TOMLKeyValue( StringView key, StringView value );
		virtual ~TOMLKeyValue();

		TOMLValueType GetType() const { return type; }

		template< typename T >
		Optional< T > GetValue() const { return {}; }

	private:
		union Value
		{
			Float f;
			Bool b;
			Int64 i;
			Transform t;
			String s;

			Value()
				: i( 0 )
			{}

			~Value()
			{} //We handle this explicitly in ~TOMLKeyValue().
		};

		Value value;
		TOMLValueType type = TOMLValueType::INVALID;
	};

	struct RED_RESOURCE_API TOMLTable : public TOMLBaseValue
	{
		RTTI_DECLARE_TYPE( TOMLTable );
		RED_USE_MEMORY_POOL( PoolEngine );

		TOMLTable();
		TOMLTable( StringView name );

		virtual ~TOMLTable();

		ArraySpan< const THandle< const TOMLBaseValue > > GetEntries() const;
		void PushBack( THandle< TOMLBaseValue >&& newEntry );
		void PushBack( const THandle< TOMLBaseValue >& newEntry );

		template< typename T >
		THandle< const T > Find( StringView key ) const
		{
			for ( auto& val : entries )
			{
				if ( val->GetKey() == key )
					return Cast< T >( val );
			}
			return nullptr;
		}

	private:
		DynArray< THandle< const TOMLBaseValue > > entries{ PoolEngine() };

		TOMLTable( const TOMLTable& table );
	};

	template< typename T >
	struct TOMLArray : public TOMLBaseValue
	{
		TOMLArray() = default;
		TOMLArray( StringView name )
			: TOMLBaseValue( name )
		{}

		TOMLArray( TOMLArray< T >&& other )
			: TOMLBaseValue( std::forward< TOMLArray< T > >( other ) )
			, values( std::move( other.values ) )
		{}

		virtual ~TOMLArray() = default;

		ArraySpan< const T > GetValues() const { return { values }; }

		void PushBack( const T& newValue ) { values.PushBack( newValue ); }

	private:
		DynArray< T > values{ PoolEngine() };
	};

	template< typename T >
	struct TOMLArrayDerived
	{
		using Derived = TOMLArray< T >;
	};

	DECLARE_TOML_VALUE_TYPE( Float );
	DECLARE_TOML_VALUE_TYPE( Bool );
	DECLARE_TOML_VALUE_TYPE( String );
	DECLARE_TOML_VALUE_TYPE( Int64 );
	DECLARE_TOML_VALUE_TYPE( Transform );

}

namespace red
{
	struct RED_RESOURCE_API TOMLArrayTable : public TOMLArray< THandle< TOMLTable > >
	{
		RTTI_DECLARE_TYPE( TOMLArrayTable );
		TOMLArrayTable();
		TOMLArrayTable( StringView name );
		virtual ~TOMLArrayTable();

	};

	template<> struct TOMLArrayDerived< THandle< TOMLTable > > { using Derived = TOMLArrayTable; };
}