/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"

#ifndef RED_MEMORY_ENABLE_DEFAULT_ALLOCATION
#define RED_MEMORY_ENABLE_DEFAULT_ALLOCATION
#endif

#include "../include/redMemoryPublic.h"
#include "../include/utils.h"
#include "../include/fixedSizeAllocator.h"
#include "../include/tlsfAllocator.h"
#include "../include/linearAllocator.h"
#include "../include/stackAllocator.h"
#include "../include/circularAllocator.h"
#include "../include/frameAllocator.h"
#include "../include/bigSizeAllocator.h"
#include "../include/metricsUtils.h"
#include "../include/memoryStream.h"
#include <stdlib.h> // for custom allocator example.

#if defined( RED_PLATFORM_LINUX )
#	include <malloc.h> // for malloc_usable_size()
#endif

// This a documentation file. For sake of readability, some variable are assign but not used.
RED_DISABLE_WARNING_CLANG( "-Wunused-variable" ); 
RED_DISABLE_WARNING_CLANG( "-Wunused-value" ); 
RED_DISABLE_WARNING_MSC( 4189 );
RED_DISABLE_WARNING_MSC( 4100 );



using namespace red;

//////////////////////////////////////////////////////////////////////////
//
// redMemory DOCUMENTATION, REFERENCE AND HOW-TO
//
// This documentation should always compile.
// If it doesn't compile because of changes in the redMemory public interface, please fix code sample and update documentation below.
//
// For more examples and use cases, please refer to the UnitTestMemory project. 
// 
// TABLE OF CONTENT
//
// 0. Introduction 
//
// 1. RED_NEW and RED_DELETE replacement utility for the new and delete operators
//	1.1 Automatic Pool resolution
//  1.2 Allocating from a Pool
//  1.3 Allocating with a specific Allocator
//	1.4 RED_NEW_WITHOUT_HOOKS and RED_DELETE_WITHOUT_HOOKS macros for disabling memory hooks for current allocation/deallocation
//
// 2. RED_NEW_ARRAY and RED_DELETE_ARRAY replacement utility for the new[] and delete[] operators
//	2.1 RED_NEW_ARRAY_WITHOUT_HOOKS and RED_DELETE_ARRAY_WITHOUT_HOOKS macros for disabling memory hooks for current allocation/deallocation
//
// 3. RED_ALLOCATE, RED_FREE and RED_REALLOCATE utility replacement for malloc, free and realloc
//	3.1 Allocating from a specific Pool
//	3.2 Allocating with a specific Allocator
//	3.3 RED_ALLOCATE_WITHOUT_HOOKS, RED_FREE_WITHOUT_HOOKS and RED_REALLOCATE_WITHOUT_HOOKS macros for disabling memory hooks for current allocation/deallocation/reallocation
//
// 4. Allocators
//	4.1 DefaultAllocator
//	4.2 TLSFAllocator
//	4.3 FixedSizeAllocator
//	4.4 LinearAllocator
//	4.5 StackAllocator
//	4.6 CircularAllocator
//	4.7 FrameAllocator
//	4.8 BigSizeAllocator
//	4.9 How to create an Allocator that is compatible with redMemory
//	4.10 Orbis Flexible Memory support
//	4.11 GPU memory support
//
// 5. Pool
//	5.1 How to create a simple Pool
//	5.2 Initializing your pool
//  5.3 Binding your own Allocator
//	5.4 Out of Memory (OOM) handling
//
// 6. Runtime Metrics
//  6.1 Memory Capture
//
// 7. Debug Utility
//  7.1 Hook system
//  7.2 Memory Marking
//  7.3 Memory Stomp Detection
//  7.4 Memory Overrun Detection
//  7.5 How to enable debug allocator on a specific pool without recompiling the application
//
//////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////
// 
// 0. Introduction
//
// redMemory aims to replace the single native platform allocator with a family of allocators, each one suited for a specific need.
// There are 3 main concepts that need to be understood:
//
// Pool: 
// Represents a logical view on top of an Allocator. You can have multiple pools using the same allocator. 
// It allows you to customize the behavior of the allocator for your needs:
//	- Budget for this pool
//  - OOM Handling
//	- Runtime Metrics 
// 
// Allocator:
// Fulfill dynamic memory allocation requests. Allocators are made to be simple and efficient. 
// They follow a strict static interface instead of a virtual one. 
// An allocator can be a composition of allocators, like the DefaultAllocator.
//
// Operators:
// All basic memory operations are wrapped via macro utility. 
// There are a few advantages of this approach:
//  - DLL support. You can free memory allocated from another dll.
//	- Alignment support. operator new do not support class alignment. Native allocator always align everything to 16 to be on the safe side. We do not to optimize memory consumption.
//  - Debug Utilities. File and line of an allocation can be extracted. Usefully for monitoring allocation.
//	- Loose allocator/pool usage. No constraint into what allocator or pool an object must be allocated from.  
// 

//////////////////////////////////////////////////////////////////////////
// 
// 1. RED_NEW and RED_DELETE replacement utility for the new and delete operators
//
// By default RED_NEW and RED_DELETE will use the DefaultAllocator via the Default Pool. It will correctly ctor/dtor your object.
// They behave like normal new/delete operators.
// 
// A simple scalar can be allocated/deallocated via RED_NEW and RED_DELETE.
// Like all scalars, the ctor parameter is optional. 
// IMPORTANT NOTE memory is not wiped by allocator!
void Sample_1_0_Scalar()
{
	int32_t * scalar = RED_NEW( int32_t );
	// scalar will be filled with an undefined value
	RED_DELETE( scalar );
}

//////////////////////////////////////////////////////////////////////////
// Simple pod objects are supported. However, pod objects do not have a ctor. 
// IMPORTANT NOTE memory is not wiped by allocator!
//
void Sample_1_0_POD()
{
	struct POD
	{
		int32_t param1;
		float param2;
	};

	POD * pod = RED_NEW( POD );
	// pod will be filled with an undefined value
	RED_DELETE( pod );
}

//////////////////////////////////////////////////////////////////////////
// Objects will have their ctor and dtor called when allocated/deallocated via RED_NEW and RED_DELETE
//
void Sample_1_0_Object()
{
	static bool ctorCalled = false;
	static bool dtorCalled = false;

	struct Object
	{
		Object() { ctorCalled = true; }
		~Object() { dtorCalled = true; }
	};

	Object * object = RED_NEW( Object );
	RED_DELETE( object );
}

//////////////////////////////////////////////////////////////////////////
// RED_NEW supports ctor with a variable count of arguments using the form RED_NEW( Foo )( param1, param2, ..., paramN );
//
void Sample_1_0_Ctor_with_params()
{
	int32_t * scalar = RED_NEW( int32_t )( 123 );
	
	struct Object
	{
		Object( int32_t , float , bool ) { }
	};

	Object * object = RED_NEW( Object )( 123, 0.123f, true );

	RED_DELETE( scalar );
	RED_DELETE( object );
}

//////////////////////////////////////////////////////////////////////////
// RED_NEW will correctly align allocated objects according to their requirements.
//
void Sample_1_0_Alignment()
{
	struct Object
	{
		RED_ALIGN( 64 ) char content[ 128 ];	// Object alignment requirement is now 64!
	};

	Object * object = RED_NEW( Object );
	RED_MEMORY_ASSERT( memory::IsAligned( object, 64 ), "Believe me, your object is correctly aligned!" );
	RED_DELETE( object );
}

