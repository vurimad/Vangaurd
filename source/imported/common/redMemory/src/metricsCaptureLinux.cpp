/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "metricsCaptureLinux.h"


namespace red
{
	namespace memory
	{
		void MetricsCaptureLinux::WritePlatfromIdentifier( Serializer& serializer )
		{
			const char platformIdentifier[] = "LINUX";
			const u16 platformIdentifierSize = static_cast< u16 >( sizeof( platformIdentifier ) - 1 );
			serializer.Serialize( platformIdentifierSize );
			serializer.Serialize( platformIdentifier, platformIdentifierSize );
		}

		void MetricsCaptureLinux::WriteLoadedModules( Serializer& serializer )
		{
			// TODO
		}
	}
}