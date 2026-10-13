/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#include "build.h"
#include "rttiArrayTypesImpl.h"
#include "rttiType.h"
#include "rttiArrayTypes.h"
#include "rttiUtils.h"
#include "../../redContainers/include/dynArrayAccessor.h"
#include "../../redContainers/include/staticArrayAccessor.h"
#include "../../redFileSystem/include/file.h"


//////////////////////////////////////////////////////////////////////////
// usings
using red::OffsetPtr;


namespace rtti
{
	ArrayType::ArrayType( const rtti::IType *innerType )
		: m_innerType( innerType )
		, m_name( FormatDynArrayTypeName( innerType->GetName() ) )
	{
	}

	void ArrayType::Construct( void *object ) const
	{
		red::DynArrayAccessor::Create( object );
	}

	void ArrayType::Destruct( void *object ) const
	{
		// destroy array fields
		if ( m_innerType->NeedsCleaning() )
		{
			const Uint32 count = GetArraySize( object );
			for ( Uint32 i = 0; i < count; ++i )
			{
				void* itemPtr = GetArrayElement( object, i );
				m_innerType->Destruct( itemPtr );
			}
		}

		// clear array's memory
		red::DynArrayAccessor& ar = red::DynArrayAccessor::GetRef( object );
		ar.Clear();
		ar.Shrink( m_innerType->GetSize(), m_innerType->GetAlignment() );
	}

	Uint32 ArrayType::GetArraySize( const void* arrayData ) const
	{	
		if ( arrayData )
		{
			return red::DynArrayAccessor::GetRef( arrayData ).Size();
		}

		return 0;
	}

	Uint32 ArrayType::GetArrayCapacity( const void* arrayData ) const
	{
		if ( arrayData )
		{
			return red::DynArrayAccessor::GetRef( arrayData ).Capacity();
		}

		return 0;
	}

	void* ArrayType::GetArrayElement( void* arrayData, Uint32 index ) const
	{
		if ( arrayData )
		{
			const red::DynArrayAccessor& ar = red::DynArrayAccessor::GetRef( arrayData );
			if ( index < ar.Size() )
			{
				const Uint32 itemSize = m_innerType->GetSize();
				return OffsetPtr( (void*)ar.Data(), index * itemSize );
			}
		}

		return NULL;
	}

	const void* ArrayType::GetArrayElement( const void* arrayData, Uint32 index ) const
	{
		if ( arrayData )
		{
			const red::DynArrayAccessor& ar = red::DynArrayAccessor::GetRef( arrayData );
			if ( index < ar.Size() )
			{
				const Uint32 itemSize = m_innerType->GetSize();
				return OffsetPtr( (void*)ar.Data(), index * itemSize );
			}
		}

		return NULL;
	}

	Int32 ArrayType::AddArrayElement( void* arrayData, Uint32 count, const red::memory::Pool & pool ) const
	{
		RED_ASSERT( count >= 1 );

		if ( arrayData )
		{
			red::DynArrayAccessor& ar = red::DynArrayAccessor::GetRef( arrayData );
			if( ar.Capacity() == 0 )
			{
				ar.SetPool( pool );
			}
			
			const Uint32 itemIndex = ar.Size();
			ar.Grow( count, m_innerType->GetSize(), m_innerType->GetAlignment() );

			// Clean memory before construct
			red::Memzero( GetArrayElement( arrayData, itemIndex ), count * m_innerType->GetSize() );

			for( Uint32 i=0; i<count; ++i )
			{
				m_innerType->Construct( GetArrayElement( arrayData, itemIndex + i ) );
			}

			return itemIndex;
		}

		return -1;
	}

	Bool ArrayType::DeleteArrayElement( void* arrayData, Int32 itemIndex ) const
	{	
		if ( arrayData )
		{
			red::DynArrayAccessor& ar = red::DynArrayAccessor::GetRef( arrayData );
			if ( itemIndex >= 0 && itemIndex < (Int32)ar.Size() )
			{
				// Shift elements
				for ( Uint32 i=itemIndex+1; i<ar.Size(); i++ )
				{
					const void* srcItem = GetArrayElement( arrayData, i );
					void* destItem = GetArrayElement( arrayData, i-1 );
					m_innerType->Copy( destItem, srcItem );
				}			

				// destroy last one (previous are destroyed properly using "copy" operation
				// which in case of objects is implemented with = operator
				m_innerType->Destruct( GetArrayElement( arrayData, ar.Size()-1 ) );

				// Resize array
				ar.Resize( ar.Size() - 1, m_innerType->GetSize(), m_innerType->GetAlignment() );
				return true;
			}
		}

		return false;
	}

