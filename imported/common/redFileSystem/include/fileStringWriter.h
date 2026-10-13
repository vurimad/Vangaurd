/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../redSystem/include/stringWriter.h"

namespace red
{

	// file output stream
	template< typename CH >
	class FileStreamWriter
	{
	public:
		FileStreamWriter( IFile* outputFile )
			: m_outputFile( outputFile )
		{}

		bool Flush( const Bool forced, const CH* data, const red::Uint32 count )
		{
			if ( m_outputFile )
				m_outputFile->Serialize( (void*)data, count * sizeof(CH) );
			return true;
		}

		static FileStreamWriter& GetInstance()
		{
			static FileStreamWriter theInstance;
			return theInstance;
		}

		void Close()
		{
			RED_DELETE( m_outputFile );
			m_outputFile = nullptr;
		}

	private:
		IFile*		m_outputFile;
	};

	// string based file writer using some stack memory for buffer
	typedef FileStreamWriter< AnsiChar > CAnsiStringFileWriter;
	typedef red::StackStringWriter< AnsiChar, 1024, FileStreamWriter< AnsiChar > > CAnsiStringFileStream;

} // Red