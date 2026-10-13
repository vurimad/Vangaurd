/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "jsonSerializer.h"
#include "../../redReflection/include/serializationSaver.h"
#include "../../redReflection/include/resourceAsyncReference.h"
#include "../../redReflection/include/variant.h"

namespace serialization
{
	namespace json
	{
		JsonSerializerContext::JsonSerializerContext()
			: m_serializeDefaultValue( false )
			, m_generateExtendedTypeInfo( false )
			, m_indentChar( ' ' )
			, m_indentCharCount( 4 )
			, m_maxFloatDecimalPlaces( std::numeric_limits<Uint32>::max() )
			, m_customSerializeFunction( CustomSerializeFunction() )
			, m_magicFlagForAudioDefaultObject( false )
		{
		}

		JsonSerializer::SerializeFunc JsonSerializer::s_parseFunctions[ s_parseFunctionsCount ]
		{
			&JsonSerializer::SerializeSimpleValue,					// RT_Name
			&JsonSerializer::SerializeFundamentalValue,				// RT_Fundamental
			&JsonSerializer::SerializeClassValue,					// RT_Class
			&JsonSerializer::SerializeDynArrayValue,				// RT_Array
			&JsonSerializer::SerializeSimpleValue,					// RT_Simple
			&JsonSerializer::SerializeEnumValue,					// RT_Enum
			&JsonSerializer::SerializeStaticArrayValue,				// RT_StaticArray
			&JsonSerializer::SerializeNativeArrayValue,				// RT_NativeArray
			nullptr,												// RT_Pointer - this case is not supported by serializer
			&JsonSerializer::SerializeHandleValue,					// RT_Handle
			&JsonSerializer::SerializeWeakHandleValue,				// RT_WeakHandle
			&JsonSerializer::SerializeResourceReferenceValue,		// RT_ResourceReference
			&JsonSerializer::SerializeResourceAsyncReferenceValue,	// RT_ResourceAsyncReference
			&JsonSerializer::SerializeBitFieldValue,				// RT_BitField
			nullptr
		};

		JsonSerializer::JsonSerializer( THandle<ISerializable> object )
			: m_root( object )
		{
		}

		Bool JsonSerializer::Serialize( String& result, Bool serializeDefaultValue, Bool generateExtendedTypeInfo, CustomSerializeFunction customSerializeFunction /*= CustomSerializeFunc()*/ )
		{
			JsonSerializerContext context;
			context.m_generateExtendedTypeInfo = generateExtendedTypeInfo;
			context.m_customSerializeFunction = customSerializeFunction;
			context.m_serializeDefaultValue = serializeDefaultValue;

			return Serialize( result, context );
		}

		Bool JsonSerializer::Serialize( String& result, const JsonSerializerContext& jsonContext )
		{
			if( !m_root )
			{
				RED_LOG_ERROR( "JsonSerializer: Object to serialize cannot be nullptr!" );
				return false;
			}

			m_generateExtendedTypeInfo = jsonContext.m_generateExtendedTypeInfo;
			m_customSerializeFunction = jsonContext.m_customSerializeFunction;
			m_serializeDefaultValue = jsonContext.m_serializeDefaultValue;

			serialization::SavingContext context( m_root );
			context.m_magicFlagForAudioDefaultObject = jsonContext.m_magicFlagForAudioDefaultObject;
			m_mapper.MapObjects( context );

			JsonTextWriter writer( result );
			JsonWriter builder( writer );

			if ( jsonContext.m_maxFloatDecimalPlaces != std::numeric_limits<Uint32>::max() )
			{
				builder.SetMaxDecimalPlaces( jsonContext.m_maxFloatDecimalPlaces );
			}

			builder.SetIndent( jsonContext.m_indentChar, jsonContext.m_indentCharCount );

			{
				builder.StartObject();

				SerializeImportTable( builder );

				if( !SerializeExportTable( builder ) )
				{
					RED_LOG_ERROR( "JsonSerializer: Cannot serialize export table!" );
					result.Clear();
					return false;
				}

				PreSave( m_root );
				if( !SerializeObjectInPlace( builder, m_root ) )
				{
					RED_LOG_ERROR( "JsonSerializer: Cannot serialize root object!" );
					result.Clear();
					return false;
				}

				builder.EndObject();
			}

			return true;
		}

