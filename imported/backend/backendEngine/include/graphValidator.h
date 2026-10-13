/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#pragma once

//#include "graphDescriptor.h"
#include "editorGraphTypes.h"

namespace tools
{
	enum class GraphValidatorOutputEntryType
	{
		Warning,
		Error,
	};
	RTTI_DECLARE_ENUM( GraphValidatorOutputEntryType );

	//////////////////////////////////////////////////////////////////////////

	typedef std::function<Bool()> ValidatorEntryAction;

	class GraphValidatorOutputEntry
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
		RTTI_DECLARE_TYPE( GraphValidatorOutputEntry )

	public:
		GraphValidatorOutputEntry();
		GraphValidatorOutputEntry( GraphValidatorOutputEntryType type, const String& message,
			const red::DynArray< GraphId >& graphIdChain, ValidatorEntryAction fixAction = ValidatorEntryAction() );

		Bool Fix() const;

		GraphValidatorOutputEntryType m_type;
		String m_message;
		red::DynArray< GraphId > m_graphIdChain;

		struct SortFunction
		{
			RED_INLINE Bool operator()( const GraphValidatorOutputEntry& entry1, const GraphValidatorOutputEntry& entry2 ) const
			{
				if ( entry1.m_type == entry2.m_type )
				{
					return entry1.m_message < entry2.m_message;
				}
				return (Uint32)entry1.m_type > (Uint32)entry2.m_type;
			}
		};

	private:
		ValidatorEntryAction m_fixAction;
		Bool m_hasFixAction;
	};

	//////////////////////////////////////////////////////////////////////////

	enum class GraphValidationState
	{
		Unknown,
		Success,
		Warnings,
		Errors,
	};

	//////////////////////////////////////////////////////////////////////////

	class IGraphValidatorOutput
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		virtual ~IGraphValidatorOutput() {}

		virtual void LogWarning( STATIC_CHECK_PRINTF_MSC const AnsiChar* message, ... ) = 0;
		virtual void LogError( STATIC_CHECK_PRINTF_MSC const AnsiChar* message, ... ) = 0;

		virtual Uint32 GetWarningsCount() const = 0;
		virtual Uint32 GetErrorsCount() const = 0;
		virtual GraphValidationState GetState() const = 0;
		virtual const red::DynArray< GraphValidatorOutputEntry >& GetEntries() const = 0;
	};

} // tools
