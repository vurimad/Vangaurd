/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "packageSerializer.h"
#include "serializable.h"

namespace red
{
	struct Package;
	class PackageTableOfContent;
	class PackageReadStream;
	class PackageTypeSerializerDictionary;
	class PackageErrorReporter;

	struct PackageReaderParameter
	{
		Uint32 version;
		PackageReadStream * stream;
		PackageTableOfContent * packageTable;
		PackageTypeSerializerDictionary * serializerDictionary;
		PackageErrorReporter * errorReporter;
	};

	class RED_REFLECTION_API PackageReader : public PackageSerializer
	{
	public:
		PackageReader();
		virtual ~PackageReader();

		void Initialize( const PackageReaderParameter & param );

		void SetContextMemoryPool( const memory::Pool & pool );
		
	private:

		virtual void OnSerialize( void * buffer, Uint64 size ) override final;
		virtual bool OnSerializeType( const PackageSerializeTypeParameter & context ) override final;	
		const memory::Pool & GetContextMemoryPool() const;

		Uint32 m_version;
		PackageTableOfContent * m_packageTable;
		PackageReadStream * m_stream;
		PackageTypeSerializerDictionary * m_serializerDictionary;
		PackageErrorReporter * m_errorReporter;
		const red::memory::Pool* m_pool;
	};

	RED_REFLECTION_API red::UniquePtr< PackageReader > CreatePackageReader( const PackageReaderParameter & param );
}
