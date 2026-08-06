/*
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redFileSystem/include/file.h"
#include "../../redFileSystem/include/compressedNumSerializer.h"
#include "../../redContainers/include/idAllocator.h"

//////////////////////////////////////////////////////////////////////////

namespace red
{
	template < typename TElement > class DynArray;
	template < typename TElement, Uint32 MaxSize > class StaticArray;
	template < typename TKey, typename TValue, typename THashPolicy > class HashMap;
	template < typename TElement, typename THashPolicy > class HashSet;
	template < typename TKey, typename TValue, typename TSortPredicate > class Map;
	template < typename TElement, typename TSortPredicate > class Set;
	template < Uint32 MaxSize, typename TStorage > class BitSetBase;
	template < typename TStorage > class BitSetDynamicBase;
}

//////////////////////////////////////////////////////////////////////////
// common arrays serializer

class ArraySizeSerializer
{
public:

	template < typename T >
	RED_INLINE static void Serialize( IFile& file, T& arr, const typename T::ElementType& defaultValue = typename T::ElementType() )
	{
		if ( file.IsReader() )
		{
			// Read the number of elements in the array
			Uint32 size = ReadVarint_LEB128_Signed( file );

			// Initialize array
			arr.Resize( size, defaultValue );
		}
		else if ( file.IsWriter() )
		{
			// Write number of elements in the array
			WriteVarint_LEB128_Signed( file, arr.Size() );
		}
	}
};

template < Bool serialize_whole_buffer >
class ArrayDataSerializer
{
public:

	template < typename TArray >
	RED_INLINE static void Serialize( IFile& file, TArray& arr )
	{
		file.Serialize( arr.Data(), arr.DataSize() );
	}
};

template <>
class ArrayDataSerializer< false >
{
public:

	template < typename TArray >
	RED_INLINE static void Serialize( IFile& file, TArray& arr )
	{
		const Uint32 size = arr.Size();
		for ( Uint32 i = 0; i < size; i++ )
		{
			file << *( arr.TypedData() + i );
		}
	}
};

class ArraySerializer
{
public:

	template < typename T >
	RED_INLINE static void Serialize( IFile& file, T& arr, const typename T::ElementType& defaultValue = typename T::ElementType() )
	{
		SerializeSize( file, arr, defaultValue );
		SerializeData( file, arr );
	}

	template < typename T >
	RED_INLINE static void SerializeSize( IFile& file,  T& arr, const typename T::ElementType& defaultValue = typename T::ElementType() )
	{
		ArraySizeSerializer::Serialize( file, arr, defaultValue );
	}

	template < typename TArray >
	RED_INLINE static void SerializeData( IFile& file, TArray& arr )
	{
		typedef typename TArray::ElementType ElementType;
		ArrayDataSerializer< std::is_fundamental< ElementType >::value || std::is_enum< ElementType >::value >::Serialize( file, arr );
	}
};

//////////////////////////////////////////////////////////////////////////
// common arrays bulk serialization

template < typename TArray >
struct ArrayBulkSerializer
{
	TArray&	m_array;

	RED_INLINE ArrayBulkSerializer( TArray& arr )
		: m_array( arr )
	{}
};

template < typename TArray >
RED_INLINE void operator<<( IFile& file, const ArrayBulkSerializer< TArray >& arr )
{
	ArraySizeSerializer::Serialize( file, arr.m_array );
	ArrayDataSerializer< true >::Serialize( file, arr.m_array );
}

//////////////////////////////////////////////////////////////////////////
// DynArray

template < typename TElement >
RED_INLINE void operator<<( IFile& file, red::DynArray< TElement >& arr )
{
	ArraySerializer::Serialize( file, arr );
}

// if you are sure, you want to use "bulk" serialization for non-PODs
template < typename TElement >
RED_INLINE ArrayBulkSerializer< red::DynArray< TElement > > BulkSerialization( red::DynArray< TElement >& arr )
{
	return ArrayBulkSerializer< red::DynArray< TElement > >( arr );
};


//////////////////////////////////////////////////////////////////////////
// StaticArray

template < typename TElement, Uint32 MaxSize >
RED_INLINE void operator<<( IFile& file, red::StaticArray< TElement, MaxSize >& arr )
{
	ArraySerializer::Serialize( file, arr );
}

// if you are sure, you want to use "bulk" serialization for non-PODs
template < typename TElement, Uint32 MaxSize >
RED_INLINE ArrayBulkSerializer< red::StaticArray< TElement, MaxSize > > BulkSerialization( red::StaticArray< TElement, MaxSize >& arr )
{
	return ArrayBulkSerializer< red::StaticArray< TElement, MaxSize > >( arr );
};

//////////////////////////////////////////////////////////////////////////
// std::pair

template < typename Type1, typename Type2 >
RED_INLINE void operator<<( IFile& file, std::pair< Type1, Type2 >& pair )
{
	file << pair.first;
	file << pair.second;
}

//////////////////////////////////////////////////////////////////////////
// HashMap

class HashMapSerializer
{
public:

	template < typename TKey, typename TValue, typename THashPolicy >
	static void Serialize( IFile& file, red::HashMap< TKey, TValue, THashPolicy >& map, TValue defaultValue = TValue() )
	{
		typedef red::HashMap< TKey, TValue, THashPolicy > HashMap;
		typedef typename HashMap::BucketElement BucketElement;
		typedef typename HashMap::ElementIndex ElementIndex;
		constexpr ElementIndex InvalidElementIndex = HashMap::InvalidElementIndex;

		if ( file.IsReader() )
		{
			map.Clear();
			Uint32 size;
			file << size;
			map.Rehash( size );

			TKey key;
			TValue value( defaultValue );
			for ( Uint32 i = 0; i < size; ++i )
			{
				file << key;
				file << value;
				map.Insert( key, value );
			}
		}
		else if ( file.IsWriter() )
		{
			// Make sure hashmap serialization is deterministic by maintaining deterministic (depending on hashmap size) number of buckets
			// Note: Shrink() will do nothing if size equals capacity already
			map.Shrink();

			// Serialize the hashmap
			Uint32 size = map.Size();
			const Uint32 capacity = map.Capacity();
			RED_ASSERT( size == capacity );

			file << size;

			for ( Uint32 i = 0; i < capacity; ++i )
			{
				ElementIndex listIndex = map.m_buckets[ i ];
				if ( listIndex != InvalidElementIndex )
				{
					BucketElement* element = static_cast< BucketElement* >( map.m_bucketsPool.GetBlock( listIndex ) );
					if ( element->m_nextIndex == InvalidElementIndex ) // Common case: single element list
					{
						file << element->m_key;
						file << element->m_value;
					}
					else // More than one element on the list
					{
						// Reverse the list before saving it out (to maintain identical binary format of the hashmap between saves)
						ElementIndex reversedIndex = InvalidElementIndex;
						while ( listIndex != InvalidElementIndex )
						{
							element = static_cast< BucketElement* >( map.m_bucketsPool.GetBlock( listIndex ) );
							const ElementIndex nextIndex = element->m_nextIndex;
							element->m_nextIndex = reversedIndex;
							reversedIndex = listIndex;
							listIndex = nextIndex;
						}

						// Serialize and reverse back
						listIndex = InvalidElementIndex;
						while ( reversedIndex != InvalidElementIndex )
						{
							element = static_cast< BucketElement* >( map.m_bucketsPool.GetBlock( reversedIndex ) );
							file << element->m_key;
							file << element->m_value;

							const ElementIndex nextIndex = element->m_nextIndex;
							element->m_nextIndex = listIndex;
							listIndex = reversedIndex;
							reversedIndex = nextIndex;
						}
					}
				}
			}
		}
	}

	template < typename TKey, typename TValue, typename THashPolicy >
	static void GetSerializationOrder_Debug( red::HashMap< TKey, TValue, THashPolicy >& map, red::DynArray< TKey >& keys )
	{
		typedef red::HashMap< TKey, TValue, THashPolicy > HashMap;
		typedef typename HashMap::BucketElement BucketElement;
		typedef typename HashMap::ElementIndex ElementIndex;
		constexpr ElementIndex InvalidElementIndex = HashMap::InvalidElementIndex;

		map.Shrink();
		const Uint32 capacity = map.Capacity();
		keys.Reserve( capacity );
		red::DynArray< TKey* > tmpKeys;

		for ( Uint32 i = 0; i < capacity; ++i )
		{
			ElementIndex listIndex = map.m_buckets[ i ];
			if ( listIndex != InvalidElementIndex )
			{
				while ( listIndex != InvalidElementIndex )
				{
					BucketElement* element = static_cast< BucketElement* >( map.m_bucketsPool.GetBlock( listIndex ) );
					tmpKeys.PushBack( &element->m_key );
					listIndex = element->m_nextIndex;
				}
				for ( TKey* k : tmpKeys.Reverse() )
				{
					keys.PushBack( *k );
				}
				tmpKeys.Clear();
			}
		}
	}

};

template < typename TKey, typename TValue, typename THashPolicy >
RED_INLINE void operator<<( IFile& file, red::HashMap< TKey, TValue, THashPolicy >& map )
{
	HashMapSerializer::Serialize( file, map );
}

//////////////////////////////////////////////////////////////////////////
// HashSet

class HashSetSerializer
{
public:

	template < typename TElement, typename THashPolicy >
	static void Serialize( IFile& file, red::HashSet< TElement, THashPolicy >& set )
	{
		if ( file.IsReader() )
		{
			Uint32 size;
			file << size;
			set.Reserve( size );

			TElement element;
			for ( Uint32 i = 0; i < size; ++i )
			{
				file << element;
				set.Insert( element );
			}
		}
		else if ( file.IsWriter() )
		{
			// Make sure hash set serialization is deterministic by maintaining deterministic (depending on hash set size) number of buckets
			// Note: Shrink() will do nothing if size equals capacity already
			set.Shrink();

			// Serialize the hash set
			file << set.m_size;
			const Uint32 size = set.m_size;
			for ( Uint32 i = 0; i < size; ++i )
			{
				file << set.m_elements[ i ];
			}
		}
	}
};

template < typename TElement, typename THashPolicy >
RED_INLINE void operator<<( IFile& file, red::HashSet< TElement, THashPolicy >& set )
{
	HashSetSerializer::Serialize( file, set );
}

//////////////////////////////////////////////////////////////////////////
// Map

class MapSerializer
{
public:

	template < typename TKey, typename TValue, typename TSortPredicate >
	static void Serialize( IFile& file, red::Map< TKey, TValue, TSortPredicate >& map )
	{
		if ( file.IsReader() )
		{
			// Read the number of elements in the map
			Uint32 size = ReadVarint_LEB128_Signed( file );

			// Initialize arrays
			map.m_keys.Resize( size );
			map.m_values.Resize( size );
		}
		else if ( file.IsWriter() )
		{
			// Write number of elements in the array
			Uint32 size = map.m_keys.Size();
			WriteVarint_LEB128_Signed( file, static_cast< Int32 >( size ) );
		}
		ArraySerializer::SerializeData( file, map.m_keys );
		ArraySerializer::SerializeData( file, map.m_values );
	}

	template < typename TKey, typename TValue, typename TSortPredicate >
	static void SerializeOld( IFile& file, red::Map< TKey, TValue, TSortPredicate >& map )
	{
		// Because of backward compatibility with TArrayMap and TSortedMap,
		// red::Map needs to be serialized as DynArray of pairs.
		Uint32 size = 0;
		if ( file.IsReader() )
		{
			// Read the number of elements in the map
			size = ReadVarint_LEB128_Signed( file );

			// Initialize arrays
			map.m_keys.Resize( size );
			map.m_values.Resize( size );
		}
		else if ( file.IsWriter() )
		{
			// Write number of elements in the array
			size = map.m_keys.Size();
			WriteVarint_LEB128_Signed( file, size );
		}

		// Serialize pairwise
		for ( Uint32 i = 0; i < size; ++i )
		{
			file << map.m_keys[ i ];
			file << map.m_values[ i ];
		}
	}

	template < typename TKey, typename TValue, typename TSortPredicate >
	static void SerializeBulk( IFile& file, red::Map< TKey, TValue, TSortPredicate >& map )
	{
		if ( file.IsReader() )
		{
			// Read the number of elements in the map
			Uint32 size = ReadVarint_LEB128_Signed( file );

			// Initialize arrays
			map.m_keys.Resize( size );
			map.m_values.Resize( size );
		}
		else if ( file.IsWriter() )
		{
			// Write number of elements in the array
			WriteVarint_LEB128_Signed( file, map.m_keys.Size() );
		}
		ArrayDataSerializer< true >::Serialize( file, map.m_keys );
		ArrayDataSerializer< true >::Serialize( file, map.m_values );
	}

	template < typename TKey, typename TValue, typename TSortPredicate >
	static void SerializeBulkOld( IFile& file, red::Map< TKey, TValue, TSortPredicate >& map )
	{
		// Because of backward compatibility with TArrayMap and TSortedMap,
		// red::Map needs to be serialized as DynArray of pairs.
		Uint32 size = 0;
		if ( file.IsReader() )
		{
			// Read the number of elements in the map
			size = ReadVarint_LEB128_Signed( file );

			// Initialize arrays
			map.m_keys.Resize( size );
			map.m_values.Resize( size );
		}
		else if ( file.IsWriter() )
		{
			// Write number of elements in the array
			size = map.m_keys.Size();
			WriteVarint_LEB128_Signed( file, size );
		}

		// Serialize pairwise
		for ( Uint32 i = 0; i < size; ++i )
		{
			file.Serialize( map.m_keys.TypedData() + i, sizeof( TKey ) );
			file.Serialize( map.m_values.TypedData() + i, sizeof( TValue ) );
		}
	}
};

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE void operator<<( IFile& file, class red::Map< TKey, TValue, TSortPredicate >& map )
{
	MapSerializer::Serialize( file, map );
}

// if you are sure, you want to use "bulk" serialization for non-PODs
template < typename TKey, typename TValue, typename TSortPredicate >
struct MapBulkSerializer
{
	red::Map< TKey, TValue, TSortPredicate >& m_map;

	RED_INLINE MapBulkSerializer( red::Map< TKey, TValue, TSortPredicate >& map )
		: m_map( map )
	{}
};

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE MapBulkSerializer< TKey, TValue, TSortPredicate > BulkSerialization( red::Map< TKey, TValue, TSortPredicate >& map )
{
	return MapBulkSerializer< TKey, TValue, TSortPredicate >( map );
};

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE void operator<<( IFile& file, const MapBulkSerializer< TKey, TValue, TSortPredicate >& map )
{
	MapSerializer::SerializeBulk( file, map.m_map );
}

//////////////////////////////////////////////////////////////////////////
// Set

class SetSerializer
{
public:

	template < typename TElement, typename TSortPredicate >
	RED_INLINE static void Serialize( IFile& file, red::Set< TElement, TSortPredicate >& set )
	{
		ArraySerializer::Serialize( file, set.m_elements );
	}

	template < typename TElement, typename TSortPredicate >
	RED_INLINE static void SerializeBulk( IFile& file, red::Set< TElement, TSortPredicate >& set )
	{
		ArraySizeSerializer::Serialize( file, set.m_elements );
		ArrayDataSerializer< true >::Serialize( file, set.m_elements );
	}
};

template < typename TElement, typename TSortPredicate >
RED_INLINE void operator<<( IFile& file, red::Set< TElement, TSortPredicate >& set )
{
	SetSerializer::Serialize( file, set );
}

// if you are sure, you want to use "bulk" serialization for non-PODs
template < typename TElement, typename TSortPredicate >
struct SetBulkSerializer
{
	red::Set< TElement, TSortPredicate >& m_set;

	RED_INLINE SetBulkSerializer( red::Set< TElement, TSortPredicate >& set )
		: m_set( set )
	{}
};

template < typename TElement, typename TSortPredicate >
RED_INLINE SetBulkSerializer< TElement, TSortPredicate > BulkSerialization( red::Set< TElement, TSortPredicate >& set )
{
	return SetBulkSerializer< TElement, TSortPredicate >( set );
};

template < typename TElement, typename TSortPredicate >
RED_INLINE void operator<<( IFile& file, const SetBulkSerializer< TElement, TSortPredicate >& set )
{
	SetSerializer::SerializeBulk( file, set.m_set );
}

//////////////////////////////////////////////////////////////////////////
// BitSet

class BitSetSerializer
{
public:

	template < Uint32 MaxSize, typename TStorage >
	RED_INLINE static void Serialize( IFile& file, red::BitSetBase< MaxSize, TStorage >& bitSet )
	{
		if ( file.IsReader() )
		{
			Uint32 size = ReadVarint_LEB128_Signed( file );
			RED_FATAL_ASSERT( size <= bitSet.Size(), "Deserialized BitSet is too big" );
			// calculate data size for read number of bits
			const Uint32 STORAGE_BITS = red::BitSetBase< MaxSize, TStorage >::STORAGE_BITS;
			const Uint32 dataSize = ( size + STORAGE_BITS - 1 ) / STORAGE_BITS;
			// read data
			file.Serialize( bitSet.m_bits, dataSize * sizeof( TStorage ) );
			// fill the rest of BitSet with null data
			red::Memset( bitSet.m_bits + dataSize, 0, red::BitSetBase< MaxSize, TStorage >::INTERNAL_SIZE - dataSize );
		}
		else
		{
			// let's serialize BitSet size for the sake of compatibility with BitSetDynamic
			WriteVarint_LEB128_Signed( file, bitSet.Size() );
			// write data
			const Uint32 internalSize = red::BitSetBase< MaxSize, TStorage >::INTERNAL_SIZE;
			file.Serialize( bitSet.m_bits, internalSize * sizeof( TStorage ) );
		}
	}
};

template < Uint32 MaxSize, typename TStorage >
RED_INLINE void operator<<( IFile& file, red::BitSetBase< MaxSize, TStorage >& bitSet )
{
	BitSetSerializer::Serialize( file, bitSet );
}

//////////////////////////////////////////////////////////////////////////
// BitSetDynamic

class BitSetDynamicSerializer
{
public:

	template < typename TStorage >
	RED_INLINE static void Serialize( IFile& file, red::BitSetDynamicBase< TStorage >& bitSet )
	{
		if ( file.IsReader() )
		{
			Uint32 size = ReadVarint_LEB128_Signed( file );
			bitSet.Resize( size );
		}
		else
		{
			WriteVarint_LEB128_Signed( file, bitSet.Size() );
		}
		const Uint32 internalSize = bitSet.InternalSize();
		file.Serialize( bitSet.m_bits.Data(), internalSize * sizeof( TStorage ) );
	}
};

template < typename TStorage >
RED_INLINE void operator<<( IFile& file, red::BitSetDynamicBase< TStorage >& bitSet )
{
	BitSetDynamicSerializer::Serialize( file, bitSet );
}

//////////////////////////////////////////////////////////////////////////
// IDAllocatorDynamic
class IDAllocatorDynamicSerializer
{
public:
	static void Serialize( IFile& file, IDAllocatorDynamic& idAllocator )
	{
		file << idAllocator.m_freeIndices;
		file << idAllocator.m_firstFreeIndex;
		file << idAllocator.m_searchIndex;
		file << idAllocator.m_numAllocated;
	}
};

RED_INLINE void operator<<( IFile& file, IDAllocatorDynamic& idAllocator )
{
	IDAllocatorDynamicSerializer::Serialize( file, idAllocator );
}