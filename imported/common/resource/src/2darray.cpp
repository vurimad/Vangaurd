/**
* Copyright (c) 2007-16 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "2darray.h"
#include "../../../common/redReflection/include/scriptStackFrame.h"
#include "../../../common/redReflection/include/scriptingSystem.h"
#include "../../../common/redReflection/include/gatheredResource.h"
#include "../../../common/redReflection/include/resourceUtils.h"
#include "textFormatResourceLoader.h"


//////////////////////////////////////////////////////////////////////////
// usings
using red::String;
using red::DynArray;


#define ALIGN_SIZE 4

RTTI_BEGIN_TYPE( C2dArray );
	RTTI_PARENT_TYPE( CResource );
	RTTI_PROPERTY( m_headers );
	RTTI_PROPERTY( m_data );
RTTI_END_TYPE();

class Loader2dArray : public serialization::TextFormatResourceLoader< C2dArray >
{
public:
	Loader2dArray() = default;
};

RED_RESOURCE_API void Register2DArrayLoader()
{
	serialization::ILoader::RegisterCustomFactory< Loader2dArray >( "csv" );
}

C2dArray::C2dArray()
	: m_headers( red::PoolResource () )
	, m_data( red::PoolResource() )
{
}

void C2dArray::GetSize( Uint32 &colSize, Uint32 &rowSize ) const
{
	colSize = m_headers.Size();
	rowSize = m_data.Size();
}

Bool C2dArray::Empty() const
{
	return m_data.Size()==0 || m_headers.Size()==0;
}

void C2dArray::OnSerialize( IFile &file )
{
	// Pass to base class
	CResource::OnSerialize( file );

	// Serialize internal data
	file << m_headers;

	ArraySerializer::Serialize( file, m_data, TStringArray( red::PoolResource() ) );
}

Uint32 C2dArray::CalculateDataLength()
{
	Uint32 dataLength = 0;

	const Uint32 headersCount = m_headers.Size();
	for ( Uint32 i = 0; i < headersCount; ++i )
	{
		dataLength += static_cast< Uint32 >( m_headers[ i ].Length() + 2 + 1 );
	}

	const Uint32 dataSize = m_data.Size();
	for ( Uint32 j = 0; j < dataSize; ++j )
	{
		const Uint32 dataJSize = m_data[ j ].Size();
		for ( Uint32 k = 0; k < dataJSize; ++k )
		{
			dataLength += static_cast< Uint32 >( m_data[ j ][ k ].Length() + 2 + 1 );
		}
	}
	return dataLength;
}

void C2dArray::PrepareData( String& strData, const String& separator/*=";"*/ )
{
	strData = String::EMPTY();

	if ( Empty() ) 
		return;

	// Headers
	WriteRow( strData, separator, m_headers );
	strData = strData + "\n";

	// Data
	for ( Uint32 j=0; j<m_data.Size(); j++ )
	{
		String str;
		WriteRow( str, separator, m_data[j] );
		strData = strData + str + "\n";
	}

	strData = strData.LeftString( strData.Length() - 1 );
}

void C2dArray::PrepareData( char* strData, const char separator )
{
	if ( Empty() == true )
	{
		return;
	}

	Uint32 writtenChars = 0;

	for ( Uint32 i = 0; i < m_headers.Size(); ++i )
	{
		WriteCell
		(
			m_headers[ i ],
			strData,
			writtenChars,
			separator, 
			FindSpecialChar( m_headers[ i ] ),
			( i == m_headers.Size() - 1 )
		);
	}
	WriteCharacter( '\n', strData, writtenChars );

	for ( Uint32 j = 0; j < m_data.Size(); ++j )
	{
		for ( Uint32 k = 0; k < m_data[ j ].Size(); ++k )
		{
			WriteCell( m_data[ j ][ k ], strData, writtenChars, separator, 
				FindSpecialChar( m_data[ j ][ k ] ), ( k == m_data[ j ].Size() - 1));
		}
		WriteCharacter( '\n', strData, writtenChars );
	}

}

