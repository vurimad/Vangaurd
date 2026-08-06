/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "packageRemapper.h"
#include "packageReadWriteStream.h"
#include "packageReadStream.h"
#include "packageWriteStream.h"
#include "packageTypeSerializerDictionary.h"
#include "packageTypeSerializer.h"
#include "packageTableOfContent.h"
#include "packageVersion.h"

namespace red
{
	PackageRemapper::PackageRemapper()
		: m_serializerDictionary( nullptr )
		, m_outputStream( nullptr )
		, m_tableOfContent( nullptr )
		, m_version( c_packageCurrentVersion )
		, m_remapMissingProperty( false )
	{
	}

	PackageRemapper::~PackageRemapper()
	{
	}

	void PackageRemapper::Initialize( const PackageRemapperParameter & param )
	{
		m_serializerDictionary = param.dictionary;
		m_outputStream = param.outputStream;
		m_inputStream = CreateReadStream( BlobView() );
		m_tableOfContent = param.tableOfContent;
		m_version = param.version;
		m_remapMissingProperty = param.remapMissingProperty;
	}

	void PackageRemapper::Execute( const rtti::ClassType * objectType, BlobView inputBuffer )
	{
		m_inputStream->SetBuffer( inputBuffer );

		PackageSerializeTypeParameter param = 
		{
			nullptr,
			objectType,
			nullptr
		};

		SerializeType( param );
	}

	void PackageRemapper::OnSerialize( void * buffer, Uint64 size )
	{
		if( !buffer )
		{
			const void * cursor = m_inputStream->GetReadCursor();
			m_outputStream->Write( cursor, size );
			m_inputStream->Skip( size );
		}
		else
		{
			m_inputStream->Read( buffer, size );
			m_outputStream->Write( buffer, size );
		}
	}

	bool PackageRemapper::OnSerializeType( const PackageSerializeTypeParameter & param )
	{
		const PackageTypeSerializer * serializer = m_serializerDictionary->FindTypeSerializer( param.type );
		if( serializer )
		{
			PackageTypeSerializer::RemapContext context =
			{
				*this,
				*m_inputStream,
				*m_outputStream,
				*m_tableOfContent,
				m_version,
				m_remapMissingProperty
			};
		
			serializer->RemapValue( context, param );
			return true;
		}

		return false;
	}

	UniquePtr< PackageRemapper > CreatePackageRemapper( const PackageRemapperParameter & param )
	{
		UniquePtr< PackageRemapper > remapper = CreateUniquePtr< PackageRemapper >();
		remapper->Initialize( param );
		return remapper;
	}

}
