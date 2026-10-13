#include "build.h"
#include "objectClipboard.h"
#include "objectClipboardWindows.h"
#include "objectPersistentCollection.h"
#include "../../../common//core/include/memoryBufferFile.h"

namespace tools
{

	ObjectClipboardWindows::ObjectClipboardWindows()
	{
		m_dataFormat = ::RegisterClipboardFormat( TXT("RedToolkitData") );
	}

	ObjectClipboardWindows::~ObjectClipboardWindows()
	{
	}

	const Bool ObjectClipboardWindows::StoreData( const red::SharedPtr< IObjectPersistentCollection >& collection )
	{
		// prepare data to store
		GrowableMemoryBuffer textData, binaryData;
		if ( collection )
		{
			// save text data
			/*if ( collection->SupportsTextFormat() )
			{
				if ( !collection->Save( IObjectPersistentCollection::EContentFormat::Text, textData ) )
				{
					RED_LOG_ERROR( "Core: Unable to save persistent collection into text buffer. Clipboard content will may be corrupted." );
					return false;
				}
			}*/

			// save binary data
			if ( !collection->Save( IObjectPersistentCollection::EContentFormat::Binary, binaryData ) )
			{
				RED_LOG_ERROR( "Core: Unable to save persistent collection into binary buffer. Clipboard content will may be corrupted." );
				return false;
			}
		}

		// open clipboard
		if ( !::OpenClipboard( NULL ) )
		{
			Uint32 lastError = ::GetLastError();
			RED_LOG_ERROR( "Core: unable to open clipboard. GetLastError returned %u", lastError );
			return false;
		}

		// clean current content
		::EmptyClipboard();

		Bool state = true;

		// store text data
		if ( textData.Size() )
		{
			HGLOBAL hMem = CreateTextData( textData );
			if ( hMem != NULL )
			{
				if ( !::SetClipboardData( CF_TEXT, hMem ) )
				{
					::GlobalFree( hMem );
					Uint32 lastError = ::GetLastError();
					RED_LOG_ERROR( "Core: Unable to store text data in the clipboard. GetLastError returned %u", lastError );
					state = false;
				}
			}
		}

		// store binary data
		if ( binaryData.Size() )
		{
			HGLOBAL hMem = CreateBinaryData( binaryData );
			if ( hMem != NULL )
			{
				if ( !::SetClipboardData( m_dataFormat, hMem ) )
				{
					::GlobalFree( hMem );
					Uint32 lastError = ::GetLastError();
					RED_LOG_ERROR( "Core: Unable to store binary data in the clipboard. GetLastError returned %u", lastError );
					state = false;
				}
			}
		}

		// finish storing data
		::CloseClipboard();

		// return merged state, we need to know in the higher layers the the putting data in the clipboard failed - i.e. when we are cutting stuff
		return state;
	}

	const Bool ObjectClipboardWindows::HasData() const
	{
		Bool hasData = false;

		// gain exclusive clipboard
		if (::OpenClipboard(NULL))
		{
			HGLOBAL handle = ::GetClipboardData(m_dataFormat);
			hasData = (NULL != handle);

			/// unlock clipboard access
			::CloseClipboard();
		}

		return hasData;
	}

	red::SharedPtr< IObjectPersistentCollection > ObjectClipboardWindows::GetData()
	{
		red::SharedPtr< IObjectPersistentCollection > ret;

		// gain exclusive clipboard
		if ( !::OpenClipboard( NULL ) )
			return ret;

		// if we have the binary data always use it (it's way more safer)
		{
			HGLOBAL handle = ::GetClipboardData( m_dataFormat );
			if ( NULL != handle )
			{
				// Get size of the data
				const auto dataSize = (const Uint32) GlobalSize( handle );

				// Get the text from the handle
				void* handleData = ::GlobalLock( handle );
				if ( NULL != handleData )
				{
					ret = IObjectPersistentCollection::CreateFromBuffer( handleData, dataSize );
					::GlobalUnlock( handle );
				}
			}
		}

		// than try the text buffer
		if ( !ret )
		{
			HGLOBAL handle = ::GetClipboardData( CF_TEXT );
			if ( NULL != handle )
			{
				// Get size of the data
				const auto dataSize = (const Uint32) GlobalSize( handle );

				// Get the text from the handle
				void* handleData = ::GlobalLock( handle );
				if ( NULL != handleData )
				{
					ret = IObjectPersistentCollection::CreateFromBuffer( handleData, dataSize );
					::GlobalUnlock( handle );
				}
			}
		}

		/// unlock clipboard access
		::CloseClipboard();

		// return parsed data
		return ret;
	}