		void JsonSerializer::SerializeImportTable( JsonWriter& builder )
		{
			if( !m_mapper.m_imports.Empty() )
			{
				builder.String( "ImportTable" );
				builder.StartArray();

				for( auto& importInfo : m_mapper.m_imports )
				{
					builder.String( importInfo.m_path.ToString().AsChar() );
				}

				builder.EndArray();
			}
		}

		Bool JsonSerializer::SerializeExportTable( JsonWriter& builder )
		{
			if( m_mapper.m_exports.Size() > 1 )	// object zero is our root object and it cannot be serialized to export table
			{
				PreSave( m_mapper.m_exports );

				builder.String( "ExportTable" );
				builder.StartArray();

				for( auto& object : m_mapper.m_exports )
				{
					if( object != m_root )
					{
						builder.StartObject();
						if( !SerializeObjectInPlace( builder, object ) )
						{
							RED_LOG_ERROR( "JsonSerializer: Cannot serialize one of objects in export table!" );
							return false;
						}
						builder.EndObject();
					}
				}

				builder.EndArray();
			}

			return true;
		}

		Bool JsonSerializer::SerializeObjectInPlace( JsonWriter &builder, THandle<ISerializable> object )
		{
			const char* typeName = object->GetClass()->GetName().AsChar();

			builder.String( "RootType" );
			builder.String( typeName );

			builder.String( "Data" );
			if( !SerializeObject( builder, object ) )
			{
				RED_LOG_ERROR( "JsonSerializer: Cannot serialize in place object of class: %s!", typeName );
				return false;
			}

			return true;
		}

		Bool JsonSerializer::SerializeObject( JsonWriter& builder, THandle<ISerializable> object )
		{
			SerializationContext context
			{
				builder,
				object->GetClass(),
				object.Get(),
				object->GetBaseObjectData(),
				object->GetBaseObjectClass()
			};
			
			if( !SerializeValue( context ) )
			{
				RED_LOG_ERROR( "JsonSerializer: Cannot serialize object in THandle" );
				return false;
			}

			return true;
		}

		Bool JsonSerializer::IsTypeSupported( const rtti::IType* propType )
		{
			static red::StaticArray<CName, 3> noSupportedTypes
			{
				RED_NAME_CONSTEXPR( "serializationDeferredDataBuffer" ),
				RED_NAME_CONSTEXPR( "DeferredDataBuffer" ),
				RED_NAME_CONSTEXPR( "DataBuffer" )
			};

			const CName& typeName = propType->GetName();
			auto iter = std::find( noSupportedTypes.Begin(), noSupportedTypes.End(), typeName );

			return iter == noSupportedTypes.End();
		}

		bool JsonSerializer::SerializeValue( const SerializationContext& context )
		{
			ERTTITypeType typeDesc = context.valueType->GetType();

			SerializeFunc func = nullptr;
			if( typeDesc < s_parseFunctionsCount )
			{
				func = s_parseFunctions[ typeDesc ];
			}

			if( !func )
			{
				return SerializeCustomValue( context );
			}

			return ( this->*func )( context );
		}

		Bool JsonSerializer::SerializeBitFieldValue( const SerializationContext& context )
		{
			const rtti::BitFieldType* bitfieldType = static_cast< const rtti::BitFieldType* >( context.valueType );

			String currentValue;
			if( !bitfieldType->ToString( context.valueData, currentValue ) )
			{
				RED_LOG_ERROR( "JsonSerializer: Cannot serialize bitfield: %s", bitfieldType->GetName().AsChar() );
				return false;
			}
			context.builder.String( currentValue.AsChar() );

			return true;
		}

