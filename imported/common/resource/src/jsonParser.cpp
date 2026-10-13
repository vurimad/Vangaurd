/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "../../../../external/rapidjson/include/rapidjson/reader.h"
#include "../../../../external/rapidjson/include/rapidjson/error/en.h"
#include "../../redReflection/include/resourceReference.h"
#include "../../redReflection/include/resourceAsyncReference.h"
#include "../../redReflection/include/variant.h"
#include "../../redReflection/include/postLoadContext.h"
#include "../../redContainers/include/fundamentalStringParser.h"
#include "jsonResource.h"
#include "jsonParser.h"
#include "../../redContainers/include/dynArrayAccessor.h"

namespace res
{
	namespace json
	{
		JsonParser::ParseFunc JsonParser::s_parseFunctions[ s_parseFunctionsCount ]
		{
			&JsonParser::ParseSimpleValue,					// RT_Name
			&JsonParser::ParseFundamentalValue,				// RT_Fundamental
			&JsonParser::ParseClassValue,					// RT_Class
			&JsonParser::ParseDynArrayValue,				// RT_Array
			&JsonParser::ParseSimpleValue,					// RT_Simple
			&JsonParser::ParseEnumValue,					// RT_Enum
			&JsonParser::ParseStaticArrayValue,				// RT_StaticArray
			&JsonParser::ParseNativeArrayValue,				// RT_NativeArray
			nullptr,										// RT_Pointer - this case is not supported by parser
			&JsonParser::ParseHandleValue,					// RT_Handle
			&JsonParser::ParseWeakHandleValue,				// RT_WeakHandle
			&JsonParser::ParseResourceReferenceValue,		// RT_ResourceReference
			&JsonParser::ParseResourceAsyncReferenceValue,	// RT_ResourceAsyncReference
			&JsonParser::ParseBitFieldValue,					// RT_BitField
			nullptr
		};

		JsonParser::JsonParser( CustomParseFunction customParseFunction /*= CustomParseFunction() */ )
			: m_doc( red::CreateUniquePtr< JSONDocument, PoolJSON >() )
			, m_exportTableStateIsEnabled( false )
			, m_customParseFunction( customParseFunction )
			, m_exportTable( PoolJSON() )
			, m_importTable( PoolJSON() )
			, m_loadingTokensImportTable( PoolJSON() )
			, m_exportTableDependecies( PoolJSON() )
			, m_exportTableWeakDependecies( PoolJSON() )
			, m_parentStack( PoolJSON() )
			, m_debugName( "<Unknown source>")
		{
		}

		JsonParser::~JsonParser()
		{
		}

		Bool IsSizeFalsePositive( const String& jsonData )
		{
			Int32 value;
			return FromString( jsonData, value );
		}

		Bool JsonParser::ParseJsonFile( String&& jsonData )
		{
			m_jsonData = jsonData;

			// in case this is a regular number, rapid json will magically assume that 
			// 1. it's a valid JSON (which apparently is true)
			// 2. "damn, that must be the size of whatever will follow", wha... what? why?
			if ( IsSizeFalsePositive( jsonData ) )
			{
				RED_LOG_ERROR( "JsonParser '%s': Parsing a single number is not supported in our use cases", m_debugName.AsChar() );
				return false;
			}

			const Uint32 parseFlags = RAPIDJSON_NAMESPACE::kParseCommentsFlag | RAPIDJSON_NAMESPACE::kParseTrailingCommasFlag | RAPIDJSON_NAMESPACE::kParseNanAndInfFlag | RAPIDJSON_NAMESPACE::kParseFullPrecisionFlag;

			m_doc->Parse<parseFlags>( m_jsonData.AsChar(), m_jsonData.Length() );

			if ( m_doc->HasParseError() )
			{
				RED_LOG_ERROR( "JsonParser '%s': Error: %s", m_debugName.AsChar(), GetParseError_En(m_doc->GetParseError()) );
				return false;
			}

			if ( ! m_doc->IsObject() )
			{
				RED_LOG_ERROR( "JsonParser '%s': Error: JSON document does not start with an object!", m_debugName.AsChar() );
				return false;
			}

			return true;
		}

		Bool JsonParser::CreateExportTable()
		{
			const JSONValue& elm = *m_doc;
			RED_FATAL_ASSERT( elm.IsObject(), "JsonParser '%s': Expecting export table to be stored in an object", m_debugName.AsChar() );

			if( elm.HasMember( "ExportTable" ) )
			{
				if( !elm[ "ExportTable" ].IsArray() )
				{
					RED_LOG_ERROR( "JsonParser '%s': Export table must be stored as JSON array of JSON objects.", m_debugName.AsChar() );
					return false;
				}

				const JSONValue& exportTable = elm[ "ExportTable" ];

				m_exportTableStateIsEnabled = true;

				Int32 counter = 0;
				for( auto it = exportTable.Begin(); it != exportTable.End(); ++it, ++counter )
				{
					const JSONValue& innerElm = ( *it );

					if( !innerElm.IsObject() )
					{
						RED_LOG_ERROR( "JsonParser '%s': Value in export table must be a JSON object", m_debugName.AsChar() );
						return false;
					}

					THandle<ISerializable> newObj = ParseSerializableValue( innerElm, false );
					m_exportTable.PushBack( newObj );
				}

				if( !ResolveDependenciesInExportTable() )
				{
					RED_LOG_ERROR( "JsonParser '%s': Error during resolving dependencies in export table", m_debugName.AsChar() );
					return false;
				}
			}

			m_exportTableStateIsEnabled = false;

			return true;
		}