//////////////////////////////////////////////////////////////////////////
// RED_DELETE compilation will fail with a static assert if the object pointer provided refers to a polymorphic object without a virtual dtor
//
void Sample_1_0_Missing_virtual_dtor()
{
	struct Interface
	{
		virtual void Foo() {}
	};

	struct Object : Interface
	{
		virtual void Foo() override final {}
	};

	Interface * object = RED_NEW( Object );
	//RED_DELETE( object ); // This won't compile. If you don't trust me, try it yourself ! :)
	RED_UNUSED( object );
}

//////////////////////////////////////////////////////////////////////////
// 
// 1.1 Automatic Pool resolution
//
// In previous sample, RED_NEW and RED_DELETE used the Default pool provided by the memory framework.
// It is possible to tag an object with a specific pool when RED_NEW/RED_DELETE are used. 
// When tagged, it can't be changed at runtime. However it can be overridden, see section 1.2
//
// For example:
//
struct Sample_1_1_Object
{
	RED_USE_MEMORY_POOL( PoolDebug );
};

struct Sample_1_1_Base_Object
{
	virtual ~Sample_1_1_Base_Object() = default;
	RED_USE_MEMORY_POOL( PoolDebug );
};

struct Sample_1_1_Derived_Object : Sample_1_1_Base_Object
{
	RED_USE_MEMORY_POOL( PoolDefault );
};

struct Sample_1_1_Polymorphic_Base_Object
{
	virtual ~Sample_1_1_Polymorphic_Base_Object() = default;
	RED_USE_POLYMORPHIC_MEMORY_POOL( PoolDebug );
};

struct Sample_1_1_Polymorphic_Derived_Object : Sample_1_1_Polymorphic_Base_Object
{
	RED_USE_MEMORY_POOL( PoolDefault );
};

void Sample_1_1_Tagged_Object()
{
	Sample_1_1_Object * object1 = RED_NEW( Sample_1_1_Object ); // Will allocate from the DefaultAllocator via PoolDebug
	RED_DELETE( object1 ); // Will deallocate from the DefaultAllocator via PoolDebug

	Sample_1_1_Base_Object * object2 = RED_NEW( Sample_1_1_Derived_Object ); // Will allocate from the DefaultAllocator via PoolDefault
	RED_DELETE( object2 ); // Will deallocate from the DefaultAllocator via PoolDebug

	Sample_1_1_Polymorphic_Base_Object * object3 = RED_NEW( Sample_1_1_Polymorphic_Derived_Object ); // Will allocate from the DefaultAllocator via PoolDefault
	RED_DELETE( object3 ); // Will deallocate from the DefaultAllocator via PoolDefault
}

//////////////////////////////////////////////////////////////////////////
// 
// 1.2 Allocating from a specific Pool
//
// It is possible to provide a pool directly to the RED_NEW/RED_DELETE operators.
// A pool that is specified directly will take precedence over a tagged pool in the object.
// 
void Sample_1_2_Specific_Pool_Type_Provided()
{
	RED_MEMORY_POOL_STATIC( PoolSample, memory::DefaultAllocator );

	int32_t * scalar = RED_NEW( int32_t, PoolSample )( 123 );
	RED_DELETE( scalar, PoolSample );

	struct Object
	{
		virtual ~Object() {}
		virtual void Foo() {}
	};

	Object * object = RED_NEW( Object, PoolSample );
	RED_DELETE( object, PoolSample );
}

//////////////////////////////////////////////////////////////////////////
// A Pool concrete type is not the only way to specify a pool when using RED_NEW/RED_DELETE
// Pools that are stored as variables are also valid using the same syntax.
// It is as fast as the previous example.
//
void Sample_1_2_Specific_Pool_Provided()
{
	RED_MEMORY_POOL_STATIC( PoolSample, memory::DefaultAllocator );
	PoolSample pool;

	int32_t * scalar = RED_NEW( int32_t, pool )( 123 );
	RED_DELETE( scalar, pool );

	struct Object
	{
		Object() : value( 123 ) {}
		int32_t value;
	};

	Object * object = RED_NEW( Object, pool );
	RED_DELETE( object, pool );
}

//////////////////////////////////////////////////////////////////////////
// It is not always possible to know the actual type of the pool, or to store the concrete pool variable.
// Fortunately, you can specify the pool using the pool interface variable.
//
void Sample_1_2_Pool_Interface_Provided()
{
	RED_MEMORY_POOL_STATIC( PoolSample, memory::DefaultAllocator );
	
	PoolSample pool;
	red::memory::Pool & poolInterface = pool;

	int32_t * scalar = RED_NEW( int32_t, poolInterface )( 123 );
	RED_DELETE( scalar, poolInterface );

	struct Object
	{
		Object( int32_t , float , bool ) { }
		RED_ALIGN( 64 ) char content[ 128 ];
	};

	Object * object = RED_NEW( Object, poolInterface )( 0, 0.12f, true );
	RED_DELETE( object, poolInterface );
}

//////////////////////////////////////////////////////////////////////////
//
// 1.3 Allocating from a specific Allocator
//
// In some cases, a local allocator is available and is not bound to any pool. 
// You can use RED_NEW/RED_DELETE with an allocator the same way as with a pool.
// In this case the Hook system will be available but the pool metrics will not.
//
void Sample_1_3_Allocator_Provided()
{
	char localBuffer[ 128 ];
	memory::LocklessStaticFixedSizeAllocatorParameter param = { localBuffer, sizeof( localBuffer ), 16, 16 };
	memory::LocklessStaticFixedSizeAllocator allocator;
	allocator.Initialize( param );

	float * scalar = RED_NEW( float, allocator )( 0.123f );
	RED_DELETE( scalar, allocator );
	
	struct Interface
	{
		Interface( int32_t value_ ) : value( value_ ) {}
		virtual ~Interface(){}
		int32_t value;
	};

	struct Object : Interface
	{
		Object( int32_t value_ ) : Interface( value_ ) {}
	};

	Interface * object = RED_NEW( Object, allocator )( 123 );
	RED_DELETE( object, allocator );
}

//////////////////////////////////////////////////////////////////////////
//
// 1.4 RED_NEW_WITHOUT_HOOKS and RED_DELETE_WITHOUT_HOOKS macros for disabling memory hooks for current allocation / deallocation
//
// If RED_MEMORY_ENABLE_HOOKS is defined then redMemory for each allocation / deallocation runs registered hooks.
// You can disable all or some of these hooks for current allocation.

void Sample_1_4_RED_NEW_WITHOUT_HOOKS_AND_RED_DELETE_WITHOUT_HOOKS()
{
	int* ptr = RED_NEW_WITHOUT_HOOKS( int, red::memory::HookType::HookType_All ); // it will disable all hooks for current allocation
	RED_DELETE_WITHOUT_HOOKS( ptr, red::memory::HookType::HookType_All ); // it will disable all hooks for current deallocation
}

