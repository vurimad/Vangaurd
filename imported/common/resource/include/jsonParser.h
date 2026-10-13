/*
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../include/jsonDataBuilder.h"
#include "../include/jsonFileHelper.h"
#include "../include/jsonAllocator.h"
#include "../../redReflection/include/resourceAsyncReference.h"

class JsonResource;

namespace res
{
	namespace json
	{
		using JSONDocument = rapidjson::JSONDocument;
		using JSONValue = rapidjson::JSONValue;
		using CustomParseFunction = std::function< Bool( const JSONValue&, const rtti::IType*, void* ) >;

		struct RED_RESOURCE_API ExportDependency
		{
			const rtti::IType* PropertyType;
			Uint32 ExportedObjectIndex;
			void* PropertyData;
			ISerializable* ParentObject;
		};

		struct RED_RESOURCE_API ImportDependency
		{
			res::ResourceTokenHandle LoadingToken;
			const rtti::IType* PropertyType;
			void* PropertyData;
		};

		class RED_RESOURCE_API JsonParser final
		{
			RED_USE_MEMORY_POOL( PoolJSON );

		public:
			JsonParser( CustomParseFunction customParseFunction = CustomParseFunction() );
			~JsonParser();

			void SetDebugName( const String& debugName );

			Bool ParseJsonFile( String&& jsonData );
			Bool CreateExportTable();
			Bool CreateImportTable();
			Bool LoadObjectStructure( const PostLoadContext& context, Bool skipPostLoad = false );

			const red::DynArray<TResAsyncRef<CResource>>& GetDependencies();
			void StoreLoadingToken( Uint32 index, res::ResourceTokenHandle loadingToken );
			void ResolveDependencies();

			void SetRoot( const THandle<ISerializable>& root );
			THandle<ISerializable> GetRoot() const;

		private:
			// general cases
			Bool ParseValue( const JSONValue& elm, const rtti::IType* propType, void* propertyData );
			Bool ParseFundamentalValue( const JSONValue &elm, const rtti::IType* propType, void* propertyData );
			Bool ParseSimpleValue( const JSONValue &elm, const rtti::IType* propType, void* propertyData );
			Bool ParseCustomValue( const JSONValue &elm, const rtti::IType* propType, void* propertyData );

			// arrays
			Bool ParseDynArrayValue( const JSONValue &elm, const rtti::IType* propType, void* propertyData );
			Bool ParseStaticArrayValue( const JSONValue &elm, const rtti::IType* propType, void* propertyData );
			Bool ParseNativeArrayValue( const JSONValue &elm, const rtti::IType* propType, void* propertyData );

			// types
			Bool ParseClassValue( const JSONValue &elm, const rtti::IType* propType, void* propertyData );
			Bool ParseEnumValue( const JSONValue &elm, const rtti::IType* propType, void* propertyData );
			Bool ParseBitFieldValue( const JSONValue &elm, const rtti::IType* propType, void* propertyData );

			// templates
			Bool ParseHandleValue( const JSONValue &elm, const rtti::IType* propType, void* propertyData );
			Bool ParseWeakHandleValue( const JSONValue &elm, const rtti::IType* propType, void* propertyData );
			Bool ParseResourceReferenceValue( const JSONValue &elm, const rtti::IType* propType, void* propertyData );
			Bool ParseResourceAsyncReferenceValue( const JSONValue &elm, const rtti::IType* propType, void* propertyData );

			// special cases
			THandle<ISerializable> ParseSerializableValue( const JSONValue& val, Bool useRoot, const char* optionalTypeName = nullptr );

			// misc
			Bool IsTypeSupported( const rtti::IType* propType );
			Bool ResolveDependenciesInExportTable();
			Bool ResolveHardDependenciesInExportTable();
			Bool ResolveWeakDependenciesInExportTable();
			void ClearInternalData();

			void PostLoad( const red::DynArray<THandle<ISerializable>>& objects, const PostLoadContext& context );
			void PostLoad( const THandle<ISerializable>& object, const PostLoadContext& context );

			void SetCorrectParent( const THandle<ISerializable>& object );

		private:
			String m_jsonData;
			red::UniquePtr<JSONDocument, PoolJSON > m_doc;

			THandle<ISerializable> m_root;

			Bool m_exportTableStateIsEnabled;
			CustomParseFunction m_customParseFunction;

			red::DynArray<THandle<ISerializable>> m_exportTable;
			red::DynArray<TResAsyncRef<CResource>> m_importTable;
			red::DynArray<res::ResourceTokenHandle> m_loadingTokensImportTable;
			red::DynArray<ExportDependency> m_exportTableDependecies;
			red::DynArray<ExportDependency> m_exportTableWeakDependecies;

			red::DynArray<ISerializable*> m_parentStack;

			String m_debugName;

			using ParseFunc = Bool( JsonParser::* )( const JSONValue&, const rtti::IType*, void* );
			static constexpr Int32 s_parseFunctionsCount = RT_Count;

			static_assert( s_parseFunctionsCount == 16, "If count of elements changed then please be sure that array with functions is still valid!" );
			static ParseFunc s_parseFunctions[ s_parseFunctionsCount ];
		};

		RED_RESOURCE_API Bool ParseSerializableObjectInline( String&& jsonData, THandle<ISerializable>& object, CustomParseFunction customParseFunction = CustomParseFunction() );

		template< typename ObjectTypeName >
		Bool ParseObjectInline( String&& jsonData, THandle<ObjectTypeName>& object, CustomParseFunction customParseFunction = CustomParseFunction() )
		{
			THandle<ISerializable> internalObject;
			if( ParseSerializableObjectInline( std::move( jsonData ), internalObject, customParseFunction ) )
			{
				object = Cast<ObjectTypeName>( internalObject );
				return object != nullptr;
			}

			return false;
		}

		RED_RESOURCE_API Bool ParseJsonResourceInline( String&& jsonData, THandle<JsonResource>& resource, CustomParseFunction customParseFunction = CustomParseFunction() );

	}	// namespace json

}	// namespace res