		Bool JsonParser::CreateImportTable()
		{
			const JSONValue& elm = *m_doc;
			RED_FATAL_ASSERT( elm.IsObject(), "JsonParser '%s': Expecting import table to be stored in an object", m_debugName.AsChar() );

			if( elm.HasMember( "ImportTable" ) )
			{
				if( !elm[ "ImportTable" ].IsArray() )
				{
					RED_LOG_ERROR( "JsonParser '%s': Import table must be stored as JSON array of text values.", m_debugName.AsChar() );
					return false;
				}

				const JSONValue& importTable = elm[ "ImportTable" ];

				for( auto it = importTable.Begin(); it != importTable.End(); ++it )
				{
					const JSONValue& innerElm = ( *it );

					if( !innerElm.IsString() )
					{
						RED_LOG_ERROR( "JsonParser '%s': Value in import table must be a resource path", m_debugName.AsChar() );
						m_importTable.PushBack( TResAsyncRef<CResource>() );
					}
					else
					{
						res::ResourcePath resPath = res::ResourcePath::Build( innerElm.GetString() );
						m_importTable.PushBack( TResAsyncRef<CResource>( resPath ) );
					}
				}
			}

			m_loadingTokensImportTable.Resize( m_importTable.Size() );

			return true;
		}

		Bool JsonParser::LoadObjectStructure( const PostLoadContext& context, Bool skipPostLoad /*= false*/ )
		{
			const JSONValue& elm = *m_doc;
			m_root = ParseSerializableValue( elm, true );

			if ( !skipPostLoad )
			{
				PostLoad( m_exportTable, context );
				PostLoad( m_root, context );
			}

			ClearInternalData();

			return m_root != nullptr;
		}

		const red::DynArray<TResAsyncRef<CResource>>& JsonParser::GetDependencies()
		{
			return m_importTable;
		}

		void JsonParser::StoreLoadingToken( Uint32 index, res::ResourceTokenHandle loadingToken )
		{
			if( index < m_loadingTokensImportTable.Size() )
			{
				m_loadingTokensImportTable[ index ] = loadingToken;
				return;
			}

			RED_DATA_ERROR( red::LoggerCategory_Engine, false, "Data error: import table in json file is broken, index = %u", index );
		}

		THandle<ISerializable> JsonParser::ParseSerializableValue( const JSONValue& val, Bool useRoot, const char* optionalTypeName /*= nullptr*/ )
		{
			CName typeName;
			
			if( val.HasMember( "RootType" ) )
			{
				if( val[ "RootType" ].IsString() )
				{
					typeName = RED_NAME( val[ "RootType" ].GetString() );
				}
			}
			else
			{
				// 'optionalTypeName' variable could contain 'handle:' prefix
				// but in this scope 'optionalTypeName' cannot have 'handle:' prefix 
				// so if 'GParseKeyword' won't find prefix and will fail it isn't a problem
				if( GParseKeyword( optionalTypeName, "handle:" ) )
				{
					typeName = RED_NAME( optionalTypeName );
				}
			}

			if( typeName == CName::NONE() )
			{
				RED_LOG_ERROR( "JsonParser '%s': Type name of object in THandle is unknown", m_debugName.AsChar() );
				return nullptr;
			}

			const rtti::ClassType* innerTypeDesc = GetRttiSystem().FindClass( typeName );
			if( !innerTypeDesc )
			{
				return nullptr;
			}

			if ( !innerTypeDesc->IsSerializable() )
			{
				RED_LOG_ERROR( "JsonParser '%s': Type '%s' is not ISerializable", m_debugName.AsChar(), innerTypeDesc->GetName().AsChar() );
				return nullptr;
			}

			THandle<ISerializable> tempObject = ( useRoot ? m_root : nullptr );
			if( tempObject )
			{
				const rtti::ClassType* currentClass = tempObject->GetClass();
				RED_FATAL_ASSERT( currentClass == innerTypeDesc, "Class changed for loaded object. This is no longer a supported behavior" );
			}
			else
			{
				tempObject = innerTypeDesc->CreateHandle<ISerializable>();
			}
			if( !tempObject )
			{
				return nullptr;
			}

			if( !val.HasMember( "Data" ) )
			{
				return nullptr;
			}

			if( val[ "Data" ].IsNull() )
			{
				tempObject = nullptr;
			}
			else
			{
				if( !val[ "Data" ].IsObject() )
				{
					RED_LOG_ERROR( "JsonParser '%s': Object data must be stored as JSON object", m_debugName.AsChar() );
					return nullptr;
				}

				m_parentStack.PushBack( tempObject.Get() );

				if( !ParseValue( val[ "Data" ], innerTypeDesc, tempObject.Get() ) )
				{
					RED_LOG_ERROR( "JsonParser '%s': Creation inner object in THandle was impossible", m_debugName.AsChar() );
				}

				m_parentStack.PopBack();
			}

			return tempObject;
		}

