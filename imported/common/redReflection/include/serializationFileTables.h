/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#pragma once

namespace serialization
{

	/// File tables for current version only - ment for INPLACE LOADING ONLY
	class RED_REFLECTION_API FileTables
	{	
	public:
		static const Uint32 FILE_MAGIC;
		static const Uint32 FILE_VERSION;

		// flags for the imports
		enum EImportFlags : Uint16
		{
			eImportFlags_Obligatory = RED_FLAG( 0 ), // This is a obligatory import - file will fail to load if this import fails
			eImportFlags_Soft		= RED_FLAG( 2 ), // This is a soft import - do not load it directly
			eImportFlags_Inplace	= RED_FLAG( 3 ), // Resource is inplace. It will be registered to resource loader.
		};

		// flags for the buffers
		enum EBufferFlags : Uint16
		{
			eBufferFlags_HintGPUMemory = RED_FLAG( 0 ),				//!< Hint that buffer can be unpacked directly into GPU memory. May affect final layout position in archives.
			eBufferFlags_HintAutoLoadPC = RED_FLAG( 1 ),				//!< Hint that buffer can be streamed and not loaded right away. May affect final layout position in archives.
			eBufferFlags_HintAutoLoadXboxOne = RED_FLAG( 2 ),				//!< Hint that buffer can be streamed and not loaded right away. May affect final layout position in archives.
			eBufferFlags_HintAutoLoadPS4 = RED_FLAG( 3 ),
		};

		// chunk types
		enum EChunkType : Uint8
		{
			eChunkType_Strings = 0,
			eChunkType_Names = 1,
			eChunkType_Imports = 2,
			eChunkType_Properties = 3,
			eChunkType_Exports = 4,
			eChunkType_Buffers = 5,
			eChunkType_InplaceData = 6,

			eChunkType_MAX = 10, // reserved for the future, total chunks sizeof(Chunk)*10 = 120 bytes
		};

		typedef Uint32				CRCValue;

		#pragma pack(push)
		#pragma pack(4)

		// file chunk
		struct Chunk
		{
			Uint32		m_offset;				// Offset to table
			Uint32		m_count;				// Number of entries in table
			CRCValue	m_crc;					// CRC of the data table (after loading)
		};

		// file header
		struct Header
		{
			Uint32		m_magic;					// File header
			Uint32		m_version;					// Version number
			Uint32		m_flags;					// Generic flags

			Uint64		m_deprecated0;				// TimeStamp WAS NOT DETERMINISTIC - REMOVED
			Uint32		m_gpuSize;					// How much video memory does the underlying resource cost? (if 0 - not a GPU resource at all)
			Uint32		m_objectsEnd;				// Where the object data ends (deserializable data)
			Uint32		m_buffersEnd;				// Where the buffer data ends (total file size)

			CRCValue	m_crc;						// CRC of the header

			Uint32		m_numChunks;				// Number of valid chunks in file
			Chunk		m_chunks[ eChunkType_MAX ]; // Chunks
		};

		// name data
		struct Name
		{
			Uint32		m_string;		// Index in string table (dynamic)
			Uint32		m_hash;			// Name hash (static)
		};

		// import data
		struct Import
		{
			Uint32		m_path;			// Import path (index in string table or HASH if the bit it set), TODO: RESIZE TO Uint64
			Uint16		m_deprecated0;	// DEPRECATED, TO REMOVE
			Uint16		m_flags;		// Import flags
		};

		// export data
		struct Export
		{
			Uint16		m_className;			// Export class (index to name table)
			Uint16		m_deprecated0;			// DEPRECATED, TO REMOVE
			Uint32		m_parent;				// Index of the parent object
			Uint32		m_dataSize;				// Object data size
			Uint32		m_dataOffset;			// Object data offset
			Int32		m_template;				// Template owner object
			CRCValue	m_crc;					// CRC of the object data
		};
		
		// ctremblay: DEPRECATED Need to be removed.
		// property info
		struct Property
		{
			Uint16		m_className;			// Parent class (index to name table)
			Uint16		m_typeName;				// Property type (index to name table)
			Uint16		m_propertyName;			// Property name (index to name table)
			Uint16		m_flags;				// Custom flags (0)
			Uint64		m_hash;					// Property hash to speedup lookup on loading
		};

		// buffer info
		struct Buffer
		{
			Uint16		m_name;					// Name of the buffer (index to name table) - placeholder for the future
			Uint16		m_flags;				// Buffer flags  - placeholder for the future
			Uint32		m_deprecated0;			// 
			Uint32		m_dataOffset;			// Offset to the buffer data (if resident)
			Uint32		m_dataSizeOnDisk;		// Size of the buffer data on disk
			Uint32		m_dataSizeInMemory;		// Size of the buffer data in memory
			CRCValue	m_crc;					// Calculated CRC of the buffer's data
		};

		// inplace resource Information
		struct InplaceResource
		{
			Uint32		m_importIndex;			// Index of the import object that is inplace
			Uint32		m_exportIndex;			// export index of the resource root.
			Uint64		m_padding;				// Some reserved bit for future.
		};

		#pragma pack(pop)

		static_assert( sizeof(Name) == 8, "Structure is read directly into memory. Size cannot be changed without an adapter." );
		static_assert( sizeof(Import) == 8, "Structure is read directly into memory. Size cannot be changed without an adapter." );
		static_assert( sizeof(Export) == 24, "Structure is read directly into memory. Size cannot be changed without an adapter." );
		static_assert( sizeof(Property) == 16, "Structure is read directly into memory. Size cannot be changed without an adapter." );
		static_assert( sizeof(Buffer) == 24, "Structure is read directly into memory. Size cannot be changed without an adapter." );
		static_assert( sizeof(InplaceResource) == 16, "Structure is read directly into memory. Size cannot be changed without an adapter." );

		typedef red::DynArray< AnsiChar > TStringTable;
		typedef red::DynArray< Name > TNameTable;
		typedef red::DynArray< Import >	TImportTable;
		typedef red::DynArray< Export >	TExportTable;
		typedef red::DynArray< Property > TPropertyTable;
		typedef red::DynArray< Buffer >	TBufferTable;
		typedef red::DynArray< InplaceResource > TInplaceResourceTable;

		TStringTable			m_strings;
		TNameTable				m_names;
		TImportTable			m_imports;
		TExportTable			m_exports;
		TPropertyTable			m_properties;
		TBufferTable			m_buffers;
		TInplaceResourceTable	m_inplace;

		Uint32					m_gpuSize;			// How much memory does this resource take in video memory (stored in header when saving)
		Uint32					m_softImportBase;	// legacy only, first soft import index (for merged soft imports)

	public:
		FileTables();

		// Store data to file
		void Save( IFile& file, const Uint64 headerOffset ) const;

		// Load data from file, will return false if validation fails
		Bool Load( IFile& file, Uint32& outVersion );

		Bool CheckHeader( const char* fileNameForDebug, const Header& header, Uint32& outVersion ) const;

	private:
		// Calculate header CRC
		static CRCValue CalcHeaderCRC( const Header& header );
	};

} // serialization