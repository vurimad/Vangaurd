/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../../common/redContainers/include/map.h"

/// Basic URL parser
class RED_CORE_API CBasicURL
{
public:
	CBasicURL();
	CBasicURL( const CBasicURL& other );
	CBasicURL( CBasicURL&& other );

	// Parse URL from string
	static CBasicURL Parse( const String& text, const Bool isRelative = false );

	// Parse parameters (POST payload)
	static CBasicURL ParseParams( const String& text );

	// Decode URL string (% encoding)
	static String Decode( const String& text );

	// Set new protocol value
	void SetProtocol( const String& protocol );

	// Set new address value
	void SetAddress( const String& address );

	// Set new path
	void SetPath( const String& path );

	// Set new value of key
	void SetKey( const String& key, const String& value );

	// Set new value of key (integer version)
	void SetKey( const String& key, const Int32 value );

	// Convert URL to full string representation
	String ToString() const;

	// Get protocol name
	RED_INLINE const String& GetProtocol() const { return m_protocol; }

	// Get address
	RED_INLINE const String& GetAddress() const { return m_address; }

	// Get server path
	RED_INLINE const String& GetPath() const { return m_path; }

	// Get number of keys
	RED_INLINE const Uint32 GetNumKeys() const { return m_keys.Size(); }

	// Get key name
	RED_INLINE const red::Map< String, String >& GetKeys() const { return m_keys; }

	// Do we have a key ?
	RED_INLINE const Bool HasKey( const String& key ) const { return m_keys.KeyExist(key); }

	// Get key value
	RED_INLINE const Bool GetKey( const String& key, String& outValue ) const { return m_keys.Find( key, outValue ); }

	// Get key value
	RED_INLINE const Bool GetKey( const String& key, Int32& outValue ) const
	{
		String txt;
		if ( !m_keys.Find( key, txt ) )
			return false;
		outValue = atoi(txt.AsChar());
		return true;
	}

private:
	typedef red::Map< String, String > TKeys;

	static const AnsiChar*		URL_PROTOCOL;	// "://"
	static const AnsiChar		URL_SEPARATOR;	// "/"
	static const AnsiChar		URL_FIRSTKEY;   // "?"
	static const AnsiChar		URL_KEY;		// "&"
	static const AnsiChar		URL_EQUALS;		// "="

	static Bool ParseURLPart( const AnsiChar*& pos, String& out );
	static Bool ParseURLPath( const AnsiChar*& pos, String& out );
	static Bool ParseURLKey( const AnsiChar*& pos, String& out );
	static Bool MatchURLPart( const AnsiChar*& pos, const AnsiChar* txt );
	static Bool MatchURLPart( const AnsiChar*& pos, const AnsiChar ch );

	String m_protocol;		// protocol name (http)
	String m_address;		// address (localhost)
	String m_path;			// full path (test/dupa.html)
	TKeys m_keys{ red::PoolDebug() };			// matched keys (?a=100&b=50)
};