void C2dArray::RemoveChars( String& source, const DynArray<String>& chars )
{
	if ( source.Empty() )
	{
		return;
	}

	Uint32 shift = 0;
	Bool searching;

	const Uint32 sourceSize = source.Length() + 1;
	while ( (sourceSize-shift) > 0 )
	{
		searching = false;
		const Uint32 charsCount = chars.Size();
		for ( Uint32 i=0; i<charsCount; i++ )
		{
			if ( source.AsChar()[shift] == chars[i].AsChar()[0] )
			{
				searching = true;
			}
		}

		if ( searching )
		{
			++shift;
		}
		else
		{
			break;
		}
	}

	source = source.RightString( source.Length() - shift );
}

void C2dArray::ParseData( red::StringView inputData, const String separator )
{
	Uint32 rowIndex = 0;
	Bool readHeaders = true;

	Uint32 currentStart = 0;
	Uint32 currentEnd = 0;
	String currentString = "";
	char currentChar = 0;

	Bool isBracket = false;

	const Uint32 inputDataLength = inputData.Length();

	for ( currentEnd = 0; currentEnd < inputDataLength; ++currentEnd )
	{
		currentChar = inputData[ currentEnd ];
		if ( separator.Contains( currentChar ) && isBracket == false )
		{
			if ( currentEnd == currentStart )
			{
				currentString = "";
			}
			else
			{
				currentString = inputData.Slice( currentStart, currentEnd ).ToString();
			}
			
			// Hacky
			if ( currentString.Length() >= 2 )
			{
				const char* currStr = currentString.AsChar();
				if ( currStr[0] == '\"' && currStr[ currentString.Length()-2 ] == '\"' )
				{
					currentString = currentString.MidString( 1, currentString.Length()-2 );
					ChangeInternalInvertedCommas( currentString, true );
				}
			}

			if ( readHeaders == true )
			{
				m_headers.PushBack( currentString );
			}
			else
			{
				if ( m_data.Size() < rowIndex + 1 )
				{
					//DynArray< String > row;
					m_data.EmplaceBack( red::PoolResource() );
				}
				m_data[ rowIndex ].PushBack( currentString );
			}
			currentStart = currentEnd + 1;
		}
		else if ( currentChar == 0x0A || currentEnd == inputDataLength - 1 )			// If 'new line' or 'last character'
		{
			if ( currentEnd > 0 && inputData[ currentEnd - 1 ] == 0x0D )	// If (...) and one character before is a 'carriage return'
			{
				currentString = inputData.Slice( currentStart, currentEnd - 1 ).ToString();
			}
			else if( currentChar != 0x0A )	// If not 'new line' (that means this is the 'last character' but without new line "\r\n")
			{
				currentString = inputData.Slice( currentStart, currentEnd + 1 ).ToString();
			}
			else	// 'new line' without 'carriage return'
			{
				currentString = inputData.Slice( currentStart, currentEnd ).ToString();
			}

			// Hacky
			if ( currentString.Length() >= 2 )
			{
				const char* currStr = currentString.AsChar();
				if ( currStr[0] == '\"' && currStr[ currentString.Length()-2 ] == '\"' )
				{
					currentString = currentString.MidString( 1, currentString.Length()-2 );
					ChangeInternalInvertedCommas( currentString, true );
				}
			}

			if ( readHeaders == true )
			{
				m_headers.PushBack( currentString );
			}
			else
			{
				if ( m_data.Size() < rowIndex + 1 )
				{
					m_data.EmplaceBack( red::PoolResource() );
				}
				m_data[ rowIndex ].PushBack( currentString );
			}
			currentStart = currentEnd + 1;

			if ( readHeaders == false )
			{
				rowIndex += 1;
			}
			readHeaders = false;
			isBracket = false;
		}
		else if ( currentChar == 34 /* "\"" */ )
		{
			if ( isBracket == false )
			{
				isBracket = true;
			}
			else 
			{
				isBracket = false;
			}
		}
	}

	const Uint32 headersSize = m_headers.Size();
	const Uint32 dataSize = m_data.Size();
	for ( Uint32 i = 0; i < dataSize; ++i )
	{
		m_data[ i ].Resize( headersSize );
	}

	OnModified();
}

