/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "md5.h"
#include "md5prv.h"

namespace crypto
{
	void CalculateMD5_Insecure( const void* buffer, const red::Uint64 size, MD5& outHash )
	{
		md5::MD5_Context context{ outHash };
		md5::Update( context, buffer, size );
	}
} // crypto
