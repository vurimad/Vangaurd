#include "build.h"
#include "rttiReplicationBinding.h"
#include "rttiType.h"
#include "rttiArrayTypesImpl.h"
#include "rttiPointerTypes.h"
#include "../../redReflection/include/rttiSystem.h"
#include "../../redReflection/include/rttiClass.h"

rep::PtrTypeHandler::~PtrTypeHandler()
{}

rep::SimpleTypeDesc::SimpleTypeDesc( const rtti::IType* coreType, const rep::SimpleTypeDesc* otherDescToCopyParamsFrom )
	: m_type( DeduceRepTypeFromCoreType( coreType ) )
	, m_coreType( coreType )
	, m_nativeSize( coreType->GetSize() )
	, m_min( otherDescToCopyParamsFrom ? otherDescToCopyParamsFrom->m_min : DefaultMin() )
	, m_max( otherDescToCopyParamsFrom ? otherDescToCopyParamsFrom->m_max : DefaultMax() )
	, m_precision( otherDescToCopyParamsFrom ? otherDescToCopyParamsFrom->m_precision : DefaultPrecision() )
	, m_maxLength( otherDescToCopyParamsFrom ? otherDescToCopyParamsFrom->m_maxLength : DeduceMaxLengthFromCoreType( coreType ) )
	, m_isReplicatedInline( otherDescToCopyParamsFrom ? otherDescToCopyParamsFrom->m_isReplicatedInline : true )
{
	RED_FATAL_ASSERT( coreType );
}

rep::SimpleTypeDesc::SimpleTypeDesc( const rep::EType type )
	: m_type( type )
{}

rep::SimpleTypeDesc::SimpleTypeDesc( const rep::EType type, const Uint32 nativeSize, const rep::Type* customInnerType, const rep::PtrTypeHandler* ptrTypeHandler )
	: m_type( type )
	, m_customInnerType( customInnerType )
	, m_nativeSize( nativeSize )
	, m_ptrTypeHandler( ptrTypeHandler )
{
	RED_FATAL_ASSERT( nativeSize > 0 );
}

rep::EType rep::SimpleTypeDesc::DeduceRepTypeFromCoreType( const rtti::IType* coreType )
{
	switch ( coreType->GetType() )
	{
		case RT_Fundamental: return rep::EType::Fundamental;
		case RT_Simple: return rep::EType::Simple;
		case RT_Class: return rep::EType::Class;
		case RT_Array: return rep::EType::DynArray;
		case RT_StaticArray: return rep::EType::StaticArray;
		case RT_Handle:
			return rep::EType::Pointer;
		case RT_WeakHandle:
		{
			static const rtti::ClassType* entEntityClass = rtti::ITypeSystem::GetInstance().FindClass( RED_NAME_CONSTEXPR_NOREG( "entEntity" ) );
			static const rtti::ClassType* entComponentClass = rtti::ITypeSystem::GetInstance().FindClass( RED_NAME_CONSTEXPR_NOREG( "entIComponent" ) );

			const rtti::IBasePointerType* corePtrType = static_cast< const rtti::IBasePointerType* >( coreType );
			if ( corePtrType->GetPointedType()->IsA( entEntityClass ) )
			{
				return rep::EType::WeakEntityHandle;
			}
			else if ( corePtrType->GetPointedType()->IsA( entComponentClass ) )
			{
				return rep::EType::WeakComponentHandle;
			}
			return rep::EType::Pointer;
		}
		case RT_Enum: return rep::EType::Enum;
		case RT_Name: return rep::EType::Simple; 
		default:
			RED_FATAL( "Unsupported core engine type for replication" );
			break;
	}
	return ( rep::EType ) -1;
}

Uint32 rep::SimpleTypeDesc::DeduceMaxLengthFromCoreType( const rtti::IType* coreType )
{
	switch ( coreType->GetType() )
	{
		case RT_StaticArray:
			return static_cast< const rtti::StaticArrayType* >( coreType )->GetMaxSize();
		default:
			return DefaultMaxLength();
	}

	return 0;
}

