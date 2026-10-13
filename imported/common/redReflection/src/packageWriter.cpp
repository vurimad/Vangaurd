/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "packageWriter.h"
#include "packageWriteStream.h"
#include "rttiProperty.h"
#include "package.h"
#include "rttiFundamentalTypes.h"
#include "packageTableOfContent.h"
#include "packageTypeSerializer.h"
#include "packageTypeSerializerDictionary.h"
#include "packageErrorReporter.h"

namespace red
{
	PackageWriter::PackageWriter()
		: m_stream( nullptr )
		, m_table( nullptr )
		, m_serializerDictionary( nullptr )
		, m_propertyFlags( PackagePropertyType_All )
		, m_cookingPlatform()
		, m_errorReporter( nullptr )
	{}

	PackageWriter::~PackageWriter()
	{}

	void PackageWriter::Initialize( const PackageWriterParameter & param )
	{
		m_stream = param.stream;
		m_table = param.table;
		m_serializerDictionary = param.dictionary;
		m_propertyFlags = param.propertyFlags;
		m_cookingPlatform = param.cookingPlatform;
		m_errorReporter = param.errorReporter;
	}

	void PackageWriter::OnSerialize( void * buffer, Uint64 size )
	{
		m_stream->Write( buffer, size );
	}

	bool PackageWriter::OnSerializeType( const PackageSerializeTypeParameter & param )
	{
		const PackageTypeSerializer * serializer = m_serializerDictionary->FindTypeSerializer( param.type );

		if( serializer )
		{
			PackageTypeSerializer::WriteContext writeContext = 
			{
				*this,
				*m_stream,
				*m_table,
				m_propertyFlags,
				m_cookingPlatform
			};

			serializer->WriteValue( writeContext, param ); 
			return true;
		}

		m_errorReporter->ReportMissingSerializer( param.type );

		return false;
	}

	red::UniquePtr< PackageWriter > CreatePackageWriter( const PackageWriterParameter & param )
	{
		red::UniquePtr< PackageWriter > writer = red::CreateUniquePtr< PackageWriter >();
		writer->Initialize( param );
		return writer;
	}
}