	Bool ArrayType::DeleteArrayElementFast( void* arrayData, Int32 itemIndex ) const
	{	
		if ( arrayData )
		{
			red::DynArrayAccessor& ar = red::DynArrayAccessor::GetRef( arrayData );
			if ( itemIndex >= 0 && itemIndex < (Int32)ar.Size() )
			{
				Int32 last = ( Int32 )( ar.Size() - 1 );

				if( itemIndex < last )
				{
					// Shift single element
					const void* srcItem = GetArrayElement( arrayData, last );
					void* destItem = GetArrayElement( arrayData, itemIndex );
					m_innerType->Copy( destItem, srcItem );
				}

				// destroy last one
				m_innerType->Destruct( GetArrayElement( arrayData, last ) );

				// Resize array
				ar.Resize( ar.Size() - 1, m_innerType->GetSize(), m_innerType->GetAlignment() );
				return true;
			}
		}

		return false;
	}

	Bool ArrayType::InsertArrayElementAt( void* arrayData, Int32 itemIndex ) const
	{	
		if ( arrayData )
		{
			red::DynArrayAccessor& ar = red::DynArrayAccessor::GetRef( arrayData );
			if ( itemIndex >= 0 && itemIndex <= (Int32)ar.Size() )
			{
				// Add one element
				ar.Grow( 1, m_innerType->GetSize(), m_innerType->GetAlignment() );

				// Clean memory before construct
				void* newElement = GetArrayElement( arrayData, ar.Size() - 1 );
				red::Memzero( newElement, m_innerType->GetSize() );

				// initialize it
				m_innerType->Construct( newElement );

				// Shift elements
				for ( Uint32 i = ar.Size() - 1; i > (Uint32)itemIndex; i-- )
				{
					const void* srcItem = GetArrayElement( arrayData, i - 1 );
					void* destItem = GetArrayElement( arrayData, i );
					m_innerType->Copy( destItem, srcItem );
				}			

				// Clear last item
				m_innerType->Destruct( GetArrayElement( arrayData, itemIndex ) );

				// Done
				return true;
			}
		}

		// Not deleted
		return false;
	}

	Bool ArrayType::Compare( const void* data1, const void* data2, Uint32 flags ) const
	{
		// Get array size
		Uint32 size1 = GetArraySize( data1 );
		Uint32 size2 = GetArraySize( data2 );

		if ( size1 != size2 )
		{	
			return false;
		}

		for ( Uint32 i=0; i<size1; i++ )
		{
			const void* item1 = GetArrayElement( data1, i );
			const void* item2 = GetArrayElement( data2, i );
			if ( !m_innerType->Compare( item1, item2, flags ) )
			{
				return false;
			}
		}

		return true;
	}

	void ArrayType::Copy( void* dest, const void* src ) const
	{
		// destroy array elements
		if ( m_innerType->NeedsCleaning() )
		{
			const Uint32 count = GetArraySize( dest );
			for ( Uint32 i = 0; i < count; ++i )
			{
				void* itemPtr = GetArrayElement( dest, i );
				m_innerType->Destruct( itemPtr );
			}
		}

		red::DynArrayAccessor& ar = red::DynArrayAccessor::GetRef( dest );
		ar.Clear(); // set array size to 0

		const Uint32 count = GetArraySize( src );
		if ( count )
		{
			// Allocate memory
			ar.GrowExact( count, m_innerType->GetSize(), m_innerType->GetAlignment() );
			red::Memset( ar.Data(), 0, m_innerType->GetSize() * count );

			// Destination items were destructed, so we need to construct them now
			for ( Uint32 i=0; i<count; i++ )
			{
				const void* srcItem = GetArrayElement( src, i );
				void* destItem = GetArrayElement( dest, i );
				m_innerType->Construct( destItem );				
				m_innerType->Copy( destItem, srcItem );
			}
		}
	}

