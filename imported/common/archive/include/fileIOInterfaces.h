/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#pragma once

namespace archive
{

class ARCHIVE_API FileReader
{
public:
	virtual bool Read( void* buffer, Uint32 size ) = 0;
	virtual bool Read( void* buffer, Uint64 position, Uint32 size ) = 0;

	virtual bool Seek( Uint64 position ) = 0;
	virtual Uint64 Tell() const = 0;
	virtual Uint64 Size() const = 0;

protected:
	FileReader() {}
	FileReader( const FileReader& ) {}
	FileReader( FileReader&& ) {}
	~FileReader() {}
	FileReader& operator=( const FileReader& ) { return *this; }
	FileReader& operator=( FileReader&& ) { return *this; }
};

class ARCHIVE_API FileWriter
{
public:
	virtual bool Write( const void* buffer, Uint32 size ) = 0;
	virtual bool Write( const void* buffer, Uint64 position, Uint32 size ) = 0;

	virtual bool Seek( Uint64 position ) = 0;
	virtual Uint64 Tell() const = 0;
	virtual Uint64 Size() const = 0;
	virtual bool Flush() = 0;

	virtual bool SetEndOfFile() = 0;

	virtual bool IsFileModified() = 0;

protected:
	FileWriter() {}
	FileWriter( const FileWriter& ) {}
	FileWriter( FileWriter&& ) {}
	~FileWriter() {}
	FileWriter& operator=( const FileWriter& ) { return *this; }
	FileWriter& operator=( FileWriter&& ) { return *this; }
};

} // namespace archive
