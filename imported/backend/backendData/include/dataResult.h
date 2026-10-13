/*
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#pragma once

namespace data
{

// Simple class to return a boolean result from a function that can dispatch jobs internally
// This value should only be checked once it is known that the jobs dispatched are complete
// NOTE: No HasValue() function exposed and no atomics used since we don't want this class 
// used in a busy loop waiting
class Result
{
public:
	Result();
	explicit Result( bool value );
	Result( const Result& ) = default;
	Result( Result&& ) = default;
	~Result() = default;

	Result& operator=( const Result& ) = default;
	Result& operator=( Result&& ) = default;

	void SetValue( bool value ) const;

	bool Successful() const;
	bool Failed() const;

private:
	struct Payload
	{
		bool m_hasValue = false;
		bool m_value = false;
	};
	red::SharedPtr< Payload, red::PoolEngine > m_payload;
};

RED_INLINE Result::Result()
	: m_payload( red::CreateSharedPtr< Payload, red::PoolEngine >() )
{
}

RED_INLINE Result::Result( bool value )
	: Result()
{
	SetValue( value );
}

RED_INLINE void Result::SetValue( bool value ) const
{
	m_payload->m_hasValue = true;
	m_payload->m_value = value;
}

RED_INLINE bool Result::Successful() const
{
	RED_FATAL_ASSERT( m_payload && m_payload->m_hasValue );
	return m_payload->m_value;
}

RED_INLINE bool Result::Failed() const
{
	RED_FATAL_ASSERT( m_payload && m_payload->m_hasValue );
	return ! m_payload->m_value;
}

} // data