		Bool JsonSerializer::SerializeResourceAsyncReferenceValue( const SerializationContext& context )
		{
			TResAsyncRef<CResource> currentValue = *( static_cast< TResAsyncRef<CResource>* >( context.valueData ) );

			if( currentValue.Empty() )
			{
				context.builder.Null();
				return true;
			}

			Int32 index = 0;
			if ( m_mapper.FindImportIndex( currentValue.GetPath(), index ) )
			{
				context.builder.Uint( index - 1 );
				return true;
			}

			RED_LOG_WARNING( "JsonSerializer: Cannot find correct entry in import table: %s.", currentValue.GetPath().ToString().AsChar() );

			context.builder.String( currentValue.GetPath().ToString().AsChar() );

			return true;
		}

		Bool JsonSerializer::SerializeResourceReferenceValue( const SerializationContext& context  )
		{
			TResRef<CResource> currentValue = *( static_cast< TResRef<CResource>* >( context.valueData ) );

			if( currentValue.Empty() )
			{
				context.builder.Null();
				return true;
			}

			Int32 index = 0;
			if ( m_mapper.FindImportIndex( currentValue.GetPath(), index ) )
			{
				context.builder.Uint( index - 1 );
				return true;
			}

			RED_LOG_WARNING( "JsonSerializer: Cannot find correct entry in import table: %s.", currentValue.GetPath().ToString().AsChar() );

			context.builder.String( currentValue.GetPath().ToString().AsChar() );

			return true;
		}

		Bool JsonSerializer::SerializeHandleValue( const SerializationContext& context  )
		{
			THandle<ISerializable> currentValue = *( static_cast< THandle<ISerializable>* >( context.valueData ) );

			if( !currentValue )
			{
				context.builder.Null();
				return true;
			}

			for( Uint32 i = 0; i < m_mapper.m_exports.Size(); ++i )
			{
				if( m_mapper.m_exports[ i ] == currentValue )
				{
					context.builder.Uint( i - 1 );	// because zero element is our root object
					return true;
				}
			}

			// serialization in place
			context.builder.StartObject();
			Bool result = SerializeObjectInPlace( context.builder, currentValue );
			context.builder.EndObject();
			
			return result;
		}

		Bool JsonSerializer::SerializeWeakHandleValue( const SerializationContext& context )
		{
			WeakHandle<ISerializable> currentValue = *( static_cast< WeakHandle<ISerializable>* >( context.valueData ) );

			if( const THandle<ISerializable> handleValue = currentValue.ToHandle() )
			{
				for( Uint32 i = 0; i < m_mapper.m_exports.Size(); ++i )
				{
					if( m_mapper.m_exports[ i ] == handleValue )
					{
						context.builder.Uint( i - 1 );	// because zero element is our root object
						return true;
					}
				}
			}

			context.builder.Null();
			return true;
		}

		Bool JsonSerializer::SerializeNativeArrayValue( const SerializationContext& context )
		{
			const rtti::NativeArrayType* arrayType = static_cast< const rtti::NativeArrayType* >( context.valueType );
			const Uint32 arraySize = arrayType->ArrayGetArraySize( context.valueData );

			context.builder.StartArray();
			for( Uint32 i = 0; i < arraySize; ++i )
			{
				void* elmData = arrayType->ArrayGetArrayElement( context.valueData, i );

				SerializationContext innerContext
				{
					context.builder,
					arrayType->ArrayGetInnerType(),
					elmData,
					nullptr,
					nullptr
				};

				if( !SerializeValue( innerContext ) )
				{
					CName innerTypeName = arrayType->GetInnerType()->GetName();
					RED_LOG_ERROR( "JsonSerializer: Cannot serialize element with index %d in native array of %s type.", i, innerTypeName.AsChar() );
					return false;
				}
			}
			context.builder.EndArray();

			return true;
		}

