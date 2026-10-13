/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "../../redContainers/include/string/string.h"
#include "../../redReflection/include/handle.h"
#include "../../redReflection/include/serializationBinaryStructureMapper.h"
#include "../include/jsonDataBuilder.h"

namespace serialization
{
	namespace json
	{
		class JsonTextWriter
		{
		public:
			typedef AnsiChar Ch;

			JsonTextWriter( String& text )
				: m_text( text )
			{
			}

			void Put( const AnsiChar ch )
			{
				m_text += ch;
			}

			void Flush()
			{
			}

			const String& GetJsonText()
			{
				return m_text;
			}

		private:
			String& m_text;
		};

		using JsonWriter = rapidjson::PrettyWriter< JsonTextWriter, rapidjson::UTF8< AnsiChar >, rapidjson::UTF8< AnsiChar >, res::json::JSONAllocator >;
		using CustomSerializeFunction = std::function< Bool( JsonWriter&, const rtti::IType*, void* ) >;

		//////////////////////////////////////////////////////////////////////////
		class RED_RESOURCE_API JsonSerializerContext
		{
		public:
			JsonSerializerContext();

			Bool m_serializeDefaultValue;
			Bool m_generateExtendedTypeInfo;
			AnsiChar m_indentChar;
			Uint32 m_indentCharCount;
			Uint32 m_maxFloatDecimalPlaces;
			CustomSerializeFunction m_customSerializeFunction;
			Bool m_magicFlagForAudioDefaultObject; // Should rtti::ClassType::GetDefaultObject be used for serialization? External audio tools depend on DefaultObject definition format.
		};

		//////////////////////////////////////////////////////////////////////////

		class RED_RESOURCE_API JsonSerializer final
		{
		public:
			JsonSerializer( THandle<ISerializable> object );

			Bool Serialize( String& result, Bool serializeDefaultValue = false, Bool generateExtendedTypeInfo = false, CustomSerializeFunction customSerializeFunction = CustomSerializeFunction() );
			Bool Serialize( String& result, const JsonSerializerContext& context );


			/// Use to serialise non iserializable rtti class definitions
			Bool SerializeClassDefinition( String& result, const rtti::ClassType* type );

		private:
			struct SerializationContext
			{
				JsonWriter& builder;
				const rtti::IType* valueType;
				void* valueData;

				const void* defaultData;
				const rtti::ClassType* defaultDataClass;
			};

			Bool SerializeExportTable( JsonWriter& builder );
			void SerializeImportTable( JsonWriter& builder );

			Bool SerializeObject( JsonWriter& builder, THandle<ISerializable> object );
			Bool SerializeObjectInPlace( JsonWriter &builder, THandle<ISerializable> object );

			Bool IsTypeSupported( const rtti::IType* propType );

			Bool SerializeValue( const SerializationContext& context );

			Bool SerializeFundamentalValue( const SerializationContext& context );
			Bool SerializeSimpleValue( const SerializationContext& context );
			Bool SerializeEnumValue( const SerializationContext& context );
			Bool SerializeClassValue( const SerializationContext& context );
			Bool SerializeDynArrayValue( const SerializationContext& context );
			Bool SerializeStaticArrayValue( const SerializationContext& context );
			Bool SerializeNativeArrayValue( const SerializationContext& context );
			Bool SerializeHandleValue( const SerializationContext& context );
			Bool SerializeWeakHandleValue( const SerializationContext& context );
			Bool SerializeResourceReferenceValue( const SerializationContext& context );
			Bool SerializeResourceAsyncReferenceValue( const SerializationContext& context );
			Bool SerializeBitFieldValue( const SerializationContext& context );
			Bool SerializeCustomValue( const SerializationContext& context );

			void PreSave( const THandle<ISerializable>& object );
			void PreSave( const red::DynArray<THandle<ISerializable>>& objects );

			serialization::StructureMapper m_mapper;
			THandle<ISerializable> m_root;

			Bool m_generateExtendedTypeInfo;
			CustomSerializeFunction m_customSerializeFunction;
			Bool m_serializeDefaultValue;

			using SerializeFunc = Bool( JsonSerializer::* )( const SerializationContext& );
			static constexpr Int32 s_parseFunctionsCount = RT_Count;

			static_assert( s_parseFunctionsCount == 16, "If count of elements changed then please be sure that array with functions is still valid!" );
			static SerializeFunc s_parseFunctions[ s_parseFunctionsCount ];
		};

	}	// namespace json

}	// namespace res