//////////////////////////////////////////////////////////////////////////
//
// 2. RED_NEW_ARRAY and RED_DELETE_ARRAY replacement utility for the new[] and delete[] operators
//
// Like RED_NEW/RED_DELETE replace the new and delete operators, RED_NEW_ARRAY and RED_DELETE_ARRAY aim to replace new [] delete [].
// The correct forms are: 
// RED_NEW_ARRAY( ObjectType, Count, (optional) Pool/Allocator ) 
// RED_DELETE_ARRAY( ObjectType, Count, (optional) Pool/Allocator )
//
// You will notice that RED_DELETE_ARRAY requires the object count. 
// The reasoning is that in order to know how many objects need their dtor called the object count needs to be stored somewhere.
// This results in wasted memory and could waste even more memory when alignment is taken into consideration.
// To use objects allocated with RED_NEW_ARRAY or new[] correctly the owner will usually keep the count locally. 
// Therefore in the majority of cases the owner can provide the object count explicitly.
//
// For example:
//
void Sample_2_0_Array()
{
	int32_t * scalarArray = RED_NEW_ARRAY( int32_t, 10 );
	RED_DELETE_ARRAY( scalarArray, 10 );

	RED_MEMORY_POOL_STATIC( PoolSample, memory::DefaultAllocator );

	struct Object
	{
		Object() : value( true ) {}
		bool value;
	};

	Object * objectArray = RED_NEW_ARRAY( Object, 10, PoolSample );
	RED_DELETE_ARRAY( objectArray, 10, PoolSample );
	
	// TAKE NOTE Like the previous Samples from RED_NEW/RED_DELETE, you can use RED_NEW_ARRAY and RED_NEW_DELETE with a pool variable or allocator 
}

//////////////////////////////////////////////////////////////////////////
//
// 2.1 RED_NEW_ARRAY_WITHOUT_HOOKS and RED_DELETE_ARRAY_WITHOUT_HOOKS macros for disabling memory hooks for current allocation/deallocation
//
// If RED_MEMORY_ENABLE_HOOKS is defined then redMemory for each allocation / deallocation runs registered hooks.
// You can disable all or some of these hooks for current allocation.

void Sample_1_4_RED_NEW_ARRAY_WITHOUT_HOOKS_AND_RED_DELETE_ARRAY_WITHOUT_HOOKS()
{
	int* ptr = RED_NEW_ARRAY_WITHOUT_HOOKS( int, 10, red::memory::HookType::HookType_All ); // it will disable all hooks for current allocation
	RED_DELETE_ARRAY_WITHOUT_HOOKS( ptr, 10, red::memory::HookType::HookType_All ); // it will disable all hooks for current deallocation
}

//////////////////////////////////////////////////////////////////////////
//
// 3. RED_ALLOCATE, RED_FREE and RED_REALLOCATE utility replacement for malloc, free, and realloc
// 
// The C allocation functions are also replaced with macros.
// However there are no default versions available. You must always provide a pool or allocator. 
// Be careful to not change the pool or proxy between allocation and free. 
// Doing so will either result in an assertion (if the allocator is different) or metric corruption (if the allocator is the same).
//
//
// 3.1 Allocating from a specific Pool
// 
// One important note, is that the previous reallocation function always had the pool or allocator specified at the end. 
// The reason why it was at the end is to make sure VisualAssist behaves nicely with the macro. It was also optional. 
// For RED_ALLOCATE, RED_FREE, and RED_REALLOCATE it is the first argument and is mandatory.
//
// For example:
//
void Sample_3_1_Allocate()
{
	RED_MEMORY_POOL_STATIC( PoolSample, memory::DefaultAllocator );

	const int32_t bufferSize = 128;
	const int32_t bufferAlignment = 16;

	void * buffer = RED_ALLOCATE( PoolSample, bufferSize );
	void * bufferAligned = RED_ALLOCATE_ALIGNED( PoolSample, bufferSize, bufferAlignment );

	RED_FREE( PoolSample, buffer );
	RED_FREE( PoolSample, bufferAligned );
}

void Sample_3_1_Reallocate()
{
	RED_MEMORY_POOL_STATIC( PoolSample, memory::DefaultAllocator );

	// providing a nullptr as an input memory block is the same as using RED_ALLOCATE
	void * buffer = RED_REALLOCATE( PoolSample, nullptr, 128 ); 
	
	void * reallocatedBuffer = RED_REALLOCATE( PoolSample, buffer, 256 );

	// providing 0 as the size is the same as calling RED_FREE with a provided input memory block. It will return a nullptr
	void * freedBuffer = RED_REALLOCATE( PoolSample, reallocatedBuffer, 0 ); 
	RED_UNUSED( freedBuffer );
}

//////////////////////////////////////////////////////////////////////////
//
// 3.2 Allocating from a specific Allocator
//
// Like with the new operator replacement (RED_NEW), you can also provide an allocator explicitly.
//
// For example:
//
void Sample_3_2_Allocator()
{
	const int32_t allocatorBufferSize = RED_KILO_BYTE( 64 );
	void * allocatorBuffer = RED_ALLOCATE( red::PoolDefault, allocatorBufferSize );
	const red::memory::StaticTLSFAllocatorParameter param = { allocatorBuffer, allocatorBufferSize };
	red::memory::StaticTLSFAllocator allocator;
	allocator.Initialize( param );

	void * buffer = RED_ALLOCATE( allocator, 128 );
	void * reallocBuffer = RED_REALLOCATE( allocator, buffer, 256 );
	RED_FREE( allocator, buffer );
	RED_FREE( allocator, reallocBuffer );
}

//////////////////////////////////////////////////////////////////////////
//
// 3.3 RED_ALLOCATE_WITHOUT_HOOKS, RED_FREE_WITHOUT_HOOKS and RED_REALLOCATE_WITHOUT_HOOKS macros for disabling memory hooks for current allocation/deallocation/reallocation
//
// If RED_MEMORY_ENABLE_HOOKS is defined then redMemory for each allocation / deallocation runs registered hooks.
// You can disable all or some of these hooks for current allocation.

void Sample_1_4_RED_ALLOCATE_WITHOUT_HOOKS_And_RED_FREE_WITHOUT_HOOKS_And_RED_REALLOCATE_WITHOUT_HOOKS()
{
	void* ptr = RED_ALLOCATE_WITHOUT_HOOKS( red::PoolDefault, 128, red::memory::HookType::HookType_All ); // it will disable all hooks for current allocation
	void* reallocatedPtr = RED_REALLOCATE_WITHOUT_HOOKS( red::PoolDefault, ptr, 256, red::memory::HookType::HookType_All ); // it will disable all hooks for current reallocation
	RED_FREE_WITHOUT_HOOKS( red::PoolDefault, reallocatedPtr, red::memory::HookType::HookType_All ); // it will disable all hooks for current deallocation
}

//////////////////////////////////////////////////////////////////////////
//
// 4. Allocators
// 
// This section will review all allocators currently available. 
// All allocators can be instantiated locally, with the exception of the DefaultAllocator.
// 
// 4.1 DefaultAllocator
//
// The DefaultAllocator is the only allocator that is always available. It is constructed and initialized in the first allocation.
// Therefore, all static allocation will go through the DefaultAllocator. 
// It is an aggregation of the following allocators: 
//	- LocklessSlabAllocator for small allocations ( less than 512 bytes )
//	- LockingTLSFAllocator for average size allocations ( between 512 bytes and 4MB ). A lockless version will most likely be introduced in future.
//  - BigSizeAllocator for big size allocations (more than 4MB).
//
// You can access the DefaultAllocator via the red::memory::AcquireDefaultAllocator() function.
// The red::PoolDefault and red::PoolDebug use the DefaultAllocator.
//
// For example:
//
void Sample_4_1_DefaultAllocator()
{
	red::memory::DefaultAllocator & allocator = red::memory::AcquireDefaultAllocator();
	int32_t * scalar = RED_NEW( int32_t, allocator );	// go through small size allocator
	RED_DELETE( scalar, allocator );

	class Object {};
	Object * object = RED_NEW( Object ); // RED_NEW/RED_DELETE route through DefaultAllocator by default
	RED_DELETE( object );

	void * buffer = RED_ALLOCATE_ALIGNED( red::PoolDefault, 1024, 16 ); // go through average size allocator
	RED_FREE( red::PoolDefault, buffer );

	void * bigBuffer = RED_ALLOCATE_ALIGNED( red::PoolDefault, RED_MEGA_BYTE( 20 ), 16 ); // go through big size allocator
	RED_FREE( red::PoolDefault, bigBuffer );
}

