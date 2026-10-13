/*
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"

#ifndef NO_DATA_VALIDATION

#include "dataError.h"

IDataErrorSystem::~IDataErrorSystem()
{
	/* intentionally empty */
}

STATIC_CHECK_USE_DECL
void IDataErrorSystem::ReportXMLError( const char* filepath, const char* error, ... )
{ 
	va_list arglist;
	va_start( arglist, error );
	red::alg::PushBackUnique( m_xmlErrors.GetRef( String ( filepath ), red::PoolDebug() ), String::PrintfV( error, arglist ) );	
	va_end( arglist );
}

IDataErrorSystem* GDataError = nullptr;
#else
RED_NO_EMPTY_FILE();
#endif // NO_DATA_VALIDATION
