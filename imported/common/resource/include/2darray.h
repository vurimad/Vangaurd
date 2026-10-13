/**
* Copyright (c) 2007-16 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../../common/redReflection/include/resource.h"
#include "../../../common/redReflection/include/stringRTTI.h"

/// CSV file, a 2D array with strings
class RED_RESOURCE_API C2dArray : public CResource
{
	RTTI_DECLARE_TYPE( C2dArray );
	IMPLEMENT_RESOURCE_INTERFACE( "csv", "csv", "2D Array" );
	RED_USE_MEMORY_POOL( red::PoolEngine );

	friend class Saver2dArray;
	friend class Loader2dArray;

protected:
	typedef red::DynArray< red::String >	TStringArray;
	typedef red::DynArray< TStringArray >		TStringArray2D;
	
	TStringArray		m_headers;	//!< columns name
	TStringArray2D		m_data;		//!< array data[col][row]

public:
	C2dArray();

	//! Serialize object ( binary form )
	void OnSerialize( IFile &file );

	static void Copy( C2dArray& dest, const C2dArray& src );
	//! if headers are different return false and do not concatenate 2darrays
	static Bool Concatenate( C2dArray& dest, const C2dArray& src );

	TStringArray2D& GetData() { return m_data; }
	const TStringArray2D& GetData() const { return m_data; }
	void SetData( const TStringArray2D& data ) { m_data = data; }

	red::String GetValue( Uint32 col, Uint32 row ) const;
	const red::String& GetValueRef( Uint32 col, Uint32 row ) const;
	red::String GetValue( const red::String &header, Uint32 row ) const;
	red::String GetHeader( Uint32 col ) const;
	TStringArray GetHeaders() const { return m_headers; }
	
	/*
		+--------+-----+-----+---------+	GetValue(Name, Name2, Attack) -> return att2
		|Name(1) | AAA | BBB |Attack(3)|			  (1)   (2)     (3)				 (4)
		+--------+-----+-----+---------+	
		|Name1   | a1  | b1  | att1    |
		|Name2(2)| a2  | b2  | att2(4) |
		|Name3   | a3  | b3  | att3    |
		+--------+-----+-----+---------+
	*/
	red::String GetValue( const red::String& keyFirstColName, const red::String& keyFirstColValue, const red::String& keySecondColName ) const;

    Bool FindHeader( const red::String &header, Uint32 &col ) const;

	Bool SetValue( red::String value, Uint32 col, Uint32 row );
	Bool SetValue( red::String value, const red::String& header, Uint32 row );
	Bool SetHeader( red::String value, Uint32 col );
	
	// See GetValue
	Bool SetValue( red::String value, const red::String& keyFirstColName, const red::String& keyFirstColValue, const red::String& keySecondColName );

	void AddColumn(const red::String& rowDefaultValue = red::String::EMPTY() );
	void AddColumn( red::String colName, const red::String& rowDefaultValue);
	void AddRow( const red::String& rowDefaultValue = red::String::EMPTY() );
	void AddRow( const red::DynArray<red::String>& rowData, const red::String& rowDefaultValue = red::String::EMPTY());

	void InsertColumn( Uint32 colNum, const red::String& rowDefaultValue = red::String::EMPTY() );
	void InsertColumn( Uint32 colNum, red::String colName, const red::String& rowDefaultValue );
	void InsertColumn( red::String colPrevName, red::String colName, const red::String& rowDefaultValue = red::String::EMPTY() );
	void InsertRow( Uint32 rowNum, const red::String& rowDefaultValue = red::String::EMPTY() );
	void InsertRow( Uint32 rowNum, const red::DynArray<red::String>& rowData, const red::String& rowDefaultValue = red::String::EMPTY() );

	void DeleteColumn( red::String colName );
	void DeleteColumn( Uint32 colNum );
	void DeleteRow( Uint32 rowNum );

	void GetSize( Uint32 &colSize, Uint32 &rowSize ) const;
	Uint32 GetNumberOfRows() const { return m_data.Size(); }
	Uint32 GetNumberOfColumns() const { return m_headers.Size(); }
	Bool Empty() const ;
	void Clear();

	void Refresh();

	Bool HasUniqueRow( Uint32 rowNum ) const;
	Bool HasUniqueColumnNames() const;
	Bool HasUniqueColumn( Uint32 colNum ) const;
	Bool HasUniqueColumn( const red::String& colName ) const;

	// Returns the index of the first column with given header, or -1 if no such header found
	Int32 GetColumnIndex( const red::String &header ) const;
	Int32 GetRowIndex( Int32 col, const red::String &value ) const;
	Int32 GetRowIndex( const red::String &header, const red::String &value) const;

	void ParseData( red::StringView strData, const red::String separator = ";" );