//////////////////////////////////////////////////////////////////////////
// 
// 4.2 TLSFAllocator
//
// Two Level Segregated Fit Allocator. 
//
// This allocator has a bounded response time, making it ideal for cases where we want a stable execution time.
// Allocation and deallocation are constant time O(1)
// Every allocation has overhead of 16 bytes for storing a header.
// It also has very low fragmentation.
//
// You should use this allocator when:
//	- Allocation sizes are not known and quite variable in size
//	- Fragmentation needs to be kept to a minimum
//	- Alignment requirement is more than 16 bytes
// 
// A few versions are available:
//	StaticTLSFAllocator. You need to provide the memory area upfront. NOT THREAD SAFE
//	DynamicTLSFAllocator. When out of memory, it will map virtual memory up to provided maximum. It can also use Flexible memory on PS4. NOT THREAD SAFE
//	LockingDynamicTLSFAllocator. Same as DynamicTLSFAllocator, but it will lock when allocating/deallocating
//
// For example:
//
void Sample_4_2_StaticTLSFAllocator()
{
	uint32_t allocatorBufferSize = RED_KILO_BYTE( 64 );
	void * allocatorBuffer = RED_ALLOCATE_ALIGNED( red::PoolDefault, allocatorBufferSize, 16 );
	red::memory::StaticTLSFAllocatorParameter param = { allocatorBuffer, allocatorBufferSize };
	red::memory::StaticTLSFAllocator allocator;
	allocator.Initialize( param );

	int32_t * scalar = RED_NEW( int32_t, allocator );
	RED_DELETE( scalar, allocator );

	void * buffer = RED_REALLOCATE( allocator, nullptr, RED_KILO_BYTE( 1 ) );
	RED_FREE( allocator, buffer );
}

void Sample_4_2_DynamicTLSFAllocator()
{
	red::memory::DynamicTLSFAllocatorParameter param = 
	{
		&red::memory::AcquireSystemAllocator(),	// Default system allocator. It is used to reserve and commit virtual memory.
		RED_MEGA_BYTE( 16 ),					// The allocator will consume a maximum of 16 Mb of virtual memory 
		RED_MEGA_BYTE( 1 ),						// It will commit 1 Mb of virtual memory when initialized
		RED_MEGA_BYTE( 2 ),						// When OOM, it will commit a 2Mb chunk of virtual memory
		red::memory::Flags_CPU_Read_Write,		// this allocator needs access to read and write, but you could also add GPU memory access right.
		0										// Virtual Range Alignment
	};

	red::memory::DynamicTLSFAllocator allocator;
	allocator.Initialize( param );

	int32_t * scalar = RED_NEW( int32_t, allocator );
	RED_DELETE( scalar, allocator );

	void * buffer = RED_REALLOCATE( allocator, nullptr, RED_KILO_BYTE( 1 ) );
	RED_FREE( allocator, buffer );
}

void Sample_4_2_LockingDynamicTLSFAllocator()
{
	red::memory::DynamicTLSFAllocatorParameter param = 
	{
		&red::memory::AcquireSystemAllocator(),	// Default system allocator. It is used to reserve and commit virtual memory.
		RED_MEGA_BYTE( 16 ),					// The allocator will consume a maximum of 16 Mb of virtual memory 
		RED_MEGA_BYTE( 1 ),						// It will commit 1 Mb of virtual memory when initialized
		RED_MEGA_BYTE( 2 ),						// When OOM, it will commit a 2Mb chunk of virtual memory
		red::memory::Flags_CPU_Read_Write,		// this allocator needs access to read and write, but you could also add GPU memory access right.
		0										// Virtual Range Alignment
	};

	red::memory::LockingDynamicTLSFAllocator allocator;
	allocator.Initialize( param );

	int32_t * scalar = RED_NEW( int32_t, allocator );
	RED_DELETE( scalar, allocator );
	void * buffer = RED_REALLOCATE( allocator, nullptr, RED_KILO_BYTE( 1 ) );
	RED_FREE( allocator, buffer );
}

//////////////////////////////////////////////////////////////////////////
// 
// 4.3 FixedSizeAllocator
//
// A simple allocator that always returns block of a given size, even if requested size is smaller.
// Allocation and deallocation are constant time O(1).
// There is no per allocation overhead, as there is no per allocation header.
// It does not fragment memory.
//
// You should use this allocator when:
//	- Allocation sizes are fixed
//  - Large numbers of allocations are needed
//  - Performance is an issue.
//  - Fragmentation is an issue.
//
// A few versions are available:
//	LocklessStaticFixedSizeAllocator. You need to provide the memory area upfront. THREAD SAFE
//	DynamicFixedSizeAllocator. When out of memory, it will map virtual memory up to provided maximum. It can also use Flexible memory on PS4. NOT THREAD SAFE
//
// For example:
//
void Sample_4_3_LocklessStaticFixedSizeAllocator()
{
	uint32_t allocatorBufferSize = RED_KILO_BYTE( 64 );
	void * allocatorBuffer = RED_ALLOCATE_ALIGNED( red::PoolDefault, allocatorBufferSize, 16 );
	red::memory::LocklessStaticFixedSizeAllocatorParameter param =
	{
		allocatorBuffer,		// 
		allocatorBufferSize,	// 
		RED_KILO_BYTE( 1 ),		// Fixed Block size returned by allocator
		16						// Fixed Block alignment
	};

	red::memory::LocklessStaticFixedSizeAllocator allocator;
	allocator.Initialize( param );

	int32_t * scalar = RED_NEW( int32_t, allocator ); // This will work, but scalar will use 1KB !  
	RED_DELETE( scalar, allocator );
	void * buffer = RED_REALLOCATE( allocator, nullptr, RED_KILO_BYTE( 1 ) );
	RED_FREE( allocator, buffer );
}

void Sample_4_3_DynamicFixedSizeAllocator()
{
	red::memory::DynamicFixedSizeAllocatorParameter param = 
	{
		&red::memory::AcquireSystemAllocator(),	// Default system allocator. It is used to reserve and commit virtual memory.
		RED_KILO_BYTE( 1 ),						// Fixed Block size returned by allocator
		16,										// Fixed Block alignment
		100,									// Initialize Block count
		1000,									// Maximum block count
		red::memory::Flags_CPU_Read_Write		// this allocator need access to read and write, but you could also add GPU memory access right.
	};

	red::memory::DynamicFixedSizeAllocator allocator;
	allocator.Initialize( param );

	int32_t * scalar = RED_NEW( int32_t, allocator ); // This will work, but scalar will use 1KB !
	RED_DELETE( scalar, allocator );
	void * buffer = RED_REALLOCATE( allocator, nullptr, RED_KILO_BYTE( 1 ) );
	RED_FREE( allocator, buffer );
}

