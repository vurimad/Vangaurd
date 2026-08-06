/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#pragma once

class IFile;

namespace fs
{
	/// Helper class for skipping blocks
	class RED_FILESYSTEM_API SkipableBlock : public red::NonCopyable
	{
	public:
		SkipableBlock( IFile& file );
		~SkipableBlock();

		// Skip this data block - only when reading
		// Moves file pointer to the end of the block
		void Skip();

		// Get the offset of the block end
		RED_FORCE_INLINE const Uint64 GetEndOffset() const
		{
			return m_skipOffset + m_archiveOffset;
		}

		// Do we have any data stored ?
		RED_FORCE_INLINE Bool HasData() const
		{
			return m_skipOffset > sizeof(m_skipOffset);
		}

		// Get size of data stored in the skip block
		RED_FORCE_INLINE const Uint32 GetDataSize() const
		{
			return HasData() ? (m_skipOffset - sizeof(m_skipOffset)) : 0;
		}

	private:
		static const Uint32 SKIP_BUFFER_SIZE = 256;

		IFile&		m_file;
		Uint64		m_archiveOffset;
		Uint32		m_skipOffset; // Serialized to file - do not make size_t/uintptr_t etc
	};

} // fs