// todo:
Bool C2dArray::Split( String& source, const String& separator, String* leftPart, String* rightPart )
{
	Uint32 commaPos = 0;
	Uint32 splitPos = 0;
	Bool FoundSplitPos = false;
	source.IndexOf( "\"", commaPos );

	if ( commaPos == 0 )
	{
		// separator can be inside "", find end of word
		Uint32 i = 1;
		Int32 commaCounter = 0;
		while ( source.Length() - i > 0 )
		{
			if ( source[i] == 34 )
			{
				commaCounter++;
			}
			else
			{
				if ( fmod( (Float)commaCounter, 2.0f) )
				{
					// End of word
					splitPos = i;
					break;
				}
				commaCounter = 0;
			}
			i++;
		}
	}
	else
	{
		FoundSplitPos = source.IndexOf( separator, splitPos );
	}

	// Split
	if ( FoundSplitPos ) 
	{   
		// Get left part
		if ( leftPart ) 
		{
			*leftPart = source.LeftString( splitPos );
		}

		// Get right part
		if ( rightPart )
		{
			*rightPart = source.MidString( splitPos + separator.Length() );
		}
	}

	return FoundSplitPos;   
}

// todo:
void C2dArray::WriteRow( String& data, const String& separator, const TStringArray& rowData )
{
	const Uint32 rowCount = rowData.Size();
	for ( Uint32 i=0; i<rowCount; i++ )
	{
		String str = rowData[i];

		if ( FindSpecialChar(str) ) 
		{
			ChangeInternalInvertedCommas( str, false );

			str = "\"" + str + "\"";
		}

		if ( i != rowCount-1 )
		{
			str = str + separator;
		}

		data = data + str;
		OnModified();
	}
}

// todo:
void C2dArray::ColumnPositions( DynArray< Uint32 >& colPos )
{
	Uint32 pos = 0;

	for (Uint32 colNum=0; colNum<m_headers.Size(); colNum++)
	{
		// Header
		Uint32 len = m_headers[ colNum ].Length();
		if ( FindSpecialChar( m_headers[colNum] ) )
		{
			len += 2;	// Add ""
		}

		// Data
		for (Uint32 i=0; i<m_data.Size(); i++)
		{
			if ( m_data[i][colNum].Length() > len )
			{
				len = m_data[i][colNum].Length();
				if ( FindSpecialChar(m_data[i][colNum]) )
				{
					len += 2;
				}
			}
		}

		// Align
		Uint32 parts = (len-1)/ALIGN_SIZE;
		len = (parts+1)*ALIGN_SIZE;

		// Acc
		pos += len;

		colPos[colNum] = pos;
	}
}

Bool C2dArray::SetHeader( String value, Uint32 col )
{ 
	if (col >= m_headers.Size()) 
	{
		return false;
	}
	else
	{
		m_headers[col] = value; 
		OnModified(col);
		return true;
	}
}

Bool C2dArray::SetValue( String value, Uint32 col, Uint32 row )	
{
	if (Empty() || row >= m_data.Size() || col >= m_data[row].Size()) 
	{
		return false;
	}
	else
	{
		m_data[row][col] = value;
		OnModified(col, row);
		return true;
	}
}

Bool C2dArray::SetValue( String value, const String& header, Uint32 row )
{
	if (Empty() || row >= m_data.Size()) return false;

	for(Uint32 i=0; i<m_headers.Size(); i++)
	{
		if (m_headers[i]==header)
		{
			m_data[row][i] = value;
			OnModified(i, row);
			return true;
		}
	}

	return false;
}

Bool C2dArray::SetValue( String value, const String& keyFirstColName, const String& keyFirstColValue, const String& keySecondColName )
{
	if(Empty()) return false;

	for(Uint32 colNum=0; colNum<m_headers.Size(); colNum++)
	{
		if (m_headers[colNum]==keyFirstColName)
		{
			for (Uint32 rowNum=0; rowNum<m_data.Size(); rowNum++)
			{
				if (m_data[rowNum][colNum]==keyFirstColValue)
				{
					for (Uint32 destColNum=0; destColNum<m_headers.Size(); destColNum++)
					{
						if (m_headers[destColNum]==keySecondColName)
						{
							// Found :)
							m_data[rowNum][destColNum] = value;
							OnModified(destColNum, rowNum);
							return true;
						}
					}
				}
			}
		}
	}

	return false;
}

String C2dArray::GetHeader( Uint32 col ) const
{
	if ( col >= m_headers.Size() ) 
		return String::EMPTY();

	return m_headers[col]; 
}

String C2dArray::GetValue( Uint32 col, Uint32 row ) const
{ 
	if (col >= m_headers.Size() || row >= m_data.Size())
		return String::EMPTY();

	return m_data[row][col]; 
}

