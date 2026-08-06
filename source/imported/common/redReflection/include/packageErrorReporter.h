/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once
#include "variant.h"
#include "handle.h"

class ISerializable;

namespace red
{
	class ClassType;
	

	class RED_REFLECTION_API PackageErrorReporter
	{
		RED_USE_MEMORY_POOL( red::PoolEngine );

	public:
		struct PropertyMissingContext
		{
			CName propertyName;
			rtti::Variant propertyData;
		};

		struct PropertyUnknownContext
		{
			const rtti::ClassType* objectType;
			CName propertyName;
			CName propertyTypeName;
		};

		struct PropertyMismatchContext
		{
			const rtti::Property* property;
			CName propertyName;
			rtti::Variant propertyData;
		};

		PackageErrorReporter();
		RED_MOCKABLE ~PackageErrorReporter();

		RED_MOCKABLE void ReportPropertyMissing( PropertyMissingContext&& context );
		void ReportPropertyMismatch( PropertyMismatchContext&& context );
		RED_MOCKABLE void ReportMissingSerializer( const rtti::IType* type );
		void ReportPropertyUnknown( const PropertyUnknownContext& context );

		void SetCurrentObject( const THandle< ISerializable > object );

		void BroadcastAllReportedErrors();

	private:
		// ctremblay: TODO this should be much more generic. I'm violating the rule of 3 here. But P0 now.
		typedef red::DynArray< std::pair< THandle< ISerializable >, PropertyMissingContext > > PropertyMissingContainer;
		typedef red::DynArray< std::pair< THandle< ISerializable >, PropertyMismatchContext > > PropertyMismatchContainer;
		typedef red::DynArray< PropertyUnknownContext > PropertyUnkownContainer;

		PropertyMissingContainer m_propertyMissingContainer;
		PropertyMismatchContainer m_propertyMismatchContainer;
		PropertyUnkownContainer m_propertyUnkownContainer;
		THandle< ISerializable > m_currentObject;
		bool m_hasErrors;
	};
}