//////////////////////////////////////////////////////////////////////////
// 
// 4.4 LinearAllocator
//
// A simple allocator that supports only allocations. In DynamicLinearAllocator deallocation occurs only once in allocator destructor.
// Allocations are constant time O(1).
// There is per allocation overhead 4 bytes.
// Reallocate and ReallocateAligned returns contiguous memory block if possible. Be careful because it might corrupt your data if alignment changed.
//
// You should use this allocator when:
//	- Freed is not needed (it can be handle on user side).
//	- Large numbers of allocations are needed.
//	- Data locality is an issue.
//
// A few versions are available:
//	LocklessStaticLinearAllocator. You need to provide the memory area upfront.
//	DynamicLinearAllocator. When out of memory, it will map virtual memory up to provided maximum. It can also use Flexible memory on PS4. NOT THREAD SAFE
//
// For example:
//
void Sample_4_4_StaticLinearAllocator()
{
	uint32_t allocatorBufferSize = RED_KILO_BYTE( 64 );
	void * allocatorBuffer = RED_ALLOCATE_ALIGNED( red::PoolDefault, allocatorBufferSize, 16 );
	red::memory::StaticLinearAllocatorParameter param =
	{
		allocatorBuffer,
		allocatorBufferSize,
	};

	red::memory::LocklessStaticLinearAllocator allocator;
	allocator.Initialize( param );

	int32_t * scalar = RED_NEW( int32_t, allocator );
	RED_DELETE( scalar, allocator );
	void * buffer = RED_REALLOCATE( allocator, nullptr, RED_KILO_BYTE( 1 ) );
	RED_FREE( allocator, buffer );
}

void Sample_4_4_DynamicLinearAllocator()
{
	red::memory::DynamicLinearAllocatorParameter param =
	{
		&red::memory::AcquireSystemAllocator(),	// Default system allocator. It is used to reserve and commit virtual memory.
		RED_KILO_BYTE( 1 ),						// Fixed, minimal chunk size allocated by system allocator
		red::memory::Flags_CPU_Read_Write		// this allocator need access to read and write, but you could also add GPU memory access right.
	};

	red::memory::DynamicLinearAllocator allocator;
	allocator.Initialize( param );

	int32_t * scalar = RED_NEW( int32_t, allocator );
	RED_DELETE( scalar, allocator );
	void * buffer = RED_REALLOCATE( allocator, nullptr, RED_KILO_BYTE( 1 ) );
	RED_FREE( allocator, buffer );
}

//////////////////////////////////////////////////////////////////////////
// 
// 4.5 StackAllocator
//
// A simple allocator that works in LIFO order.
// Allocation and deallocation are constant time O(1).
// There is per allocation overhead (8 bytes), each allocation header contains informations about size of the allocation and previous size of the stack.
// It does fragment memory when block is reallocated from the middle of stack.
//
// You should use this allocator when:
//	- Freed need to be made in reverse order to allocation
//  - Large numbers of allocations are needed
//
// A few versions are available:
//	StaticStackAllocator. You need to provide the memory area upfront. NOT THREAD SAFE
//	DynamicStackAllocator. When out of memory, it will map virtual memory up to provided maximum. It can also use Flexible memory on PS4. NOT THREAD SAFE
//
// For example:
//
void Sample_4_5_StaticStackAllocator()
{
	uint32_t allocatorBufferSize = RED_KILO_BYTE( 64 );
	void * allocatorBuffer = RED_ALLOCATE_ALIGNED( red::PoolDefault, allocatorBufferSize, 16 );
	red::memory::StaticStackAllocatorParameter param =
	{
		allocatorBuffer,
		allocatorBufferSize,
	};

	red::memory::StaticStackAllocator allocator;
	allocator.Initialize( param );

	int32_t * scalar = RED_NEW( int32_t, allocator );
	RED_DELETE( scalar, allocator );
	void * buffer = RED_REALLOCATE( allocator, nullptr, RED_KILO_BYTE( 1 ) );
	RED_FREE( allocator, buffer );
}

void Sample_4_5_DynamicStackAllocator()
{
	red::memory::DynamicStackAllocatorParameter param =
	{
		&red::memory::AcquireSystemAllocator(),	// Default system allocator. It is used to reserve and commit virtual memory.
		RED_KILO_BYTE( 1 ),						// Fixed, minimal chunk size allocated by system allocator
		red::memory::Flags_CPU_Read_Write		// this allocator need access to read and write, but you could also add GPU memory access right.
	};

	red::memory::DynamicStackAllocator allocator;
	allocator.Initialize( param );

	int32_t * scalar = RED_NEW( int32_t, allocator );
	RED_DELETE( scalar, allocator );
	void * buffer = RED_REALLOCATE( allocator, nullptr, RED_KILO_BYTE( 1 ) );
	RED_FREE( allocator, buffer );
}

//////////////////////////////////////////////////////////////////////////
// 
// 4.6 CircularAllocator
//
// A simple allocator that works in FIFO order.
// Allocation and deallocation are constant time O(1).
// There is per allocation overhead (8 bytes), each allocation header contains informations about size of the allocation and allocation marker.
// It does fragment memory when it is not possible to grow the block.
// It support overrun detection and FIFO order detection. Notice that it is turned off in release and final build.
//
// You should use this allocator when:
//  - Large numbers of small allocations are needed
//  - There are >= 1 threads which allocate memory and only one thread which deallocate the memory.
//
// CircularAllocator. THREAD SAFE in single producer/multiple producer - single consumer scenarios. NOT THREAD SAFE in single producer/multiple producer - multiple consumer scenarios.
//
// For example:
//
void Sample_4_6_CircularAllocator()
{
	uint32_t allocatorBufferSize = RED_KILO_BYTE( 64 );
	void * allocatorBuffer = RED_ALLOCATE_ALIGNED( red::PoolDefault, allocatorBufferSize, 16 );

	red::memory::SystemBlock block = { reinterpret_cast< uint64_t >( allocatorBuffer ), allocatorBufferSize };
	memory::CircularAllocatorParameter param =
	{
		block
	};

	red::memory::CircularAllocator allocator;
	allocator.Initialize( param );

	int32_t * scalar = RED_NEW( int32_t, allocator );
	RED_DELETE( scalar, allocator );
	void * buffer = RED_REALLOCATE( allocator, nullptr, RED_KILO_BYTE( 1 ) );
	RED_FREE( allocator, buffer );
}

//////////////////////////////////////////////////////////////////////////
// 
// 4.7 FrameAllocator
//
// A form of linear allocator that is synchronized with the game frame.
// Allocation and deallocation are constant time O(1).
// There is per allocation overhead (8 bytes), each allocation header contains informations about size of the allocation and allocation marker.
// It does fragment memory when it is not possible to grow the block.
// It support underrun and overrun detection.
//
// You should use this allocator when:
//  - Large numbers of small allocations are needed
//	- Allocation lifetime cycle is fitting in the fixed amount of game frames
//
//	LocklessFrameAllocator. It decommits the memory allocated on M-th frame at beginning of (N + M)-th frame, where N is the provided number of frames. THREAD SAFE
//
// For example:
//
void Sample_4_7_LocklessFrameAllocator()
{
	uint32_t frameAllocatorSize = RED_MEGA_BYTE( 16 );
	memory::FrameAllocatorParameter param =
	{
		&red::memory::AcquireSystemAllocator(),
		frameAllocatorSize, // block size for each frame
		1, // number of frames
		memory::Flags_CPU_Read_Write
	};

	red::memory::LocklessFrameAllocator allocator;
	allocator.Initialize( param );

	int32_t * scalar = RED_NEW( int32_t, allocator );
	RED_DELETE( scalar, allocator );
	void * buffer = RED_REALLOCATE( allocator, nullptr, RED_KILO_BYTE( 1 ) );
	RED_FREE( allocator, buffer );

	allocator.Reset();
}

