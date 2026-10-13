/// RenderLab (C) 2015 by Tomasz "RexDex" Jonarski
/// All Right Reserved

#pragma once

#include "../../redReflection/include/types.h"

class IFile;

namespace xml
{

	/// XML raw document reader - no automatic node stacking
	/// NOTE: the advantage is that the whole interface is constant
	/// NOTE: the disadvantage is that the whole XML needs to be in the memory
	/// NOTE: when NULL node is passed the result is always nullptr
	class RED_RESOURCE_API Document : private red::NonCopyable
	{
		RED_USE_MEMORY_POOL( red::PoolEngine );

	public:
		virtual ~Document();

		typedef std::function<void(void*)> TFreeFunction;
		typedef std::function<void(const Uint32 line, const String& error)> TErrorFunction;

		/// XML stuff
		class Node;
		class Attribute;

		/// Get root document node
		virtual const Node* GetRootNode() const = 0;

		/// Get parent node
		virtual const Node* GetParentNode( const Node* node ) const = 0;

		/// Node access
		virtual const AnsiChar* GetNodeName( const Node* node ) const = 0;
		virtual const AnsiChar* GetNodeValue( const Node* node ) const = 0;

		/// Attribute access
		virtual const AnsiChar* GetAttributeName( const Attribute* attribute ) const = 0;
		virtual const AnsiChar* GetAttributeValue( const Attribute* attribute ) const = 0;

		/// Child node iteration
		virtual const Node* GetFirstNode( const Node* node, const AnsiChar* name ) const = 0;
		virtual const Node* GetNextNode( const Node* node, const AnsiChar* name ) const = 0;

		/// Attribute iteration
		virtual const Attribute* GetFirstAttribute( const Node* node, const AnsiChar* name ) const = 0;
		virtual const Attribute* GetNextAttribute( const Attribute* attr, const AnsiChar* name ) const = 0;

		/// Report error
		virtual void Error( STATIC_CHECK_PRINTF_MSC const AnsiChar* txt, ... ) const = 0;

		/// Bind error function with the document
		virtual void SetErrorFunc( TErrorFunction errorFunc ) = 0;

		// create a reader from string held in the memory
		/// NOTE: data is still owned by the caller and should not be deleted while this object is used
		/// NOTE: lambda can be provided to free the memory
		/// NOTE: data buffer will be modified in order to parse the XML
		static Document* Create( AnsiChar* dataBuffer, TFreeFunction freeFunction = nullptr );

		// create a reader from file content
		static Document* Create( IFile* file, const Uint32 sizeToRead = std::numeric_limits<Uint32>::max() );

		// create a reader from file content
		static Document* Create( red::UniquePtr<IFile> file, const Uint32 sizeToRead = std::numeric_limits<Uint32>::max() );
	};

	/// XML string
	class RED_RESOURCE_API StaticString
	{
	public:
		RED_FORCE_INLINE StaticString()
			: m_txt( nullptr )
		{}

		RED_FORCE_INLINE StaticString( const AnsiChar* str )
			: m_txt( str )
		{}

		RED_FORCE_INLINE StaticString( const StaticString& other )
			: m_txt( other.m_txt )
		{}

		RED_FORCE_INLINE ~StaticString()
		{}

		// get content
		RED_FORCE_INLINE const AnsiChar* AsChar() const
		{
			return m_txt ? m_txt : "";
		}

		// compare
		RED_FORCE_INLINE const Bool operator==( const StaticString& other ) const
		{
			return 0 == red::Strcmp( AsChar(), other.AsChar() );
		}

		// compare
		RED_FORCE_INLINE const Bool operator!=( const StaticString& other ) const
		{
			return 0 != red::Strcmp( AsChar(), other.AsChar() );
		}

		// compare with direct string
		RED_FORCE_INLINE const Bool operator==( const AnsiChar* other ) const
		{
			return 0 == red::Strcmp( AsChar(), other ? other : "" );
		}

		// compare with direct string
		RED_FORCE_INLINE const Bool operator!=( const AnsiChar* other ) const
		{
			return 0 != red::Strcmp( AsChar(), other ? other : "" );
		}

		// empty string ?
		RED_FORCE_INLINE const Bool Empty() const
		{
			return !m_txt || !*m_txt;
		}

		// convert to float value (defaults if empty)
		const Float ToFloat( const Float defaultValue = 0.0f ) const;

		// convert to integer value (defaults if empty)
		const Int32 ToInt32( const Int32 defaultValue = 0 ) const;

		// convert to boolean value (defaults if empty)
		const Bool ToBool( const Bool defaultValue = false ) const;

	private:
		const AnsiChar*			m_txt;
	};

	/// Iterator for nodes
	class RED_RESOURCE_API NodeIterator
	{
	public:
		// create node iterator helper, nodeName is optional if you want to filter stuff
		NodeIterator( const Document& doc, const Document::Node* node, const AnsiChar* nodeName = nullptr );
		NodeIterator( const NodeIterator& node, const AnsiChar* nodeName = nullptr );

		// is valid ?
		RED_FORCE_INLINE operator Bool() const { return m_current != nullptr; }

		// get node
		RED_FORCE_INLINE const Document::Node* operator->() const { return m_current; }
			
		// get the document
		RED_FORCE_INLINE const Document& GetDocument() const { return *m_doc; }

		// get the node
		RED_FORCE_INLINE const Document::Node* GetCurrent() const { return m_current; }

		// go to next element
		RED_FORCE_INLINE void operator++() { Advance(); }
		RED_FORCE_INLINE void operator++(int) { Advance(); }

		// get name of the current node
		const StaticString GetName() const;

		// get value (text) of the current node
		const StaticString GetValue() const;

	private:
		const Document*			m_doc;
		const Document::Node*	m_node;
		const Document::Node*	m_current;
		const AnsiChar*			m_name;

		void Advance();
	};

	/// Iterator for attributes
	class RED_RESOURCE_API AttributeIterator
	{
	public:
		// create node iterator helper, nodeName is optional if you want to filter stuff
		AttributeIterator( const Document& doc, const Document::Node* node, const AnsiChar* attrName = nullptr );
		AttributeIterator( const NodeIterator& node, const AnsiChar* attrName = nullptr );

		// is valid ?
		RED_FORCE_INLINE operator Bool() const { return m_current != nullptr; }

		// get the document
		RED_FORCE_INLINE const Document& GetDocument() const { return *m_doc; }

		// get node
		RED_FORCE_INLINE const Document::Attribute* operator->() const { return m_current; }

		// get the node
		RED_FORCE_INLINE const Document::Attribute* GetCurrent() const { return m_current; }

		// go to next element
		RED_FORCE_INLINE void operator++() { Advance(); }
		RED_FORCE_INLINE void operator++(int) { Advance(); }

		// get name of the current attribute
		const StaticString GetName() const;

		// get value (text) of the current attribute
		const StaticString GetValue() const;

	private:
		const Document*				m_doc;
		const Document::Node*		m_node;
		const Document::Attribute*	m_current;
		const AnsiChar*				m_name;

		void Advance();
	};

} // xml