	void ArrayType::Move( void* dest, void* src ) const
	{
		red::DynArrayAccessor& destArray = red::DynArrayAccessor::GetRef( dest );
		red::DynArrayAccessor& srcArray = red::DynArrayAccessor::GetRef( src );

		// destroy array elements
		if ( m_innerType->NeedsCleaning() )
		{
			const Uint32 count = GetArraySize( dest );
			for ( Uint32 i = 0; i < count; ++i )
			{
				void* itemPtr = GetArrayElement( dest, i );
				m_innerType->Destruct( itemPtr );
			}
		}

		destArray.Clear();
		destArray.Swap( srcArray );
	}

	Bool ArrayType::Serialize( IFile& file, void* data, ISerializable* owner  ) const
	{
		RED_ASSERT( m_innerType );

		// IMPORTANT: the static and dynamic array are what's called "binary compatible"
		// It means that they are able to load each other's serialized data without problems.
		// Modify this code very carefully.

		if ( file.IsWriter() )
		{
			// Write array count
			Uint32 count = GetArraySize( data );
			file << count;

			// Save elements
			if( m_innerType->GetType() == RT_Fundamental )
			{
				red::DynArrayAccessor& ar = red::DynArrayAccessor::GetRef( data );
				const Uint32 elementSize = m_innerType->GetSize();
				file.Serialize( ar.Data(), count * elementSize );
			}
			else
			{
				for ( Uint32 i = 0; i < count; i++ )
				{
					// Save property
					void* itemData = GetArrayElement( data, i );
					if ( !m_innerType->Serialize( file, itemData, owner ) )
					{
						return false;
					}
				}
			}
		}
		else if ( file.IsReader() )
		{
			// Read array count
			Uint32 count = 0;
			file << count;

			// Setup array
			Destruct( data );

			// Load items
			if ( count )
			{
				// Allocate memory
				red::DynArrayAccessor& ar = red::DynArrayAccessor::GetRef( data );
				const Uint32 elementSize = m_innerType->GetSize();
				ar.GrowExact( count, elementSize, m_innerType->GetAlignment() );

				if( m_innerType->GetType() == RT_Fundamental )
				{
					file.Serialize( ar.Data(), count * elementSize );
				}
				else
				{
					for( Uint32 i=0; i<count; ++i )
					{
						red::Memzero( GetArrayElement( data, i ), m_innerType->GetSize() );
						m_innerType->Construct( GetArrayElement( data, i ) );
					}

					// Load elements
					for ( Uint32 i=0; i<count; i++ )
					{
						// Load property
						void* itemData = GetArrayElement( data, i );
						if ( !m_innerType->Serialize( file, itemData, owner ) )
						{
							return false;
						}
					}
				}
			}
		}

		// Serialized
		return true;
	}

	Uint32 ArrayType::TryResizingArray( const void* arrayData, Uint32 size, const red::memory::Pool & pool ) const
	{
		red::DynArrayAccessor& arrayAccessor = red::DynArrayAccessor::GetRef( arrayData );
		const Uint32 elementSize = m_innerType->GetSize();
		const Uint32 elementAlignment = m_innerType->GetAlignment();
		arrayAccessor.ResizeExact( size, elementSize, elementAlignment );
		return size;
	}

	Bool ArrayType::NeedsCleaning() const
	{
		return true;
	}

	//////////////////////////////////////////////////////////////////////////

	NativeArrayType::NativeArrayType( const rtti::IType *innerType, const Uint32 elementCount )
		: m_elementCount( elementCount )
		, m_innerType( innerType )
		, m_name( FormatNativeArrayTypeName( innerType->GetName(), elementCount ) )
		, m_refName( FormatScriptedReferenceTypeName( m_name ) )
	{
		RED_ASSERT( m_elementCount >= 1 );
		RED_ASSERT( innerType != NULL );
	}

	Uint32	NativeArrayType::GetSize() const
	{
		return m_innerType->GetSize() * m_elementCount;
	}

	Uint32 NativeArrayType::GetAlignment() const
	{
		return m_innerType->GetAlignment();
	}

