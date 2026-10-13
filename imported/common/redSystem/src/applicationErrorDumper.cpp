/**
* Copyright (c) 2013 CDProjekt Red, Inc. All Rights Reserved.
*/

#include "build.h"
#include "applicationErrorDumper.h"


namespace red
{
	namespace err
	{
		Uint32 ApplicationErrorDumper::m_dumpersCount = 0;
		ApplicationDumpDataOnErrorFunc* ApplicationErrorDumper::m_dumpers[ c_MaxDumpers ] = {};

		void ApplicationErrorDumper::RegisterDumperOnce( ApplicationDumpDataOnErrorFunc* func )
		{
			// Duplicates are ok, just don't register them
			for( Uint32 i = 0; i < m_dumpersCount; ++i )
			{
				if( m_dumpers[ i ] == func )
				{
					return;
				}
			}

			RED_ASSERT( m_dumpersCount < c_MaxDumpers, "There are no more application error dumper slots available" );
			if( m_dumpersCount == c_MaxDumpers )
			{
				// No more dumper slots available
				return;
			}

			m_dumpers[m_dumpersCount] = func;
			++m_dumpersCount;
		}

		void ApplicationErrorDumper::RunDumpers( const red::err::ApplicationDataDumpContext& context )
		{
			for( Uint32 i = 0; i < m_dumpersCount; ++i )
			{
				if( m_dumpers[ i ] )
				{
					( *m_dumpers[ i ] )( context );
				}
			}
		}
	}
}
