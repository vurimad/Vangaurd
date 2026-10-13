/**
* Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"

#include "toml.h"
#include "../../redContainers/include/string/stringUtils.h"

namespace red
{
	namespace prv
	{
		template< typename T >
		RED_INLINE Bool IsBetween( const T& what, const T& left, const T& right, Bool includingLeft = true, Bool includingRight = true )
		{
			if ( left > right )
				return false;

			return (includingLeft ? what >= left : what > left) && (includingRight ? what <= right : what < right);
		}

		Bool IsValidBareKeyCharacter( const char character )
		{
			return prv::IsBetween( character, 'a', 'z' ) ||
				prv::IsBetween( character, 'A', 'Z' ) ||
				prv::IsBetween( character, '0', '9' ) ||
				character == '_' ||
				character == '-';
		}
	}

	//////////////////////////////////////////////////////////////////////////
	//// Parsing
	//////////////////////////////////////////////////////////////////////////

	struct ParseOutput_KeyValue
	{
		static ParseOutput_KeyValue Error( STATIC_CHECK_PRINTF_MSC const char* format, ... )
		{
			va_list arglist;
			va_start( arglist, format );
			ParseOutput_KeyValue outputValue;
			outputValue.errorMessage = String::PrintfV( format, arglist );
			va_end( arglist );
			return outputValue;
		}

		static ParseOutput_KeyValue Error( StringView errMsg )
		{
			ParseOutput_KeyValue outputValue;
			outputValue.errorMessage = errMsg.ToString();
			return outputValue;
		}

		static ParseOutput_KeyValue Success( StringView key, StringView value )
		{
			ParseOutput_KeyValue outputValue;
			outputValue.key = Trim( key ).ToString();
			outputValue.value = Trim( value ).ToString();
			return outputValue;
		}


		String errorMessage;
		String key; //quotation marks are dropped, if any
		String value; //quotation marks are kept
	};


	ParseOutput_KeyValue ParseKeyValue( StringView line )
	{
		line = Trim( line );

		RED_FATAL_ASSERT( !line.Empty() );

		Bool insideQuotations = false;
		Bool escapedCharacter = false;
		Uint32 equalsPos = StringView::npos;

		Int32 escapedCharacterSequenceCount = -1;

		StringView::const_iterator iter = line.begin();

		while ( iter != line.end() )
		{
			if ( !escapedCharacter )
			{
				if ( escapedCharacterSequenceCount != -1 )
				{
					if ( escapedCharacterSequenceCount != 4 && escapedCharacterSequenceCount != 8 )
					{
						return ParseOutput_KeyValue::Error( "Invalid length of escaped character in 'unicode scalar value' format. Expected 4 or 8, found %i", escapedCharacterSequenceCount );
					}
				}
				else if ( *iter == '\\' )
				{
					if ( !insideQuotations )
						return ParseOutput_KeyValue::Error( "Escaped character is not allowed outside quotation marks or comment blocks." );
					
					escapedCharacter = true;
					iter++;
					continue;
				}
				else if ( *iter == '#' )
				{
					break; //We've reached the comment block; Cut the line at this point.
				}
				else if ( *iter == '\"' )
				{
					insideQuotations = !insideQuotations;
					iter++;
					continue;
				}
				else if ( *iter == '=' )
				{

					if ( equalsPos == StringView::npos )
					{
						RED_FATAL_ASSERT( iter >= line.cbegin() );

						equalsPos = (Uint32)(iter - line.cbegin());
					}

					iter++;
					continue;
				}
				else
				{
					iter++;
					continue;
				}
			}
			else
			{
				if ( escapedCharacterSequenceCount == -1 )
				{
					//Begin the sequence
					if ( *iter == 'u' )
					{
						escapedCharacterSequenceCount = 0;
						iter++;
						continue;
					}
					else
					{
						const char character = *iter;
						Bool ok = false;
						switch ( character )
						{
							case 'b': //Backspsace
							case 't': //Tab
							case 'n': //Linefeed
							case 'f': //Form feed
							case 'r': //Carriage return
							case '"': //Quote
							case '\\'://Backslash
							{
								ok = true;
								break;
							}
							default:
							{
								ok = false;
								break;
							}
						}

						if ( !ok )
						{
							return ParseOutput_KeyValue::Error( "Unrecognized escaped character found: '\\%c'", character );
						}
						else
						{
							escapedCharacter = false;
							iter++;
							continue;
						}
					}
				}
				else
				{
					//Unicode scalar format:
					const char character = *iter;
					Bool ok = false;
					switch ( character )
					{
						case '0':
						case '1':
						case '2':
						case '3':
						case '4':
						case '5':
						case '6':
						case '7':
						case '8':
						case '9':
						case 'A':
						case 'B':
						case 'C':
						case 'D':
						case 'E':
						case 'F':
						{
							ok = true;
							break;
						}
						default:
						{
							ok = false;
							break;
						}
					}

					if ( !ok )
					{
						if ( escapedCharacterSequenceCount != 4 && escapedCharacterSequenceCount != 8 )
							return ParseOutput_KeyValue::Error( "Unrecognized 'unicode scalar value' character found: '%c'", character );
						
						escapedCharacterSequenceCount = -1;
						escapedCharacter = false;
						iter++;
						continue;
					}

					escapedCharacterSequenceCount++;
					iter++;
					continue;
				}
			}

		}

		if ( insideQuotations )
			return ParseOutput_KeyValue::Error( "Quotations weren't closed properly" );

		line = red::StringView{ line.cbegin(), iter };
		if ( line.Empty() )
			return ParseOutput_KeyValue::Error( "Unknown parse error. Line was empty after cutting the comment block out" );

		if ( equalsPos == StringView::npos )
			return ParseOutput_KeyValue::Error( "No '=' sign was found" );

		//Validate Key
		StringView keyView = Trim( line.SubView( 0u, equalsPos ) );
		if ( keyView.StartsWith( '"' ) )
		{
			if ( keyView.EndsWith( '"' ) )
			{
				keyView = keyView.SubView( 1u, keyView.Length() - 2u );
			}
			else
			{
				RED_FATAL( "At this stage this should be impossible." );
			}
		}
		else
		{
			const char* invalidCharacterPtr = std::find_if( keyView.cbegin(), keyView.cend(), [&] ( const char character )
			{
				return !prv::IsValidBareKeyCharacter( character );
			} );

			if ( invalidCharacterPtr != keyView.cend() )
				return ParseOutput_KeyValue::Error( "Invalid character '%c' found! Bare keys may only contain (A-Za-z0-9_-) characters. Use quotation marks if necessary.", *invalidCharacterPtr );
		}

		//Validate Value 
		StringView valueView = Trim( line.SubView( equalsPos + 1 ) );
		
		if ( valueView.StartsWith( '"' ) != valueView.EndsWith( '"' ) )
		{
			RED_FATAL( "At this stage this should be impossible." );
		}

		return ParseOutput_KeyValue::Success( keyView, valueView );
	}

	struct ParseOutput_Table
	{
		static ParseOutput_Table Error( STATIC_CHECK_PRINTF_MSC const char* format, ... )
		{
			va_list arglist;
			va_start( arglist, format );
			ParseOutput_Table outputValue;
			outputValue.errorMessage = String::PrintfV( format, arglist );
			va_end( arglist );
			return outputValue;
		}

		static ParseOutput_Table Error( StringView errMsg )
		{
			ParseOutput_Table outputValue;
			outputValue.errorMessage = errMsg.ToString();
			return outputValue;
		}

		static ParseOutput_Table Success( StringView header )
		{
			if ( header.StartsWith( '[' ) )
			{
				if ( header.EndsWith( ']' ) )
				{
					header = header.SubView( 1u, header.Length() - 2u );
				}
				else
				{
					RED_FATAL( "This should not happen at this point." );
				}
			}

			ParseOutput_Table outputValue;
			outputValue.tableHeader = header.ToString();
			return outputValue;
		}

		String errorMessage;
		String tableHeader;
	};

	ParseOutput_Table ParseTableHeader( StringView line )
	{
		line = Trim( line );

		RED_FATAL_ASSERT( !line.Empty() );

		StringView::const_iterator iter = line.cbegin();

		if ( *iter != '[' )
			return ParseOutput_Table::Error( "Table line does not begin with ']'" );

		iter++;

		const StringView::const_iterator headerNameBegin = iter;

		Bool insideTable = true;
		Bool insideQuote = false;
		StringView::const_iterator endTablePos = nullptr;
		while ( iter != line.cend() )
		{
			if ( insideTable )
			{
				if ( *iter == ']' )
				{
					insideTable = false; //Only comments and white spaces are okay from now on
					endTablePos = iter;
					iter++;
					continue;
				}
				else
				{
					if ( insideQuote )
					{
						if ( *iter == '\\' )
							return ParseOutput_Table::Error( "Escaped characters in table names are not supported yet." );

						if ( *iter == '"' )
						{
							insideQuote = false;
						}

						iter++;
						continue;
					}
					else
					{
						if ( *iter == '"' )
						{
							insideQuote = true;
						}
						else if ( !prv::IsValidBareKeyCharacter( *iter ) )
						{
							return ParseOutput_Table::Error( "Invalid character '%c' found! Bare table headers may only contain (A-Za-z0-9_-) characters. Use quotation marks if necessary.", *iter );
						}

						iter++;
						continue;
					}
				}
			}
			else
			{
				if ( red::IsWhiteSpace( *iter ) )
				{
					iter++;
					continue;
				}
				else if ( *iter == '#' )
				{
					iter++;
					break;
				}
				else
				{
					return ParseOutput_Table::Error( "Invalid character after table found '%c'.", *iter );
				}
			}
		}

		if ( insideTable )
			return ParseOutput_Table::Error( "Missing ']' at this table header." );

		StringView outputTableHeader{ headerNameBegin, endTablePos };
		return ParseOutput_Table::Success( outputTableHeader );
	}

	struct ParseOutput_ArrayTableHeader
	{
		static ParseOutput_ArrayTableHeader Error( STATIC_CHECK_PRINTF_MSC const char* format, ... )
		{
			va_list arglist;
			va_start( arglist, format );
			ParseOutput_ArrayTableHeader outputValue;
			outputValue.errorMessage = String::PrintfV( format, arglist );
			va_end( arglist );
			return outputValue;
		}

		static ParseOutput_ArrayTableHeader Error( StringView errMsg )
		{
			ParseOutput_ArrayTableHeader outputValue;
			outputValue.errorMessage = errMsg.ToString();
			return outputValue;
		}

		static ParseOutput_ArrayTableHeader Success( StringView header )
		{
			if ( header.StartsWith( "[[" ) )
			{
				if ( header.EndsWith( "]]" ) )
				{
					header = header.SubView( 1u, header.Length() - 2u );
				}
				else
				{
					RED_FATAL( "This should not happen at this point." );
				}
			}

			ParseOutput_ArrayTableHeader outputValue;
			outputValue.tableHeader = header.ToString();
			return outputValue;
		}

		String errorMessage;
		String tableHeader;
	};

	ParseOutput_ArrayTableHeader ParseArrayTableHeader( StringView line )
	{
		line = Trim( line );

		RED_FATAL_ASSERT( line.StartsWith( "[[" ), "This should not happen at this point." );

		const Uint32 endTableHeader = line.Find( "]]" );

		if ( endTableHeader == StringView::npos )
			return ParseOutput_ArrayTableHeader::Error( "End of array table ']]' not found." );

		line = line.SubView( 1u, endTableHeader );

		ParseOutput_Table output = ParseTableHeader( line );

		if ( !output.errorMessage.Empty() )
			return ParseOutput_ArrayTableHeader::Error( output.errorMessage );
		else
			return ParseOutput_ArrayTableHeader::Success( output.tableHeader );
	}

	//////////////////////////////////////////////////////////////////////////
	//// TOMLParser definitions
	//////////////////////////////////////////////////////////////////////////

	TOMLParser::TOMLParser( StringView buffer )
		: m_root( CreateHandle< TOMLTable >() )
	{
		DynArray< StringView > lines = StrSplit( buffer, '\n' );

		THandle< TOMLTable > currentTable = m_root;
		String currentTablePrefix = "";

		for ( m_currentLine = 0; m_currentLine < lines.Size(); ++m_currentLine )
		{
			StringView line = lines[m_currentLine];

			line = Trim( line );

			if ( line.Empty() )
			{
				continue;
			}
			else if ( line.StartsWith( '#' ) )
			{
				continue;
			}
			else if ( line.StartsWith( "[[" ) )
			{
				auto result = ParseArrayTableHeader( line );

				if ( !result.errorMessage.Empty() )
					m_parsingErrors.EmplaceBack( m_currentLine + 1, result.errorMessage );
				else
				{
					currentTable = GetOrCreateArrayOfTables( result.tableHeader );
					currentTablePrefix = result.tableHeader + currentTable->GetKey().ToString(); //GetKey() is empty for regular tables, and a number for array tables;
					if ( !currentTable )
					{
						m_parsingErrors.EmplaceBack( m_currentLine + 1, String::Printf( "Invalid table of arrays name! Name already used." ) );
						currentTable = m_root;
					}
				}
			}
			else if ( line.StartsWith( '[' ) )
			{
				auto result = ParseTableHeader( line );

				if ( !result.errorMessage.Empty() )
					m_parsingErrors.EmplaceBack( m_currentLine + 1, result.errorMessage );
				else
				{
					currentTable = GetOrCreateTable( result.tableHeader );
					currentTablePrefix = result.tableHeader;

					if ( !currentTable )
					{
						m_parsingErrors.EmplaceBack( m_currentLine + 1, String::Printf( "Invalid table name! Name already used." ) );
						currentTable = m_root;
					}
				}
			}
			//else if
			//{
			//  //TODO add InlineArrays
			//}
			else 
			{
				auto result = ParseKeyValue( line );

				if ( !result.errorMessage.Empty() )
					m_parsingErrors.EmplaceBack( m_currentLine, result.errorMessage );
				else
				{
					String keyWithTablePrefix = currentTablePrefix + "." + result.key;
					if ( Find( keyWithTablePrefix ) )
					{
						m_parsingErrors.EmplaceBack( m_currentLine, String::Printf( "Duplicate key found! '%s'", keyWithTablePrefix.AsChar() )  );
					}
					else
					{
						currentTable->PushBack( CreateHandle< TOMLKeyValue >( result.key, result.value ) );
					}
				}
			}

		}

	}

	TOMLParser::TOMLParser( const THandle< TOMLTable >& root )
		: m_root( root )
	{}

	TOMLParser::~TOMLParser() = default;

	Uint32 TOMLParser::GetArrayOfTablesSize( const char* key ) const
	{
		auto found = Find( key );
		if ( auto foundArray = Cast< TOMLArrayTable >( found ) )
		{
			auto values = foundArray->GetValues();
			
			return values.Size();
		}

		return 0u;
	}

	Bool TOMLParser::HasParseErrors() const
	{
		return !m_parsingErrors.Empty();
	}

	red::ArraySpan< const TOMLParser::ParseError > TOMLParser::GetParseErrors() const
	{
		return { m_parsingErrors };
	}

	THandle< TOMLBaseValue > TOMLParser::Find( StringView key ) const
	{
		//TODO add support for quotation marks in keys
		if ( key.Find( '\"' ) != StringView::npos )
			return {};

		THandle< TOMLTable > currentTable = m_root;

		Uint32 idx = StringView::npos;
		while ( true )
		{
			const auto currentEntries = currentTable->GetEntries();

			idx = key.Find( '.' );
			if ( idx == StringView::npos )
			{
				auto iter = std::find_if( currentEntries.begin(), currentEntries.end(), [key] ( const THandle< TOMLBaseValue >& v ) {
					return v->GetKey() == key;
				} );

				if ( iter != currentEntries.end() )
					return *iter;
				else
					return nullptr;
			}
			else
			{
				StringView firstPart = key.Slice( 0u, idx );
				

				auto iter = std::find_if( currentEntries.begin(), currentEntries.end(), [firstPart] ( const THandle< TOMLBaseValue >& v ) {
					return v->GetKey() == firstPart;
				} );

				if ( iter != currentEntries.end() )
				{
					key = key.SubView( idx + 1 );
					currentTable = Cast< TOMLTable >( *iter );

					RED_FATAL_ASSERT( currentTable, "Invalid access. Tried to access a non-table TOMLvalue like a table." );
					continue;
				}
				else
				{
					return nullptr;
				}
			}
		}
	}

	//@return nullptr if its a parsing error
	THandle< TOMLTable > TOMLParser::GetOrCreateTable( StringView tableHeader )
	{
		THandle< TOMLBaseValue > alreadyExists = Find( tableHeader );

		if ( alreadyExists )
			return Cast< TOMLTable >( alreadyExists );

		Uint32 lastDotPos = tableHeader.FindReverse( '.' );
		if ( lastDotPos == StringView::npos )
		{
			for ( auto& entry : m_root->GetEntries() )
			{
				if ( entry->GetKey() == tableHeader )
					return Cast< TOMLTable >( entry );
			}

			//If not found, then create a new one
			auto newTable = CreateHandle< TOMLTable >( tableHeader );
			m_root->PushBack( newTable );
			return newTable;
		}
		else
		{
			tableHeader = tableHeader.SubView( 0u, lastDotPos );
			if ( auto table = GetOrCreateTable( tableHeader ) )
			{
				auto newTable = CreateHandle< TOMLTable >( tableHeader );
				table->PushBack( newTable );
				return newTable;
			}
			return nullptr;
		}
	}

	THandle< TOMLTable > TOMLParser::GetOrCreateArrayOfTables( StringView arrayOfTablesHeader )
	{
		THandle< TOMLArrayTable > arrayOfTables;

		THandle< TOMLBaseValue > found = Find( arrayOfTablesHeader );
		if ( found )
		{
			arrayOfTables = Cast< TOMLArrayTable >( found );
		}
		else
		{
			//TODO add quotation marks in tables headers support
			Uint32 lastDot = arrayOfTablesHeader.FindReverse( '.' );
			if ( lastDot == StringView::npos )
			{
				arrayOfTables = CreateHandle< TOMLArrayTable >( arrayOfTablesHeader );
				m_root->PushBack( arrayOfTables );
			}
			else
			{
				StringView subView = arrayOfTablesHeader.SubView( 0u, lastDot );
				auto currTable = GetOrCreateTable( subView );

				if ( currTable )
				{
					arrayOfTables = CreateHandle< TOMLArrayTable >( arrayOfTablesHeader );
					currTable->PushBack( arrayOfTables );
				}
			}
		}

		//Name collision; something else exists of this name that is not a table
		if ( !arrayOfTables )
			return nullptr;

		String name = String::Printf( "%u", arrayOfTables->GetValues().Size() );
		auto newTable = CreateHandle< TOMLTable >( name );
		arrayOfTables->PushBack( newTable );
		return newTable;
	}

}