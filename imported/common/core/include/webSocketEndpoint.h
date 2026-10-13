/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once


class RED_CORE_API WebSocketEndpoint
{
public:
	enum class EWebSocketPayloadType
	{
		TextUtf8,
		Binary,
	};

public:
	WebSocketEndpoint();
	virtual ~WebSocketEndpoint() = default;

	void SetLastUpdateTime( Double time );
	Double GetLastUpdateTime() const;

	virtual void OnReadPayload( EWebSocketPayloadType type, IFile& reader ) = 0;
	virtual EWebSocketPayloadType OnWritePayload( IFile& writer ) = 0;
	virtual Float GetUpdateFrequency() const; // >= 0 zero means override

private:
	Double m_lastUpdateTime;
};
