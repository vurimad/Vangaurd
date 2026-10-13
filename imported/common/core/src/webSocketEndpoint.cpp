#include "build.h"
#include "../include/webSocketEndpoint.h"

WebSocketEndpoint::WebSocketEndpoint()
	: m_lastUpdateTime( 0 )
{}

void WebSocketEndpoint::SetLastUpdateTime( Double time )
{
	m_lastUpdateTime = time;
}

Double WebSocketEndpoint::GetLastUpdateTime() const
{
	return m_lastUpdateTime;
}

Float WebSocketEndpoint::GetUpdateFrequency() const
{
	return -1.f;
}