#pragma once

namespace rep
{
	class Type;
}

namespace rtti
{
	class Function;
	class IType;

	class RED_REFLECTION_API FunctionParamBuilder
	{
		RED_USE_MEMORY_POOL( red::PoolRTTI );

	public:
		FunctionParamBuilder( const rtti::Function* parentFunction, const CName name, const rtti::IType* type );

		// sets range
		FunctionParamBuilder& range( const Float min, const Float max );

		// sets float precision; used by replication to compress value better and determine if value needs to be replicated
		FunctionParamBuilder& precision( const Float precision );

		// overrides default replication type for parameter with custom one; only to be used with replicated functions
		FunctionParamBuilder& replicated( const rep::Type* repType );

		// sets max size; only usable by dynamic size containers (e.g. dynarray or string); used by replication to optimize bandwidth
		FunctionParamBuilder& maxSize( const Uint32 maxSize );

		void AddParamToFunction();

	private:
		const rtti::Function*			m_parentFunction;

		Uint32					m_maxSize;			// max dynamic container size
		Float					m_min;				// min range value
		Float					m_max;				// max range value
		Float					m_precision;		// float precision

		CName					m_name;
		const rtti::IType*		m_type;

		const rep::Type*		m_repType;
	};
}