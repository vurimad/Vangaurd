/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#pragma once

namespace archive
{

typedef Uint64 FileIDCode;

class FileID
{
public:
	ARCHIVE_API FileID();
	ARCHIVE_API explicit FileID( FileIDCode id );

	FileIDCode GetCode() const;
	ARCHIVE_API red::String ToDebugString() const;

	friend bool operator == ( const FileID& left, const FileID& right );
	friend bool operator != ( const FileID& left, const FileID& right );
	friend bool operator < ( const FileID& left, const FileID& right );

private:
	FileIDCode m_hash;
};

RED_INLINE bool operator == ( const FileID& left, const FileID& right )
{
	return left.m_hash == right.m_hash;
}

RED_INLINE bool operator != ( const FileID& left, const FileID& right )
{
	return left.m_hash != right.m_hash;
}

RED_INLINE bool operator < ( const FileID& left, const FileID& right )
{
	return left.m_hash < right.m_hash;
}

RED_INLINE FileIDCode FileID::GetCode() const
{
	return m_hash;
}

} // namespace archive