const String& C2dArray::GetValueRef( Uint32 col, Uint32 row ) const
{ 
	if (col >= m_headers.Size() || row >= m_data.Size())
		return String::EMPTY();

	return m_data[row][col]; 
}

String C2dArray::GetValue( const String& header, Uint32 row ) const
{ 
	if (row >= m_data.Size())
		return String::EMPTY();

	Uint32 col;
	if ( !FindHeader( header, col ) )
		return String::EMPTY();

	return m_data[row][col]; 
}

String C2dArray::GetValue(const String& keyFirstColName, const String& keyFirstColValue, const String& keySecondColName ) const
{
	if( Empty() )
	{
		return String::EMPTY();
	}

	for(Uint32 colNum=0; colNum<m_headers.Size(); colNum++)
	{
		if (m_headers[colNum]==keyFirstColName)
		{
			for (Uint32 rowNum=0; rowNum<m_data.Size(); rowNum++)
			{
				if (m_data[rowNum][colNum]==keyFirstColValue)
				{
					for (Uint32 destColNum=0; destColNum<m_headers.Size(); destColNum++)
					{
						if (m_headers[destColNum]==keySecondColName)
						{
							// Found :)
							return m_data[rowNum][destColNum];
						}
					}
				}
			}
		}
	}

	return String::EMPTY();
}

Bool C2dArray::FindHeader( const String& header, Uint32& col ) const
{
	for( Uint32 colNum=0; colNum < m_headers.Size(); colNum++ )
		if ( m_headers[colNum] == header )
		{
			col = colNum;
			return true;
		}

		return false;
}

void C2dArray::AddColumn( const String& rowDefaultValue /* = String::EMPTY() */ )
{
	m_headers.PushBack( GenerateUniqueHeaderName() );

	for (Uint32 i=0; i<m_data.Size(); i++)
	{
		m_data[i].PushBack(rowDefaultValue);
	}
}

void C2dArray::AddColumn( String colName, const String& rowDefaultValue )
{
	m_headers.PushBack( colName );

	for (Uint32 i=0; i<m_data.Size(); i++)
	{
		m_data[i].PushBack(rowDefaultValue);
	}
}

void C2dArray::AddRow( const DynArray<String> &rowData, const String& rowDefaultValue /* = String::EMPTY() */)
{
	TStringArray& row = m_data.EmplaceBack( red::PoolResource() );

	for (Uint32 i=0; i<m_headers.Size() && i<rowData.Size(); i++)
	{
		row.PushBack( rowData[i] );
	}

	if (rowData.Size()<m_headers.Size())
	{
		for (Uint32 i=rowData.Size(); i<m_headers.Size(); i++)
		{
			row.PushBack( rowDefaultValue );
		}
	}

	OnModified();
}

void C2dArray::AddRow( const String& rowDefaultValue /* = String::EMPTY() */ )
{
	TStringArray row = m_data.EmplaceBack( red::PoolResource() );

	for (Uint32 i=0; i<m_headers.Size(); i++)
	{
		row.PushBack( rowDefaultValue );
	}
	OnModified();
}

void C2dArray::InsertColumn( Uint32 colNum, const String& rowDefaultValue /* = String::EMPTY() */ )
{
	if (colNum >= m_headers.Size()) return;

	m_headers.InsertAt(colNum, GenerateUniqueHeaderName());

	for (Uint32 i=0; i<m_data.Size(); i++)
	{
		m_data[i].InsertAt(colNum, rowDefaultValue);
	}
	OnModified(colNum);
}

void C2dArray::InsertColumn( Uint32 colNum, String colName, const String& rowDefaultValue )
{
	if (colNum >= m_headers.Size()) return;

	m_headers.InsertAt( colNum, colName );

	for ( Uint32 i=0; i<m_data.Size(); i++ )
	{
		m_data[i].InsertAt(colNum, rowDefaultValue);
	}
	OnModified(colNum);
}

void C2dArray::InsertColumn( String colPrevName, String colName, const String& rowDefaultValue /* = String::EMPTY() */ )
{
	for (Uint32 i=0; i<m_headers.Size(); i++)
	{
		if (m_headers[i]==colPrevName)
		{
			m_headers.InsertAt(i, colName);

			for (Uint32 j=0; j<m_data.Size(); j++)
			{
				m_data[j].InsertAt( i, rowDefaultValue );
			}
			OnModified(i);
		}
	}
}