	void NativeArrayType::Construct( void *object ) const
	{
		for ( Uint32 i=0; i<m_elementCount; ++i )
		{
			void* elementPtr = GetArrayElement( object, i );
			m_innerType->Construct( elementPtr );
		}
	}

	void NativeArrayType::Destruct( void *object ) const
	{
		if ( m_innerType->NeedsCleaning() )
		{
			for ( Uint32 i=0; i<m_elementCount; ++i )
			{
				void* elementPtr = GetArrayElement( object, i );
				m_innerType->Destruct( elementPtr );
			}
		}
	}

	Bool NativeArrayType::Compare( const void* data1, const void* data2, Uint32 flags ) const
	{
		for ( Uint32 i=0; i<m_elementCount; ++i )
		{
			const void* elementPtr1 = GetArrayElement( data1, i );
			const void* elementPtr2 = GetArrayElement( data2, i );
			if ( !m_innerType->Compare( elementPtr1, elementPtr2, flags ) )
			{
				return false;
			}
		}

		return true;
	}

	void NativeArrayType::Copy( void* dest, const void* src ) const
	{
		Destruct( dest );

		for ( Uint32 i=0; i<m_elementCount; ++i )
		{
			void* elementPtrDest = GetArrayElement( dest, i );
			const void* elementPtrSrc = GetArrayElement( src, i );
			m_innerType->Copy( elementPtrDest, elementPtrSrc );
		}
	}

	Bool NativeArrayType::Serialize( IFile& file, void* data, ISerializable* owner  ) const
	{
		RED_ASSERT( m_innerType );

		// IMPORTANT: the static and dynamic array are what's called "binary comaptible"
		// It means that they are able to load each other's serialized data without problems.
		// Modify this code very carefully.

		if ( file.IsWriter() )
		{
			// Write array count
			Uint32 count = m_elementCount;
			file << count;

			// Save elements
			for ( Uint32 i=0; i<count; i++ )
			{
				// Save property
				void* itemData = GetArrayElement( data, i );
				if ( !m_innerType->Serialize( file, itemData ) )
				{
					return false;
				}
			}
		}
		else if ( file.IsReader() )
		{
			// Read array count
			Uint32 count = 0;
			file << count;

			// Cleanup the array buffer, this can cause potential memory leaks but it seems 
			// we cannot trust the original content enough to call destructors here 
			red::Memzero( data, GetSize() );

			// Construct all array elements
			for ( Uint32 i=0; i<m_elementCount; i++ )
			{
				void* itemData = GetArrayElement( data, i );
				m_innerType->Construct( itemData );
			}

			// Create temporary buffer for extra elements
			void* tempMemory = NULL;
			Bool tempMemoryAllocated = false;
			if ( count > m_elementCount )
			{
				const Uint32 innerTypeSize = m_innerType->GetSize();
				if ( innerTypeSize <= 256 )
				{
					tempMemory = alloca( innerTypeSize );
				}
				else
				{
					tempMemory = RED_ALLOCATE( red::PoolEngine, innerTypeSize );
					tempMemoryAllocated = true;
				}

				red::Memzero( tempMemory, innerTypeSize );
			}

			// Read all saved elements
			for ( Uint32 i=0; i<count; i++ )
			{
				// Load only elements within the size of the array, discard rest
				if ( i < m_elementCount )
				{
					void* itemData = GetArrayElement( data, i );
					if ( !m_innerType->Serialize( file, itemData ) )
					{
						return false;
					}
				}
				else
				{
					// We have stored more elements that we can handle in the static array, we need to discard those elements SAFELY and without any memory leaks
					m_innerType->Construct( tempMemory );
					if ( !m_innerType->Serialize( file, tempMemory ) )
					{
						m_innerType->Destruct( tempMemory );
						return false;
					}
					m_innerType->Destruct( tempMemory );
				}
			}

			// Release allocated memory
			if ( tempMemoryAllocated )
			{
				RED_FREE( red::PoolEngine, tempMemory );
			}
		}

		// Serialized
		return true;
	}

	Bool NativeArrayType::NeedsCleaning() const
	{
		// ctremblay: return true even though it might not need to. 
		// at this stage you might need trying to figure out if property array of current class need to be cleaned. 
		// This result in undefined behavior.
		return true; 
	}

