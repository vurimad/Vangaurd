/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#if !defined( NO_DEBUG_PAGES )

#include "../../../common/redMemory/include/sharedPtr.h"
#include "htmlDocument.h"

namespace res
{
	class ResourceProxy;
};

/// HTML document wrapper for debug pages - using this template provides best visual results and makes everything consistent
class CDebugPageHTMLResponse
{
public:
	CDebugPageHTMLResponse( const CBasicURL& fullURL );

	// Get response data - a HTML document
	class CHTTPResponseData* GetData();

public:
	CBasicURL				m_fullURL;
	CBasicTextBuilder		m_text;
};

/// HTML document wrapper for debug pages - using this template provides best visual results and makes everything consistent
class CDebugPageHTMLDocument : public CHTMLDocument
{
public:
	CDebugPageHTMLDocument( CDebugPageHTMLResponse& responseContext, const String& title );
	~CDebugPageHTMLDocument();

private:
	Double			m_startTime;
};

/// Information block - helper class
class RED_CORE_API CDebugPageHTMLInfoBlock : public red::NonCopyable
{
public:
	CDebugPageHTMLInfoBlock( CDebugPageHTMLDocument& doc, STATIC_CHECK_PRINTF_MSC const AnsiChar* title, ... );
	~CDebugPageHTMLInfoBlock();

	CDebugPageHTMLDocument& Info( STATIC_CHECK_PRINTF_MSC const AnsiChar* txt, ... );

private:
	CDebugPageHTMLDocument&		m_doc;
	Bool						m_hasInfoBlockOpened;
};

/// HTML table helper - with sorting
class RED_CORE_API CDebugPageHTMLTable : public red::NonCopyable
{
public:
	CDebugPageHTMLTable( CDebugPageHTMLDocument& doc, const AnsiChar* tableName );

	// row of source data
	class IRow
	{
		RED_USE_MEMORY_POOL( red::PoolDebug );

	public:
		virtual ~IRow() {};
		virtual Bool OnCompare( const IRow* other, const Int32 columnID ) = 0;
		virtual void OnGenerateHTML( CDebugPageHTMLDocument& doc, const Int32 columntID ) = 0;
	};

	// row data
	class IRowData
	{
		RED_USE_MEMORY_POOL( red::PoolDebug );

	public:
		virtual ~IRowData() {}; 
		virtual Bool OnCompare( const IRowData* other ) const = 0;
		virtual void OnGenerateHTML( CDebugPageHTMLDocument& doc ) const = 0;
	};

	// create row data
	static IRowData* CreateRowData( const AnsiChar* txt );
	static IRowData* CreateRowData( const String& txt );
	static IRowData* CreateRowData( const Float value );
	static IRowData* CreateRowData( const Int64 value );
	static IRowData* CreateRowData( const ISerializable* object );
	static IRowData* CreateRowData( const rtti::IType* dataType, const void* data );

	// data
	void AddColumn( const AnsiChar* caption, const Float width=0.0f, const Bool sortable=false, const Bool isVisible=true );
	void AddRow( IRow* row );
	void AddRow( IRowData* a, IRowData* b = nullptr, IRowData* c = nullptr, IRowData* d = nullptr );

	// render the table to the final document
	void Render( const Uint32 tableWidth, const AnsiChar* styleClass, const CBasicURL& url ); // helper - will extract table sorting information from the URL

private:
	CDebugPageHTMLDocument&						m_doc;
	String									m_tableName;

	struct Column
	{
		Bool			m_visible;				// is this column visible ?
		String		m_name;
		Bool			m_sortable;
		Float			m_widthRequested;		// 0.0-1.0 - weighted fraction of the size, >1.0 - absolute pixel width
		Uint32			m_width;
	};

	red::DynArray< red::SharedPtr< IRow > >			m_rows{ red::PoolDebug() };
	red::DynArray< Int32 >							m_rowsOrder{ red::PoolDebug() };
	red::DynArray< Column >							m_columns{ red::PoolDebug() };

	void SetupColumnWidths( const Uint32 tableWidth );
};

/// HTML helper to print property value
class CDebugPageHTMLPropertyDump : public red::NonCopyable
{
public:
	CDebugPageHTMLPropertyDump( CDebugPageHTMLDocument& doc );

