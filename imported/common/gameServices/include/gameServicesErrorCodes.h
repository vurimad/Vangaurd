/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#pragma once
#include "gameServicesApi.h"

namespace services
{
	enum GAME_SERVICES_API ServiceError : Int32
	{
		ServiceError_OK = 0,
		ServiceError_NotSupported,
		ServiceError_InvalidUser,
		ServiceError_InternalError,
		ServiceError_FailedToGatherSaves,
		ServiceError_FailedToLoadSave,
		ServiceError_InvalidSaveID,
		ServiceError_InvalidSavePath,
		ServiceError_InvalidSaveMetadataPath,
		ServiceError_InvalidSaveScreenshotPath,
		ServiceError_NotEnoughSpace,
		ServiceError_NotEnoughSlots,
		ServiceError_FailedToLoadScreenshot,
		ServiceError_FileNotFound
	};
}