#include "build.h"

#include "../../redFileSystem/include/file.h"
#include "../../redContainers/include/string/string.h"
#include "xmlNewReader.h"

#include "rapidXml.h"

using red::DynArray;

namespace xml
{
	// RapidXML types
	typedef rapidxml::xml_document<char>	TXMLDocument;
	typedef rapidxml::xml_node<char>		TXMLNode;
	typedef rapidxml::xml_attribute<char>	TXMLAttribute;

	/// XML reader based on the RapidXML interface
	class ReaderRapidXML : public Reader
	{
	public:
		ReaderRapidXML( AnsiChar* data, TFreeFunction freeFunction, red::UniquePtr<TXMLDocument, red::PoolEngine> doc );
		virtual ~ReaderRapidXML();

		// Reader interface
		virtual void Error( STATIC_CHECK_PRINTF_MSC const AnsiChar* name, ... ) const override final;
		virtual Bool Open( const AnsiChar* name ) override final;
		virtual Bool Iterate( const AnsiChar* name ) override final;
		virtual Bool IterateNodes( const AnsiChar* name ) override final;
		virtual Bool IterateReverse( const AnsiChar* name ) override final;
		virtual Bool Close() override final;

		virtual Bool PeekAttribute( const AnsiChar* name, String& outValue ) const override final;
		virtual Bool PeekAttribute( const AnsiChar* name, Float& outValue ) const override final;
		virtual Bool PeekAttribute( const AnsiChar* name, Int32& outValue ) const override final;
		virtual Bool PeekAttribute( const AnsiChar* name, Bool& outValue ) const override final;

		virtual String GetAttribute( const AnsiChar* name, const String& defaultValue ) const override final;
		virtual Float GetAttribute( const AnsiChar* name, const Float defaultValue ) const override final;
		virtual Int32 GetAttribute( const AnsiChar* name, const Int32 defaultValue ) const override final;
		virtual Bool GetAttribute( const AnsiChar* name, const Bool defaultValue ) const override final;

		virtual Bool GetValue( String& outValue ) const override final;
		virtual Bool GetValue( Float& outValue ) const override final;
		virtual Bool GetValue( Int32& outValue ) const override final;
		virtual Bool GetValue( Bool& outValue ) const override final;

		virtual Bool GetName( String& outValue ) const override final;

	private:
		AnsiChar*			m_data; // owned
		TFreeFunction		m_freeFunction;

		typedef TXMLNode* (TXMLNode::*get_node)( const char* name, size_t name_size, bool case_sensitive ) const;

		Bool OpenInternal( const AnsiChar* name, get_node func );
		Bool IterateInternal( const AnsiChar* name, get_node open, get_node next );
		Bool IterateNodesInternal(const AnsiChar* name, get_node open);

		const TXMLNode* GetTopNode() const;
		const TXMLAttribute* GetTopAttribute( const AnsiChar* name ) const;

		red::UniquePtr<TXMLDocument, red::PoolEngine> m_doc;
		DynArray< TXMLNode* > m_nodes{ red::PoolEngine() };
	};

	//-----------------------------------------------------------------------------

	Reader::~Reader()
	{
	}

	//-----------------------------------------------------------------------------

	ReaderRapidXML::ReaderRapidXML( AnsiChar* data, TFreeFunction freeFunction, red::UniquePtr<TXMLDocument, red::PoolEngine> doc )
		: m_data( data )
		, m_freeFunction( freeFunction )
		, m_doc( std::move( doc ) )
	{
		if ( m_doc )
		{
			m_nodes.PushBack( m_doc.Get() );
		}
	}

	ReaderRapidXML::~ReaderRapidXML()
	{
		// cleanup memory
		if ( m_freeFunction )
			m_freeFunction( m_data );

		// reset state
		m_data = nullptr;
	}

	STATIC_CHECK_USE_DECL
	void ReaderRapidXML::Error( const AnsiChar* name, ... ) const
	{
		char buf[2048];
		va_list args;

		va_start( args, name );
		red::VSNPrintF( buf, 2048, name, args );
		va_end( args );

		RED_LOG_ERROR( "Core: XML: Parsing error: '%hs'\n", buf );
	}

	RED_INLINE Bool ReaderRapidXML::OpenInternal( const AnsiChar* name, get_node func )
	{
		auto node = GetTopNode();
		if ( !node )
			return false;

		auto child = (node->*func)( name, 0, true );
		if ( !child )
			return false;

		m_nodes.PushBack( child );
		return true;
	}