		Bool JsonParser::ParseValue( const JSONValue& elm, const rtti::IType* propType, void* propertyData )
		{
			if( !IsTypeSupported( propType ) )
			{
				return true;
			}

			ERTTITypeType typeDesc = propType->GetType();

			switch ( typeDesc )
			{
			case RT_Class:
				if ( !elm.IsObject() )
				{
					RED_LOG_ERROR( "JsonParser '%s': Property data is compatible only with JSON object value", propType->GetName().AsChar() );
					return false;
				}
				break;

			case RT_Array:
				if ( !elm.IsArray() )
				{
					RED_LOG_ERROR( "JsonParser '%s': Property data is compatible only with JSON array value", propType->GetName().AsChar() );
					return false;
				}
				break;

			default:
				break;
			}

			ParseFunc func = nullptr;
			if( typeDesc < s_parseFunctionsCount )
			{
				func = s_parseFunctions[ typeDesc ];
			}

			if( !func )
			{
				return ParseCustomValue( elm, propType, propertyData );
			}

			return (this->*func)( elm, propType, propertyData );
		}

		Bool JsonParser::ParseFundamentalValue( const JSONValue &elm, const rtti::IType* propType, void* propertyData )
		{
			CName propTypeName = propType->GetName();

			if( propTypeName == GetTypeName<Bool>() )
			{
				if( !elm.IsBool() )
				{
					RED_LOG_ERROR( "JsonParser '%s': Property data is compatible only with JSON boolean value", m_debugName.AsChar() );
					return false;
				}

				Bool parsedData( elm.GetBool() );
				propType->Copy( propertyData, &parsedData );
				return true;
			}
			else if( propTypeName == GetTypeName<Float>() )
			{
				Double currentValue = elm.GetDouble();
				Float parsedData( static_cast< Float >( currentValue ) );
				propType->Copy( propertyData, &parsedData );
				return true;
			}
			else if( propTypeName == GetTypeName<Double>() )
			{
				Double parsedData( elm.GetDouble() );
				propType->Copy( propertyData, &parsedData );
				return true;
			}
			else if( propTypeName == GetTypeName<Uint8>() )
			{
				if( !elm.IsUint() )
				{
					RED_LOG_ERROR( "JsonParser '%s': Property data is compatible only with JSON unsigned integer value", m_debugName.AsChar() );
					return false;
				}

				Uint32 currentValue = elm.GetUint();
				if( currentValue > (Uint32)std::numeric_limits<Uint8>::max() )
				{
					RED_LOG_ERROR( "JsonParser '%s': Value is not in range of value for Uint8", m_debugName.AsChar() );
					return false;
				}

				Uint8 parsedData( static_cast< Uint8 >( elm.GetUint() ) );
				propType->Copy( propertyData, &parsedData );
				return true;
			}
			else if( propTypeName == GetTypeName<Int8>() )
			{
				if( !elm.IsInt() )
				{
					RED_LOG_ERROR( "JsonParser '%s': Property data is compatible only with JSON signed integer value", m_debugName.AsChar() );
					return false;
				}

				Int32 currentValue = elm.GetInt();
				if( currentValue < std::numeric_limits<Int8>::min() || currentValue > std::numeric_limits<Int8>::max() )
				{
					RED_LOG_ERROR( "JsonParser '%s': Value is not in range of value for Int8", m_debugName.AsChar() );
					return false;
				}

				Int8 parsedData( static_cast< Int8 >( elm.GetInt() ) );
				propType->Copy( propertyData, &parsedData );
				return true;
			}
			else if( propTypeName == GetTypeName<Uint16>() )
			{
				if( !elm.IsUint() )
				{
					RED_LOG_ERROR( "JsonParser '%s': Property data is compatible only with JSON unsigned integer value", m_debugName.AsChar() );
					return false;
				}

				Uint32 currentValue = elm.GetUint();
				if( currentValue > std::numeric_limits<Uint16>::max() )
				{
					RED_LOG_ERROR( "JsonParser '%s': Value is not in range of value for Uint16", m_debugName.AsChar() );
					return false;
				}

				Uint16 parsedData( static_cast< Uint16 >( currentValue ) );
				propType->Copy( propertyData, &parsedData );
				return true;
			}
			else if( propTypeName == GetTypeName<Int16>() )
			{
				if( !elm.IsInt() )
				{
					RED_LOG_ERROR( "JsonParser '%s': Property data is compatible only with JSON signed integer value", m_debugName.AsChar() );
					return false;
				}

				Int32 currentValue = elm.GetInt();
				if( currentValue < std::numeric_limits<Int16>::min() || currentValue > std::numeric_limits<Int16>::max() )
				{
					RED_LOG_ERROR( "JsonParser '%s': Value is not in range of value for Int16", m_debugName.AsChar() );
					return false;
				}

				Int16 parsedData( static_cast< Int16 >( elm.GetInt() ) );
				propType->Copy( propertyData, &parsedData );
				return true;
			}
			else if( propTypeName == GetTypeName<Uint32>() )
			{
				if( !elm.IsUint() )
				{
					RED_LOG_ERROR( "JsonParser '%s': Property data is compatible only with JSON unsigned integer value", m_debugName.AsChar() );
					return false;
				}

				Uint32 parsedData( elm.GetUint() );
				propType->Copy( propertyData, &parsedData );
				return true;
			}
			else if( propTypeName == GetTypeName<Int32>() )
			{
				if( !elm.IsInt() )
				{
					RED_LOG_ERROR( "JsonParser '%s': Property data is compatible only with JSON signed integer value", m_debugName.AsChar() );
					return false;
				}

				Int32 parsedData( elm.GetInt() );
				propType->Copy( propertyData, &parsedData );
				return true;
			}
			else if( propTypeName == GetTypeName<Uint64>() )
			{
				if( !elm.IsUint64() )
				{
					RED_LOG_ERROR( "JsonParser '%s': Property data is compatible only with JSON unsigned integer value", m_debugName.AsChar() );
					return false;
				}

				Uint64 parsedData( elm.GetUint64() );
				propType->Copy( propertyData, &parsedData );
				return true;
			}
			else if( propTypeName == GetTypeName<Int64>() )
			{
				if( !elm.IsInt64() )
				{
					RED_LOG_ERROR( "JsonParser '%s': Property data is compatible only with JSON signed integer value", m_debugName.AsChar() );
					return false;
				}

				Int64 parsedData( elm.GetInt64() );
				propType->Copy( propertyData, &parsedData );
				return true;
			}

			return false;
		}