	Uint32 NativeArrayType::GetArraySize( const void* ) const
	{
		// this does not depend on the data
		return m_elementCount;
	}

	void* NativeArrayType::GetArrayElement( void* arrayData, Uint32 index ) const
	{
		RED_ASSERT( index < m_elementCount );
		if ( index < m_elementCount )
		{
			return OffsetPtr( arrayData, index * m_innerType->GetSize() );
		}
		else
		{
			// this will cause access violation in the calling code, always better than overwrite some random memory
			return nullptr;
		}
	}

	const void* NativeArrayType::GetArrayElement( const void* arrayData, Uint32 index ) const
	{
		RED_ASSERT( index < m_elementCount );
		if ( index < m_elementCount )
		{
			return OffsetPtr( arrayData, index * m_innerType->GetSize() );
		}
		else
		{
			// this will cause access violation in the calling code, always better than overwrite some random memory
			return nullptr;
		}
	}

	Uint32 NativeArrayType::TryResizingArray( const void* arrayData, Uint32 size, const red::memory::Pool & pool ) const
	{
		return std::min( size, m_elementCount );
	}

	//////////////////////////////////////////////////////////////////////////

	StaticArrayType::StaticArrayType( const rtti::IType *innerType, const Uint32 maxSize )
		: m_innerType( innerType )
		, m_maxSize( maxSize )
		, m_name( FormatStaticArrayTypeName( innerType->GetName(), maxSize ) )
		, m_refName( FormatScriptedReferenceTypeName( m_name ) )
	{
		RED_ASSERT( m_maxSize >= 1 );
		RED_ASSERT( m_innerType != NULL );
	}

	Uint32 StaticArrayType::GetSize() const
	{
		return red::StaticArrayAccessor::CalcTypeSize( m_innerType->GetSize(), m_maxSize, m_innerType->GetAlignment() );
	}

	Uint32 StaticArrayType::GetAlignment() const
	{
		return red::StaticArrayAccessor::CalcTypeAlignment( m_innerType->GetAlignment() );
	}

	void StaticArrayType::Construct( void *object ) const
	{
		red::StaticArrayAccessor& ar = red::StaticArrayAccessor::GetRef( object );
		ar.Clear( m_innerType->GetSize(), m_maxSize );
	}

	void StaticArrayType::Destruct( void *data ) const
	{
		if ( m_innerType->NeedsCleaning() )
		{
			const Uint32 size = ArrayGetArraySize( data );
			for ( Uint32 i=0; i<size; ++i )
			{
				void* elemData = ArrayGetArrayElement( data, i );
				m_innerType->Destruct( elemData );
			}	
		}

		red::StaticArrayAccessor& ar = red::StaticArrayAccessor::GetRef( data );
		ar.Clear( m_innerType->GetSize(), m_maxSize );
	}

	Bool StaticArrayType::Compare( const void* data1, const void* data2, Uint32 flags ) const
	{
		const Uint32 size1 = ArrayGetArraySize( data1 );
		const Uint32 size2 = ArrayGetArraySize( data2 );
		if ( size1 != size2 )
		{
			return false;
		}

		for ( Uint32 i=0; i<size1; ++i )
		{
			const void* elemData1 = ArrayGetArrayElement( data1, i );
			const void* elemData2 = ArrayGetArrayElement( data2, i );

			if ( !m_innerType->Compare( elemData1, elemData2, flags ) )
			{
				return false;
			}
		}

		return true;
	}

	void StaticArrayType::Copy( void* dest, const void* src ) const
	{
		Destruct( dest );

		const Uint32 count = ArrayGetArraySize( src );
		if ( count )
		{
			// Allocate memory
			red::StaticArrayAccessor& ar = red::StaticArrayAccessor::GetRef( dest );
			ar.Grow( m_innerType->GetSize(), m_maxSize, count );

			// Copy elements
			for ( Uint32 i=0; i<count; i++ )
			{
				const void* srcItem = ArrayGetArrayElement( src, i );
				void* destItem = ArrayGetArrayElement( dest, i );
				red::Memset( destItem, 0, m_innerType->GetSize() );
				m_innerType->Construct( destItem );
				m_innerType->Copy( destItem, srcItem );
			}
		}
	}