//////////////////////////////////////////////////////////////////////////
// 
// 4.8 BigSizeAllocator
//
// A allocator that is designed to work with large allocations.
// Allocated memory is aligned to page size ( the same one which is used by system allocator, 16-64 Kb depending on platform ).
// There is no per allocation overhead, as there is no per allocation header. Warning: note there is big page sized alignment for each allocation.
// It does fragment memory when block is not reallocated/freed from the end of the committed virtual memory.
//
// You should use this allocator when:
//	- Allocation sizes are big
//  - Small numbers of allocations are needed
//  - Fragmentation is an issue.
//  - Allocations occur rarely
//
// For example:
//
void Sample_4_8_BigSizeAllocator()
{
	red::memory::BigSizeAllocatorParameter param =
	{
		&red::memory::AcquireSystemAllocator(),	// Default system allocator. It is used to reserve and commit virtual memory.
		RED_GIGA_BYTE( 1 ),						// The allocator will consume a maximum of 1 Gb of virtual memory
		red::memory::Flags_CPU_Read_Write		// this allocator need access to read and write, but you could also add GPU memory access right.
	};

	red::memory::BigSizeAllocator allocator;
	allocator.Initialize( param );

	int32_t * scalar = RED_NEW( int32_t, allocator ); // returned memory will be aligned to page size ( used by system allocator )
	RED_DELETE( scalar, allocator );

	void * buffer = RED_REALLOCATE( allocator, nullptr, RED_MEGA_BYTE( 40 ) );
	RED_FREE( allocator, buffer );
}

//////////////////////////////////////////////////////////////////////////
// 
// 4.9 How to create an Allocator compatible with redMemory
//
// Allocators do not need to inherit from a base class to be compatible with redMemory.
// However, allocators need to conform to a specific public static interface. 
// 
// First you need to use a macro with this form: RED_MEMORY_DECLARE_ALLOCATOR( allocName, allocMetricsName, defaultAlignment )
//
// Then, the following functions must be declared and defined in your allocator:
// 
// Block Allocate( u32 size );
// Block AllocateAligned( u32 size, u32 alignment );
// Block Reallocate( Block & block, u32 size );
// Block ReallocateAligned( Block & block, u32 size, u32 alignment );
// void Free( Block & block );
// u64 GetBlockSize( u64 ) const;
//
// Optional metrics serialization function might be defined in your allocator:
//
// void SerializeMetrics( Serializer & serializer );
//
// Here's an example of an Allocator that simply routes to the system malloc/free (No alignment support for the sake of simplicity)
//
void Sample_4_9_CustomAllocator()
{
	struct CustomAllocatorMetrics {};

	class CustomAllocator
	{
	public:
		RED_MEMORY_DECLARE_ALLOCATOR( CustomAllocator, CustomAllocatorMetrics, 16 );

		memory::Block Allocate( uint32_t size ) 
		{ 
			void * ptr = malloc( size );
			memory::Block block = { memory::AddressOf( ptr ), size };
			return block;
		}

		memory::Block AllocateAligned( uint32_t size, uint32_t alignment )
		{ 
			RED_MEMORY_ASSERT( alignment <= 16, "Alignment can't be bigger than 16." ); 
			return Allocate( size ); 
		}

		memory::Block Reallocate( memory::Block & block, uint32_t size )
		{
			block.size = GetBlockSize( block.address ); // IMPORTANT for performance reasons, the block size needs to be written here so that metrics can fetch it afterwards.
			void * ptr = realloc( reinterpret_cast< void* >( block.address ), size );
			memory::Block result = { memory::AddressOf( ptr ), size };
			return result;
		}

		memory::Block ReallocateAligned( memory::Block & block, uint32_t size, uint32_t alignment )
		{ 
			RED_MEMORY_ASSERT( alignment <= 16, "Alignment can't be bigger than 16." ); 
			return Reallocate( block, size );  
		}
		
		void Free( memory::Block & block )
		{
			block.size = GetBlockSize( block.address ); // IMPORTANT for performance reasons, the block size needs to be written here so that metrics can fetch it afterwards.
			void * ptr = reinterpret_cast< void* >( block.address );
			free( ptr );
		}

		uint64_t GetBlockSize( uint64_t block ) const
		{
			void * ptr = reinterpret_cast< void* >( block );
#if defined( RED_COMPILER_CLANG )
			return malloc_usable_size( ptr );
#else 
			return _msize( ptr );
#endif
		}

		void SerializeMetrics( red::memory::Serializer & serializer )
		{
			CustomAllocatorMetrics metrics;
			Memzero( &metrics, sizeof( metrics ) );

			// Build metrics here
			// BuildMetrics( metrics );

			serializer.Serialize( &metrics, sizeof( metrics ) );
		}
	};

	CustomAllocator allocator;

	int32_t * scalar = RED_NEW( int32_t, allocator ); 
	RED_DELETE( scalar, allocator );
	void * buffer = RED_REALLOCATE( allocator, nullptr, RED_KILO_BYTE( 1 ) );
	RED_FREE( allocator, buffer );
}

//////////////////////////////////////////////////////////////////////////
// 
// 4.10 Orbis Flexible Memory support
//
// All allocators can easily be made to use Flexible instead of Direct memory on Orbis.
// To do so, you only need to provide the correct System Allocator when initializing your allocator.
// Note that the Flexible System Allocator is available on all platforms, but it behaves like the regular System Allocator on all platform but Orbis
//
void Sample_4_10_FlexibleMemory()
{
	red::memory::DynamicTLSFAllocatorParameter param = 
	{
		&red::memory::AcquireFlexibleSystemAllocator(),	// Flexible System Allocator, on Orbis, virtual memory will be fetched from Flexible Memory.
		RED_MEGA_BYTE( 16 ),							// Allocator will consume a maximum of 16 Mb of virtual memory 
		RED_MEGA_BYTE( 1 ),								// It will commit 1 Mb of virtual memory when initialized
		RED_MEGA_BYTE( 2 ),								// When OOM, it will commit a 2Mb chunk of virtual memory
		red::memory::Flags_CPU_Read_Write,				// this allocator need access to read and write, but you could also add GPU memory access right.
		0												// Virtual Range Alignment
	};

	red::memory::DynamicTLSFAllocator allocator;
	allocator.Initialize( param );

	int32_t * scalar = RED_NEW( int32_t, allocator );
	RED_DELETE( scalar, allocator );

	void * buffer = RED_REALLOCATE( allocator, nullptr, RED_KILO_BYTE( 1 ) );
	RED_FREE( allocator, buffer );
}

