/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#pragma once

namespace rtti
{
	class IType;
	class ClassType;
}

// File flags
enum EFileFlags
{
	FF_Writer				= RED_FLAG( 0 ),	//!< Stream is writing	
	FF_Reader				= RED_FLAG( 1 ),	//!< Stream is reading
	FF_FileBased			= RED_FLAG( 3 ),	//!< Stream is file based
	FF_MemoryBased			= RED_FLAG( 4 ),	//!< Stream is memory based
	FF_NullBased			= RED_FLAG( 5 ),	//!< Stream is null device. Throws data away
	FF_SaveStream			= RED_FLAG( 8 ),	//!< This is a save stream 
	FF_Cooker				= RED_FLAG( 9 ),	//!< This is cooker serialize
	FF_Mapper				= RED_FLAG( 10 ),	//!< Mapper
	FF_ResourceResave		= RED_FLAG( 11 ),	//!< Resource is being resaved to the same file it was opened from
	FF_Cloner			= RED_FLAG( 12 ),	//!< Resource is being resaved to the same file it was opened from
	FF_ErrorOccured			= RED_FLAG( 19 ),	//!< File is in error state
	FF_ResourceCollector = RED_FLAG( 20 ), //!< Basically a hack for the ResourceCollector class; give it resources it can't discover through the mapper.
};

namespace serialization
{
	class IMapper; // all binary serialization is handled by this class
}

// File based serialization interface
class RED_FILESYSTEM_API IFile
{
	RED_USE_POLYMORPHIC_MEMORY_POOL( red::PoolEngine );

public:
	explicit IFile( Uint32 flags );
	virtual ~IFile();

	// Serialize data buffer of given size
	virtual void Serialize( void* buffer, size_t size )=0;

	// Get position in file stream
	virtual Uint64 GetOffset() const=0;

	// Get size of the file stream
	virtual Uint64 GetSize() const=0;

	// Seek to file position
	virtual void Seek( Int64 offset )=0;

	// Flush any pending writes
	virtual void Flush() = 0;

	// Clear file error flag - use with care and only if you know what you are doing
	virtual void ClearError();

	// Report IO error on this file, puts file into the "Error" mode which can be quieried via HasErrors() and reset via ClearError()
	// NOTE: Only first IO error is reported on the file
	// NOTE: When a file is in error state most of the operation will automatically fail
	void HandleIOError( STATIC_CHECK_PRINTF_MSC const AnsiChar* txt, ... );

public:
	// Get file flags
	RED_INLINE Uint32 GetFlags() const { return m_flags; }

	// Get file version
	RED_INLINE Uint32 GetVersion() const { return m_version; }

	// Is this a writer ?
	RED_INLINE Bool IsWriter() const { return ( m_flags & FF_Writer ) != 0; }

	// Is this a reader
	RED_INLINE Bool IsReader() const { return ( m_flags & FF_Reader ) != 0; }

	// Is this a file based interface
	RED_INLINE Bool IsFileBased() const { return ( m_flags & FF_FileBased) != 0; }

	// Is this a memory based interface
	RED_INLINE Bool IsMemoryBased() const { return ( m_flags & FF_MemoryBased) != 0; }

	// Is this a memory based interface
	RED_INLINE Bool IsNullBased() const { return (m_flags & FF_NullBased) != 0; }

	// Is this a save stream
	RED_INLINE Bool IsSaveStream() const { return ( m_flags & FF_SaveStream ) != 0; }

	// Is this a cooker serialized ?
	RED_INLINE Bool IsCooker() const { return ( m_flags & FF_Cooker) != 0; }

	// Is this a mapper ?
	RED_INLINE Bool IsMapper() const { return ( m_flags & FF_Mapper ) != 0; }

	// Is this a mapper ?
	RED_INLINE Bool IsCloner() const { return (m_flags & FF_Cloner) != 0; }

	// Is this a resource resave ?
	RED_INLINE Bool IsResourceResave() const { return ( m_flags & FF_ResourceResave ) != 0; }

	// Is this file in error state ?
	RED_INLINE Bool HasErrors() const { return ( m_flags & FF_ErrorOccured ) != 0; }

	// Do we have the file mapper ?
	RED_INLINE Bool HasMapper() const { return (m_mapper != nullptr); }

	// Enable/Disable cooker flag
	RED_INLINE void SetCooker( Bool isCooker ) { m_flags = isCooker ? (m_flags | FF_Cooker) : (m_flags & ~FF_Cooker); }

	// Enable/Disable replication flag
	RED_INLINE void SetCloner( Bool isReplication ) { m_flags = isReplication ? (m_flags | FF_Cloner) : (m_flags & ~FF_Cloner); }

	RED_INLINE Bool IsResourceCollector() const { return (m_flags & FF_ResourceCollector) != 0; }

	// Get the serialization mapper (may not exist)
	RED_INLINE serialization::IMapper* GetMapper() const { return m_mapper; }

	void SetInternalMemoryPool( const red::memory::Pool & pool );
	const red::memory::Pool & GetInternalMemoryPool() const;

public:
	// Serialization operators for basic types
	friend RED_FILESYSTEM_API void operator<<( IFile& file, Bool& val )     { file.Serialize( &val, sizeof( val ) ); }
	friend RED_FILESYSTEM_API void operator<<( IFile& file, Int8& val )     { file.Serialize( &val, sizeof( val ) ); }
	friend RED_FILESYSTEM_API void operator<<( IFile& file, Uint8& val )    { file.Serialize( &val, sizeof( val ) ); }
	friend RED_FILESYSTEM_API void operator<<( IFile& file, Int16& val )    { file.Serialize( &val, sizeof( val ) ); }
	friend RED_FILESYSTEM_API void operator<<( IFile& file, Uint16& val )   { file.Serialize( &val, sizeof( val ) ); }
	friend RED_FILESYSTEM_API void operator<<( IFile& file, Int32& val )    { file.Serialize( &val, sizeof( val ) ); }
	friend RED_FILESYSTEM_API void operator<<( IFile& file, Uint32& val )   { file.Serialize( &val, sizeof( val ) ); }
	friend RED_FILESYSTEM_API void operator<<( IFile& file, Int64& val )    { file.Serialize( &val, sizeof( val ) ); }
	friend RED_FILESYSTEM_API void operator<<( IFile& file, Uint64& val )   { file.Serialize( &val, sizeof( val ) ); }
	friend RED_FILESYSTEM_API void operator<<( IFile& file, Float& val )    { file.Serialize( &val, sizeof( val ) ); }
	friend RED_FILESYSTEM_API void operator<<( IFile& file, Double& val )   { file.Serialize( &val, sizeof( val ) ); }
	friend RED_FILESYSTEM_API void operator<<( IFile& file, Char& val )	    { file.Serialize( &val, sizeof( val ) ); }

public:
	// Get the absolute path of the actual file we are reading/writing from - for debug purposes only
	virtual const char *GetFileNameForDebug() const;

public:
	Uint32						m_flags;		// File flags
	Uint32						m_version;		// Protocol/file version
	serialization::IMapper*		m_mapper;		// File data mapper (used for more complicated serialization)
	const red::memory::Pool*	m_pool;
};
 