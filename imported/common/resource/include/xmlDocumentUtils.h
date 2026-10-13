/// RenderLab (C) 2015 by Tomasz "RexDex" Jonarski
/// All Right Reserved

#pragma once

#include "xmlDocument.h"

namespace xml
{

	/// Node enumerator
	class RED_RESOURCE_API DocumentNodeLister
	{
	public:
		DocumentNodeLister( const Document& xml, const Document::Node* node );
		~DocumentNodeLister();

		const Document::Node* Get( const char* name );

	private:
		const Document*			m_doc;
		const Document::Node*	m_baseNode;

		const Document::Node*		m_curNode;
	};

	/// Attribute enumerator
	class RED_RESOURCE_API DocumentAttributeLister
	{
	public:
		DocumentAttributeLister( const Document& xml, const Document::Node* node );
		~DocumentAttributeLister();

		const Document::Attribute* Get( const char* name );

	private:
		const Document*				m_doc;
		const Document::Node*		m_baseNode;

		const Document::Attribute*	m_curAttribute;
	};

	/// Array iterator
	class RED_RESOURCE_API DocumentNodeArrayIterator
	{
	public:
		DocumentNodeArrayIterator( const Document& xml, const Document::Node* node );
		~DocumentNodeArrayIterator();

		/// Number of elements
		const Uint32 Size() const;

		/// Get element
		const Document::Node* operator[]( const Uint32 index ) const;

	private:
		const Document*							m_doc;
		const Document::Node*					m_baseNode;

		red::DynArray< const Document::Node* >	m_elements{ red::PoolEngine() };
	};

	RED_RESOURCE_API Bool TryGetAttributeValue( const Document& doc, const Document::Node* node, const AnsiChar* attributeName, red::String& value );

} // xml
