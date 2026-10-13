/*
 * Copyright (c)2017 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "../../../common/redReflection/include/resource.h"

namespace xml
{
	class Document;
}

class RED_RESOURCE_API XmlResource : public CResource
{
	RTTI_DECLARE_TYPE( XmlResource );
	IMPLEMENT_RESOURCE_INTERFACE( "xml", "xml", "XmlResource" );
	RED_USE_MEMORY_POOL( red::PoolEngine );

public:
	XmlResource();
	~XmlResource();

	virtual void OnSerialize( IFile &file ) override final;

	const xml::Document& GetDocument() const;

	Bool ParseData( String&& data );

	Bool InternalParseData();

private:
	String m_data;

	red::UniquePtr<xml::Document> m_document;
};