//////////////////////////////////////////////////////////////////////////
// 
// 4.11 GPU memory support
//
// All allocators in redMemory can be initialized to map virtual memory as GPU memory. 
// However, currently all allocators need gpu AND cpu write access unfortunately (for bookkeeping). 
// More specialized allocators will be required in near future.
// NOTE the old GPU allocator from Witcher 3 is still around: red::memory::LegacyGpuAllocator. 
//
void Sample_4_11_GPUMemory()
{
	// this allocator need access to read and write, CPU and GPU!
	const uint32_t flags = red::memory::Flags_CPU_Read_Write | red::memory::Flags_GPU_Read_Write;

	red::memory::DynamicTLSFAllocatorParameter param = 
	{
		&red::memory::AcquireSystemAllocator(),	// Default system allocator. It is used to reserve and commit virtual memory.
		RED_MEGA_BYTE( 16 ),					// The allocator will consume a maximum of 16 Mb of virtual memory 
		RED_MEGA_BYTE( 1 ),						// It will commit 1 Mb of virtual memory when initialized
		RED_MEGA_BYTE( 2 ),						// When OOM, it will commit a 2Mb chunk of virtual memory
		flags,
		0										// Virtual Range Alignment
	};

	red::memory::DynamicTLSFAllocator allocator;
	allocator.Initialize( param );

	int32_t * scalar = RED_NEW( int32_t, allocator );
	RED_DELETE( scalar, allocator );

	void * buffer = RED_REALLOCATE( allocator, nullptr, RED_KILO_BYTE( 1 ) );
	RED_FREE( allocator, buffer );
}

//////////////////////////////////////////////////////////////////////////
//
// 5. Pool
// 
// Pools act as a logical view on top of a memory Allocator.
// They have a few purposes:
// - Explicit memory budgeting  
// - Define how to handle OOM situations from an allocator.
// - Allow a logical partition of memory between systems and subsystems
// - Provide access to metrics local to a Pool and it's children.
//
//
// 5.1 How to create a simple Pool
// 
// Pools are a simple stateless class. They can be declared virtually anywhere and can even be forward declared.
// There are two macro utilities used to declare them:
//
// First one is for pools that are used in multiple project/lib/dll:
// RED_MEMORY_POOL( poolName, allocatorType, dllTag )
//
// Second one is for a pool not used anywhere other than locally to a project/lib/dll:
// RED_MEMORY_POOL_STATIC( poolName, allocatorType, dllTag )
//
// If in doubt, use first macro. 
//
// If a pool is linked to the DefaultAllocator, it doesn't need to be initialized. 
// However it will not have any metrics available, and will break when OOM.
//

RED_MEMORY_POOL( PoolSample_5_1_Simple, red::memory::DefaultAllocator, RED_MEMORY_API );

void Sample_5_1_Pool()
{
	int32_t * scalar = RED_NEW( int32_t, PoolSample_5_1_Simple )( 123 );
	RED_DELETE( scalar, PoolSample_5_1_Simple );

	struct Object
	{
		virtual ~Object() {}
		virtual void Foo() {}
	};

	Object * object = RED_NEW( Object, PoolSample_5_1_Simple );
	RED_DELETE( object, PoolSample_5_1_Simple );
}

RED_MEMORY_POOL_STATIC( PoolSample_5_1_SimpleStatic, red::memory::DefaultAllocator );

void Sample_5_1_StaticPool()
{
	int32_t * scalar = RED_NEW( int32_t, PoolSample_5_1_SimpleStatic )( 123 );
	RED_DELETE( scalar, PoolSample_5_1_SimpleStatic );

	struct Object
	{
		virtual ~Object() {}
		virtual void Foo() {}
	};

	Object * object = RED_NEW( Object, PoolSample_5_1_SimpleStatic );
	RED_DELETE( object, PoolSample_5_1_SimpleStatic );
}

//////////////////////////////////////////////////////////////////////////
//
// 5.2 Initializing Pools
// 
// For a Pool to be visible in memory reports and to accumulate allocation metrics, it needs to be initialized.
// To do so, use the RED_INITIALIZE_MEMORY_POOL macro. 
// It will generate the correct initialization boilerplate code needed by redMemory.
// It will also register a proper allocator metrics serializer if serializer is defined.
// The syntax is as follows: RED_INITIALIZE_MEMORY_POOL( poolName, poolParentName, allocatorReference, budget )
// NOTE this generates a function call, not a class definition/declaration. Therefore it must be put inside a function that is called somewhere.
//
void Sample_5_2_Initialization()
{
	RED_MEMORY_POOL_STATIC( PoolSample, red::memory::DefaultAllocator );

	red::memory::DefaultAllocator & allocator = red::memory::AcquireDefaultAllocator();
	const uint32_t budget = RED_MEGA_BYTE( 16 );

	RED_INITIALIZE_MEMORY_POOL( PoolSample, red::PoolDefault, allocator, budget );
}

//////////////////////////////////////////////////////////////////////////
//
// 5.3 Binding your own Allocator
// 
// Pools can be linked to any allocator type. 
// Simply provide your allocator reference to the initialization macro utility.
//
// NOTE pools do not take ownership of the allocator. Make sure your allocator is not destroyed while the Pool is still in use.
// KNOWN ISSUE: Pools cannot change their allocators once they have been set. 
// My reasoning is that it is super dangerous. The only way it can be allowed safely is when all the allocations made with a specific allocator are freed first.  
// If anyone needs this feature, I could be convinced to allow it, but it would be at your own risk. 
//
void Sample_5_3_BindingAllocator()
{
	uint32_t allocatorBufferSize = RED_KILO_BYTE( 64 );
	void * allocatorBuffer = RED_ALLOCATE_ALIGNED( red::PoolDefault, allocatorBufferSize, 16 );
	red::memory::StaticTLSFAllocatorParameter param = { allocatorBuffer, allocatorBufferSize };
	red::memory::StaticTLSFAllocator allocator;
	allocator.Initialize( param );

	RED_MEMORY_POOL_STATIC( PoolParentSample, red::memory::StaticTLSFAllocator );
	RED_MEMORY_POOL_STATIC( PoolFirstChildSample, red::memory::StaticTLSFAllocator );
	RED_MEMORY_POOL_STATIC( PoolSecondChildSample, red::memory::StaticTLSFAllocator );
	
	RED_INITIALIZE_MEMORY_POOL( PoolParentSample, red::PoolDefault, allocator, RED_KILO_BYTE( 64 ) );
	RED_INITIALIZE_MEMORY_POOL( PoolFirstChildSample, PoolParentSample, allocator, RED_KILO_BYTE( 32 ) );
	RED_INITIALIZE_MEMORY_POOL( PoolSecondChildSample, PoolParentSample, allocator, RED_KILO_BYTE( 32 ) );
};

//////////////////////////////////////////////////////////////////////////
//
// 5.4 OOM handling
// 
// By default, when an OOM event occurs, redMemory will output a Full Memory Report and break process.
// However, it is possible to override this behavior at the pool level using the red::memory::SetPoolOOMHandler utility function
// Simply inherit and implement red::memory::PoolOOMHandler interface and pass it to red::memory::SetPoolOOMHandler function.
// 
// NOTE once again, Pools do not take ownership of the PoolOOMHandler so make sure the lifetime is handled correctly.
//
void Sample_5_4_CustomOOMHandling()
{
	RED_MEMORY_POOL_STATIC( PoolSample, red::memory::DefaultAllocator );

	class MyOOMHandler : public red::memory::PoolOOMHandler
	{
		virtual void OnHandlePoolAllocateFailure( const char * poolName, const char * allocatorName, uint32_t size, uint32_t /*alignment*/ ) override final
		{
#if defined( RED_LOGGING_ENABLED )
			RED_LOG_ERROR( "Pool %hs failed allocating %d bytes. Breaking process now.", poolName, size );
#else
			RED_UNUSED( poolName );
			RED_UNUSED( allocatorName );
			RED_UNUSED( size );
#endif
			RED_DEBUG_BREAK();
		}
	};

	MyOOMHandler oomHandler;
	red::memory::SetPoolOOMHandler< PoolSample >( &oomHandler );
	
	void * buffer = RED_ALLOCATE( PoolSample, ~0 ); // Impossible memory allocation request. Will trigger OOM handler ! 
	RED_FREE( PoolSample, buffer );
}