void C2dArray::InsertRow( Uint32 rowNum, const DynArray<String>& rowData, const String& rowDefaultValue /* = String::EMPTY() */)
{
	TStringArray row{ red::PoolEngine() };
	m_data.InsertAt( rowNum, row );

	for (Uint32 i=0; i<m_headers.Size() && i<rowData.Size(); i++)
	{
		m_data[rowNum].PushBack( rowData[i] );
	}

	if (rowData.Size()<m_headers.Size())
	{
		for (Uint32 i=rowData.Size(); i<m_headers.Size(); i++)
		{
			m_data[rowNum].PushBack( rowDefaultValue );
		}
	}
	OnModified();
}

void C2dArray::InsertRow( Uint32 rowNum, const String& rowDefaultValue /* = String::EMPTY() */ )
{
	TStringArray row{ red::PoolEngine() };
	m_data.InsertAt(rowNum, row);

	for ( Uint32 i=0; i<m_headers.Size(); i++ )
	{
		m_data[rowNum].PushBack( rowDefaultValue );
	}
	OnModified();
}

void C2dArray::DeleteColumn( String colName )
{
	for (Uint32 col=0; col<m_headers.Size(); col++)
	{
		if (m_headers[col]==colName)
		{
			// Headers
			m_headers.Remove( m_headers.Begin()+col );

			// Data
			for (Uint32 row=0; row<m_data.Size(); row++)
			{
				m_data[row].Remove( m_data[row].Begin()+col );
			}

			OnModified(col);
			break;
		}
	}
}

void C2dArray::DeleteColumn( Uint32 colNum )
{
	if (colNum >= m_headers.Size()) return;

	// Headers
	m_headers.Remove( m_headers.Begin()+colNum );

	// Data
	for ( Uint32 row=0; row<m_data.Size(); row++ )
	{
		m_data[row].Remove( m_data[row].Begin()+colNum );
	}
	OnModified(colNum);
}

void C2dArray::DeleteRow( Uint32 rowNum )
{
	if (Empty() || rowNum >= m_data.Size()) return;

	m_data.Remove( m_data.Begin() + rowNum );
	OnModified();
}

Bool C2dArray::HasUniqueColumnNames() const
{
	if (Empty()) return true;

	for ( Uint32 col=0; col<m_headers.Size(); col++ )
	{
		String value = m_headers[col];

		for (Uint32 i=col+1; i<m_headers.Size(); i++)
		{
			if (m_headers[i]==value)
			{
				return false;
			}
		}
	}

	return true;
}

Bool C2dArray::HasUniqueRow( Uint32 rowNum ) const
{
	if (Empty()) return true;
	if (rowNum >= m_data.Size()) return false;

	if (rowNum == (Uint32)-1) return HasUniqueColumnNames();

	for (Uint32 row=0; row<m_data.Size(); row++)
	{
		String value = m_data[rowNum][row];

		for (Uint32 i=row+1; i<m_data[rowNum].Size(); i++)
		{
			if (m_data[rowNum][i]==value)
			{
				return false;
			}
		}
	}

	return true;
}

Bool C2dArray::HasUniqueColumn( Uint32 colNum ) const
{
	if (Empty()) return true;
	if (colNum >= m_headers.Size()) return false;

	for (Uint32 row=0; row<m_data.Size(); row++)
	{
		String value = m_data[row][colNum];

		for (Uint32 i=row+1; i<m_data.Size(); i++)
		{
			if (m_data[i][colNum] == value)
			{
				return false;
			}
		}
	}

	return true;
}

Bool C2dArray::HasUniqueColumn(const String& colName) const
{
	if (Empty()) return true;

	for (Uint32 col=0; col<m_headers.Size(); col++)
	{
		if (colName == m_headers[col])
		{
			return HasUniqueColumn(col);
		}
	}

	return true;
}

void C2dArray::Clear()
{
	m_headers.Clear();

	for (Uint32 row=0; row<m_data.Size(); row++)
	{
		m_data[row].Clear();
	}
	m_data.Clear();
	OnModified();
}

void C2dArray::Refresh()
{
	RED_ASSERT( !"Refreshing 2D array is not supported. Use config or definitions." );
}

