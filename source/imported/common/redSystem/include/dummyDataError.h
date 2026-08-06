/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_SYSTEM_DUMMY_DATA_ERROR_
#define _RED_SYSTEM_DUMMY_DATA_ERROR_

#include "abstractDataError.h"

namespace red
{
	class DataErrorAction;

	class REDSYSTEM_API DummyDataError final : public AbstractDataError
	{
	public:
		RED_FORCE_INLINE DummyDataError() {}

		RED_FORCE_INLINE AbstractDataError& AddShowAssetAction( const char* ) override { return *this; };
		RED_FORCE_INLINE AbstractDataError& AddShowEntityAction( const char* ) override { return *this; };
		RED_FORCE_INLINE AbstractDataError& AddShowCustomAssetAction( const char*, const char* ) override { return *this; };

		RED_FORCE_INLINE const char* GetDataError() const override { return nullptr; }
		RED_FORCE_INLINE const char* GetDataErrorActions() const override { return nullptr; }
		RED_FORCE_INLINE Uint32 GetMessageFormatHash() const override { return 0; }
	};
}

#endif