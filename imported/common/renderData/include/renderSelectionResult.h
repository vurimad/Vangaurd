/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "renderSelectable.h"
#include "renderCamera.h"
#include "renderPublicEnums.h"

namespace rend
{
	//*******************************************************************************************************************************************//
	//*******************************************************************************************************************************************//
	
	class RENDER_DATA_API SelectedObjectInfo final
	{
	public:
		RED_FORCE_INLINE SelectedObjectInfo( Int32 posX, Int32 posY, IRenderSelectable::GlobalID id, Float zOrder, Bool transparent )
			: m_pos( posX, posY )
			, m_zOrder( zOrder )
			, m_transparent( transparent )
			, m_id( id )
		{
		}

		RED_FORCE_INLINE IRenderSelectable::GlobalID GetId() const 
		{
			return m_id; 
		}

		RED_FORCE_INLINE Float GetZOrder() const
		{
			return m_zOrder;
		}

		RED_FORCE_INLINE const Point& GetPos() const
		{
			return m_pos;
		}

		RED_FORCE_INLINE Bool IsSolid() const
		{
			return !m_transparent;
		}

		RED_FORCE_INLINE Bool IsTransparent() const
		{
			return m_transparent;
		}

	private:
		Point m_pos;
		Float m_zOrder;
		Bool m_transparent;
		IRenderSelectable::GlobalID m_id;
	};

	//*******************************************************************************************************************************************//
	//*******************************************************************************************************************************************//

	namespace SelectionResultType
	{
		struct GPUBufferPixel
		{
			Int32 x, y;
			Float depth;
			Int32 transparent; // if true then 1; otherwise 0
			Vector4 id;
		};
	}

	class RENDER_DATA_API SelectionResult final
	{
		RED_USE_MEMORY_POOL( rend::PoolImageProcessing );

	public:
		RED_FORCE_INLINE SelectionResult( const red::memory::Pool& pool )
		{
			m_isValid.SetValue( false );
		}

		SelectionResult( const SelectionResult& rhs )
			: m_selectedObjects( rhs.m_selectedObjects)
		{
			m_isValid.SetValue( rhs.m_isValid.GetValue() );
		}

		SelectionResult( SelectionResult&& rhs )
			: m_selectedObjects( std::move( rhs.m_selectedObjects ) )
		{
			 m_isValid.SetValue( rhs.m_isValid.GetValue() );
			 rhs.m_isValid.SetValue( false );
		}

		SelectionResult& operator = ( const SelectionResult& rhs )
		{
			m_selectedObjects = rhs.m_selectedObjects;
			m_isValid.SetValue( rhs.m_isValid.GetValue() );
			return *this;
		}

		SelectionResult& operator = ( SelectionResult&& rhs )
		{
			m_selectedObjects = std::move( rhs.m_selectedObjects );
			m_isValid.SetValue( rhs.m_isValid.GetValue() );
			rhs.m_isValid.SetValue( false );
			return *this;
		}		
		
		// Extract list of unique object info (object ID is not duplicated if the object falls into multiple pixels)
		red::DynArray<SelectedObjectInfo> GetUniqueSelectedObjects( const Rect* rect = nullptr ) const;

		RED_FORCE_INLINE void AddSelectedObjectInfo( Int32 x, Int32 y, IRenderSelectable::GlobalID objId, Float zOrder )
		{
			m_selectedObjects.PushBack( SelectedObjectInfo( x, y, objId, zOrder, false ) );
		}

		RED_FORCE_INLINE void AddSelectedObjectInfo( const SelectionResultType::GPUBufferPixel& pixel )
		{
			IRenderSelectable::GlobalID id = IRenderSelectable::DecodeID( pixel.id );
			if ( id != 0 )
			{
				m_selectedObjects.PushBack( SelectedObjectInfo( pixel.x, pixel.y, id, pixel.depth, pixel.transparent != 0 ) );
			}
		}

		RED_FORCE_INLINE void ProcessData()
		{
			Sort();
		}

		void Clear()
		{
			m_isValid.SetValue( false );
			m_selectedObjects.Clear();
		}

		void Complete()
		{
			m_isValid.SetValue( true );
		}

		Bool IsValid()
		{
			return m_isValid.GetValue();
		}

	private:
		void Sort();					// sorts points by coordinates -> object ids -> depth values

	private:
		red::DynArray<SelectedObjectInfo> m_selectedObjects{ PoolImageProcessing() };
		red::Atomic< Bool > m_isValid;
	};

	//*******************************************************************************************************************************************//
	//*******************************************************************************************************************************************//