	void DumpProperty( const rtti::Property* prop, const void* data );
	void DumpTypedData( const rtti::IType* type, const void* data );

private:
	CDebugPageHTMLDocument&						m_doc;
};

/// HTML helper for bigger lists
class CDebugPageHTMLBigList : public red::NonCopyable
{
public:
	CDebugPageHTMLBigList( CDebugPageHTMLDocument& doc, const AnsiChar* listHeader );
	~CDebugPageHTMLBigList();

	void StartElement();

private:
	CDebugPageHTMLDocument&						m_doc;
	Bool										m_hasElement;
};

/// HTML helper for AJAX forms (yup, I'm sorry)
/// The form is serialized and sent as POST request to given target URL
class CDebugPageHTMLFormHelperAJAX : public red::NonCopyable
{
public:
	CDebugPageHTMLFormHelperAJAX( CDebugPageHTMLDocument& doc, const String& id, const String& target = String::EMPTY() );
	~CDebugPageHTMLFormHelperAJAX();

	// controls creation
	CDebugPageHTMLDocument& InputBox( const String& id, const String& caption, const String& initialText, const Int32 numChars = -1 );
	CDebugPageHTMLDocument& Button( const String& id, const String& caption, const Int32 width=-1, const Int32 height=-1 );
	CDebugPageHTMLDocument& SubmitButton( const String& caption, const Int32 width=-1, const Int32 height=-1 );

private:
	CDebugPageHTMLDocument&				m_doc;
	String							m_id;
};


// Expandable panel with +/- icons to expand/collapse it
class CDebugPageHTMLExpandablePanel : public red::NonCopyable
{
public:
	CDebugPageHTMLExpandablePanel( CDebugPageHTMLDocument& doc, const Bool isExpaneded, STATIC_CHECK_PRINTF_MSC const AnsiChar* title, ... );
	~CDebugPageHTMLExpandablePanel();

private:
	CDebugPageHTMLDocument&				m_doc;
};

class CDebugPageHTMLRowString : public CDebugPageHTMLTable::IRowData
{
public:
	CDebugPageHTMLRowString( const String& txt );
	virtual Bool OnCompare( const IRowData* other ) const;
	virtual void OnGenerateHTML( CDebugPageHTMLDocument& doc ) const; 

protected:
	String		m_data;
};

class CDebugPageHTMLRowInt : public CDebugPageHTMLTable::IRowData
{
public:
	CDebugPageHTMLRowInt( const Int64 value );
	virtual Bool OnCompare( const IRowData* other ) const;
	virtual void OnGenerateHTML( CDebugPageHTMLDocument& doc ) const; 

private:
	Int64		m_data;
};

class CDebugPageHTMLRowFloat : public CDebugPageHTMLTable::IRowData
{
public:
	CDebugPageHTMLRowFloat( const Float value );
	virtual Bool OnCompare( const IRowData* other ) const;
	virtual void OnGenerateHTML( CDebugPageHTMLDocument& doc ) const; 

private:
	Float		m_data;
};

class CDebugPageHTMLRowObject : public CDebugPageHTMLTable::IRowData
{
public:
	CDebugPageHTMLRowObject( const ISerializable* object );
	virtual Bool OnCompare( const IRowData* other ) const;
	virtual void OnGenerateHTML( CDebugPageHTMLDocument& doc ) const; 

private:
	const class ISerializable*	m_object;
};

class CDebugPageHTMLRowFile : public CDebugPageHTMLTable::IRowData
{
public:
	CDebugPageHTMLRowFile( const res::ResourceProxy* file );
	virtual Bool OnCompare( const IRowData* other ) const;
	virtual void OnGenerateHTML( CDebugPageHTMLDocument& doc ) const; 

private:
	String				m_path;
};

class CDebugPageHTMLRowGenericData : public CDebugPageHTMLTable::IRowData
{
public:
	CDebugPageHTMLRowGenericData( const rtti::IType* dataType, const void* data );
	virtual Bool OnCompare( const IRowData* other ) const;
	virtual void OnGenerateHTML( CDebugPageHTMLDocument& doc ) const; 

private:
	const class rtti::IType*	m_type;
	const void*				m_data;
};

#endif