	Bool StaticArrayType::Serialize( IFile& file, void* data, ISerializable* owner  ) const
	{
		RED_ASSERT( m_innerType );

		// IMPORTANT: the static and dynamic array are what's called "binary compatible"
		// It means that they are able to load each other's serialized data without problems.
		// Modify this code very carefully.

		if ( file.IsWriter() )
		{
			// Write array count
			Uint32 count = ArrayGetArraySize( data );
			file << count;

			// Save elements
			for ( Uint32 i=0; i<count; i++ )
			{
				// Save property
				void* itemData = ArrayGetArrayElement( data, i );
				if ( !m_innerType->Serialize( file, itemData ) )
				{
					return false;
				}
			}
		}
		else if ( file.IsReader() )
		{
			// Read array count
			Uint32 count = 0;
			file << count;

			// Setup array
			Destruct( data );

			// Load items
			if ( count )
			{
				// Clamp
				const Uint32 maxElements = Min< Uint32 >( count, m_maxSize );

				// Allocate memory
				red::StaticArrayAccessor& ar = red::StaticArrayAccessor::GetRef( data );
				ar.Grow( m_innerType->GetSize(), m_maxSize, maxElements );

				// Create elements
				for( Uint32 i=0; i<maxElements; ++i )
				{
					void* element = ArrayGetArrayElement( data, i );
					red::Memzero( element, m_innerType->GetSize() );
					m_innerType->Construct( element );
				}

				// Create temporary buffer for extra elements
				void* tempMemory = NULL;
				Bool tempMemoryAllocated = false;
				if ( count > m_maxSize )
				{
					const Uint32 innerTypeSize = m_innerType->GetSize();
					if ( innerTypeSize <= 256 )
					{
						tempMemory = alloca( innerTypeSize );
					}
					else
					{
						tempMemory = RED_ALLOCATE( red::PoolEngine, innerTypeSize );
						tempMemoryAllocated = true;
					}

					red::Memzero( tempMemory, innerTypeSize );
				}

				// Load elements
				for ( Uint32 i=0; i<count; i++ )
				{
					if ( i < m_maxSize )
					{
						// Load property
						void* itemData = ArrayGetArrayElement( data, i );
						if ( !m_innerType->Serialize( file, itemData ) )
						{
							return false;
						}
					}
					else
					{
						// We have stored more elements that we can handle in the static array, we need to discard those elements SAFELY and without any memory leaks
						m_innerType->Construct( tempMemory );
						if ( !m_innerType->Serialize( file, tempMemory ) )
						{
							m_innerType->Destruct( tempMemory );
							return false;
						}
						m_innerType->Destruct( tempMemory );
					}
				}

				// Release allocated memory
				if ( tempMemoryAllocated )
				{
					RED_FREE( red::PoolEngine, tempMemory );
				}
			}
		}

		// Serialized
		return true;
	}

	Uint32 StaticArrayType::TryResizingArray( const void* arrayData, Uint32 size, const red::memory::Pool & pool ) const
	{
		// Clamp
		const Uint32 maxElements = Min< Uint32 >( size, m_maxSize );

		// Allocate memory
		red::StaticArrayAccessor& ar = red::StaticArrayAccessor::GetRef( arrayData );
		ar.Grow( m_innerType->GetSize(), m_maxSize, maxElements );

		return maxElements;
	}

	Bool StaticArrayType::NeedsCleaning() const
	{
		// ctremblay: return true even though it might not need to. 
		// at this stage you might need trying to figure out if property array of current class need to be cleaned. 
		// This result in undefined behavior.
		return true;
	}

	Uint32 StaticArrayType::ArrayGetArraySize( const void* arrayData ) const
	{
		red::StaticArrayAccessor& ar = red::StaticArrayAccessor::GetRef( arrayData );
		return ar.GetSize( m_innerType->GetSize(), m_maxSize );
	}

	Uint32 StaticArrayType::ArrayGetArrayCapacity( const void* arrayData ) const
	{
		return m_maxSize;
	}

