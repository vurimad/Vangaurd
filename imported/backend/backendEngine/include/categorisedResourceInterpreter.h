/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#pragma once

namespace tools
{
	class BACKEND_ENGINE_API CategorisedResourceInterpreter : public ISerializable
	{
		RTTI_DECLARE_TYPE( CategorisedResourceInterpreter )
	public:
		// based on that 
		virtual const rtti::ClassType& GetSupportedOwnerType() const = 0;
		virtual Bool IsCategorisedViewUsed( const THandle< ISerializable >& obj ) const = 0;

		// thats sets up entire category pipeline
		virtual String GetConfigFilePath( const THandle< ISerializable >& obj ) const = 0;
		virtual String GetResourceFileExtension( const THandle< ISerializable >& obj ) const = 0;
		virtual String GetRootResourceDirectory( const THandle< ISerializable >& obj ) const = 0;
		virtual CName GetRootCategory( const THandle< ISerializable >& obj ) const = 0;
	};
}
