/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once
#include "packageSerializer.h"
#include "../../redContainers/include/blob.h"

namespace red
{
	class PackageWriteStream;
	class PackageReadStream;
	class PackageTableOfContent;
	class PackageTypeSerializerDictionary;

	struct PackageRemapperParameter
	{
		Uint32 version;
		PackageWriteStream * outputStream;
		PackageTableOfContent * tableOfContent;
		const PackageTypeSerializerDictionary * dictionary;
		bool remapMissingProperty;
	};

	class RED_REFLECTION_API PackageRemapper : public PackageSerializer
	{
	public:
		PackageRemapper();
		virtual ~PackageRemapper();

		void Initialize( const PackageRemapperParameter & param );

		void Execute( const rtti::ClassType * objectType, BlobView inputBuffer );

	private:

		virtual void OnSerialize( void * buffer, Uint64 size ) override final;
		virtual bool OnSerializeType( const PackageSerializeTypeParameter & param ) override final;
	
		const PackageTypeSerializerDictionary * m_serializerDictionary;
		red::UniquePtr< PackageReadStream > m_inputStream;
		PackageWriteStream * m_outputStream;
		PackageTableOfContent * m_tableOfContent;
		Uint32 m_version;
		bool m_remapMissingProperty;
	};

	UniquePtr< PackageRemapper > CreatePackageRemapper( const PackageRemapperParameter & param );
}
