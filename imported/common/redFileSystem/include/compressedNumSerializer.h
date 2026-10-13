#pragma once

class IFile;

//#if 0
// ReadVarint_LEB128_Signed is 20% faster, WriteVarint_LEB128_Signed is 2.5x faster
class RED_FILESYSTEM_API CCompressedNumSerializer
{
private:
	Int32&		m_num;

public:
	explicit CCompressedNumSerializer( Uint32& num ) : m_num( reinterpret_cast<Int32&>( num ) ){};
	explicit CCompressedNumSerializer( Int32& num ) : m_num( num ){};

	
	friend RED_FILESYSTEM_API void operator<<( IFile& file, CCompressedNumSerializer c );

	CCompressedNumSerializer &operator=( CCompressedNumSerializer& c )
	{
		m_num = c.m_num;
		return *this;
	}
};

RED_FILESYSTEM_API void operator<<( IFile& file, CCompressedNumSerializer c );
//#endif

// Implementation of signed LEB128. Maximum 5 byte write size.
RED_FILESYSTEM_API void WriteVarint_LEB128_Signed( IFile& writer, const Int32 value );

// Implementation of unsigned LEB128. Maximum 5 byte write size.
RED_FILESYSTEM_API void WriteVarint_LEB128_Unsigned( IFile& writer, const Uint32 value );

// Implementation of modified 4-byte unsigned LEB128. Maximum 4 byte size. 512MiB (29 bit) addressable max.
RED_FILESYSTEM_API void WriteVarint_LEB128m4_Unsigned( IFile& writer, const Uint32 value );

// Implementation of standard signed LEB128. Maximum 5 byte read size.
RED_FILESYSTEM_API Int32 ReadVarint_LEB128_Signed( IFile& reader );

// Implementation of standard unsigned LEB128. Maximum 5 byte read size.
RED_FILESYSTEM_API Uint32 ReadVarint_LEB128_Unsigned( IFile& reader );

// Implementation of modified 4-byte unsigned LEB128. Maximum 4 byte size. 512MiB (29 bit) addressable max.
RED_FILESYSTEM_API Uint32 ReadVarint_LEB128m4_Unsigned( IFile& reader );
