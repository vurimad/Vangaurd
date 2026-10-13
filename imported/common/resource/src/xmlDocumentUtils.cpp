#include "build.h"

#include "xmlDocument.h"
#include "xmlDocumentUtils.h"

namespace xml
{
	//-------------------------------------------------------------

	DocumentNodeLister::DocumentNodeLister( const Document& xml, const Document::Node* node )
		: m_doc( &xml )
		, m_baseNode( node )
	{
		m_curNode = m_doc->GetFirstNode( m_baseNode, "" );
	}

	DocumentNodeLister::~DocumentNodeLister()
	{
	}

	const Document::Node* DocumentNodeLister::Get( const char* name )
	{
		if ( m_curNode )
		{
			const Document::Node* node = m_doc->GetNextNode( m_curNode, name );
			if ( node )
			{
				m_curNode = node;
				return node;
			}
		}

		m_curNode = m_doc->GetFirstNode( m_baseNode, name );
		return m_curNode;	
	}

	//-------------------------------------------------------------

	DocumentAttributeLister::DocumentAttributeLister( const Document& xml, const Document::Node* node )
		: m_doc( &xml )
		, m_baseNode( node )
	{
		m_curAttribute = m_doc->GetFirstAttribute( m_baseNode, "" );
	}

	DocumentAttributeLister::~DocumentAttributeLister()
	{
	}

	const Document::Attribute* DocumentAttributeLister::Get( const char* name )
	{
		if ( m_curAttribute )
		{
			const Document::Attribute* attr = m_doc->GetNextAttribute( m_curAttribute, name );
			if ( attr )
			{
				m_curAttribute = attr;
				return attr;
			}
		}

		m_curAttribute = m_doc->GetFirstAttribute( m_baseNode, name );
		return m_curAttribute;	
	}

	//-------------------------------------------------------------

	DocumentNodeArrayIterator::DocumentNodeArrayIterator( const Document& xml, const Document::Node* node )
		: m_doc( &xml )
		, m_baseNode( node )
	{
		if ( m_baseNode != nullptr )
		{
			Uint32 count = 0;

			// count child elements
			{
				const Document::Node* child = m_doc->GetFirstNode( node, nullptr );
				while ( child )
				{
					count += 1;
					child = m_doc->GetNextNode( child, nullptr );
				}
			}

			// capture child elements
			m_elements.Reserve( count );
			const Document::Node* child = m_doc->GetFirstNode( node, nullptr );
			while ( child )
			{
				m_elements.PushBack( child );
				child = m_doc->GetNextNode( child, nullptr );
			}
		}
	}

	DocumentNodeArrayIterator::~DocumentNodeArrayIterator()
	{
	}

	const Uint32 DocumentNodeArrayIterator::Size() const
	{
		return m_elements.Size();
	}

	const Document::Node* DocumentNodeArrayIterator::operator[]( const Uint32 index ) const
	{
		return m_elements[ index ];
	}

	Bool TryGetAttributeValue( const Document& doc, const Document::Node* node, const AnsiChar* attributeName, red::String& value )
	{
		const auto attribute = doc.GetFirstAttribute( node, attributeName );
		if ( !attribute )
		{
			return false;
		}

		const auto attributeValue = doc.GetAttributeValue( attribute );
		if ( !attributeValue )
		{
			return false;
		}

		value = attributeValue;

		return true;
	}

} // xml