		Bool JsonParser::ParseSimpleValue( const JSONValue &elm, const rtti::IType* propType, void* propertyData )
		{
			CName propTypeName = propType->GetName();

			if( propTypeName == GetTypeName<CName>() )
			{
				if( !elm.IsString() )
				{
					RED_LOG_ERROR( "JsonParser '%s': Property data is compatible only with JSON text value", m_debugName.AsChar() );
					return false;
				}

				CName parsedData = RED_NAME( elm.GetString() );
				propType->Copy( propertyData, &parsedData );
				return true;
			}
			else if( propTypeName == GetTypeName<String>() )
			{
				if( !elm.IsString() )
				{
					RED_LOG_ERROR( "JsonParser '%s': Property data is compatible only with JSON text value", m_debugName.AsChar() );
					return false;
				}

				String parsedData( elm.GetString() );
				propType->Copy( propertyData, &parsedData );

				return true;
			}
			else if( propTypeName == GetTypeName<CRUID>() )
			{
				if( !elm.IsString() )
				{
					RED_LOG_ERROR( "JsonParser '%s': Property data is compatible only with JSON text value", m_debugName.AsChar() );
					return false;
				}

				CRUID parsedData;
				if( !parsedData.FromString( elm.GetString() ) )
				{
					RED_LOG_ERROR( "JsonParser '%s': JSON text value cannot be converted to CRUID value", m_debugName.AsChar() );
					return false;
				}
				propType->Copy( propertyData, &parsedData );

				return true;
			}
			else if( propTypeName == GetTypeName<CRUIDRef>() )
			{
				if( !elm.IsString() )
				{
					RED_LOG_ERROR( "JsonParser '%s': Property data is compatible only with JSON text value", m_debugName.AsChar() );
					return false;
				}

				CRUID parsedData;
				if( !parsedData.FromString( elm.GetString() ) )
				{
					RED_LOG_ERROR( "JsonParser '%s': JSON text value cannot be converted to CRUID value", m_debugName.AsChar() );
					return false;
				}
				CRUIDRef parsedDataRef( parsedData );
				propType->Copy( propertyData, &parsedDataRef );

				return true;
			}
			else if( propTypeName == GetTypeName<CGUID>() )
			{
				if( !elm.IsString() )
				{
					RED_LOG_ERROR( "JsonParser '%s': Property data is compatible only with JSON text value", m_debugName.AsChar() );
					return false;
				}

				CGUID parsedData;
				if( !parsedData.FromString( elm.GetString() ) )
				{
					RED_LOG_ERROR( "JsonParser '%s': JSON text value cannot be converted to CGUID value", m_debugName.AsChar() );
					return false;
				}
				propType->Copy( propertyData, &parsedData );

				return true;
			}
			else if( propTypeName == GetTypeName<rtti::Variant>() )
			{
				if( !elm.IsObject() )
				{
					RED_LOG_WARNING( "JsonParser: Property data is compatible only with JSON object value" );
					return false;
				}

				const auto& variantObject = elm.GetObject();

				CName innerTypeName;
				if( variantObject.HasMember( "InnerType" ) )
				{
					if( variantObject[ "InnerType" ].IsString() )
					{
						innerTypeName = RED_NAME( variantObject[ "InnerType" ].GetString() );
					}
				}

				if( innerTypeName == CName::NONE() )
				{
					RED_LOG_WARNING( "JsonParser: Type name of object in Variant is unknown" );
					return false;
				}

				const rtti::IType* innerTypeDesc = GetRttiSystem().FindType( innerTypeName );
				if( !innerTypeDesc )
				{
					RED_LOG_WARNING( "JsonParser: Type name of object in Variant is unknown" );
					return false;
				}

				if(! variantObject.HasMember( "Data" ) )
				{
					RED_LOG_WARNING( "JsonParser: Data for variant object doesn't exist in json file" );
					return false;
				}

				const auto& innerDataElm = variantObject[ "Data" ];

				rtti::Variant& parsedData = ( *reinterpret_cast< rtti::Variant* >( propertyData ) );
				parsedData.Set( innerTypeDesc, nullptr );
				void* innerData = const_cast<void*>( parsedData.GetData() );

				if( !ParseValue( innerDataElm, innerTypeDesc, innerData ) )
				{
					RED_LOG_ERROR( "JsonParser '%s': Creation inner object in Variant was impossible", m_debugName.AsChar() );
					return false;
				}

				return true;
			}
			// NodeRef was added because we support json serialization for VFX events in the scene editor
			else if (propTypeName == RED_NAME_CONSTEXPR_NOREG( "locLocalizationString" ) || propTypeName == RED_NAME_CONSTEXPR_NOREG( "LocalizationString" ) || propTypeName == RED_NAME_CONSTEXPR_NOREG( "NodeRef" ) )
			{
				if (!elm.IsString())
				{
					RED_LOG_ERROR("JsonParser '%s': Property data is compatible only with JSON text value", m_debugName.AsChar() );
					return false;
				}

				propType->FromString(propertyData, elm.GetString());

				return true;
			}

			return ParseCustomValue( elm, propType, propertyData );
		}

