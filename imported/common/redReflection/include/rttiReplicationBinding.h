#pragma once

#include "redReflectionApi.h"
#include "rttiPropertyBuilder.h"

namespace rtti
{
	class IType;
	class Property;
	class ClassType;
	class Function;
}

namespace res
{
	class ResourcePath;
}

// Function execution target; indicates where the function should get executed
enum EFunctionExecutionTarget
{
	// ----------------------------------------
	// Targets only available from C++
	// ----------------------------------------

	// Callable only on the host
	// Executes on specific peers (specified at call time via extra parameters)
	FET_Custom = 0,

	// ----------------------------------------
	// Targets available from scripts (and C++)
	// ----------------------------------------

	// Callable only on the host
	// Executes on all peers (host and all clients)
	FET_Multicast,

	// Callable only on the peer (host or client) that controls callee object
	// Executes only on the host
	FET_Host,

	// Callable only on the host
	// Executes only on the client peer that controls callee object
	FET_Client,

	FET_Invalid
};

namespace rep
{
	class Type;
	class Function;
	class IReplicationHandler;
	class CustomPtrTypeHandler;

	enum class EType
	{
		Fundamental,			// POD type
		Simple,					// non POD type
		Enum,					// rtti::Enum
		ResourcePath,			// res::ResourcePath type
		Class,					// rtti::Class
		DynArray,				// red::DynArray type
		StaticArray,			// red::StaticArray
		DynMapArray,			// red::DynArray used as a map; where first member of each element is considered unique key
		Pointer,				// rtti::IBasePtrType
		WeakEntityHandle,
		WeakComponentHandle,
		UniquePtr,				// red::UniquePtr<> (not a core rtti type)
	};

	// Helper interface that handles native pointer/handle type
	class RED_REFLECTION_API PtrTypeHandler
	{
	public:
		virtual ~PtrTypeHandler();
		// Gets pointed type class
		virtual const rtti::ClassType* GetPointedClass( const void* nativePtrData ) const = 0;
		// Gets native pointer type size
		virtual Uint32 GetPointerTypeSize() const = 0;
		// Constructs native pointer value
		virtual void Construct( void* ) const = 0;
		// Destructs native pointer value
		virtual void Destruct( void* ) const = 0;
		// Extracts object from pointer value
		virtual const void* GetObject( const void* nativePtrData ) const = 0;
		// Extracts object from pointer value (non-const version)
		RED_INLINE void* GetObject( void* nativePtrData ) const { return const_cast< void* >( GetObject( ( const void* ) nativePtrData ) ); }
		// Sets object to pointer
		virtual void SetObject( void* nativePtrData, void* nativeObject ) const = 0;
	};

	// Simple type description; used to retrieve replication type based on core type and some additional parameters
	struct RED_REFLECTION_API SimpleTypeDesc
	{
		static Float DefaultMin() { return -std::numeric_limits< Float >::infinity(); }
		static Float DefaultMax() { return std::numeric_limits< Float >::infinity(); }
		static Float DefaultPrecision() { return std::numeric_limits< Float >::min(); }
		static Uint32 DefaultMaxLength() { return ( 1 << 8 ) - 1; }

		// Corresponding core type; not valid for non-engine-RTTI types (e.g. unique pointer type)
		const rtti::IType* m_coreType = nullptr;

		// Type enum
		EType m_type;

		// Optional native size of the type; only used when core type isn't specified
		const Uint32 m_nativeSize = 0;

		// Optional inner type
		const Type* m_customInnerType = nullptr;

		// Optional pointer type handler; only used with pointer types
		const PtrTypeHandler* m_ptrTypeHandler = nullptr;

		// Minimal value; only used for numeric types
		Float m_min = DefaultMin();

		// Maximal value; only used for numeric types
		Float m_max = DefaultMax();

		// Expected precision; only used for floats
		Float m_precision = DefaultPrecision();

		// Max. length; only used for dynamic size vars (e.g. dynarray or string)
		Uint32 m_maxLength = DefaultMaxLength();

		// Is this property to be inlined wrt. replication? only usable with pointer/handle types; defaults to true
		Bool m_isReplicatedInline = true;

		// Is this array replicated as a map?; only used for (dynamic and static) arrays
		ArrayReplicationMode m_arrayReplicationMode = ArrayReplicationMode::Default;

		// Key member name used to uniquely identify array elements; only used with array replicated as a map
		CName m_keyMemberName;

		// Constructs simple type description for given core type and, optionally, copies parameters from other simple type description
		SimpleTypeDesc( const rtti::IType* coreType, const SimpleTypeDesc* otherDescToCopyParamsFrom = nullptr );

