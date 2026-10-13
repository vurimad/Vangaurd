/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "fileIOInterfaces.h"
#include "../../../common/redIO/include/redIOFile.h"
#include "../../../common/redContainers/include/blob.h"

namespace red
{
	class AbsolutePath;
}

namespace archive
{

//
// Null reader to be used as the other interface when only one is needed
// For example when only writing, pass this as the FileReader interface
//
class ARCHIVE_API NullFileReader : public FileReader
{
public:
	virtual bool Read( void* buffer, Uint32 size ) override;
	virtual bool Read( void* buffer, Uint64 position, Uint32 size ) override;

	virtual bool Seek( Uint64 position ) override;
	virtual Uint64 Tell() const override;
	virtual Uint64 Size() const override;
};

//
// Null writer to be used as the other interface when only one is needed
// For example when only reading, pass this as the FileWriter interface
//
class ARCHIVE_API NullFileWriter : public FileWriter
{
public:
	virtual bool Write( const void* buffer, Uint32 size ) override;
	virtual bool Write( const void* buffer, Uint64 position, Uint32 size ) override;

	virtual bool Seek( Uint64 position ) override;
	virtual Uint64 Tell() const override;
	virtual Uint64 Size() const override;
	virtual bool Flush() override;

	virtual bool SetEndOfFile() override;

	virtual bool IsFileModified() override;
};

//
// Simple synchronous reader, only opens the file for reading
//
class ARCHIVE_API SyncFileReader : public FileReader
{
public:
	SyncFileReader();

	SyncFileReader( SyncFileReader&& other );
	~SyncFileReader();
	SyncFileReader& operator=( SyncFileReader&& other );

	static SyncFileReader Open( const red::AbsolutePath& path );
	bool Opened() const;
	void Close();

	virtual bool Read( void* buffer, Uint32 size ) override;
	virtual bool Read( void* buffer, Uint64 position, Uint32 size ) override;

	virtual bool Seek( Uint64 position ) override;
	virtual Uint64 Tell() const override;
	virtual Uint64 Size() const override;

private:
	explicit SyncFileReader( io::NativeFileHandle&& handle );

	io::NativeFileHandle m_handle;
};


//
// Simple synchronous writer, only opens the file for writing
//
class ARCHIVE_API SyncFileWriter : public FileWriter
{
public:
	SyncFileWriter();

	static SyncFileWriter Open( const red::AbsolutePath& path );
	bool Opened() const;
	void Close();

	virtual bool Write( const void* buffer, Uint32 size ) override;
	virtual bool Write( const void* buffer, Uint64 position, Uint32 size ) override;

	virtual bool Seek( Uint64 position ) override;
	virtual Uint64 Tell() const override;
	virtual Uint64 Size() const override;
	virtual bool Flush() override;

	virtual bool SetEndOfFile() override;

	virtual bool IsFileModified() override;

private:
	explicit SyncFileWriter( io::NativeFileHandle&& handle );

	io::NativeFileHandle m_handle;
	bool m_isFileModifiedByWriter;
};

//
// Unified synchronous reader and writer, opens the same file handle
// to perform both reading and writing.
//
class ARCHIVE_API SyncFileReaderWriter : public FileReader, public FileWriter
{
public:
	SyncFileReaderWriter();

	static SyncFileReaderWriter Open( const red::AbsolutePath& path );
	bool Opened() const;
	void Close();

	virtual bool Read( void* buffer, Uint32 size ) override;
	virtual bool Read( void* buffer, Uint64 position, Uint32 size ) override;

	virtual bool Write( const void* buffer, Uint32 size ) override;
	virtual bool Write( const void* buffer, Uint64 position, Uint32 size ) override;

	virtual bool Seek( Uint64 position ) override;
	virtual Uint64 Tell() const override;
	virtual Uint64 Size() const override;
	virtual bool Flush() override;

	virtual bool SetEndOfFile() override;

	virtual bool IsFileModified() override;

private:
	explicit SyncFileReaderWriter( io::NativeFileHandle&& handle );

	io::NativeFileHandle m_handle;
	bool m_isFileModifiedByWriter;
};


//
// Simple synchronous buffered writer, only opens the file for writing
//
class ARCHIVE_API BufferedFileWriter : public FileWriter
{
public:
	BufferedFileWriter();
	~BufferedFileWriter();

	BufferedFileWriter( BufferedFileWriter&& other );
	BufferedFileWriter& operator=( BufferedFileWriter&& other );

	static BufferedFileWriter Open( const red::AbsolutePath& path );
	bool Opened() const;
	void Close();

	virtual bool Write( const void* buffer, Uint32 size ) override;
	virtual bool Write( const void* buffer, Uint64 position, Uint32 size ) override;

	virtual bool Seek( Uint64 position ) override;
	virtual Uint64 Tell() const override;
	virtual Uint64 Size() const override;
	virtual bool Flush() override;

	virtual bool SetEndOfFile() override;

	virtual bool IsFileModified() override;

private:
	explicit BufferedFileWriter( io::NativeFileHandle&& handle );

	bool Internal_WriteToFile( const void* buffer, Uint32 size );
	void Internal_WriteToBuffer( const void* buffer, Uint32 size );
	bool Buffer_Write( const void* buffer, Uint32 size );

	io::NativeFileHandle m_handle;

	static constexpr Uint32 c_bufferSize = RED_MEGA_BYTE( 1u );
	red::Blob m_buffer;
	Uint32 m_bufferOffset;

	bool m_isFileModifiedByWriter;
};


} // namespace archive