	class RENDER_DATA_API ColorResult final
	{
		RED_USE_MEMORY_POOL( rend::PoolImageProcessing );

	public:

		ColorResult( const red::memory::Pool& pool );

		~ColorResult();
		
		RED_FORCE_INLINE Uint32 GetWidth() const { return m_width; }
		RED_FORCE_INLINE Uint32 GetHeight() const { return m_height; }
		RED_FORCE_INLINE const Color* GetColors() const { return m_image.TypedData(); }
		RED_FORCE_INLINE void* GetWriteData() {  return m_image.Data(); }
		
		// #GAMMA
		RED_FORCE_INLINE Color GetColor( Uint32 x, Uint32 y ) const 
		{ 
			RED_FATAL_ASSERT( x < m_width, "" );
			RED_FATAL_ASSERT( y < m_height, "" );
			return m_image[ x + y * m_width ]; 
		}

		RED_FORCE_INLINE void SetColor( Color color , Uint32 x, Uint32 y )
		{
			RED_FATAL_ASSERT( x < m_width, "" );
			RED_FATAL_ASSERT( y < m_height, "" );
			m_image[x + y * m_width] = color;
		}

		RED_FORCE_INLINE void ReserveMemory( Uint32 width, Uint32 height )
		{
			m_width = width;
			m_height = height;
			m_image.Resize( width * height );
		}

	private:
		Uint32 m_width;
		Uint32 m_height;
		red::DynArray<Color> m_image{ PoolImageProcessing() };
	};

	//*******************************************************************************************************************************************//
	//*******************************************************************************************************************************************//

	class RENDER_DATA_API GIVolumeResult final
	{
		RED_USE_MEMORY_POOL( rend::PoolImageProcessing );

	public:

		GIVolumeResult( const red::memory::Pool& pool );

		~GIVolumeResult();

		RED_FORCE_INLINE Uint32 GetWidth() const { return m_width; }
		RED_FORCE_INLINE Uint32 GetHeight() const { return m_height; }
		RED_FORCE_INLINE const Uint8* GetValues() const { return m_image.TypedData(); }
		RED_FORCE_INLINE void* GetWriteData() { return m_image.Data(); }

		// #GAMMA
		RED_FORCE_INLINE Uint8 GetValue( Uint32 x, Uint32 y ) const
		{
			RED_FATAL_ASSERT( x < m_width, "" );
			RED_FATAL_ASSERT( y < m_height, "" );
			return m_image[x + y * m_width];
		}

		RED_FORCE_INLINE void ReserveMemory( Uint32 width, Uint32 height )
		{
			m_width = width;
			m_height = height;
			m_image.Resize( width * height );
		}

	private:
		Uint32 m_width;
		Uint32 m_height;
		red::DynArray<Uint8> m_image{ PoolImageProcessing() };
	};

	//*******************************************************************************************************************************************//
	//*******************************************************************************************************************************************//

	class RENDER_DATA_API HdrResult final
	{
		RED_USE_MEMORY_POOL( rend::PoolImageProcessing );

	public:
		HdrResult( const red::memory::Pool& pool );

		~HdrResult();

		RED_FORCE_INLINE Uint32 GetWidth() const { return m_width; }
		RED_FORCE_INLINE Uint32 GetHeight() const { return m_height; }
		RED_FORCE_INLINE const float* GetPixels() const { return m_image.TypedData(); }
		RED_FORCE_INLINE float* GetWriteData() {  return m_image.TypedData(); }
		
		RED_FORCE_INLINE void ReserveMemory( Uint32 width, Uint32 height )
		{
			m_width = width;
			m_height = height;
			m_image.Resize( width * height * 4 );
		}

	private:
		Uint32 m_width;
		Uint32 m_height;
		red::DynArray<float> m_image{ PoolImageProcessing() };
	};

	//*******************************************************************************************************************************************//
	//*******************************************************************************************************************************************//

	class RENDER_DATA_API Color10Result final
	{
		RED_USE_MEMORY_POOL( rend::PoolImageProcessing );

	public:

		struct Value
		{
			Uint32				DATA;

			RED_FORCE_INLINE Uint32 X() const { return (DATA >> 0) & 1023; }
			RED_FORCE_INLINE Uint32 Y() const { return (DATA >> 10) & 1023; }
			RED_FORCE_INLINE Uint32 Z() const { return (DATA >> 20) & 1023; }
			RED_FORCE_INLINE Uint32 W() const { return (DATA >> 30) & 3; }
		};

	public:
		Color10Result( const red::memory::Pool& pool );

		~Color10Result();

