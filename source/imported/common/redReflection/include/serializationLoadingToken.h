/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redJobs2/include/jobCounterOwner.h"

namespace serialization
{

class RED_NODISCARD LoadingToken : red::NonCopyable
{
public:
	explicit LoadingToken(job::Counter&& counter)
		: m_counter(std::move(counter))
	{
		// #fixme: can't assert until remove priority remapping for backend
		//RED_FATAL_ASSERT(m_counter.Internal_GetPriority() == job::Priority::Latent);
	}

	LoadingToken(LoadingToken&&) = default;

	const job::Counter& GetWaitCounter() const
	{
		return m_counter;
	}

private:
	job::Counter m_counter;
};

}
