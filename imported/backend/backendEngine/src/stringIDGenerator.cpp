/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "../include/stringIDGenerator.h"

namespace tools
{
	StringIDGenerator::StringIDGenerator() { }

	StringIDGenerator::~StringIDGenerator() { }

	red::RUID StringIDGenerator::BuildStringID()
	{
		return m_generator.Next();
	}
}