	Bool ReaderRapidXML::Open( const AnsiChar* name )
	{
		return OpenInternal( name, &TXMLNode::first_node );
	}

	RED_INLINE Bool ReaderRapidXML::IterateInternal( const AnsiChar* name, get_node open, get_node next )
	{
		auto node = GetTopNode();
		if ( !node )
			return false;

		// iteration
		if ( 0 == strcmp( node->name(), name ) )
		{
			auto nextNode = (node->*next)( name, 0, true );
			if ( nextNode )
			{
				m_nodes.Back() = nextNode;
				return true;
			}
			else
			{
				m_nodes.PopBack();
				return false;
			}
		}

		// first node
		return OpenInternal( name, open );
	}

	RED_INLINE Bool ReaderRapidXML::IterateNodesInternal(const AnsiChar* name, get_node open)
	{
		auto node = GetTopNode();
		if (!node)
			return false;

		get_node next;

		if (0 == strcmp(node->name(), name))
		{
			// found parent node
			next = &TXMLNode::first_node;
		}
		else 
		{
			// iterate on child nodes
			next = &TXMLNode::next_sibling;
		}

		// iteration
		auto nextNode = (node->*next)(0, 0, true);
		if (nextNode)
		{
			m_nodes.Back() = nextNode;
			return true;
		}
		else
		{
			m_nodes.PopBack();
			return false;
		}

		// first node
		return OpenInternal(node->name(), open);
	}

	Bool ReaderRapidXML::Iterate( const AnsiChar* name )
	{
		return IterateInternal( name, &TXMLNode::first_node, &TXMLNode::next_sibling );
	}

	Bool ReaderRapidXML::IterateNodes(const AnsiChar* name)
	{
		return IterateNodesInternal(name ,&TXMLNode::first_node);
	}

	Bool ReaderRapidXML::IterateReverse( const AnsiChar* name )  
	{
		return IterateInternal( name, &TXMLNode::last_node, &TXMLNode::previous_sibling );
	}

	Bool ReaderRapidXML::Close()
	{
		if ( m_nodes.Empty() )
			return false;

		m_nodes.PopBack();
		return true;
	}

	Bool ReaderRapidXML::PeekAttribute( const AnsiChar* name, String& outValue ) const
	{
		auto attr = GetTopAttribute( name );
		if ( !attr )
			return false;

		auto value = attr->value();
		if ( !value )
			return false;

		outValue = value;
		return true;
	}

	Bool ReaderRapidXML::PeekAttribute( const AnsiChar* name, Int32& outValue ) const
	{
		auto attr = GetTopAttribute( name );
		if ( !attr )
			return false;

		auto value = attr->value();
		if ( !value )
			return false;

		Int32 number = 0;
		if ( 1 != red::SScanf( value, "%d", &number ) )
			return false;

		outValue = number;
		return true;
	}

	Bool ReaderRapidXML::PeekAttribute( const AnsiChar* name, Float& outValue ) const
	{
		auto attr = GetTopAttribute( name );
		if ( !attr )
			return false;

		auto value = attr->value();
		if ( !value )
			return false;

		Float number = 0.0f;
		if ( 1 != red::SScanf( value, "%f", &number ) )
			return false;

		outValue = number;
		return true;
	}

	Bool ReaderRapidXML::PeekAttribute( const AnsiChar* name, Bool& outValue ) const
	{
		auto attr = GetTopAttribute( name );
		if ( !attr )
			return false;

		auto value = attr->value();
		if ( !value || !value[0] )
			return false;

		if ( 0 == red::StrcmpNC( value, "true" ) )
		{
			outValue = true;
			return true;
		}
		else if ( 0 == red::StrcmpNC( value, "false" ) )
		{
			outValue = false;
			return true;
		}

		int number = 0;
		if ( 1 != red::SScanf( value, "%d", &number ) )
			return false;

		outValue = (number != 0);
		return true;
	}

	String ReaderRapidXML::GetAttribute( const AnsiChar* name, const String& defaultValue ) const
	{
		auto attr = GetTopAttribute( name );
		if ( !attr )
			return defaultValue;

		auto value = attr->value();
		if ( !value )
			return defaultValue;

		return value;
	}

	Int32 ReaderRapidXML::GetAttribute( const AnsiChar* name, const Int32 defaultValue ) const
	{
		auto attr = GetTopAttribute( name );
		if ( !attr )
			return defaultValue;

		auto value = attr->value();
		if ( !value || !value[0] )
			return defaultValue;

		Int32 number = 0;
		if ( 1 != red::SScanf( value, "%d", &number ) )
			return defaultValue;

		return number;
	}