		Bool JsonParser::ParseDynArrayValue( const JSONValue &elm, const rtti::IType* propType, void* propertyData )
		{
			if( !elm.IsArray() )
			{
				RED_LOG_ERROR( "JsonParser '%s': Property data is compatible only with JSON array", m_debugName.AsChar() );
				return false;
			}

			// Get the array type
			const rtti::ArrayType* arrayType = static_cast< const rtti::ArrayType* >( propType );
			const rtti::IType* innerType = arrayType->GetInnerType();

			// Clean current array
			arrayType->Destruct( propertyData );
			arrayType->Construct( propertyData );

			// Create array
			Uint32 arrayCount = elm.Capacity();
			if( arrayCount > 0 )
			{
				red::DynArrayAccessor& ar = red::DynArrayAccessor::GetRef( propertyData );
			
				// Allocate memory for array's elements
				arrayType->AddArrayElement( propertyData, arrayCount, PoolJSON() );

				Int32 counter = 0;
				for( auto it = elm.Begin(); it != elm.End(); ++it, ++counter )
				{
					const JSONValue& innerElm = ( *it );

					void* innerPropertyData = arrayType->GetArrayElement( propertyData, counter );
					if( !ParseValue( innerElm, innerType, innerPropertyData ) )
					{
						RED_LOG_ERROR( "JsonParser '%s': Property data is impossible to parse", m_debugName.AsChar() );
					}
				}
			}

			return true;
		}

		Bool JsonParser::ParseStaticArrayValue( const JSONValue &elm, const rtti::IType* propType, void* propertyData )
		{
			if( !elm.IsArray() )
			{
				RED_LOG_ERROR( "JsonParser '%s': Property data is compatible only with JSON array", m_debugName.AsChar() );
				return false;
			}

			// Get the array type
			const rtti::StaticArrayType* arrayType = static_cast< const rtti::StaticArrayType* >( propType );
			const rtti::IType* innerType = arrayType->ArrayGetInnerType();

			// Check max size
			Uint32 arrayCount = elm.Capacity();
			if( arrayCount > 0 )
			{
				if( arrayCount > arrayType->GetMaxSize() )
				{
					RED_LOG_ERROR( "JsonParser '%s': Native array cannot have more elements than %d but JSON file has %d elements", m_debugName.AsChar(), arrayType->GetMaxSize(), arrayCount );
					return false;
				}

				// Allocate memory for array's elements
				arrayType->ArrayAddElement( propertyData, arrayCount );

				// Create array
				Int32 counter = 0;
				for( auto it = elm.Begin(); it != elm.End(); ++it, ++counter )
				{
					const JSONValue& innerElm = ( *it );

					void* innerPropertyData = arrayType->ArrayGetArrayElement( propertyData, counter );
					if( !ParseValue( innerElm, innerType, innerPropertyData ) )
					{
						RED_LOG_ERROR( "JsonParser '%s': Property data is impossible to parse", m_debugName.AsChar() );
					}
				}
			}

			return true;
		}

		Bool JsonParser::ParseNativeArrayValue( const JSONValue &elm, const rtti::IType* propType, void* propertyData )
		{
			if( !elm.IsArray() )
			{
				RED_LOG_ERROR( "JsonParser '%s': Property data is compatible only with JSON array", m_debugName.AsChar() );
				return false;
			}

			// Get the array type
			const rtti::NativeArrayType* arrayType = static_cast< const rtti::NativeArrayType* >( propType );
			const rtti::IType* innerType = arrayType->ArrayGetInnerType();

			// Check max size
			Uint32 arrayCount = elm.Capacity();
			if( arrayCount > 0 )
			{
				if( arrayCount > arrayType->GetElementCount() )
				{
					RED_LOG_ERROR( "JsonParser '%s': Native array cannot have more elements than %d but JSON file has %d elements", m_debugName.AsChar(), arrayType->GetElementCount(), arrayCount );
					return false;
				}

				// Allocate memory for array's elements
				arrayType->ArrayAddElement( propertyData, arrayCount );

				// Create array
				Int32 counter = 0;
				for( auto it = elm.Begin(); it != elm.End(); ++it, ++counter )
				{
					const JSONValue& innerElm = ( *it );

					void* innerPropertyData = arrayType->ArrayGetArrayElement( propertyData, counter );
					if( !ParseValue( innerElm, innerType, innerPropertyData ) )
					{
						RED_LOG_ERROR( "JsonParser '%s': Property data is impossible to parse", m_debugName.AsChar() );
					}
				}
			}

			return true;
		}

