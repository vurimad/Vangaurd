/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

class IFile;

/// Script data format, used to save compiled scripts
class CScriptDataFormat
{
public:
	CScriptDataFormat();
	~CScriptDataFormat();

	Bool ValidateHeader( IFile& file );

	/// Load data from file stream
	const Bool Load( IFile& file );

	/// Save data to fie
	const Bool Save( IFile& file ) const;

public:
	static const Uint32 FILE_MAGIC;
	static const Uint32 FILE_VERSION;

	// timestamp type
	typedef red::DateTime TimeStamp;
	typedef Uint32				CRCValue;

#pragma pack(push)
#pragma pack(4)

	// chunk types
	enum class EChunkType : Uint8
	{
		Strings = 0,
		Names = 1,
		TweakDBIDs = 2,
		ResRefs = 3,
		Objects = 4,

		MAX = 5,
	};

	// file chunk
	struct Chunk
	{
		Uint32		m_offset;				// Offset to table
		Uint32		m_count;				// Number of elements
		CRCValue	m_crc;					// CRC of the data stored in chunk
	};

	// file header
	struct Header
	{
		Uint32		m_magic;					// File header
		Uint32		m_version;					// Version number
		Uint32		m_flags;					// Generic flags

		TimeStamp	m_timeStamp;				// When was this file saved
		Uint32		m_buildVersion;				// Version number of the tool used to save this file (CL number)

		CRCValue	m_crc;						// CRC of the header

		Uint32		m_numChunks;				// Number of valid chunks in file
		Chunk		m_chunks[ (Uint32)EChunkType::MAX ]; // Chunks
	};

	// name data
	struct Name
	{
		Uint32		m_string;		// Index in string table (dynamic)
	};

	struct TweakDBID
	{
		Uint32		m_string;		// Index in tweak DBID table (dynamic)
	};

	struct ResRef
	{
		Uint32		m_string;		// Index in resref table (dynamic)
	};

	// imported type information
	struct Object
	{
		Uint32		m_name;			// type name
		Uint32		m_parent;		// parent object
		Uint32		m_dataOffset;	// offset to data
		Uint32		m_dataSize;		// size of data
		Uint16		m_type;			// type type :)
	};
	
#pragma pack(pop)

	typedef red::DynArray< AnsiChar >		TStringTable;
	typedef red::DynArray< Name >			TNameTable;
	typedef red::DynArray< TweakDBID >		TTweakDBIDTable;
	typedef red::DynArray< ResRef >			TResRefTable;
	typedef red::DynArray< Object >			TObjectTable;

	TStringTable			m_strings;
	TNameTable				m_names;
	TTweakDBIDTable			m_tweakDBIDIs;
	TResRefTable			m_resRefs;
	TObjectTable			m_objects;

private:
	// Calculate header CRC
	static CRCValue CalcHeaderCRC( const Header& header );
};