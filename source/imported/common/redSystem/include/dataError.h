/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_SYSTEM_DATA_ERROR_H_
#define _RED_SYSTEM_DATA_ERROR_H_

#if defined( RED_LOGGING_ENABLED )
#include "internalDataError.h"
#else
#include "dummyDataError.h"
#endif

#if defined( RED_LOGGING_ENABLED )
namespace red
{
	using DataError = InternalDataError;
}

#define RED_DATA_ERROR( category, expression, message, ... )\
	do\
	{\
		if ( !( expression ) )\
		{\
			red::LogDataError( red::LoggerLevel_Error, category, red::DataError{ message, ##__VA_ARGS__ } );\
		}\
	} while( ( void )0, 0 )

#define RED_DATA_ERROR_BEGIN( category, expression, message, ... )\
	do\
	{\
		if ( !( expression ) )\
		{\
			const auto _category_ = category;\
			red::DataError _dataError_{ message, ##__VA_ARGS__ }

#define RED_DATA_ERROR_END()\
			red::LogDataError( red::LoggerLevel_Error, _category_, _dataError_ );\
		}\
	} while( ( void )0, 0 )

#define RED_DATA_ERROR_ACTION_SHOW_ASSET( resourcePath )\
		 _dataError_.AddShowAssetAction( resourcePath )

#define RED_DATA_ERROR_ACTION_SHOW_ENTITY( resourcePath )\
		 _dataError_.AddShowEntityAction( resourcePath )

#define RED_DATA_ERROR_ACTION_SHOW_CUSTOM( customDescription, resourcePath )\
		 _dataError_.AddShowCustomAssetAction( customDescription, resourcePath )


#else

namespace red
{
	using DataError = DummyDataError;
}

#define RED_DATA_ERROR( category, expression, message, ... )\
	do {} while( ( void )0, 0 )

#define RED_DATA_ERROR_BEGIN( category, expression, message, ... )\
	do {

#define RED_DATA_ERROR_END()\
	} while ( ( void )0, 0 )

#define RED_DATA_ERROR_ACTION_SHOW_ASSET( resourcePath )\
	do {} while ( ( void )0, 0 )

#define RED_DATA_ERROR_ACTION_SHOW_ENTITY( resourcePath )\
	do {} while ( ( void )0, 0 )

#define RED_DATA_ERROR_ACTION_SHOW_CUSTOM( customDescription, resourcePath )\
	do {} while ( ( void )0, 0 )

#endif

#endif