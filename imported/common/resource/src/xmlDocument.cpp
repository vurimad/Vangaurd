/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "xmlDocument.h"
#include "../../redReflection/include/stringParser.h"

#include "rapidXml.h"

using red::DynArray;

namespace xml
{

	// RapidXML types
	typedef rapidxml::xml_document<char>	TXMLDocument;
	typedef rapidxml::xml_node<char>		TXMLNode;
	typedef rapidxml::xml_attribute<char>	TXMLAttribute;

	/// XML raw reader based on the RapidXML interface
	class DocumentRapidXML : public Document
	{
	public:
		DocumentRapidXML( AnsiChar* data, TFreeFunction freeFunction, TXMLDocument* doc );
		virtual ~DocumentRapidXML();

		/// Get root document node
		virtual const Node* GetRootNode() const override final;

		/// Get parent node
		virtual const Node* GetParentNode( const Node* node ) const override final;

		/// Node access
		virtual const AnsiChar* GetNodeName( const Node* node ) const override final;
		virtual const AnsiChar* GetNodeValue( const Node* node ) const override final;

		/// Attribute access
		virtual const AnsiChar* GetAttributeName( const Attribute* attribute ) const override final;
		virtual const AnsiChar* GetAttributeValue( const Attribute* attribute ) const override final;

		/// Child node iteration
		virtual const Node* GetFirstNode( const Node* node, const AnsiChar* name ) const override final;
		virtual const Node* GetNextNode( const Node* node, const AnsiChar* name ) const override final;

		/// Attribute iteration
		virtual const Attribute* GetFirstAttribute( const Node* node, const AnsiChar* name ) const override final;
		virtual const Attribute* GetNextAttribute( const Attribute* attr, const AnsiChar* name ) const override final;

		/// Report error
		virtual void Error( STATIC_CHECK_PRINTF_MSC const AnsiChar* txt, ... ) const;

		/// Bind error function with the document
		void SetErrorFunc( TErrorFunction errorFunc );

	private:
		AnsiChar*			m_data; // owned
		TFreeFunction		m_freeFunction;

		TXMLDocument*			m_doc;
		DynArray< TXMLNode* >	m_nodes{ red::PoolEngine() };

		TErrorFunction		m_errorFunction;

		RED_FORCE_INLINE static const Node* ToNode( const TXMLNode* node )
		{
			return (const Node*) node;
		}

		RED_FORCE_INLINE static const TXMLNode* FromNode( const Node* node )
		{
			return (const TXMLNode*) node;
		}

		RED_FORCE_INLINE static const Attribute* ToAttribute( const TXMLAttribute* attr )
		{
			return (const Attribute*) attr;
		}

		RED_FORCE_INLINE static const TXMLAttribute* FromAttribute( const Attribute* attr )
		{
			return (const TXMLAttribute*) attr;
		}
	};

	//-----------------------------------------------------------------------------

	Document::~Document()
	{
	}

	//-----------------------------------------------------------------------------

	const Float StaticString::ToFloat( const Float defaultValue/*= 0.0f*/ ) const
	{
		Float ret = defaultValue;
		const AnsiChar* txt = AsChar();
		GParseFloat( txt, ret );
		return ret;
	}

	const Int32 StaticString::ToInt32( const Int32 defaultValue /*= 0*/ ) const
	{
		Int32 ret = defaultValue;
		const AnsiChar* txt = AsChar();
		GParseInteger( txt, ret );
		return ret;
	}

	const Bool StaticString::ToBool( const Bool defaultValue /*= false*/ ) const
	{
		Bool ret = defaultValue;
		const AnsiChar* txt = AsChar();
		GParseBool( txt, ret );
		return ret;
	}

	//-----------------------------------------------------------------------------

	NodeIterator::NodeIterator( const Document& doc, const Document::Node* node, const AnsiChar* nodeName /*= nullptr*/ )
		: m_doc( &doc )
		, m_node( node )
		, m_current( node ? doc.GetFirstNode( node, nodeName ) : nullptr )
		, m_name( nodeName )
	{}

	NodeIterator::NodeIterator( const NodeIterator& node, const AnsiChar* nodeName /*= nullptr*/ )
		: m_doc( &node.GetDocument() )
		, m_node( node.GetCurrent() )
		, m_current( node.GetCurrent() ? node.GetDocument().GetFirstNode( node.GetCurrent(), nodeName ) : nullptr )
		, m_name( nodeName )
	{}

	const StaticString NodeIterator::GetName() const
	{
		return GetCurrent() ? m_doc->GetNodeName( GetCurrent() ) : "";
	}

	const StaticString NodeIterator::GetValue() const
	{
		return GetCurrent() ? m_doc->GetNodeValue( GetCurrent() ) : "";
	}

	void NodeIterator::Advance()
	{
		if ( m_current )
			m_current = m_doc->GetNextNode( m_current, m_name );
	}

	//-----------------------------------------------------------------------------

	AttributeIterator::AttributeIterator( const Document& doc, const Document::Node* node, const AnsiChar* attrName /*= nullptr*/ )
		: m_doc( &doc )
		, m_node( node )
		, m_current( node ? doc.GetFirstAttribute( node, attrName ) : nullptr )
		, m_name( attrName )
	{}

	AttributeIterator::AttributeIterator( const NodeIterator& node, const AnsiChar* attrName /*= nullptr*/ )
		: m_doc( &node.GetDocument() )
		, m_node( node.GetCurrent() )
		, m_current( node.GetCurrent() ? node.GetDocument().GetFirstAttribute( node.GetCurrent(), attrName ) : nullptr )
		, m_name( attrName )
	{
	}

