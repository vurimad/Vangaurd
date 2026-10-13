/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../../common/localizationPersistence/include/locRUIDGenerator.h"
#include "../../common/engine/include/ruidGenerator.h"

namespace tools
{
	class BACKEND_ENGINE_API StringIDGenerator : public localizationPersistence::LocalizationRUIDGeneratorBase, red::NonCopyable
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		StringIDGenerator();
		virtual ~StringIDGenerator();

		red::RUID BuildStringID();

	protected: 
		RUIDGenerator m_generator; 
	};
}
