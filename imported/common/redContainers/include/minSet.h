#pragma once

namespace red
{
namespace alg
{
	// Helper used to find K minimal elements out of N elements with defined comparator
	// Internally uses heap structure to optimize number of comparisons to O(NlogK)
	template < typename TElement >
	class MinSet
	{
	public:
		using TComparator = red::FixedSizeFunction< Bool ( const TElement& a, const TElement& b ) >;

		// Initializes minimal set with comparator, pool and a max number of minimal elements to find
		MinSet( const red::memory::Pool& pool, const Uint32 maxCount, TComparator&& comparator );

		// Pushes an object into the set, complexity: O(logK)
		// Note: Only actually adds an element if it fits within K minimal elements out of all elements considered so far
		void Push( const TElement& newElement );

		// Acquires minimal elements sorted from minimal to maximal, complexity: O(KlogK)
		// Note: Empties the set
		red::DynArray< TElement > AcquireSortedElements();
	
	private:
		TComparator m_comparator;
		red::DynArray< TElement > m_elements;
	};

	// --- template implementation ---

	template < typename TElement >
	MinSet< TElement >::MinSet( const red::memory::Pool& pool, const Uint32 maxCount, TComparator&& comparator )
		: m_comparator( std::move( comparator ) )
		, m_elements( pool )
	{
		RED_ASSERT( m_comparator );
		RED_ASSERT( maxCount > 0 );
		m_elements.Reserve( maxCount );
	}

	template < typename TElement >
	void MinSet< TElement >::Push( const TElement& newElement )
	{
		// Remove top element if it compares less with the new one

		if ( m_elements.Size() == m_elements.Capacity() )
		{
			if ( !m_comparator( newElement, m_elements[ 0 ] ) )
			{
				return;
			}
			std::pop_heap( m_elements.Begin(), m_elements.End(), m_comparator );
			m_elements.PopBack();
		}

		// Add new element

		m_elements.PushBack( newElement );
		std::push_heap( m_elements.Begin(), m_elements.End(), m_comparator );
	}

	template < typename TElement >
	red::DynArray< TElement > MinSet< TElement >::AcquireSortedElements()
	{
		std::sort_heap( m_elements.Begin(), m_elements.End(), m_comparator );
		return std::move( m_elements );
	}

} // alg
} // red

