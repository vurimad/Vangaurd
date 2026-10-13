/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

/// Original code (C) RenderLab 2014 by Tomasz "RexDex" Jonarski
/// Copied with permission from author by author :)

#pragma once

namespace img
{
	/// Generic image iterator that allows us to easily "walk" through every pixel in the image
	class REDIMAGE_API ImageIterator
	{
	public:
		RED_FORCE_INLINE ImageIterator();
		RED_FORCE_INLINE ImageIterator( const ImageIterator& other );
		RED_FORCE_INLINE ImageIterator( const Int32 x, const Int32 y, const Int32 z );
		RED_FORCE_INLINE ImageIterator( const Int32 minX, const Int32 minY, const Int32 minZ, const Int32 maxX, const Int32 maxY, const Int32 maxZ );
		RED_FORCE_INLINE ~ImageIterator() {};

		/// Get current location X
		RED_FORCE_INLINE const Int32 GetX() const;

		/// Get current location Y
		RED_FORCE_INLINE const Int32 GetY() const;

		/// Get current location Z
		RED_FORCE_INLINE const Int32 GetZ() const;

		/// Get X dimension minimum
		RED_FORCE_INLINE const Int32 GetMinX() const;

		/// Get Y dimension minimum
		RED_FORCE_INLINE const Int32 GetMinY() const;

		/// Get Z dimension minimum
		RED_FORCE_INLINE const Int32 GetMinZ() const;

		/// Get X dimension maximum
		RED_FORCE_INLINE const Int32 GetMaxX() const;

		/// Get Y dimension maximum
		RED_FORCE_INLINE const Int32 GetMaxY() const;

		/// Get Z dimension maximum
		RED_FORCE_INLINE const Int32 GetMaxZ() const;

		/// Go to next pixel within image, returns false at the last pixel
		RED_FORCE_INLINE const Bool Next();

		/// Get spanned size in X dimension
		RED_FORCE_INLINE const Int32 GetSizeX() const;

		/// Get spanned size in Y dimension
		RED_FORCE_INLINE const Int32 GetSizeY() const;

		/// Get spanned size in Z dimension
		RED_FORCE_INLINE const Int32 GetSizeZ() const;

		/// Is the represented image region empty ?
		RED_FORCE_INLINE const Bool IsEmpty() const;

		/// Reset iterator
		RED_FORCE_INLINE void Reset();

	private:
		Int32						m_x;
		Int32						m_y;
		Int32						m_z;

		Int32						m_minX;
		Int32						m_minY;
		Int32						m_minZ;

		Int32						m_maxX;
		Int32						m_maxY;
		Int32						m_maxZ;

		friend class Image;
	};
} // img

#include "imageIterator.inl"
