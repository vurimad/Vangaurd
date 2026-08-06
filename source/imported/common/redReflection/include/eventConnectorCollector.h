/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "eventTypes.h"

namespace red
{
	class EventConnectorCollector
	{
	public: 
		EventConnectorCollector();
		virtual ~EventConnectorCollector();

		virtual void CollectConnector( Uint16 eventId, const EventConnector & connector ) = 0;
		virtual void CollectScriptedConnector( Uint16 eventId, const EventConnector & connector, CName functionName ) = 0;
	};
}