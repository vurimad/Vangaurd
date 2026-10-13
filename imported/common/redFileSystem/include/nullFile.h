/**
* Copyright (c) 2007-20 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "file.h"

/// Null file writer capable of tracking the proper file size and write offset
class RED_FILESYSTEM_API CNullFileWriter final : public IFile
{
public:
	CNullFileWriter()
		: IFile( FF_Writer | FF_NullBased )
		, m_pos(0)
		, m_maxSize(0)
	{};

	// interface
	virtual void Serialize( void* buffer, size_t size ) override;
	virtual Uint64 GetOffset() const override;
	virtual Uint64 GetSize() const override;
	virtual void Seek( Int64 offset ) override;
	virtual void Flush() override;

private:
	Uint64 m_pos;
	Uint64 m_maxSize;
};

