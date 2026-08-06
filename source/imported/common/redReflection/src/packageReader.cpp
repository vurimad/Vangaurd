/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "packageReader.h"
#include "package.h"
#include "packageReadStream.h"
#include "packageUtils.h"
#include "packageTableOfContent.h"
#include "packageTypeSerializerDictionary.h"
#include "packageTypeSerializer.h"
#include "packageVersion.h"

namespace red
{
	PackageReader::PackageReader()
		: m_version( c_packageCurrentVersion )
		, m_packageTable( nullptr )
		, m_stream( nullptr )
		, m_serializerDictionary( nullptr )
		, m_errorReporter( nullptr )
		, m_pool( nullptr )
	{}

	PackageReader::~PackageReader()
	{}

	void PackageReader::Initialize( const PackageReaderParameter& param )
	{
		m_version = param.version;
		m_packageTable = param.packageTable;
		m_stream = param.stream;
		m_serializerDictionary = param.serializerDictionary;
		m_errorReporter = param.errorReporter;
	}

	void PackageReader::OnSerialize( void* buffer, Uint64 size )
	{
		m_stream->Read( buffer, size );
	}

	bool PackageReader::OnSerializeType( const PackageSerializeTypeParameter& context )
	{
		const PackageTypeSerializer* serializer = m_serializerDictionary->FindTypeSerializer( context.type );

		if( serializer )
		{
			PackageTypeSerializer::ReadContext readContext =
			{
				*this,
				*m_stream,
				*m_packageTable,
				m_version,
				*m_errorReporter,
				GetContextMemoryPool()
			};

			serializer->ReadValue( readContext, context );
			return true;
		}

		return false;
	}

	void PackageReader::SetContextMemoryPool( const memory::Pool& pool )
	{
		m_pool = &pool;
	}

	const memory::Pool& PackageReader::GetContextMemoryPool() const
	{
		return m_pool ? *m_pool : PoolSerializable::GetInstance();
	}

	red::UniquePtr< PackageReader > CreatePackageReader( const PackageReaderParameter& param )
	{
		red::UniquePtr< PackageReader > reader = red::CreateUniquePtr< PackageReader >();
		reader->Initialize( param );
		return reader;
	}
}
