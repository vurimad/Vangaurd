/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/
 
#pragma once
#include "session.h"
#include "../../../common/redConfig/include/configVarStorage.h"
#include "../../common/redFileSystem/include/fileSys.h"

namespace ses
{
	class ConfigSystemSessionDataProvider : public ISessionDataProvider
	{
	public:
		ConfigSystemSessionDataProvider( CFileManager* fileManager, const red::AbsolutePath& filePath )
			: m_fileManager( fileManager )
			, m_filePath( filePath )
		{
		}

		virtual Bool GetEntry( const red::String& group, const red::String& name, red::String& output ) const override
		{
			Bool found = m_storage.GetEntry( group.AsChar(), name.AsChar(), output );
			if( found == false )
				return false;

			return true;
		}

		virtual void SetEntry( const red::String& group, const red::String& name, const red::String& input ) override
		{
			m_storage.SetEntry( group.AsChar(), name.AsChar(), input );
			Save();
		}

		virtual void ClearGroup( const red::String& group ) override
		{
			m_storage.RemoveGroup( group.AsChar() );
			Save();
		}

		void Save()
		{
			String content{ red::PoolBackend() };
			if( m_storage.SaveToString( content ) )
			{
				red::SaveStringToFile( m_filePath, content );
			}
		}

		void Load()
		{
			red::String content { red::PoolBackend() };
			red::LoadFileToString( m_filePath, content );
			m_storage.LoadFromString( content );
		}

	private:
		Config::CConfigVarStorage m_storage;
		red::AbsolutePath m_filePath;
		CFileManager* m_fileManager;
	};
}