		Bool JsonSerializer::SerializeStaticArrayValue( const SerializationContext& context  )
		{
			const rtti::StaticArrayType* arrayType = static_cast< const rtti::StaticArrayType* >( context.valueType );
			const Uint32 arraySize = arrayType->ArrayGetArraySize( context.valueData );

			context.builder.StartArray();
			for( Uint32 i = 0; i < arraySize; ++i )
			{
				void* elmData = arrayType->ArrayGetArrayElement( context.valueData, i );

				SerializationContext innerContext
				{
					context.builder,
					arrayType->ArrayGetInnerType(),
					elmData,
					nullptr,
					nullptr
				};

				if( !SerializeValue( innerContext ) )
				{
					CName innerTypeName = arrayType->ArrayGetInnerType()->GetName();
					RED_LOG_ERROR( "JsonSerializer: Cannot serialize element with index %d in StaticArray of %s type.", i, innerTypeName.AsChar() );
					return false;
				}
			}
			context.builder.EndArray();

			return true;
		}

		Bool JsonSerializer::SerializeDynArrayValue( const SerializationContext& context  )
		{
			const rtti::ArrayType* arrayType = static_cast< const rtti::ArrayType* >( context.valueType );
			const Uint32 arraySize = arrayType->GetArraySize( context.valueData );

			context.builder.StartArray();
			for( Uint32 i = 0; i < arraySize; ++i )
			{
				void* elmData = arrayType->GetArrayElement( context.valueData, i );

				SerializationContext innerContext
				{
					context.builder,
					arrayType->ArrayGetInnerType(),
					elmData,
					nullptr,
					nullptr
				};

				if( !SerializeValue( innerContext ) )
				{
					CName innerTypeName = arrayType->GetInnerType()->GetName();
					RED_LOG_ERROR( "JsonSerializer: Cannot serialize element with index %d in DynArray of %s type.", i, innerTypeName.AsChar() );
					return false;
				}
			}
			context.builder.EndArray();

			return true;
		}

