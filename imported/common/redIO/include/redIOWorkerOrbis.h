/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redSystem/include/redThreadsThread.h"

namespace io
{
namespace prv
{

class IOWorkerOrbis : red::Thread
{
public:
	IOWorkerOrbis();
	~IOWorkerOrbis();
};

}
}