//////////////////////////////////////////////////////////////////////////
//
// 6. Runtime Metrics
// 
// Runtime metrics are currently limited. Hopefully more will be available as feature request comes my way.
// Still, a few basic but useful metrics functions are available.
// 
// GetTotalBytesAllocated() returns bytes allocated for all registered pools
//
void Sample_6_0_GetTotalBytesAllocated()
{
	const uint64_t original = red::memory::GetTotalBytesAllocated();
	void * buffer = RED_ALLOCATE( red::PoolDefault, RED_KILO_BYTE( 1 ) );
	const uint64_t current = red::memory::GetTotalBytesAllocated(); // current == original + 1KB
	RED_FREE( red::PoolDefault, buffer );
}

//////////////////////////////////////////////////////////////////////////
// GetTotalBytesAllocated< PoolType >() returns bytes allocated from a pool and its children.
//
void Sample_6_0_GetTotalBytesAllocated_Pool()
{
	RED_MEMORY_POOL_STATIC( PoolSample, red::memory::DefaultAllocator );
	RED_INITIALIZE_MEMORY_POOL( PoolSample, red::PoolDefault, red::memory::AcquireDefaultAllocator(), RED_MEGA_BYTE( 16 ) );

	const uint64_t original = red::memory::GetTotalBytesAllocated< red::PoolDefault >();
	void * buffer = RED_ALLOCATE( PoolSample, RED_KILO_BYTE( 64 ) );
	const uint64_t current = red::memory::GetTotalBytesAllocated< red::PoolDefault >(); // current == original + 1KB
	RED_FREE( PoolSample, buffer );
}  

//////////////////////////////////////////////////////////////////////////
// GetPoolTotalBytesAllocated< PoolType > returns bytes allocated only for the specified Pool
//
void Sample_6_0_GetPoolTotalBytesAllocated()
{
	RED_MEMORY_POOL_STATIC( PoolSample, red::memory::DefaultAllocator );
	RED_INITIALIZE_MEMORY_POOL( PoolSample, red::PoolDefault, red::memory::AcquireDefaultAllocator(), RED_MEGA_BYTE( 16 ) );

	const uint64_t original = red::memory::GetTotalBytesAllocated< red::PoolDefault >();
	void * buffer = RED_ALLOCATE( PoolSample, RED_KILO_BYTE( 64 ) );
	const uint64_t current = red::memory::GetPoolTotalBytesAllocated< red::PoolDefault >(); // current == original
	RED_FREE( PoolSample, buffer );
}

//////////////////////////////////////////////////////////////////////////
// GetRuntimeMetrics( RuntimeMetrics & metrics ) fill a RuntimeMetrics object with basic allocation info. 
//
void Sample_6_0_GetRuntimeMetrics()
{
	red::memory::RuntimeMetrics metrics;
	red::memory::GetRuntimeMetrics( metrics );

	metrics.totalPhysicalMemory;		// How much memory currently committed. Not exclusive to Pool!
	metrics.availablePhysicalMemory;	// How much memory still available to commit. On console, this should converge to 0.

	metrics.cpuBytesAllocated;	// how much memory committed on all CPU pools
	metrics.gpuBytesAllocated;	// how much memory committed on all GPU pools

	metrics.cpuAllocationCount; // How many allocation on all CPU pools
	metrics.gpuAllocationCount; // How many allocation on all GPU pools
}

//////////////////////////////////////////////////////////////////////////
//
// 6.1 Memory Capture
// 
// It is possible to capture and write to disk all memory allocations made within a given scope.
// To do so, use red::memory::StartMemoryCapture( filename ) to start your capture.
// Then to stop the capture use red::memory::StopMemoryCapture();
//
// The resulting file can then be used to track Allocations per callstack, Leaks etc..
// NOTE Currently, the MemoryDumpViewer in the internal dev tool folder can be used. 
// It will be replaced with new Profiler developed by Krakow.
//
void Sample_6_1_MemoryCapture()
{
	red::memory::StartMemoryCapture( "c:\\memory_dump.rmm" );
	void * firstAlloc = RED_ALLOCATE( red::PoolDefault, RED_KILO_BYTE( 1 ) );
	void * secondAlloc = RED_ALLOCATE( red::PoolDefault, RED_KILO_BYTE( 2 ) );
	RED_FREE( red::PoolDefault, firstAlloc );
	red::memory::StopMemoryCapture();

	// When opening file via MemoryDumpViewer, 2 allocation, 1 free, and 1 leak will be visible, with full callstack.
}

//////////////////////////////////////////////////////////////////////////
//
// 7. Debug Utility
// 
// redMemory provides a few built in debug utilities to track down classic memory issues.
// redMemory also provides a way to inject metadata into an allocation via the hook system.
//

//////////////////////////////////////////////////////////////////////////
//
// 7.1 Hook system
// 
// TODO. Hook system can't be extended for now.
//

//////////////////////////////////////////////////////////////////////////
//
// 7.2 Memory Marking
// 
// Allocate/Reallocate/Free operation can apply a pattern to a memory block. 
// Refer yourself to redMemory/include/hookSettings.h 
//

//////////////////////////////////////////////////////////////////////////
//
// 7.3 Memory Stomp Detection
// 
// Memory stomps can be easily tracked down by following these steps:
// - in redMemory/include/settings.h, define RED_MEMORY_FORCE_DEBUG_ALLOCATOR
//   or enable debug pool for the pools you are interested in (read more: 7.5)
// - rebuild your application
// - run redMemory/src/setup_debug_allocator.bat
// - run your application
//
// When a memory block read or write access is illegal, the process will break.
// 
// NOTE: PC ONLY 
//

//////////////////////////////////////////////////////////////////////////
//
// 7.4 Memory Overrun Detection
// 
// A few allocators provide an allocated block without a header or a footer. 
// When a memory overrun happens on these blocks, the conventional Memory Stomp Detection might not be able to detect this issue. 
// In such a case, the Memory overrun hook can help you. Every time a block is freed, the integrity of the block will be verified. 
// Refer yourself to redMemory/include/hookSettings.h
//


//////////////////////////////////////////////////////////////////////////
//
// 7.5 How to enable debug allocator on a specific pool without recompiling the application
//
// NOTE: PC ONLY
//
// You can enable debug allocator per specific pool to get better results when investigating
// memory stomps.
//
// To do so make sure RED_MEMORY_ALLOW_DEBUG_ALLOCATOR is defined in redMemory/include/settings.h.
// By default most of the internally used configurations have it enabled.
//
// Next in the folder where you have application executable (example: bin/x64_DLL.Release) create new file
// named debugPools.list.
//
// In the file you can list pools that should have debug allocator enabled. For example:
//
//   PoolGMPL_Quest
//   PoolGMPL_Devices
//
// Each line is new pool name. There is hard coded limit of how many pools you can have enabled for debug
// see PoolDebugAllocatorSettings::c_maxPools.
//
// There are also special keywords and operators available.
//
//   ALL - enable debug allocator for all pools
//   !PoolName - the pool will not have debug allocator enabled
//   .processName.exe - start section of pools that will be enabled only for the given process (there can be multiple in file),
//                      everything before the section will be parsed for all processes.
//
