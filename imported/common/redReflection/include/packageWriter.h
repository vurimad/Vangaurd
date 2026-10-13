/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "packageSerializer.h"
#include "serializable.h"

namespace red
{
	class PackageWriteStream;
	class PackageTableOfContent;
	class PackageTypeSerializerDictionary;
	class PackageErrorReporter;

	enum PackagePropertyFlags : Uint32;

	struct PackageWriterParameter
	{
		PackageWriteStream * stream;
		PackageTableOfContent * table;
		const PackageTypeSerializerDictionary * dictionary;
		PackageErrorReporter * errorReporter;
		PackagePropertyFlags propertyFlags;
		ECookingPlatform cookingPlatform;
	};

	class RED_REFLECTION_API PackageWriter : public PackageSerializer
	{
	public:
		
		PackageWriter();
		virtual ~PackageWriter();

		void Initialize( const PackageWriterParameter & param );

	private:

		virtual void OnSerialize( void * buffer, Uint64 size ) override final;
		virtual bool OnSerializeType( const PackageSerializeTypeParameter & context ) override final;

		PackageWriteStream * m_stream;
		PackageTableOfContent * m_table;
		const PackageTypeSerializerDictionary * m_serializerDictionary; 
		Uint32 m_propertyFlags;
		ECookingPlatform m_cookingPlatform;
		PackageErrorReporter * m_errorReporter;
	};

	red::UniquePtr< PackageWriter > CreatePackageWriter( const PackageWriterParameter & param );
}