	HGLOBAL ObjectClipboardWindows::CreateBinaryData( const GrowableMemoryBuffer& buffer )
	{
		// Create a global movable handle
		const auto dataSize = buffer.Size();
		HGLOBAL handle = ::GlobalAlloc( GMEM_MOVEABLE, dataSize );
		if ( !handle )
		{
			RED_LOG_ERROR( "Core: Failed to allocate buffer via GlobalAlloc" );
			return nullptr;
		}

		// Put the text in the handle
		void* handleData = ::GlobalLock( handle );
		if ( !handleData )
		{
			::GlobalFree( handle );
			return nullptr;
		}

		// place the data in global memory
		::CopyMemory( handleData, buffer.Data(), dataSize );

		// Unlock it
		::GlobalUnlock( handle );
		return handle;
	}

	namespace helper
	{
		class TextSizeCounter final : public IFile
		{
		public:
			TextSizeCounter()
				: IFile( FF_Writer | FF_FileBased )
				, m_additionalSizeNeeded( 0 )
				, m_offset( 0 )
				, m_size( 0 )
			{}

			virtual void Serialize( void* buffer, size_t size ) override
			{
				m_offset += size;
				m_size = math::Max< Uint64 >( m_size, m_offset );

				for ( size_t i=0; i<size; ++i )
				{
					const auto ch =  ((const AnsiChar*)buffer)[i];
					if ( ch == '\n' )
						m_additionalSizeNeeded += 1;
				}
			}

			virtual Uint64 GetOffset() const override
			{
				return m_offset;
			}

			virtual Uint64 GetSize() const override
			{
				return m_size;
			}

			virtual void Seek( Int64 offset ) override
			{
				m_offset = offset;
			}

			virtual void Flush() override
			{
			}

			RED_FORCE_INLINE const Uint64 GetActualSizeNeeded() const
			{
				return m_size + m_additionalSizeNeeded;
			}

		private:
			Uint64	m_additionalSizeNeeded; // \n -> \r\n
			Uint64	m_offset;
			Uint64	m_size;
		};

		class TextLineEndingConverter final : public IFile
		{
		public:
			TextLineEndingConverter( void* memory )
				: IFile( FF_Writer | FF_FileBased )
				, m_memory( (AnsiChar*)memory )
				, m_offset( 0 )
			{}

			virtual void Serialize( void* buffer, size_t size ) override
			{
				for ( size_t i=0; i<size; ++i )
				{
					const auto ch =  ((const AnsiChar*)buffer)[i];
					if ( ch == '\n' )
					{
						m_memory[ m_offset++ ] = '\r';
						m_memory[ m_offset++ ] = '\n';
					}
					else if ( ch != '\r' )
					{
						m_memory[ m_offset++ ] = ch;
					}
				}
			}

			virtual Uint64 GetOffset() const override
			{
				return m_offset;
			}

			virtual Uint64 GetSize() const override
			{
				return m_offset;
			}

			virtual void Seek( Int64 offset ) override
			{
				RED_FATAL( "Seeking in text serialization with line conversion is not supported" );
			}

			virtual void Flush() override
			{
			}

		private:
			AnsiChar*	m_memory;
			Uint64		m_offset;
		};

	} // helper

	HGLOBAL ObjectClipboardWindows::CreateTextData( const GrowableMemoryBuffer& buffer )
	{
		// count the true needed size (for line endings to)
		helper::TextSizeCounter sizeCounter;
		sizeCounter.Serialize( const_cast<void*>( buffer.Data() ), buffer.Size() ); // Need const_cast because our serialization is of poor quality

		// Create a global movable handle
		const auto dataSize = sizeCounter.GetActualSizeNeeded();
		HGLOBAL handle = ::GlobalAlloc( GMEM_MOVEABLE, dataSize );
		if ( !handle )
		{
			RED_LOG_ERROR( "Core: Failed to allocate buffer via GlobalAlloc" );
			return nullptr;
		}

		// Put the text in the handle
		void* handleData = ::GlobalLock( handle );
		if ( !handleData )
		{
			::GlobalFree( handle );
			return nullptr;
		}

		// place the data in global memory
		helper::TextLineEndingConverter textWriter( handleData );
		textWriter.Serialize( const_cast<void*>( buffer.Data() ), buffer.Size() );

		// Unlock it
		::GlobalUnlock( handle );
		return handle;
	}

	void ObjectClipboardWindows::SetupInterop( ObjectClipboardWindows* ptr, ::interop::utils::Object& op )
	{
		// nothing
	}

} // tools
