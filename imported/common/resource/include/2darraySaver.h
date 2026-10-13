/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../../common/redReflection/include/serializationSaver.h"

class Saver2dArray : public serialization::ISaver
{
public:
	Saver2dArray();

	// Save objects, returns true on success
	virtual Bool SaveObjects( IFile& file, const serialization::SavingContext& context ) const override final;
};
