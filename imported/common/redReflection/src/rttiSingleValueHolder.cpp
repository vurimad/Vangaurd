/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#include "build.h"
#include "rttiSingleValueHolder.h"
#include "resource.h"

namespace rtti
{
	SingleValueHolder::SingleValueHolder()
	{
	}

	SingleValueHolder::SingleValueHolder( const SingleValueHolder& other )
		: m_text( other.m_text )
	{
	}

	SingleValueHolder::~SingleValueHolder()
	{
	}

	SingleValueHolder& SingleValueHolder::operator=( const SingleValueHolder& other )
	{
		if ( this != &other )
		{
			m_text = other.m_text;
		}

		return *this;
	}

	SingleValuePtr SingleValueHolder::Create( const AnsiChar* valueText )
	{
		SingleValuePtr ret( RED_NEW( SingleValueHolder ) );
		ret->m_text = valueText;
		return ret;
	}

} // rtti