		// Constructs simple type description
		SimpleTypeDesc( const EType type );

		// Constructs simple type description with custom inner type and optional pointer type handler
		SimpleTypeDesc( const EType type, const Uint32 nativeSize, const Type* innerType, const PtrTypeHandler* ptrTypeHandler = nullptr );

	private:
		static EType DeduceRepTypeFromCoreType( const rtti::IType* coreType );
		static Uint32 DeduceMaxLengthFromCoreType( const rtti::IType* coreType );
	};

	// Additional property flags
	struct PropertyFlags
	{
		// when true, property is assumed to never change (thus updates won't be replicated)
		Bool m_isNeverChanges = false;

		// only replicated to peer who controls parent object
		Bool m_isReplicatedToControllingPeerOnly = false;

		// replicated to all peers that don't control parent object
		Bool m_isReplicatedToNonControllingPeers = false;

		// print out every time the property value changes
		Bool m_isChangesLogged = false;
	};

	// Helper class for replication RTTI set up; to be provided by replication library
	class RED_REFLECTION_API IRTTIService
	{
	public:
		static void Initialize( IRTTIService* impl );
		static IRTTIService& GetInstance();

		virtual ~IRTTIService();

		// Gets hash of all replication RTTI types; to be used for compatibility validation between peers
		virtual Uint32 GetRTTIHash() = 0;

		// Notifies replication type system that types are going to be rebuilt (e.g. as a result of script recompilation)
		virtual void InvalidateRuntimeData() = 0;
		// Notifies replication type system that types rebuild is finished; this is where replication types get prepared for replication
		virtual void RebuildRuntimeData() = 0;
		// Checks if fundamental type of given name supports replication
		virtual Bool ValidateSimpleType( const CName typeName ) = 0;

		// Marks given type as replicated (even if it doesn't have any replicated properties)
		virtual void MakeReplicable( const rtti::IType* coreType ) = 0;
		// Marks all subclasses (with all of their properties) as replicated
		virtual void MakeReplicableSubclassesAndProperties( const rtti::IType* coreType ) = 0;
		
		// Sets default replication type for given core type
		virtual void SetDefaultRepType( const rtti::IType* coreType, const Type* defaultRepType ) = 0;
		// Sets replication handler for given replication type
		virtual void SetReplicationHandler( const rtti::IType* coreType, IReplicationHandler* replicationHandler ) = 0;
		// Sets replicable replication ptr offset
		virtual void RegisterRepPtrOffset( rtti::IType* coreType, const Uint32 repPtrMemberOffset ) = 0;
		// Determines replication type matching given type description = 0; returns nullptr on failure
		virtual const Type* DetermineMatchingType( const SimpleTypeDesc& typeDesc ) = 0;
		// Determines corresponding map array type
		virtual const Type* GetCorrespondingMapArrayType( const rep::Type* type, const CName keyMemberName ) = 0;
		// Determines corresponding simple array type
		virtual const Type* GetCorrespondingSimpleArrayType( const rep::Type* type ) = 0;

		// Registers custom replication type
		virtual void RegisterCustomRepType( rep::Type* customType ) = 0;
		// Unregisters custom replication type
		virtual void UnregisterCustomRepType( rep::Type* customType ) = 0;

		// Notifies replication RTTI service of core RTTI type added
		virtual void OnCoreTypeRegistered( const rtti::IType* coreType ) = 0;
		// Notifies replication RTTI service of core RTTI type removed
		virtual void OnCoreTypeUnregistered( const rtti::IType* coreType ) = 0;
		// Notifies replication RTTI service of core RTTI function added
		virtual void OnCoreGlobalFunctionRegistered( const rtti::Function* coreFunction ) = 0;
		// Notifies replication RTTI service of core RTTI function removed
		virtual void OnCoreGlobalFunctionUnregistered( const rtti::Function* coreFunction ) = 0;

		// Resets class
		virtual void ResetClass( const rtti::ClassType* coreClass ) = 0;
		// Adds replicated property to class
		virtual void AddProperty( const rtti::ClassType* coreClass, const rtti::Property* property, const SimpleTypeDesc& typeDesc, const PropertyFlags& flags, const CName& tag ) = 0;
		// Adds replicated property to class
		virtual void AddProperty( const rtti::ClassType* coreClass, const rtti::Property* property, const Type* type, const PropertyFlags& flags, const CName& tag ) = 0;
		// Adds replicated property to class
		virtual void AddProperty( const rtti::ClassType* coreClass, const Uint32 nativeDataOffset, const CName name, const Type* type, const PropertyFlags& flags, const CName& tag ) = 0;

