/*
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace red {

template< Uint32 Size, Uint32 Alignment >
class FixedBuffer
{
public:

	RED_INLINE void* Data() { return m_buffer; }
	RED_INLINE const void* Data() const { return m_buffer; }

protected:

	Uint8	m_buffer[ Size ];
};

template< Uint32 Size >
class FixedBuffer< Size, 4 >
{
public:

	RED_INLINE void* Data() { return m_buffer; }
	RED_INLINE const void* Data() const { return m_buffer; }

protected:

	RED_ALIGNED_VAR( Uint8, 4 ) m_buffer[ Size ];
};

template< Uint32 Size >
class FixedBuffer< Size, 8 >
{
public:

	RED_INLINE void* Data() { return m_buffer; }
	RED_INLINE const void* Data() const { return m_buffer; }

protected:

	RED_ALIGNED_VAR( Uint8, 8 ) m_buffer[ Size ];
};

template< Uint32 Size >
class FixedBuffer< Size, 16 >
{
public:

	RED_INLINE void* Data() { return m_buffer; }
	RED_INLINE const void* Data() const { return m_buffer; }

protected:

	RED_ALIGNED_VAR( Uint8, 16 ) m_buffer[ Size ];
};

template< Uint32 Size >
class FixedBuffer< Size, 32 >
{
public:

	RED_INLINE void* Data() { return m_buffer; }
	RED_INLINE const void* Data() const { return m_buffer; }

protected:

	RED_ALIGNED_VAR( Uint8, 32 ) m_buffer[ Size ];
};

template< Uint32 Size >
class FixedBuffer< Size, 64 >
{
public:

	RED_INLINE void* Data() { return m_buffer; }
	RED_INLINE const void* Data() const { return m_buffer; }

protected:

	RED_ALIGNED_VAR( Uint8, 64 ) m_buffer[ Size ];
};

} // red