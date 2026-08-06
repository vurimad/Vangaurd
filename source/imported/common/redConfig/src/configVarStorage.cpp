/**
* Copyright (c) 2007-16 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "configVarStorage.h"
#include "../../redFileSystem/include/fileSys.h"
#include "../../redFileSystem/include/fileStringReader.h"


//////////////////////////////////////////////////////////////////////////
// using
using red::String;
using red::AnsiChar;


namespace Config
{
/******** Parse Helper ********/

Bool CParseHelper::IsIdent( const String& str )
{
	const Uint32 length = str.Length();

	if( length > 0 && IsCharNum( str[0] ) )		// Identifiers can't begin with number
		return false;

	for( Uint32 i=0; i<length; ++i )
	{
		if( IsCharAlphaNum( str[i] ) == false )
			return false;
	}

	return true;
}

Bool CParseHelper::IsString( const String& str )
{
	if( IsNumber( str ) == true )
		return false;
	if( IsIdent( str ) == true )
		return false;

	return true;
}

Bool CParseHelper::IsNumber( const String& str )
{
	const Uint32 length = str.Length();

	Uint32 startIdx = 0;
	if( length > 0 && str[0] == '-' )		// First character can be minus
		startIdx++;

	for( Uint32 i=startIdx; i<length; ++i )
	{
		// Is not - is num or dot with previous character as num (so you can't type '.0')
		if( IsCharNum( str[i] ) == false && !(str[i] == '.' && i>0 && IsCharNum( str[i-1] )) )
			return false;
	}

	return true;
}

Bool CParseHelper::IsCharAlpha( const AnsiChar chr )
{
	if ( (chr >= 'a' && chr <= 'z') || (chr >= 'A' && chr <= 'Z') || ( chr == '_' ) )
	{
		return true;
	}

	return  false;
}

Bool CParseHelper::IsCharNum( const AnsiChar chr )
{
	if ( (chr >= '0' && chr <= '9') )
	{
		return true;
	}

	return  false;
}

Bool CParseHelper::IsCharAlphaNum( const AnsiChar chr )
{
	if ( (chr >= 'a' && chr <= 'z') || (chr >= 'A' && chr <= 'Z') || (chr >= '0' && chr <= '9') || ( chr == '_' ) )
	{
		return true;
	}

	return  false;
}

/******** Storage ********/

CConfigVarStorage::CConfigVarStorage()
	: m_groups( red::PoolEngine() )
{
}


CConfigVarStorage::~CConfigVarStorage()
{
	red::alg::ClearPtr( m_groups );
}

Bool CConfigVarStorage::GetEntry( const AnsiChar* groupName, const AnsiChar* varName, String& outValue ) const
{
	Group* group = nullptr;
	if ( m_groups.Find( CalcHash( groupName ), group ) )
	{
		const Entry* entry = group->m_entries.FindPtr( CalcHash( varName ) );
		if ( entry )
		{
			outValue = entry->m_value;
			return true;
		}
	}

	return false;
}

Bool CConfigVarStorage::SetEntry( const AnsiChar* groupName, const AnsiChar* varName, const String& value )
{
	// request group
	Group* group = nullptr;
	const Uint32 groupHash = CalcHash( groupName );
	if ( !m_groups.Find( groupHash, group ) )
	{
		group = RED_NEW( Group )( groupName );
		m_groups.Insert( groupHash, group );
	}

	// request entry
	const Uint32 entryHash = CalcHash( varName );
	Entry* entry = group->m_entries.FindPtr( entryHash );
	if ( entry )
	{
		if ( entry->m_value != value )
		{
			entry->m_value = value;
			m_isModified = true;
		}
	}
	else
	{
		group->m_entries.Insert( entryHash, Entry( varName, value ) );
		m_isModified = true;
	}

	return true;
}

Bool CConfigVarStorage::RemoveGroup( const AnsiChar* groupName )
{
	String groupNameStr = groupName;
	for ( auto it = m_groups.Begin(); it != m_groups.End(); ++it )
	{
		auto& group = it.Value();
		const String& currentGroupName = group->m_name;
		if( currentGroupName == groupNameStr )
		{
			group->m_entries.Clear();
			return true;
		}
	}

	return false;
}

Bool CConfigVarStorage::RemoveEntry( const AnsiChar* groupName, const AnsiChar* varName )
{
	return true;
}

Bool CConfigVarStorage::FilterDifferences( const CConfigVarStorage& base, CConfigVarStorage& outDifference ) const
{
	for ( auto it = m_groups.Begin(); it != m_groups.End(); ++it )
	{
		const auto& groupName = it.Value()->m_name;
		for ( auto jt = it.Value()->m_entries.Begin(); jt != it.Value()->m_entries.End(); ++jt )
		{
			const auto& varName = jt.Value().m_name;
			const auto& varValue = jt.Value().m_value;

			// get base value
			String baseValue;
			if ( base.GetEntry( groupName.AsChar(), varName.AsChar(), baseValue ) )
			{
				// compare values
				if ( baseValue == varValue )
					continue;
			}

			// store in the difference mapping
			outDifference.SetEntry( groupName.AsChar(), varName.AsChar(), varValue );
		}
	}

	// done
	return true;
}