		// Creates replicable function based on given core function
		virtual void MakeFunctionReplicable( const rtti::Function* coreFunction, const Bool isStaticClassFunction ) = 0;
		// Sets replication target for network function
		virtual void SetFunctionExecutionTarget( const rtti::Function* coreFunction, const EFunctionExecutionTarget target ) = 0;
		// Makes replicated function reliable (defaults to unreliable)
		virtual void SetFunctionReliable( const rtti::Function* coreFunction ) = 0;
		// Sets replicated function for given core function
		virtual void SetRepFunction( const rtti::Function* coreFunction, const Function* repFunction ) = 0;
		// Adds function parameter
		virtual void AddFunctionParam( const rtti::Function* coreFunction, const rtti::Property* param, const Type* repType ) = 0;

		virtual void RegisterWeakEntityHandleHandler( const CustomPtrTypeHandler& handler ) = 0;
		virtual void RegisterWeakComponentHandleHandler( const CustomPtrTypeHandler& handler ) = 0;

	private:
		static IRTTIService* s_instance;
	};

	// Note: Replicated type resolver exists only for selected special cases
	// One such special case is UniquePtr<>: there is no corresponding RTTI type, so replication layer tries to work around it

	template < typename TYPE >
	class ReplicatedTypeResolver
	{
	public:
		RED_INLINE static const Type* GetType()
		{
			return nullptr;
		}
	};

	template < typename TYPE, typename POOL_TYPE >
	class ReplicatedTypeResolver< red::UniquePtr< TYPE, POOL_TYPE > >
	{
	public:
		typedef red::UniquePtr< TYPE, POOL_TYPE > PtrType;
		class UniquePtrHandler : public PtrTypeHandler
		{
		public:
			const rtti::ClassType* GetPointedClass( const void* nativeData ) const override
			{
				PtrType& ptr = *( PtrType* ) nativeData;
				return ptr ? ptr->GetClass() : nullptr;
			}
			Uint32 GetPointerTypeSize() const override
			{
				return sizeof( PtrType );
			}
			void Construct( void* memory ) const override
			{
				new ( memory ) PtrType();
			}
			void Destruct( void* memory ) const override
			{
				( ( PtrType* ) memory )->~PtrType();
			}
			const void* GetObject( const void* nativeData ) const override
			{
				PtrType& ptr = *( PtrType* ) nativeData;
				return ptr.Get();
			}
			void SetObject( void* nativeData, void* _nativeObject ) const override
			{
				PtrType& ptr = *( PtrType* ) nativeData;
				TYPE* nativeObject = ( TYPE* ) _nativeObject;
				ptr = PtrType( nativeObject );
			}
		};

		RED_INLINE static const Type* GetType()
		{
			static const Type* pointedType = IRTTIService::GetInstance().DetermineMatchingType( SimpleTypeDesc( TYPE::GetStaticClass() ) );
			if ( !pointedType )
			{
				return nullptr;
			}

			static UniquePtrHandler uniquePtrHandler;
			static const Type* type = IRTTIService::GetInstance().DetermineMatchingType( SimpleTypeDesc( EType::UniquePtr, sizeof( PtrType ), pointedType, &uniquePtrHandler ) );
			return type;
		}
	};

	template < typename INNER_TYPE >
	class ReplicatedTypeResolver< red::DynArray< INNER_TYPE > >
	{
	public:
		typedef red::DynArray< INNER_TYPE > ArrayType;
		RED_INLINE static const Type* GetType()
		{
			static const Type* innerType = ReplicatedTypeResolver< INNER_TYPE >::GetType();
			if ( !innerType )
			{
				return nullptr;
			}
			static const Type* type = IRTTIService::GetInstance().DetermineMatchingType( SimpleTypeDesc( EType::DynArray, sizeof( ArrayType ), innerType ) );
			return type;
		}
	};

	template <>
	class ReplicatedTypeResolver< res::ResourcePath >
	{
	public:
		RED_INLINE static const Type* GetType()
		{
			static const Type* type = IRTTIService::GetInstance().DetermineMatchingType( SimpleTypeDesc( EType::ResourcePath ) );
			return type;
		}
	};

	template < typename TYPE >
	RED_INLINE const Type* GetReplicatedTypeObject( const TYPE& )
	{
		static const Type* type = ReplicatedTypeResolver< TYPE >::GetType();
		return type;
	}
}