protected:
	void PrepareData( red::String& strData, const red::String& separator = ";" );
	void PrepareData( char* strData, const char separator = ';' );

	Uint32 CalculateDataLength();

	void WriteRow( red::String& data, const red::String& separator, const TStringArray& rowData);
	Bool Split( red::String& source, const red::String& separator, red::String* leftPart, red::String* rightPart );
	void RemoveChars( red::String& source, const red::DynArray<red::String> &chars);

	void ColumnPositions( red::DynArray< Uint32 >& colPos );
	red::String GenerateUniqueHeaderName();
	Bool FindSpecialChar( red::String& str );
	
	void ChangeInternalInvertedCommas( red::String& str, Bool loading );

	virtual void OnModified( Int32 col = 0, Int32 row = 0 ) {};

public:
	template< class T >
	static Bool ConvertValue( const red::String &src, T &dst )
	{
		return FromString( src.AsChar(), dst );
	}

	// a variation of the GetValue function described above. Returns the value of the given type. 
	// Will return 0 if value is not found or can not be converted into the given type.
	template< class T >
	T GetValue( const red::String& keyFirstColName, const red::String& keyFirstColValue, const red::String& keySecondColName ) const
	{
		red::String stringVal = GetValue( keyFirstColName, keyFirstColValue, keySecondColName );
		T value;
		if ( ConvertValue( stringVal, value ) )
		{
			return value;
		}
		else 
		{
			return 0;
		}
	}

	template< class T >
	red::DynArray< T > GetValues( const red::String& keyFirstColName, const red::String& keyFirstColValue, const red::String& keySecondColName) const
	{ 
		red::DynArray< T > values{ red::PoolEngine() };
		Uint32 destColNum = 0;
		while ( destColNum < m_headers.Size() && m_headers[ destColNum ] != keySecondColName )
		{
			destColNum++;
		}

		for( Uint32 colNum = 0; colNum < m_headers.Size(); colNum++ )
		{
			if ( m_headers[ colNum ] == keyFirstColName )
			{
				for ( Uint32 rowNum = 0; rowNum < m_data.Size(); rowNum++ )
				{
					if ( m_data[ rowNum ][ colNum ] == keyFirstColValue )
					{
						T val;
						if ( !ConvertValue( m_data[ rowNum ][ destColNum ], val ) )
						{
							val = 0;
						}
						values.PushBack( val );
					}
				}
				break;
			}
		}
		return values;
	}

	template< class T >
	red::DynArray< T > GetColumn( const red::String &keyFirstColName ) const
	{
		red::DynArray< T > values{ red::PoolEngine() };
		Uint32 colnum;
		if ( FindHeader( keyFirstColName, colnum ) )
		{
			for ( Uint32 rowNum = 0; rowNum < m_data.Size(); rowNum++ )
			{
				T val;
				if ( !ConvertValue( m_data[ rowNum ][ colnum ], val ) )
				{
					val = 0;
				}
				values.PushBack( val );
			}
		}
		return values;
	}

// Load/Save helper methods;
private:
	RED_INLINE void WriteCell( red::String cellString, char* dataBuffer, Uint32& position, const char separator, Bool appendBrackets, Bool isLastCellInRow );
	RED_INLINE void WriteCharacter( const char& character, char* dataBuffer, Uint32& position );
};

template<>
RED_INLINE Bool C2dArray::ConvertValue<red::String>( const red::String& src, red::String& dst )
{
	dst = src;
	return true;
}


// Structure for passing additional parameters to 2da value selection custom editor

struct S2daValueFilter
{
	red::String					m_columnName; // CSV file column name
	red::DynArray< red::String > m_keywords;   // filters (logic OR - filter is active if one keyword matches)
};

struct S2daValueProperties
{
	C2dArray*						m_array;
	red::String					m_descrColumnName;
	red::String					m_valueColumnName;
	red::DynArray< S2daValueFilter > m_filter{ red::PoolEngine() }; // filters (logic AND - all S2daValueFilter have to match)

	S2daValueProperties()
		: m_array( nullptr )
	{}
};

struct SConst2daValueProperties
{
	THandle< C2dArray >				m_array;
	red::String					m_descrColumnName;
	red::String					m_valueColumnName;
	red::DynArray< S2daValueFilter > m_filters{ red::PoolEngine() }; // filters (logic AND - all S2daValueFilter have to match)

	SConst2daValueProperties()
		: m_array( nullptr )
	{}
};

// interface for communication between 2da value selection custom editor and component, which is the property owner
class I2dArrayPropertyOwner
{
public:
	virtual void Get2dArrayPropertyAdditionalProperties( const rtti::Property* property, SConst2daValueProperties& valueProperties ) = 0;
	virtual Bool SortChoices() const;
};