		Bool JsonSerializer::SerializeClassValue( const SerializationContext& context )
		{
			JsonWriter& builder = context.builder;

			builder.StartObject();

			const rtti::ClassType* typeDesc = static_cast< const rtti::ClassType* >( context.valueType );

			red::DynArray<const rtti::Property*> properties{ red::PoolEngine() };
			typeDesc->GetProperties( properties );

			for( const rtti::Property* prop : properties )
			{
				// Skip unserializable properties
				if( !prop->IsSerializable() )
				{
					continue;
				}

				// Do not save scripted properties that are not editable (internal scripted properties)
				{
					const Bool isScriptedStruct = typeDesc->IsScriptedStruct();
					const Bool isScriptedProp = prop->IsScripted() || isScriptedStruct;
					if( isScriptedProp && !prop->IsEditable() )
					{
						continue;
					}
				}

				// Skip property if it has the same value as in the default object
				if( !m_serializeDefaultValue )
				{
					if( context.defaultData )
					{
						// Property should be defined in the default class, if it's not defined that we cannot diff it
						if( ( context.defaultDataClass == context.valueData ) || context.defaultDataClass->FindProperty( prop->GetName() ) == prop )
						{
							const void* srcData1 = prop->GetOffsetPtr( context.valueData );
							const void* srcData2 = prop->GetOffsetPtr( context.defaultData );
							if( srcData1 && srcData2 )
							{
								if( prop->GetType()->Compare( srcData1, srcData2, 0 ) )
								{
									continue;
								}
							}
						}
					}
				}

				// serialization
				const rtti::IType* propType = prop->GetType();
				void* propData = prop->GetOffsetPtr( context.valueData );
				const char* propName = prop->GetName().AsChar();
				const char* propTypeName = prop->GetType()->GetName().AsChar();

				if( !IsTypeSupported( propType ) )
				{
					continue;
				}

				builder.String( propName );

				if( m_generateExtendedTypeInfo )
				{
					builder.StartObject();

					builder.String( "_TypeName" );
					builder.String( propTypeName );

#ifndef NO_EDITOR
					builder.String( "_Category" );
					builder.String( prop->GetCategory().AsChar() );

					builder.String( "_IsReadOnly" );
					builder.Bool( prop->IsReadOnly() );

					builder.String( "_IsEditable" );
					builder.Bool( prop->IsEditable() );

					builder.String( "_CustomEditor" );
					builder.String( prop->GetCustomEditorType().AsChar() );

					if( prop->IsInlined() )
					{
						builder.String("_IsInlined");
						builder.Bool( true );
					}

					if ( prop->HasTooltip() )
					{
						builder.String( "_Tooltip" );
						builder.String( prop->GetTooltip().AsChar() );
					}

					if ( prop->IsRanged() )
					{
						builder.String( "_MinValue" );
						builder.Double( prop->GetRangeMin() );
						builder.String( "_MaxValue" );
						builder.Double( prop->GetRangeMax() );
					}

#endif

					if( propType->GetType() == ERTTITypeType::RT_Enum )
					{
						builder.String( "_PossibleValues" );
						builder.StartArray();

						const rtti::EnumType* enumType = static_cast< const rtti::EnumType* >( propType );
						red::DynArray< CName > names = enumType->GetOptions();
						for( const auto& name : names )
						{
							builder.String( name.AsChar() );
						}

						builder.EndArray();
					}
					if (propType->GetType( ) == ERTTITypeType::RT_Array)
					{
						const rtti::ArrayType* arrayType = static_cast< const rtti::ArrayType* >( propType );
						const rtti::IType* arrayInnerType = arrayType->GetInnerType();
						if( arrayInnerType->GetType() == ERTTITypeType::RT_Enum )
						{
							builder.String( "_PossibleValues" );
							builder.StartArray();

							const rtti::EnumType* enumType = static_cast< const rtti::EnumType* >( arrayInnerType );
							red::DynArray< CName > names = enumType->GetOptions();
							for( const auto& name : names )
							{
								builder.String( name.AsChar( ) );
							}
							builder.EndArray( );
						}
					}

					builder.String( "_PropertyValue" );

					const void* propDefaultData = nullptr;
					if ( context.defaultData )
					{
						propDefaultData = prop->GetOffsetPtr( context.defaultData );
					}

					const rtti::ClassType* defaultDataClass = nullptr;
					if ( prop->GetType()->GetType() == RT_Class )
					{
						defaultDataClass = static_cast<const rtti::ClassType*>( prop->GetType() );
					}

					SerializationContext context
					{
						builder,
						propType,
						propData,
						propDefaultData,
						nullptr
					};

					if( !SerializeValue( context ) )
					{
						RED_LOG_ERROR( "JsonSerializer: Property '%s' of '%s' cannot be serialized to JSON.", propName, propTypeName );
						builder.String( "" );
					}

					builder.EndObject();
				}
				else
				{
					const void* propDefaultData = nullptr;
					if ( context.defaultData )
					{
						propDefaultData = prop->GetOffsetPtr( context.defaultData );
					}

					const rtti::ClassType* defaultDataClass = nullptr;
					if ( prop->GetType()->GetType() == RT_Class )
					{
						defaultDataClass = static_cast< const rtti::ClassType* >( prop->GetType() );
					}

					SerializationContext context
					{
						builder,
						propType,
						propData,
						propDefaultData,
						defaultDataClass
					};

					if( !SerializeValue( context ) )
					{
						RED_LOG_ERROR( "JsonSerializer: Property '%s' of '%s' cannot be serialized to JSON.", propName, propTypeName );
						builder.String( "" );
					}
				}
			}

			builder.EndObject();

			return true;
		}

