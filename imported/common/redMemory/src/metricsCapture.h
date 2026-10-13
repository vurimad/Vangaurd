/**
 * Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_METRICS_CAPTURE_H_
#define _RED_MEMORY_METRICS_CAPTURE_H_

#if defined( RED_PLATFORM_WINPC )
#include "metricsCaptureWin.h"
#elif defined( RED_PLATFORM_ORBIS )
#include "metricsCaptureOrbis.h"
#elif defined( RED_PLATFORM_DURANGO )
#include "metricsCaptureDurango.h"
#elif defined( RED_PLATFORM_LINUX )
#include "metricsCaptureLinux.h"
#endif

namespace red
{
namespace memory
{
#if defined( RED_PLATFORM_WINPC )
	typedef MetricsCaptureWin MetricsCapture;
#elif defined( RED_PLATFORM_ORBIS )
	typedef MetricsCaptureOrbis MetricsCapture;
#elif defined( RED_PLATFORM_DURANGO )
	typedef MetricsCaptureDurango MetricsCapture;
#elif defined( RED_PLATFORM_LINUX )
	typedef MetricsCaptureLinux MetricsCapture;
#endif
}
}

#endif
