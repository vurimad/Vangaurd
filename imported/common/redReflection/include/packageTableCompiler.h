/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once
#include "../../redContainers/include/blob.h"
#include "packageTable.h"
#include "packageTableOfContent.h"
#include "packageSerializer.h"

namespace red
{
	class PackageTypeSerializerDictionary;
	class PackageTableOfContent;
	class PackageReadStream;
	class PackageWriteStream;
	class PackageReadWriteStream;

	struct PackageTableCompilerParameter
	{
		PackageTable * inputTable = nullptr;
		PackageTable * outputTable = nullptr;
		red::BlobView inputBuffer;
		const PackageTypeSerializerDictionary * serializerDictionary = nullptr;
		Uint32 memoryReserve = RED_KILO_BYTE( 4 );
	};

	class RED_REFLECTION_API PackageTableCompiler 
	{
		RED_USE_MEMORY_POOL( red::PoolEngine );

	public:
		PackageTableCompiler();
		~PackageTableCompiler();

		void Initialize( const PackageTableCompilerParameter & param );

		UniqueBuffer Execute();
		
	private:

		void SanitizeAllPendingObject( PackageTableOfContent * toc );

		PackageTable * m_outputTable;
		PackageTable * m_inputTable;
		red::UniquePtr< PackageReadStream > m_inputStream;
		red::UniquePtr< PackageWriteStream > m_outputStream;
		const PackageTypeSerializerDictionary * m_serializerDictionary;
		rtti::ITypeSystem * m_system; 
	};

	red::UniquePtr< PackageTableCompiler > CreatePackageTableCompiler( const PackageTableCompilerParameter & param );


	struct PackageTableOfContentSanitizerParameter
	{
		PackageTable * inputTable;
		PackageTable * outputTable;
	};

	class PackageTableOfContentCompiler : public PackageTableOfContent
	{
	public:

		PackageTableOfContentCompiler();
		virtual ~PackageTableOfContentCompiler();

		void Initialize( const PackageTableOfContentSanitizerParameter & param );

		void UpdateInputTable();

	private:

		virtual NameIndex OnMapName( CName name ) override final;
		virtual ObjectIndex OnMapObject( const ISerializable * object, const void * referenceObject ) override final;
		virtual ResourceIndex OnMapResource( const res::ResourcePath & path, PackageResourceImportType importType ) override final;

		virtual CName OnUnmapName( NameIndex index ) const;
		virtual void OnUnmapObject( ObjectIndex index, SerializableHandle& handle ) const override final;
		virtual res::ResourcePath OnUnmapResource( ResourceIndex index, res::ResourceTokenHandle & token ) const override final;

		virtual NameIndex OnRemapName( NameIndex index ) override final;
		virtual ResourceIndex OnRemapResource( ResourceIndex index, PackageResourceImportType importType ) override final;
		virtual ObjectIndex OnRemapObject( ObjectIndex index ) override final;

		PackageTable * m_inputTable;
		PackageTable * m_outputTable;

		constexpr static ObjectIndex c_invalidIndex = std::numeric_limits< ObjectIndex >::max();
		red::DynArray< ObjectIndex > m_remappedObject;
		red::DynArray< ObjectIndex > m_remappedObjectLookup;
		red::DynArray< NameIndex > m_remappedName;
		red::DynArray< ResourceIndex > m_remappedResource;
	};

	struct PackageSerializerSanitizerParameter
	{
		PackageReadStream * inputStream;
		PackageWriteStream * outputStream;
		PackageTableOfContent * tableOfContent;
		const PackageTypeSerializerDictionary * dictionary;
	};

	class PackageTableCompilerSerializer : public PackageSerializer
	{
	public:
		PackageTableCompilerSerializer();
		virtual ~PackageTableCompilerSerializer();

		void Initialize( const PackageSerializerSanitizerParameter & param );

	private:

		virtual void OnSerialize( void * buffer, Uint64 size ) override final;
		virtual bool OnSerializeType( const PackageSerializeTypeParameter & context ) override final;

		PackageReadStream * m_inputStream;
		PackageWriteStream * m_outputStream;
		PackageTableOfContent * m_tableOfContent;
		const PackageTypeSerializerDictionary * m_dictionary;
	};
}