		Bool JsonSerializer::SerializeEnumValue( const SerializationContext& context )
		{
			const rtti::EnumType* enumType = static_cast< const rtti::EnumType* >( context.valueType );

			String currentValue;
			if( !enumType->ToString( context.valueData, currentValue ) )
			{
				RED_LOG_ERROR( "JsonSerializer: Cannot serialize enum: %s", enumType->GetName().AsChar() );
				return false;
			}
			context.builder.String( currentValue.AsChar() );

			return true;
		}

		Bool JsonSerializer::SerializeSimpleValue( const SerializationContext& context )
		{
			CName propTypeName = context.valueType->GetName();
			auto valueData = context.valueData;
			JsonWriter& builder = context.builder;

			if( propTypeName == GetTypeName<CName>() )
			{
				const CName* currentValue = static_cast< CName* >( valueData );
				auto currentValue_sv = currentValue->AsStringView();
				builder.String(currentValue_sv.Empty() ? "" : currentValue_sv.Data());
				return true;
			}
			else if( propTypeName == GetTypeName<String>() )
			{
				const String& currentValue = *( static_cast< String* >( valueData ) );
				builder.String( currentValue.AsChar() );
				return true;
			}
			else if( propTypeName == GetTypeName<CRUID>() )
			{
				const CRUID& currentValue = *( static_cast< CRUID* >( valueData ) );

				String outputValue;
				if( !ToStringT<CRUID>( outputValue, currentValue ) )
				{
					RED_LOG_ERROR( "JsonSerializer: Cannot convert CRUID to string!" );
					return false;
				}

				builder.String( outputValue.AsChar() );
				return true;
			}
			else if( propTypeName == GetTypeName<CRUIDRef>() )
			{
				const CRUIDRef& currentValue = *( static_cast< CRUIDRef* >( valueData ) );

				String outputValue;
				if( !ToStringT<CRUIDRef>( outputValue, currentValue ) )
				{
					RED_LOG_ERROR( "JsonSerializer: Cannot convert CRUIDRef to string!" );
					return false;
				}

				builder.String( outputValue.AsChar() );
				return true;
			}
			else if( propTypeName == GetTypeName<CGUID>() )
			{
				const CGUID& currentValue = *( static_cast< CGUID* >( valueData ) );

				String outputValue;
				if( !ToStringT<CGUID>( outputValue, currentValue ) )
				{
					RED_LOG_ERROR( "JsonSerializer: Cannot convert CGUID to string!" );
					return false;
				}

				builder.String( outputValue.AsChar() );
				return true;
			}
			else if( propTypeName == GetTypeName<rtti::Variant>() )
			{
				const rtti::Variant& currentValue = *( static_cast< rtti::Variant* >( valueData ) );

				if( currentValue.IsValid() )
				{
					const rtti::IType* innerType = currentValue.GetRTTIType();
					void* innerData = const_cast<void*>( currentValue.GetData() );

					builder.StartObject();
					{
						builder.String( "InnerType" );
						builder.String( innerType->GetName().AsChar() );

						SerializationContext context
						{
							builder,
							innerType,
							innerData,
							nullptr,
							nullptr
						};

						builder.String( "Data" );
						if( !SerializeValue( context ) )
						{
							RED_LOG_WARNING( "JsonSerializer: Cannot serialize object in Variant" );
							builder.Null();
						}
					}
					builder.EndObject();
				}

				return true;
			}
			// NodeRef was added because we support json serialization for VFX events in the scene editor
			else if ( propTypeName == RED_NAME_CONSTEXPR_NOREG( "LocalizationString" ) || propTypeName == RED_NAME_CONSTEXPR_NOREG( "NodeRef" ) )
			{
				String out;
				context.valueType->ToString( valueData, out );
				builder.String( out.AsChar() );
				return true;
			}

			return SerializeCustomValue( context );
		}