	const StaticString AttributeIterator::GetName() const
	{
		return GetCurrent() ? m_doc->GetAttributeName( GetCurrent() ) : "";
	}

	const StaticString AttributeIterator::GetValue() const
	{
		return GetCurrent() ? m_doc->GetAttributeValue( GetCurrent() ) : "";
	}

	void AttributeIterator::Advance()
	{
		if ( m_current )
			m_current = m_doc->GetNextAttribute( m_current, m_name );
	}

	//-----------------------------------------------------------------------------

	DocumentRapidXML::DocumentRapidXML( AnsiChar* data, TFreeFunction freeFunction, TXMLDocument* doc )
		: m_data( data )
		, m_freeFunction( freeFunction )
		, m_doc( doc )
	{
	}

	DocumentRapidXML::~DocumentRapidXML()
	{
		// delete XML structure
		RED_DELETE( m_doc, red::PoolEngine );

		// cleanup memory
		if ( m_freeFunction )
			m_freeFunction( m_data );

		// reset state
		m_data = nullptr;
	}

	const DocumentRapidXML::Node* DocumentRapidXML::GetRootNode() const
	{
		if ( !m_doc )
			return nullptr;

		return (const Node*) m_doc->first_node();
	}

	const DocumentRapidXML::Node* DocumentRapidXML::GetParentNode( const Node* node ) const
	{
		if ( !node )
			return nullptr;

		return ToNode( FromNode(node)->parent() );
	}

	const AnsiChar* DocumentRapidXML::GetNodeName( const Node* node ) const
	{
		if ( !node )
			return nullptr;

		return FromNode(node)->name();
	}

	const AnsiChar* DocumentRapidXML::GetNodeValue( const Node* node ) const
	{
		if ( !node )
			return nullptr;

		return FromNode(node)->value();
	}

	const AnsiChar* DocumentRapidXML::GetAttributeName( const Attribute* attribute ) const
	{
		if ( !attribute )
			return nullptr;

		return FromAttribute(attribute)->name();
	}

	const AnsiChar* DocumentRapidXML::GetAttributeValue( const Attribute* attribute ) const
	{
		if ( !attribute )
			return nullptr;
		return FromAttribute(attribute)->value();
	}

	const DocumentRapidXML::Node* DocumentRapidXML::GetFirstNode( const Node* node, const AnsiChar* name ) const
	{
		if ( !node )
			return nullptr;

		return ToNode( FromNode(node)->first_node( name ) );
	}

	const DocumentRapidXML::Node* DocumentRapidXML::GetNextNode( const Node* node, const AnsiChar* name ) const
	{
		if ( !node )
			return nullptr;

		return ToNode( FromNode(node)->next_sibling(name) );
	}

	const DocumentRapidXML::Attribute* DocumentRapidXML::GetFirstAttribute( const Node* node, const AnsiChar* name ) const
	{
		if ( !node )
			return nullptr;

		return ToAttribute( FromNode(node)->first_attribute( name ) );
	}

	const DocumentRapidXML::Attribute* DocumentRapidXML::GetNextAttribute( const Attribute* attr, const AnsiChar* name ) const
	{
		if ( !attr )
			return nullptr;

		return ToAttribute( FromAttribute(attr)->next_attribute(name) );
	}

	STATIC_CHECK_USE_DECL
	void DocumentRapidXML::Error( const AnsiChar* txt, ... ) const
	{
		char buf[ 1024 ];
		va_list args;

		va_start( args, txt );
		red::VSPrintF( buf, RED_ARRAY_COUNT(buf), txt, args );
		va_end( args );

		if ( m_errorFunction )
		{
			m_errorFunction(0, buf); // TODO: String should not be created here
		}
		else
		{
			RED_LOG_ERROR( "Core: XML parsing error: %hs", buf );
		}
	}

	void DocumentRapidXML::SetErrorFunc( TErrorFunction errorFunc )
	{
		m_errorFunction = errorFunc;
	}

	//-----------------------------------------------------------------------------

	Document* Document::Create( AnsiChar* dataBuffer, TFreeFunction freeFunction )
	{
		// no data
		if ( !dataBuffer )
			return nullptr;

		// parse XML
		TXMLDocument* doc = RED_NEW( TXMLDocument, red::PoolEngine )();
		rapidxml::ResetErrors();
		doc->parse<0>( dataBuffer );
		if ( rapidxml::HasError() )
		{
			if( freeFunction )
			{
				freeFunction( dataBuffer );
			}

			RED_DELETE( doc, red::PoolEngine );
			return nullptr;
		}

		// create wrapper
		return RED_NEW( DocumentRapidXML )( dataBuffer, freeFunction, doc );
	}

	Document* Document::Create( IFile* file, const Uint32 sizeToRead )
	{
		// no file
		if ( !file )
			return nullptr;

		// get size of the data to read
		const Uint32 dataLeft = Min< Uint32 >( (Uint32)( file->GetSize() - file->GetOffset() ), sizeToRead );
		if ( dataLeft == 0 )
		{
			return nullptr;
		}

		// allocate buffer
		AnsiChar* buffer = (AnsiChar*) RED_ALLOCATE( red::PoolEngine, sizeof(AnsiChar) * (dataLeft+1) );

		// load data
		file->Serialize( buffer, dataLeft );
		buffer[ dataLeft ] = 0;

		// parse file
		return Create( buffer, [](void* data) { RED_FREE( red::PoolEngine, data); } );
	}

	Document* Document::Create( red::UniquePtr<IFile> file, const Uint32 sizeToRead )
	{
		return Create( file.Get() );
	}

} // xml