		RED_FORCE_INLINE Uint32 GetWidth() const { return m_width; }
		RED_FORCE_INLINE Uint32 GetHeight() const { return m_height; }
		RED_FORCE_INLINE const Value* GetNormals() const { return m_image.TypedData(); }
		RED_FORCE_INLINE void* GetWriteData() { return m_image.Data(); }

		RED_FORCE_INLINE Value GetColor( Uint32 x, Uint32 y ) const
		{
			RED_FATAL_ASSERT( x < m_width, "" );
			RED_FATAL_ASSERT( y < m_height, "" );
			return m_image[x + y * m_width];
		}

		RED_FORCE_INLINE Uint32 GetW( Uint32 x, Uint32 y ) const
		{
			RED_FATAL_ASSERT( x < m_width, "" );
			RED_FATAL_ASSERT( y < m_height, "" );
			return m_image[x + y * m_width].W();
		}

		RED_FORCE_INLINE Vector3 GetNormalized( Uint32 x, Uint32 y ) const
		{
			const Value n = GetColor( x, y );
			const Vector3 ret( n.X() / 1023.0f, n.Y() / 1023.0f, n.Z() / 1023.0f );
			return (ret * 2.0f - 1.0f).Normalized();
		}

		RED_FORCE_INLINE void ReserveMemory( Uint32 width, Uint32 height )
		{
			m_width = width;
			m_height = height;
			m_image.Resize( width * height );
		}

	private:
		Uint32				m_width;
		Uint32				m_height;
		red::DynArray<Value> m_image{ PoolImageProcessing() };
	};

	//*******************************************************************************************************************************************//
	//*******************************************************************************************************************************************//

	class RENDER_DATA_API DepthResult final
	{
		RED_USE_MEMORY_POOL( rend::PoolImageProcessing );

	public:

		DepthResult( const red::memory::Pool& pool );

		~DepthResult();

		/// get a world space position at given pixel in the depth map
		/// returns false if the point is out of range or there's no depth information for it
		Bool CalcWorldSpacePoint( const Int32 pixelX, const Int32 pixelY, Vector3& outWorldPoint, const Float maxZ = 200.0f ) const;

	public:
		RED_FORCE_INLINE void ReserveMemory()
		{
			RED_FATAL_ASSERT( m_width * m_height > 0 );
			m_data.Resize( m_width * m_height );
		}

		RED_FORCE_INLINE red::DynArray<Float>& GetData()
		{
			return m_data; 
		}

		RED_FORCE_INLINE void* GetWriteData() { return m_data.Data(); }

		RED_FORCE_INLINE Float GetDepth( Uint32 x, Uint32 y ) const
		{
			RED_FATAL_ASSERT( x < m_width, "" );
			RED_FATAL_ASSERT( y < m_height, "" );
			return m_data[x + y * m_width];
		}

		// add viewport settings (this will be filled as a part of the result)
		void SetViewport( const Uint32 width, const Uint32 height, const Rect& dataRect );

		// add camera region (this will be filled as a part of the result)
		void AddCameraRegion( Uint32 priority, const Rect &area, const CRenderCamera &camera );

	private:
		// - - - - - - - - - - - - - - -
		struct SRegion
		{
			Uint32 m_priority;			//< priority of the region (in case two are overlapping, then the higher priority has the precedence)
			Rect m_area;				//< which pixels in viewport are handled by this camera region
			CRenderCamera m_camera;		//< camera which was used to render the region
		};
		// - - - - - - - - - - - - - - -

		Int32 FindRegionForPixel( const Int32 pixelX, const Int32 pixelY ) const;

	private:
		red::DynArray<Float>	m_data{ PoolImageProcessing() };
		Rect					m_dataRect;
		Uint32					m_width;
		Uint32					m_height;
		red::DynArray<SRegion>	m_regions{ PoolImageProcessing() };
		red::SpinLock			m_regionsLock;
	};

	//*******************************************************************************************************************************************//
	//*******************************************************************************************************************************************//

	class RENDER_DATA_API IColorCapture
	{
		RED_USE_MEMORY_POOL( rend::PoolImageProcessing );

	public:

		IColorCapture() {}

		virtual ~IColorCapture() {}

		// - - - - - - - - - - - - - - -

		virtual Bool ProcessResults( ColorResult* result, EEnvManagerModifier capType = EMM_None ) = 0;
		virtual Bool ProcessResults( HdrResult* results, EEnvManagerModifier capType = EMM_None ) = 0;

		virtual void SetupVideoCapture( const Uint32 frameWidth, const Uint32 frameHeight ) = 0;
		// - - - - - - - - - - - - - - -

	};

	//*******************************************************************************************************************************************//
	//*******************************************************************************************************************************************//

}