		Bool JsonParser::ParseEnumValue( const JSONValue &elm, const rtti::IType* propType, void* propertyData )
		{
			const rtti::EnumType* enumType = static_cast< const rtti::EnumType* >( propType );

			if( elm.IsNumber() )
			{
				Int64 enumValue = elm.GetInt64();

				CName enumName;
				if( !enumType->FindName( enumValue, enumName ) )
				{
					RED_LOG_ERROR( "JsonParser '%s': Enum type doesn't have value: %ld", m_debugName.AsChar(), enumValue );
					return false;
				}

				propType->Copy( propertyData, &enumValue );

				return true;
			}
			else if( elm.IsString() )
			{
				CName enumName = RED_NAME( elm.GetString() );

				Int64 enumValue = -1;
				if( !enumType->FindValue( enumName, enumValue ) )
				{
					RED_LOG_ERROR( "JsonParser '%s': Enum type doesn't have value: %s", m_debugName.AsChar(), enumName.AsChar() );
					return false;
				}

				propType->Copy( propertyData, &enumValue );

				return true;
			}

			return false;
		}

		Bool JsonParser::ParseBitFieldValue( const JSONValue &elm, const rtti::IType* propType, void* propertyData )
		{
			const rtti::BitFieldType* bitfieldType = static_cast< const rtti::BitFieldType* >( propType );

			if( elm.IsNumber() )
			{
				Int64 bitfieldValue = elm.GetInt64();

				if( bitfieldType->GetBitName( bitfieldValue ) == CName::NONE() )
				{
					RED_LOG_ERROR( "JsonParser '%s': Bitfield type doesn't have value: %ld", m_debugName.AsChar(), bitfieldValue );
					return false;
				}

				bitfieldValue = RED_FLAG64( bitfieldValue );
				propType->Copy( propertyData, &bitfieldValue );

				return true;
			}
			else if( elm.IsString() )
			{
				const char* bitFieldValue = elm.GetString();

				if( bitFieldValue[ 0 ] == '<' )
				{
					return bitfieldType->FromString( propertyData, bitFieldValue );
				}
				else
				{
					CName bitfieldName = RED_NAME( elm.GetString() );

					Int64 bitfieldValue = RED_FLAG64( bitfieldType->GetBitValue( bitfieldName ) );
					if( bitfieldValue == -1 )
					{
						RED_LOG_ERROR( "JsonParser '%s': Bitfield type doesn't have value: %s", m_debugName.AsChar(), bitfieldName.AsChar() );
						return false;
					}

					propType->Copy( propertyData, &bitfieldValue );

					return true;
				}
			}

			return false;
		}

		Bool JsonParser::ParseClassValue( const JSONValue &elm, const rtti::IType* propType, void* propertyData )
		{
			const rtti::ClassType* typeDesc = static_cast< const rtti::ClassType* >( propType );

			red::DynArray<const rtti::Property*> properties{ PoolJSON() };
			typeDesc->GetProperties( properties );

			for( const rtti::Property* prop : properties )
			{
				const CName propName = prop->GetName();

				if( !elm.HasMember( propName.AsChar() ) )
				{
					continue;
				}

				void* propData = prop->GetOffsetPtr( propertyData );
				const rtti::IType* propType = prop->GetType();
				const CName propTypeCName = propType->GetName();

				const JSONValue &propObj = elm[ propName.AsChar() ];
				if( propObj.IsObject() )
				{
					if( propObj.HasMember( "Data" ) )
					{
						if( !ParseValue( propObj, prop->GetType(), propData ) )
						{
							RED_LOG_ERROR( "JsonParser '%s': Property '%s' of '%s' cannot be parsed from JSON. Property will have default value.", m_debugName.AsChar(), propName.AsChar(), propTypeCName.AsChar() );
						}
						continue;
					}

					// special case for extended description
					if( propObj.HasMember( "_PropertyValue" ) )
					{
						if( !ParseValue( propObj[ "_PropertyValue" ], prop->GetType(), propData ) )
						{
							RED_LOG_ERROR( "JsonParser '%s': Property '%s' of '%s' cannot be parsed from JSON. Property will have default value.", m_debugName.AsChar(), propName.AsChar(), propTypeCName.AsChar() );
						}
						else
						{
							continue;
						}
					}

					if( !ParseValue( propObj, prop->GetType(), propData ) )
					{
						RED_LOG_ERROR( "JsonParser '%s': Property '%s' of '%s' cannot be parsed from JSON. Property will have default value.", m_debugName.AsChar(), propName.AsChar(), propTypeCName.AsChar() );
					}
				}
				else
				{
					if( !ParseValue( propObj, prop->GetType(), propData ) )
					{
						RED_LOG_ERROR( "JsonParser '%s': Property '%s' of '%s' cannot be parsed from JSON. Property will have default value.", m_debugName.AsChar(), propName.AsChar(), propTypeCName.AsChar() );
					}
				}
			}

			return true;
		}

