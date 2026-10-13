/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_SYSTEM_INTERNAL_DATA_ERROR_H_
#define _RED_SYSTEM_INTERNAL_DATA_ERROR_H_

#include "abstractDataError.h"

namespace red
{
	class DataErrorAction;

	class REDSYSTEM_API InternalDataError final : public AbstractDataError
	{
	public:
		InternalDataError( STATIC_CHECK_PRINTF_MSC const char* message );

		template< typename T, typename... Args >
		InternalDataError( STATIC_CHECK_PRINTF_MSC const char* message, T&& arg, Args&&... args );

		AbstractDataError& AddShowAssetAction( const char* resourcePath ) override;
		AbstractDataError& AddShowEntityAction( const char* resourcePath ) override;
		AbstractDataError& AddShowCustomAssetAction( const char* customDescription, const char* resourcePath ) override;

		virtual const char* GetDataError() const override { return m_serializedDataError; }
		virtual const char* GetDataErrorActions() const override { return m_serializedActions; }
		virtual Uint32 GetMessageFormatHash() const override { return m_messageFormatHash; }

	private:
		static constexpr Uint32 s_serializedDataErrorMaxSize = 1536;
		static constexpr Uint32 s_serializedActionsMaxSize = 2048;
		char m_serializedDataError[ s_serializedDataErrorMaxSize ];
		char m_serializedActions[ s_serializedActionsMaxSize ];
		char* m_currentSerializedAction;
		const Uint32 m_messageFormatHash;
	};
}

#include "internalDataError.hpp"

#endif