String C2dArray::GenerateUniqueHeaderName()
{
	String name = "Col ";
	Int32 num = 0;
	Bool searching = true;

	String uniqueName;

	while(searching)
	{
		searching = false;

		uniqueName = name + String::Printf( "%d", ++num );

		for ( Uint32 i=0; i<m_headers.Size(); i++ )
		{
			if (m_headers[i]==uniqueName)
			{
				searching = true;
				break;
			}
		}
	}

	return uniqueName;
}

Bool C2dArray::FindSpecialChar( String& str )
{
	return ( str.Contains( ';' ) || ( str.Contains( '\"' ) ) );
}

void C2dArray::WriteCell( String cellString, char* dataBuffer, Uint32& position, const char separator, Bool appendBrackets, Bool isLastCellInRow )
{
	if ( appendBrackets == true )
	{
		ChangeInternalInvertedCommas( cellString, false );
		WriteCharacter( '\"', dataBuffer, position );
	}

	red::Memcpy( dataBuffer + position, cellString.Data(), cellString.Length() * sizeof( char ) );
	position += cellString.Length();

	if ( appendBrackets == true )
	{
		WriteCharacter( '\"', dataBuffer, position );
	}

	if ( isLastCellInRow == false )
	{
		WriteCharacter( separator, dataBuffer, position );
	}
}
RED_INLINE void C2dArray::WriteCharacter( const char& character, char* dataBuffer, Uint32& position )
{
	dataBuffer[ position ] = character;
	position += 1;
}

// todo:
void C2dArray::ChangeInternalInvertedCommas( String& str, Bool loading )
{
	if ( loading )
	{
		// Loading
		Uint32 i = 1;
		while ( (str.Length()-i) > 0 )
		{
			if (str[i]==34 && str[i-1]==34 )
			{
				str = str.LeftString(i) + str.RightString( str.Length() - i - 2 );
			}
			i++;
		}
	}
	else
	{
		// Saving
		Uint32 i = 0;
		while ( (str.Length()-i) > 0 )
		{
			if (str[i]==34)
			{
				str = str.LeftString(i) + "\"" + str.RightString( str.Length() - i - 1 );
				i++;
			}
			i++;
		}
	}
}

Int32 C2dArray::GetColumnIndex( const String& header ) const
{
	for ( Uint32 i = 0; i < m_headers.Size(); i++ )
	{
		if ( header == m_headers[ i ] )
		{
			return ( Int32 ) i;
		}
	}
	return -1;
}

Int32 C2dArray::GetRowIndex( Int32 col, const String& value ) const
{
	if (col == -1) return col;

	for (Uint32 row=0; row<m_data.Size(); row++)
	{
		if (m_data[row][col]==value)
		{
			return (Int32)row;
		}
	}

	return -1;
}

Int32 C2dArray::GetRowIndex( const String& header, const String& value ) const
{
	return GetRowIndex( GetColumnIndex( header ), value );
}


Bool I2dArrayPropertyOwner::SortChoices() const
{
	return false;
}

void C2dArray::Copy( C2dArray& dest, const C2dArray& src )
{
	dest.m_headers = src.m_headers;
	dest.m_data = src.m_data;
}

Bool C2dArray::Concatenate( C2dArray& dest, const C2dArray& src )
{
	Uint32 colCount = dest.GetNumberOfColumns();
	if( colCount != src.GetNumberOfColumns() )
	{
		return false;
	}

	//! check are headers the same
	for( Uint32 columnIndex = 0; columnIndex < colCount; ++columnIndex )
	{
		if( red::StrcmpNC( dest.GetHeader( columnIndex ).AsChar(), src.GetHeader( columnIndex ).AsChar() ) != 0 )
		{
			return false;
		}
	}

	Uint32 rowCount = src.GetNumberOfRows();

	DynArray<String> rowData{ red::PoolEngine() };
	rowData.Reserve( colCount );

	for( Uint32 rowIndex = 0; rowIndex < rowCount; ++rowIndex )
	{
		rowData.Clear();
		for( Uint32 columnIndex = 0; columnIndex < colCount; ++columnIndex )
		{
			rowData.PushBack( src.GetValueRef( columnIndex, rowIndex ) );
		}
		dest.AddRow( rowData );			
	}

	return true;
}
