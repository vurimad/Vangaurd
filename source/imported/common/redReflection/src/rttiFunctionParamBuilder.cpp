#include "build.h"
#include "rttiFunctionParamBuilder.h"
#include "rttiFunction.h"
#include "rttiReplicationBinding.h"

using namespace rep;

namespace rtti
{
	FunctionParamBuilder::FunctionParamBuilder( const rtti::Function* parentFunction, const CName name, const rtti::IType* type )
		: m_parentFunction( parentFunction )
		, m_name( name )
		, m_type( type )
		, m_repType( nullptr )
		, m_min( 0.0f )
		, m_max( 0.0f )
		, m_maxSize( 0 )
		, m_precision( std::numeric_limits< Float >::min() )

	{
		RED_FATAL_ASSERT( m_type, "Core: Trying to create parameter %hs in function %hs - invalid type!",
				m_name.AsChar(), m_parentFunction ? m_parentFunction->GetName().AsChar() : "<unknown>" );
	}

	FunctionParamBuilder& FunctionParamBuilder::replicated( const rep::Type* repType )
	{
		RED_FATAL_ASSERT( repType );
		RED_FATAL_ASSERT( m_parentFunction->IsReplicable() );
		m_repType = repType;
		return *this;
	}

	// sets max size; only usable by dynamic size containers (e.g. dynarray or string); used by replication to optimize bandwidth
	FunctionParamBuilder& FunctionParamBuilder::maxSize( const Uint32 maxSize )
	{
		m_maxSize = maxSize;
		return *this;
	}

	// sets range
	FunctionParamBuilder& FunctionParamBuilder::range( const Float min, const Float max )
	{
		m_min = min;
		m_max = max;
		return *this;
	}

	// sets float precision; used by replication to compress value better and determine if value needs to be replicated
	FunctionParamBuilder& FunctionParamBuilder::precision( const Float precision )
	{
		m_precision = precision;
		return *this;
	}

	void FunctionParamBuilder::AddParamToFunction()
	{
		// If parent function is replicated, then force parameter to be replicated too

		if ( m_parentFunction->IsReplicable() && !m_repType )
		{
			rep::SimpleTypeDesc repTypeDesc( m_type );
			if ( m_min != 0.0f || m_max != 0.0f )
			{
				repTypeDesc.m_min = m_min;
				repTypeDesc.m_max = m_max;
				repTypeDesc.m_precision = m_precision;
			}
			if ( m_maxSize > 0 )
			{
				repTypeDesc.m_maxLength = m_maxSize;
			}
			const rep::Type* defaultRepType = rep::IRTTIService::GetInstance().DetermineMatchingType( repTypeDesc );
			replicated( defaultRepType );
		}

		// Add parameter to core function

		const Property* param = nullptr;
		const_cast<rtti::Function*>(m_parentFunction)->AddParameter( m_name, m_type, false, false, false, param );

		// If replicable add it to network function too

		if ( m_parentFunction->IsReplicable() )
		{
			rep::IRTTIService::GetInstance().AddFunctionParam( m_parentFunction, param, m_repType );
		}
	}
}