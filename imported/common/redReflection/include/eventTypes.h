/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redMemory/include/function.h"


class ISerializable;

template< typename T >
class THandle;

namespace red
{
	class Event;

	// ctremblay: Clang store "Fat pointer" for member function pointer. So I need to store in 16 byte storage.
	typedef red::FixedSizeFunction< void( ISerializable&, const THandle< red::Event >& ), 16, 8 > EventConnector;
}