		Bool JsonParser::ParseHandleValue( const JSONValue &elm, const rtti::IType* propType, void* propertyData )
		{
			if( elm.IsObject() )
			{
				const char* propTypeName = propType->GetName().AsChar();
				THandle<ISerializable> parsedData = ParseSerializableValue( elm, false, propTypeName );
				SetCorrectParent( parsedData );
				propType->Copy( propertyData, &parsedData );
				return true;
			}
			else if( elm.IsUint() )
			{
				Uint32 exportTableIndex = elm.GetUint();

				if( m_exportTableStateIsEnabled )
				{
					if( exportTableIndex == m_exportTable.Size() )
					{
						RED_LOG_ERROR( "JsonParser '%s': Object in handle cannot have dependency to itself!", m_debugName.AsChar() );
						return false;
					}

					ISerializable* currentParentObj = ( !m_parentStack.Empty() ) ? m_parentStack.Back() : nullptr;
					m_exportTableDependecies.PushBack( ExportDependency{ propType, exportTableIndex, propertyData, currentParentObj } );
					return true;
				}

				if( exportTableIndex >= m_exportTable.Size() )
				{
					RED_LOG_ERROR( "JsonParser '%s': Wrong resource index. Object is not exist in export table.", m_debugName.AsChar() );
					return false;
				}

				THandle<ISerializable> parsedData = m_exportTable[ exportTableIndex ];
				SetCorrectParent( parsedData );
				propType->Copy( propertyData, &parsedData );

				return true;
			}
			else if( elm.IsNull() )
			{
				THandle<ISerializable> parsedData( nullptr );
				propType->Copy( propertyData, &parsedData );
				return true;
			}

			RED_LOG_ERROR( "JsonParser '%s': Property data is compatible only with JSON object value or index to object in export table", m_debugName.AsChar() );
			return false;
		}

		Bool JsonParser::ParseWeakHandleValue( const JSONValue &elm, const rtti::IType* propType, void* propertyData )
		{
			if( elm.IsUint() )
			{
				Uint32 exportTableIndex = elm.GetUint();

				if( m_exportTableStateIsEnabled )
				{
					if( exportTableIndex == m_exportTable.Size() )
					{
						RED_LOG_ERROR( "JsonParser '%s': Object in handle cannot have dependency to itself!", m_debugName.AsChar() );
						return false;
					}

					m_exportTableWeakDependecies.PushBack( ExportDependency{ propType, exportTableIndex, propertyData } );
					return true;
				}

				if( exportTableIndex >= m_exportTable.Size() )
				{
					RED_LOG_ERROR( "JsonParser '%s': Wrong resource index. Object is not exist in export table.", m_debugName.AsChar() );
					return false;
				}

				WeakHandle<ISerializable> parsedData = m_exportTable[ exportTableIndex ];
				propType->Copy( propertyData, &parsedData );

				return true;
			}
			else if( elm.IsNull() )
			{
				WeakHandle<ISerializable> parsedData( nullptr );
				propType->Copy( propertyData, &parsedData );
				return true;
			}

			RED_LOG_ERROR( "JsonParser '%s': Property data is compatible only with JSON object value or index to object in export table", m_debugName.AsChar() );
			return false;
		}

		Bool JsonParser::ParseResourceReferenceValue( const JSONValue &elm, const rtti::IType* propType, void* propertyData )
		{
			if( elm.IsUint() )
			{
				Uint32 importTableIndex = elm.GetUint();
				if( importTableIndex >= m_importTable.Size() )
				{
					RED_LOG_ERROR( "JsonParser '%s': Wrong resource index. Resource does not exist in import table.", m_debugName.AsChar() );
					return false;
				}

				TResRef<CResource> parsedData( m_loadingTokensImportTable[importTableIndex] );
				propType->Copy( propertyData, &parsedData );

				return true;
			}
			else if( elm.IsString() )
			{
				res::ResourcePath resPath = res::ResourcePath::Build( elm.GetString() );
				TResAsyncRef<CResource> localRes( resPath );
				res::ResourceTokenHandle token = localRes.IssueLoadingRequest();
				TResRef<CResource> parsedData( token );
				propType->Copy( propertyData, &parsedData );

				return true;
			}
			else if( elm.IsNull() )
			{
				TResRef<CResource> parsedData;
				propType->Copy( propertyData, &parsedData );

				return true;
			}

			return false;
		}

		Bool JsonParser::ParseResourceAsyncReferenceValue( const JSONValue &elm, const rtti::IType* propType, void* propertyData )
		{
			if( elm.IsUint() )
			{
				Uint32 importTableIndex = elm.GetUint();
				if( importTableIndex >= m_importTable.Size() )
				{
					RED_LOG_ERROR( "JsonParser '%s': Wrong resource index. Resource does not exist in import table.", m_debugName.AsChar() );
					return false;
				}

				res::ResourceAsyncReference parsedData( m_importTable[ importTableIndex ].GetPath() );
				propType->Copy( propertyData, &parsedData );

				return true;
			}
			else if( elm.IsString() )
			{
				res::ResourcePath resPath = res::ResourcePath::Build( elm.GetString() );
				res::ResourceAsyncReference parsedData( resPath );
				propType->Copy( propertyData, &parsedData );

				return true;
			}
			else if( elm.IsNull() )
			{
				res::ResourceAsyncReference parsedData;
				propType->Copy( propertyData, &parsedData );

				return true;
			}

			RED_LOG_ERROR( "JsonParser '%s': Property data is compatible only with JSON text value", m_debugName.AsChar() );
			return false;
		}