Bool CConfigVarStorage::Load( const red::AbsolutePath& absoluteFilePath )
{
	String contentAnsi;
	Bool result = red::LoadFileToString( absoluteFilePath, contentAnsi );
	if( result == false )
		return false;

	result = LoadFromString( contentAnsi );
	if( result == false )
	{
		RED_LOG_ERROR( "Core: Parsing ini file failed: %hs", absoluteFilePath.AsChar() );
		return false;
	}

	// done
	return true;
}

Bool CConfigVarStorage::Save( const red::AbsolutePath& absoluteFilePath )
{
	// Save config to string
	String contentAnsi = "";
	if( SaveToString( contentAnsi ) == false )
		return false;

	Bool result = red::SaveStringToFile( absoluteFilePath, contentAnsi );
	if( result == false )
		return false;

	return true;
}

Bool CConfigVarStorage::LoadFromString(const String& content)
{
	// Create text reader (simple parser)
	red::CAnsiStringFileReader reader( content );

	while ( !reader.EndOfFile() )
	{
		// parse comment
		if ( reader.ParseKeyword( ";" ) )
		{
			reader.SkipCurrentLine();
			continue;
		}

		// parse section header
		if ( !reader.ParseKeyword( "[") )
		{
			RED_LOG_ERROR( "Core: Parsing ini file: Expected '[' at line %d", reader.GetLine() );
			return false;
		}

		// parse group name
		String groupName;
		if ( !reader.ParseIdent( groupName ) )
		{
			RED_LOG_ERROR( "Core: Parsing ini file: Expected group name at line %d", reader.GetLine() );
			return false;
		}

		// parse section tail
		if ( !reader.ParseKeyword( "]") )
		{
			RED_LOG_ERROR( "Core: Parsing ini file: Expected ']' at line %d in section '%hs'", reader.GetLine(), groupName.AsChar() );
			return false;
		}

		// create the group
		Group* group = nullptr;
		const TNameHash groupHash = CalcHash( groupName );
		if ( !m_groups.Find( groupHash, group ) )
		{
			group = RED_NEW( Group )( groupName.AsChar() );
			m_groups.Insert( groupHash, group );
		}		

		// read the options
		while ( !reader.EndOfFile() )
		{
			// parse comment
			if ( reader.ParseKeyword( ";" ) )
			{
				reader.SkipCurrentLine();
				continue;
			}

			// read the entry name
			String entryName;
			if ( !reader.ParseIdent( entryName ) )
			{
				// end of the list
				break;
			}

			// parse the 'equal' sign
			if ( !reader.ParseKeyword( "=" ) )
			{
				RED_LOG_ERROR( "Core: Parsing ini file: Expected '=' at line %d in section '%hs', entry '%hs'", reader.GetLine(), groupName.AsChar(), entryName.AsChar() );
				return false;
			}

			// parse the value - as a token, should be in the same line
			String entryValue;
			reader.ParseToken( entryValue, /* line break */ false );

			// find existing entry of create new one
			const TNameHash nameHash = CalcHash( entryName );
			Entry* entry = group->m_entries.FindPtr( nameHash );
			if ( entry )
			{
				if ( entry->m_name != entryName )
				{
					RED_LOG_ERROR( "Core: Parsing ini file: Hash collision between '%hs' and '%hs' at line %d in section '%hs'", entryName.AsChar(), entry->m_name.AsChar(),	reader.GetLine(), groupName.AsChar() );
					return false;
				}

				entry->m_value = entryValue.AsChar();
			}
			else
			{
				// Create new entry
				group->m_entries.Insert( nameHash, Entry( entryName.AsChar(), entryValue.AsChar() ) );
			}
		}
	}
	
	return true;
}

Bool CConfigVarStorage::SaveToString( String& output ) const
{
	// process groups - the save order is stable (because we use Map)
	// do not save empty groups
	for ( auto it = m_groups.Begin(); it != m_groups.End(); ++it )
	{
		const Group* group = it.Value();

		// empty ?
		if ( group->m_entries.Empty() )
			continue;

		// header
		output += String::Printf( "[%s]\r\n", group->m_name.AsChar() );

		// values
		for ( auto jt = group->m_entries.Begin(); jt != group->m_entries.End(); ++jt )
		{
			const Entry& entry = jt.Value();
			if ( !entry.m_name.Empty() )
			{
				String ansiValue = entry.m_value.AsChar();
				if( CParseHelper::IsString( ansiValue ) == true )
				{
					// Save as string, within quotation characters
					ansiValue = "\"" + ansiValue + "\"";
					output += String::Printf( "%s=%s\r\n", entry.m_name.AsChar(), ansiValue.AsChar() );
				}
				else
				{
					// Save as number or identifier, without quotation characters
					output += String::Printf( "%s=%s\r\n", entry.m_name.AsChar(), ansiValue.AsChar() );
				}
			}
		}
	}

	// saved
	return true;
}

CConfigVarStorage::TNameHash CConfigVarStorage::CalcHash( const AnsiChar* text )
{
	return red::CalculateHash32( text );
}

CConfigVarStorage::TNameHash CConfigVarStorage::CalcHash( const String& text )
{
	return red::CalculateHash32( text.AsChar() );
}

void CConfigVarStorage::Clear()
{
	m_groups.Clear();
	m_isModified = true;
}

} // Console
