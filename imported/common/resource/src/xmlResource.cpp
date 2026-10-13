/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "xmlResource.h"
#include "xmlDocument.h"
#include "textFormatResourceLoader.h"

RTTI_BEGIN_TYPE( XmlResource );
	RTTI_PARENT_TYPE( CResource );
	RTTI_PROPERTY( m_data );
RTTI_END_TYPE();

class XmlResourceLoader : public serialization::TextFormatResourceLoader< XmlResource, true >
{
public:
	XmlResourceLoader() = default;
};

RED_RESOURCE_API void RegisterXmlResourceLoader()
{
	serialization::ILoader::RegisterCustomFactory< XmlResourceLoader >( "xml" );
}

XmlResource::XmlResource()
{
}

XmlResource::~XmlResource()
{
}

void XmlResource::OnSerialize( IFile &file )
{
	file << m_data;

	if( file.IsReader() )
	{
		InternalParseData();
	}
}

Bool XmlResource::ParseData( String&& data )
{
	m_data = data;

	return InternalParseData();
}

Bool XmlResource::InternalParseData()
{
	auto document = xml::Document::Create( const_cast< char* >( m_data.AsChar() ) );
	if( !document )
	{
		return false;
	}

	m_document.Reset( document );
	return true;
}

const xml::Document& XmlResource::GetDocument() const
{
	return *m_document;
}