		Bool JsonParser::ParseCustomValue( const JSONValue &elm, const rtti::IType* propType, void* propertyData )
		{
			if ( m_customParseFunction )
			{
				if ( !m_customParseFunction( elm, propType, propertyData ) )
				{
					RED_LOG_ERROR( "JsonParser '%s': JSON file doesn't support property of %s type", m_debugName.AsChar(), propType->GetName().AsChar() );
					return false;
				}

				return true;
			}

			return false;
		}

		Bool JsonParser::ResolveDependenciesInExportTable()
		{
			if( !ResolveHardDependenciesInExportTable() )
			{
				return false;
			}
			if( !ResolveWeakDependenciesInExportTable() )
			{
				return false;
			}

			return true;
		}

		Bool JsonParser::ResolveHardDependenciesInExportTable()
		{
			for( auto& elm : m_exportTableDependecies )
			{
				Uint32 exportedObjectIndex = elm.ExportedObjectIndex;
				const rtti::IType* propType = elm.PropertyType;
				void* propertyData = elm.PropertyData;

				if( exportedObjectIndex >= m_exportTable.Size() )
				{
					RED_LOG_ERROR( "JsonParser '%s': Wrong resource index. Object is not exist in export table.", m_debugName.AsChar() );
					return false;
				}

				THandle<ISerializable> parsedData = m_exportTable[ exportedObjectIndex ];
				if( parsedData )
				{
					parsedData->SetParent( elm.ParentObject );
				}
				propType->Copy( propertyData, &parsedData );
			}

			return true;
		}

		Bool JsonParser::ResolveWeakDependenciesInExportTable()
		{
			for( auto& elm : m_exportTableWeakDependecies )
			{
				Uint32 exportedObjectIndex = elm.ExportedObjectIndex;
				const rtti::IType* propType = elm.PropertyType;
				void* propertyData = elm.PropertyData;

				if( exportedObjectIndex >= m_exportTable.Size() )
				{
					RED_LOG_ERROR( "JsonParser '%s': Wrong resource index. Object is not exist in export table.", m_debugName.AsChar() );
					return false;
				}

				WeakHandle<ISerializable> parsedData = m_exportTable[ exportedObjectIndex ];
				propType->Copy( propertyData, &parsedData );
			}

			return true;
		}

		void JsonParser::ClearInternalData()
		{
			m_exportTableDependecies.Clear();
			m_exportTableWeakDependecies.Clear();
			m_exportTable.Clear();
			m_importTable.Clear();
			m_loadingTokensImportTable.Clear();
			m_parentStack.Clear();
		}

		void JsonParser::PostLoad( const red::DynArray<THandle<ISerializable>>& objects, const PostLoadContext& context )
		{
			for( const THandle<ISerializable>& obj : objects )
			{
				PostLoad( obj, context );
			}
		}

		void JsonParser::PostLoad( const THandle<ISerializable>& object, const PostLoadContext& context )
		{
			if( object )
			{
				object->OnPostLoad( context );
			}
		}

		void JsonParser::SetRoot( const THandle<ISerializable>& root )
		{
			m_root = root;
		}

		THandle<ISerializable> JsonParser::GetRoot() const
		{
			return m_root;
		}

		Bool JsonParser::IsTypeSupported( const rtti::IType* propType )
		{
			static red::StaticArray<CName, 3> noSupportedTypes
			{
				RED_NAME_CONSTEXPR( "serializationDeferredDataBuffer" ),
				RED_NAME_CONSTEXPR("DeferredDataBuffer"),
				RED_NAME_CONSTEXPR("DataBuffer")
			};

			const CName& typeName = propType->GetName();
			auto iter = std::find( noSupportedTypes.Begin(), noSupportedTypes.End(), typeName );
			
			return iter == noSupportedTypes.End();
		}

		void JsonParser::SetCorrectParent( const THandle<ISerializable>& object )
		{
			if( object && !m_parentStack.Empty() )
			{
				object->SetParent( m_parentStack.Back() );
			}
		}

		void JsonParser::SetDebugName( const String& debugName )
		{
			m_debugName = debugName;
		}

		Bool ParseSerializableObjectInline( String && jsonData, THandle<ISerializable>& object, CustomParseFunction customParseFunction )
		{
			res::json::JsonParser jsonParser( customParseFunction );

			if( !jsonParser.ParseJsonFile( std::move( jsonData ) ) )
			{
				return false;
			}

			if( !jsonParser.CreateExportTable() )
			{
				return false;
			}

			if( !jsonParser.CreateImportTable() )
			{
				return false;
			}

			if( !jsonParser.LoadObjectStructure( PostLoadContext{} ) )
			{
				return false;
			}

			object = jsonParser.GetRoot();

			return object != nullptr;
		}

		Bool ParseJsonResourceInline( String && jsonData, THandle<JsonResource>& resource, CustomParseFunction customParseFunction )
		{
			THandle<ISerializable> object;

			if( ParseSerializableObjectInline( std::move( jsonData ), object, customParseFunction ) )
			{
				resource->SetRoot( object );
				return true;
			}
			
			return false;
		}

}	// namespace json

}	// namespace res
