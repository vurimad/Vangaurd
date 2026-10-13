/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_SYSTEM_ABSTRACT_DATA_ERROR_
#define _RED_SYSTEM_ABSTRACT_DATA_ERROR_

namespace red
{
	class REDSYSTEM_API AbstractDataError
	{
	public:
		virtual ~AbstractDataError() = default;

		virtual AbstractDataError& AddShowAssetAction( const char* resourcePath ) = 0;
		virtual AbstractDataError& AddShowEntityAction( const char* resourcePath ) = 0;
		virtual AbstractDataError& AddShowCustomAssetAction( const char* customDescription, const char* resourcePath ) = 0;

		virtual const char* GetDataError() const = 0;
		virtual const char* GetDataErrorActions() const = 0;
		virtual Uint32 GetMessageFormatHash() const = 0;
	};
}

#endif