namespace rep
{
	IRTTIService::~IRTTIService()
	{}

	// Implementation used in cases where replication isn't needed at all (e.g. tools, editors, etc.)
	class RTTIServiceStub : public IRTTIService
	{
	public:
		virtual void InvalidateRuntimeData() override {}
		virtual void RebuildRuntimeData() override {}

		virtual void MakeReplicable( const rtti::IType* coreType ) override {}
		virtual void MakeReplicableSubclassesAndProperties( const rtti::IType* coreType ) override {}
		virtual void SetDefaultRepType( const rtti::IType* coreType, const Type* defaultRepType ) override {}
		virtual void SetReplicationHandler( const rtti::IType* coreType, IReplicationHandler* replicationHandler ) override {}
		virtual void RegisterRepPtrOffset( rtti::IType* coreType, const Uint32 repPtrMemberOffset ) override {}
		virtual const Type* DetermineMatchingType( const SimpleTypeDesc& typeDesc ) override { return reinterpret_cast< const Type* >( 0x1 ); }
		virtual const Type* GetCorrespondingMapArrayType( const rep::Type* type, const CName keyMemberName ) override { return reinterpret_cast< const Type* >( 0x1 ); }
		virtual const Type* GetCorrespondingSimpleArrayType( const rep::Type* type ) override { return reinterpret_cast< const Type* >( 0x1 ); }
		virtual Bool ValidateSimpleType( const CName typeName ) override { return false; }

		virtual void RegisterCustomRepType( rep::Type* customRepType ) override {}
		virtual void UnregisterCustomRepType( rep::Type* customRepType ) override {}

		virtual void OnCoreTypeRegistered( const rtti::IType* type ) override {}
		virtual void OnCoreTypeUnregistered( const rtti::IType* type ) override {}
		virtual void OnCoreGlobalFunctionRegistered( const rtti::Function* function ) override {}
		virtual void OnCoreGlobalFunctionUnregistered( const rtti::Function* function ) override {}

		virtual void ResetClass( const rtti::ClassType* coreClass ) override {}
		virtual void AddProperty( const rtti::ClassType* coreClass, const rtti::Property* property, const SimpleTypeDesc& typeDesc, const PropertyFlags& flags, const CName& tag ) override {}
		virtual void AddProperty( const rtti::ClassType* coreClass, const rtti::Property* property, const Type* type, const PropertyFlags& flags, const CName& tag ) override {}
		virtual void AddProperty( const rtti::ClassType* coreClass, const Uint32 nativeDataOffset, const CName name, const Type* type, const PropertyFlags& flags, const CName& tag ) override {}

		virtual void MakeFunctionReplicable( const rtti::Function* coreFunction, const Bool isStaticClassFunction ) override {}
		virtual void SetFunctionExecutionTarget( const rtti::Function* coreFunction, const EFunctionExecutionTarget target ) override {}
		virtual void SetFunctionReliable( const rtti::Function* coreFunction ) override {}
		virtual void SetRepFunction( const rtti::Function* coreFunction, const Function* repFunction ) override {}
		virtual void AddFunctionParam( const rtti::Function* coreFunction, const rtti::Property* param, const Type* repType ) override {}

		virtual Uint32 GetRTTIHash() override { return 0; }

		virtual void RegisterWeakEntityHandleHandler( const CustomPtrTypeHandler& handler ) override {}
		virtual void RegisterWeakComponentHandleHandler( const CustomPtrTypeHandler& handler ) override {}
	};
} // namespace rep

static rep::RTTIServiceStub RTTIServiceStubInstance;
rep::IRTTIService* rep::IRTTIService::s_instance = &RTTIServiceStubInstance;

void rep::IRTTIService::Initialize( rep::IRTTIService* impl )
{
	s_instance = impl;
}

rep::IRTTIService& rep::IRTTIService::GetInstance()
{
	RED_FATAL_ASSERT( s_instance );
	return *s_instance;
}