	void* StaticArrayType::ArrayGetArrayElement( void* arrayData, Uint32 index ) const
	{
		red::StaticArrayAccessor& ar = red::StaticArrayAccessor::GetRef( arrayData );
		const Uint32 innerTypeSize = m_innerType->GetSize();
		return ar.GetElement( index, innerTypeSize );
	}

	const void* StaticArrayType::ArrayGetArrayElement( const void* arrayData, Uint32 index ) const
	{
		red::StaticArrayAccessor& ar = red::StaticArrayAccessor::GetRef( arrayData );
		const Uint32 innerTypeSize = m_innerType->GetSize();
		return ar.GetElement( index, innerTypeSize );
	}

	Int32 StaticArrayType::ArrayAddElement( void* arrayData, Uint32 count/*=1*/ ) const
	{
		RED_ASSERT( count >= 1 );

		if ( arrayData )
		{
			red::StaticArrayAccessor& ar = red::StaticArrayAccessor::GetRef( arrayData );
			const Uint32 itemIndex = ar.GetSize( m_innerType->GetSize(), m_maxSize );
			if ( itemIndex + count <= m_maxSize )
			{
				const Uint32 innerTypeSize = m_innerType->GetSize();

				ar.Grow( m_innerType->GetSize(), m_maxSize, count );
				red::Memzero( ar.GetElement( innerTypeSize, itemIndex ), count * innerTypeSize );

				for ( Uint32 i=0; i<count; ++i )
				{
					void* elementData = ar.GetElement( innerTypeSize, i + itemIndex );
					m_innerType->Construct( elementData );
				}

				return itemIndex;
			}
		}

		return -1;
	}

	Bool StaticArrayType::ArrayDeleteElement( void* arrayData, Int32 itemIndex ) const
	{
		if ( arrayData )
		{
			red::StaticArrayAccessor& ar = red::StaticArrayAccessor::GetRef( arrayData );
			const Uint32 size = ar.GetSize( m_innerType->GetSize(), m_maxSize );

			if ( itemIndex >= 0 && itemIndex < (Int32)size )
			{
				const Uint32 innerTypeSize = m_innerType->GetSize();

				// Shift elements
				for ( Uint32 i=itemIndex+1; i<size; i++ )
				{
					const void* srcItem = ar.GetElement( innerTypeSize, i );
					void* destItem = ar.GetElement( innerTypeSize, i-1 );
					m_innerType->Copy( destItem, srcItem );
				}

				// destroy last one (previous are destroyed properly using "copy" operation
				// which in case of objects is implemented with = operator
				void* lastElement = ar.GetElement( innerTypeSize, size-1 );
				m_innerType->Destruct( lastElement );

				// Resize array
				ar.Remove( m_innerType->GetSize(), m_maxSize, 1 );
				return true;
			}
		}

		return false;

	}

	Bool StaticArrayType::ArrayInsertElement( void* arrayData, Int32 itemIndex ) const
	{
		if ( arrayData )
		{
			red::StaticArrayAccessor& ar = red::StaticArrayAccessor::GetRef( arrayData );
			const Uint32 size = ar.GetSize( m_innerType->GetSize(), m_maxSize );

			if ( itemIndex >= 0 && itemIndex <= (Int32)size && size < m_maxSize )
			{
				// Add one element
				ar.Grow( m_innerType->GetSize(), m_maxSize, 1 );

				// initialize it
				const Uint32 innerTypeSize = m_innerType->GetSize();

				// Clean memory before construct
				void* item = ar.GetElement( innerTypeSize, ar.GetSize( m_innerType->GetSize(), m_maxSize ) - 1 );
				red::Memzero( item, innerTypeSize );
				m_innerType->Construct( item );

				// Shift elements
				for ( Uint32 i = ar.GetSize( m_innerType->GetSize(), m_maxSize )-1; i > (Uint32)itemIndex; i-- )
				{
					const void* srcItem = ar.GetElement( innerTypeSize, i - 1 );
					void* destItem = ar.GetElement( innerTypeSize, i );
					m_innerType->Copy( destItem, srcItem );
				}			

				// Clear item
				m_innerType->Destruct( ar.GetElement( innerTypeSize, itemIndex ) );
				return true;
			}
		}

		// Not inserted
		return false;
	}
} // rtti