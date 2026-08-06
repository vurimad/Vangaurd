/**
* Copyright (c) 2019 CDProjekt Red, Inc. All Rights Reserved.
*/

#pragma once

#include "jobBuilder.h"

namespace job
{

	struct ParallelSortParams
	{
		Uint32 fallbackTreshold = 50000;
	};

	template < typename IterType, typename Comparator >
	void ParallelSort( job::Builder& builder, IterType first, IterType last, Comparator comparator, const ParallelSortParams& params = ParallelSortParams() )
	{
		const auto numElements = last - first;

		const Bool split = numElements > params.fallbackTreshold;

		if ( !split )
		{
			std::sort( first, last, comparator );
			return;
		}

		const IterType middle = first + ( numElements / 2 );

		builder.DispatchJob< Fence::None >( "ParallelSortLeft", [ first, middle, comparator, params ]( const job::RunContext& context )
		{
			job::Builder depBuilder{ context };
			ParallelSort( depBuilder, first, middle, comparator );
		} );
		builder.DispatchJob< Fence::None >( "ParallelSortRight", [ middle, last, comparator, params ]( const job::RunContext& context )
		{
			job::Builder depBuilder{ context };
			ParallelSort( depBuilder, middle, last, comparator );
		} );

		builder.DispatchFenceExplicitly();

		builder.DispatchJob( "ParallelSortMerge", [ first, middle, last, comparator, params ] ( const job::RunContext& context )
		{
			std::inplace_merge( first, middle, last, comparator );
		} );
	}

}