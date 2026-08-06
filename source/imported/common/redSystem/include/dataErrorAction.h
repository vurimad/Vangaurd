/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_SYSTEM_DATA_ERROR_ACTION_H_
#define _RED_SYSTEM_DATA_ERROR_ACTION_H_

namespace red
{
	enum DataErrorActionType : Uint16
	{
		ShowAsset
	};

	class REDSYSTEM_API DataErrorAction
	{
	public:
		static const char* s_actionTypeSeparator;
		static const char* s_actionDescriptionSeparator;
		static const char* s_actionSeparator;
		static const Uint32 s_actionSeparatorSize;

		DataErrorAction( const DataErrorActionType& type, const char* description );

		virtual ~DataErrorAction();

		virtual Bool Serialize( char* buffer, Uint32 bufferSize, Uint32& bytesStored ) const = 0;

	protected:
		virtual Uint32 GetMaximumActionSize() const = 0;

		static constexpr Uint32 s_typeMaxSize = 10;
		static constexpr Uint32 s_descriptionMaxSize = 128;
		DataErrorActionType m_type;
		char m_description[ s_descriptionMaxSize ];

	private:
		Bool HasEnoughSpaceToStoreAction( Uint32 ) const;
		Bool ValidateDescription( const char* description ) const;
	};

	class REDSYSTEM_API ShowAssetDataErrorAction final : public DataErrorAction
	{
	public:
		ShowAssetDataErrorAction( const char* description, const char* assetPath );

		virtual Bool Serialize( char* buffer, Uint32 bufferSize, Uint32& bytesStored ) const override final;

	private:
		virtual Uint32 GetMaximumActionSize() const override final;

		static constexpr Uint32 s_assetPathMaxSize = 200;
		char m_assetPath[ s_assetPathMaxSize ];
	};
}

#endif