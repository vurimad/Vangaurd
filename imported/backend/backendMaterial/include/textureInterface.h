/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

class ITexture;

//***************************************************************************************************************************//
//***************************************************************************************************************************//

namespace tools
{
	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 

	class ChannelMask
	{
	public:
		ChannelMask() {}
		~ChannelMask() {}
		void Resize( Uint32 size );
		Uint32 GetSize() const { return m_channelMask.Size(); }
		red::DynArray<Bool>* GetMaskArray() { return &m_channelMask; }
		Uint8 GetMask() const;
	private:
		red::DynArray<Bool> m_channelMask;
	};

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 

	class GroupHelper
	{
	public:
		GroupHelper() {}
		~GroupHelper() {}

		void SetTexture( THandle< ITexture >& texture );
		void SetGroup( const String& group );
		const String& GetGroup() const { return m_currentGroup; }
		const red::DynArray< String >& GetPossibleValues() const { return m_possibleValues; }
	private:
		THandle< ITexture > m_texture;
		String m_currentGroup;
		red::DynArray< String > m_possibleValues;
		red::HashMap< String, GpuWrapApi::eTextureGroup > m_groupMap;
	};
}

//***************************************************************************************************************************//
//***************************************************************************************************************************//