		Bool JsonSerializer::SerializeFundamentalValue( const SerializationContext& context )
		{
			CName propTypeName = context.valueType->GetName();
			auto valueData = context.valueData;
			JsonWriter& builder = context.builder;

			if( propTypeName == GetTypeName<Bool>() )
			{
				Bool currentValue = *( static_cast< Bool* >( valueData ) );
				builder.Bool( currentValue );
				return true;
			}
			else if( propTypeName == GetTypeName<Float>() )
			{
				Float currentValue = *(static_cast< Float* >( valueData ));
				builder.Double( currentValue );
				return true;
			}
			else if( propTypeName == GetTypeName<Double>() )
			{
				Double currentValue = *( static_cast< Double* >( valueData ) );
				builder.Double( currentValue );
				return true;
			}
			else if( propTypeName == GetTypeName<Uint8>() )
			{
				Uint8 currentValue = *( static_cast< Uint8* >( valueData ) );
				builder.Uint( currentValue );
				return true;
			}
			else if( propTypeName == GetTypeName<Int8>() )
			{
				Int8 currentValue = *( static_cast< Int8* >( valueData ) );
				builder.Int( currentValue );
				return true;
			}
			else if( propTypeName == GetTypeName<Uint16>() )
			{
				Uint16 currentValue = *( static_cast< Uint16* >( valueData ) );
				builder.Uint( currentValue );
				return true;
			}
			else if( propTypeName == GetTypeName<Int16>() )
			{
				Int16 currentValue = *( static_cast< Int16* >( valueData ) );
				builder.Int( currentValue );
				return true;
			}
			else if( propTypeName == GetTypeName<Uint32>() )
			{
				Uint32 currentValue = *( static_cast< Uint32* >( valueData ) );
				builder.Uint( currentValue );
				return true;
			}
			else if( propTypeName == GetTypeName<Int32>() )
			{
				Int32 currentValue = *( static_cast< Int32* >( valueData ) );
				builder.Int( currentValue );
				return true;
			}
			else if( propTypeName == GetTypeName<Uint64>() )
			{
				Uint64 currentValue = *( static_cast< Uint64* >( valueData ) );
				builder.Uint64( currentValue );
				return true;
			}
			else if( propTypeName == GetTypeName<Int64>() )
			{
				Int64 currentValue = *( static_cast< Int64* >( valueData ) );
				builder.Int64( currentValue );
				return true;
			}

			return SerializeCustomValue( context );
		}

		Bool JsonSerializer::SerializeCustomValue( const SerializationContext& context )
		{
			if ( m_customSerializeFunction )
			{
				if ( !m_customSerializeFunction( context.builder, context.valueType, context.valueData ) )
				{
					RED_LOG_ERROR( "JsonSerializer: JSON file doesn't support property of type: %s", context.valueType->GetName().AsChar() );
					return false;
				}

				return true;
			}

			return false;
		}

		void JsonSerializer::PreSave( const THandle<ISerializable>& object )
		{
			const PreSaveContext context{};
			object->OnPreSave( context );
		}

		void JsonSerializer::PreSave( const red::DynArray<THandle<ISerializable>>& objects )
		{
			for( const THandle<ISerializable>& obj : objects )
			{
				PreSave( obj );
			}
		}

		Bool JsonSerializer::SerializeClassDefinition( String& result, const rtti::ClassType* type )
		{
			JsonTextWriter writer( result );
			JsonWriter builder( writer );

			SerializationContext context
			{
				builder,
				type,
				type->GetDefaultObject(),
				nullptr,
				nullptr
			};

			m_serializeDefaultValue = true;
			m_generateExtendedTypeInfo = true;
			context.builder.StartObject();
			builder.String( "RootType" );
			builder.String( type->GetName().AsChar() );
			builder.String( "Data" );

			if( ! SerializeClassValue( context ) )
			{
				RED_LOG_ERROR( "JsonSerializer: Cannot serialize class definition" );
				return false;
			}
			context.builder.EndObject();
			return true;
		}

	}	// namespace json

}	// namespace res
