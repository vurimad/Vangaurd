/**
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_METRICS_CAPTURE_LINUX_H_
#define _RED_MEMORY_METRICS_CAPTURE_LINUX_H_

#include "abstractMetricsCapture.h"

namespace red
{
namespace memory
{
	class MetricsCaptureLinux : public AbstractMetricsCapture
	{
	public:
		virtual void WritePlatfromIdentifier( Serializer& serializer ) override final;
		virtual void WriteLoadedModules( Serializer& serializer ) override final;
	};
}
}

#endif