	Float ReaderRapidXML::GetAttribute( const AnsiChar* name, const Float defaultValue ) const
	{
		auto attr = GetTopAttribute( name );
		if ( !attr )
			return defaultValue;

		auto value = attr->value();
		if ( !value || !value[0] )
			return defaultValue;

		Float number = 0;
		if ( 1 != red::SScanf( value, "%f", &number ) )
			return defaultValue;

		return number;
	}

	Bool ReaderRapidXML::GetAttribute( const AnsiChar* name, const Bool defaultValue ) const
	{
		auto attr = GetTopAttribute( name );
		if ( !attr )
			return defaultValue;

		auto value = attr->value();
		if ( !value || !value[0] )
			return defaultValue;

		if ( 0 == red::StrcmpNC( value, "true" ) )
		{
			return true;
		}
		else if ( 0 == red::StrcmpNC( value, "false" ) )
		{
			return false;
		}

		Int32 number = 0;
		if ( 1 != red::SScanf( value, "%d", &number ) )
			return defaultValue;

		return (number != 0);
	}

	Bool ReaderRapidXML::GetValue( String& outValue ) const
	{
		auto node = GetTopNode();
		if ( !node )
			return false;

		outValue = node->value();
		return true;
	}

	Bool ReaderRapidXML::GetValue( Float& outValue ) const
	{
		auto node = GetTopNode();
		if ( !node )
			return false;

		Float number = 0.0f;
		if ( 1 != red::SScanf( node->value(), "%f", &number ) )
			return false;

		outValue = number;
		return true;
	}

	Bool ReaderRapidXML::GetValue( Int32& outValue ) const
	{
		auto node = GetTopNode();
		if ( !node )
			return false;

		Int32 number = 0;
		if ( 1 != red::SScanf( node->value(), "%d", &number ) )
			return false;

		outValue = number;
		return true;
	}

	Bool ReaderRapidXML::GetValue( Bool& outValue ) const
	{
		auto node = GetTopNode();
		if ( !node )
			return false;

		if ( 0 == red::StrcmpNC( node->value(), "true" ) )
		{
			outValue = true;
			return true;
		}
		else if ( 0 == red::StrcmpNC( node->value(), "false" ) )
		{
			outValue = false;
			return true;
		}

		Int32 number = 0;
		if ( 1 != red::SScanf( node->value(), "%d", &number ) )
			return false;

		outValue = (number != 0);
		return true;
	}

	Bool ReaderRapidXML::GetName(String& outValue) const
	{
		auto top = GetTopNode();
		if (!top)
			return false;

		auto value = top->name();
		if (!value)
			return false;

		outValue = value;
		return true;
	}

	const TXMLNode* ReaderRapidXML::GetTopNode() const
	{
		if ( !m_doc )
			return nullptr;

		if ( m_nodes.Empty() )
			return nullptr;

		return m_nodes.Back();
	}

	const TXMLAttribute* ReaderRapidXML::GetTopAttribute( const AnsiChar* name ) const
	{
		auto top = GetTopNode();
		if ( !top )
			return nullptr;

		return top->first_attribute( name );
	}

	red::UniquePtr<Reader> Reader::Create( AnsiChar* dataBuffer, TFreeFunction freeFunction )
	{
		// no data
		if ( !dataBuffer )
			return nullptr;

		// parse XML
		red::UniquePtr<TXMLDocument, red::PoolEngine > doc = red::CreateUniquePtr< TXMLDocument, red::PoolEngine >();
		rapidxml::ResetErrors();
		doc->parse<0>( dataBuffer );
		if ( rapidxml::HasError() )
		{
			freeFunction( dataBuffer );
			return nullptr;
		}

		// create wrapper
		return red::UniquePtr<Reader>( RED_NEW( ReaderRapidXML )( dataBuffer, freeFunction, std::move( doc ) ) );
	}

	red::UniquePtr<Reader> Reader::Create( IFile* file, const Uint32 sizeToRead )
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
		return Create( buffer, [](void* data) { RED_FREE( red::PoolEngine, data ); } );
	}

	red::UniquePtr<Reader> Reader::Create( red::UniquePtr<IFile> file, const Uint32 sizeToRead )
	{
		return Create( file.Get() );
	}

} // xml