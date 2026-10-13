/**
* Copyright (c) 2017-2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "editControlCategorisedResource.h"

#include "categorisedResourceInterpreter.h"
#include "../../../common/redReflection/include/editableView.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( EditControlCategorisedResource, tools, prv );
	RTTI_PARENT_TYPE( tools::IEditableCustomProperty );
RTTI_END_TYPE();


namespace tools
{
	namespace prv
	{
		EditControlCategorisedResource::EditControlCategorisedResource()
		{
		}

		const Bool EditControlCategorisedResource::HandlesCustomType( const CName customTypeName, const EditableCollectionRootItem* /*root*/ ) const
		{
			return customTypeName == RED_NAME_CONSTEXPR_NOREG( "CategoryResource" );
		}

		Bool CheckPropertyContext( CName desiredContext, const red::SharedPtr< IEditableView >& currentView )
		{
			if ( !currentView )
				return false;

			if ( currentView->GetViewClass()->GetName() == desiredContext )
				return true;

			return CheckPropertyContext( desiredContext, currentView->GetParentView() );
		}

		CName EditControlCategorisedResource::GetControlType() const
		{
			struct Processor : public ParentObjectVisitor
			{
				Processor()
					: m_interpreterFound( nullptr )
				{
					GetRttiSystem().EnumClasses( CategorisedResourceInterpreter::GetStaticClass(), m_interpreterClasses, nullptr, false );
				}

				Bool ProcessParentObject( THandle< ISerializable >& obj ) override
				{
					const rtti::ClassType* objClass = obj->GetClass();
					for ( const rtti::ClassType* interpreterClass : m_interpreterClasses )
					{
						const CategorisedResourceInterpreter* interpreter = interpreterClass->GetDefaultObject< CategorisedResourceInterpreter >();
						if ( objClass->IsA( &interpreter->GetSupportedOwnerType() ) )
						{
							m_objFound = obj;
							m_interpreterFound = interpreter;
							return true;
						}
					}
					return false;
				}
				red::DynArray< const rtti::ClassType* > m_interpreterClasses;
				THandle< ISerializable > m_objFound;
				const CategorisedResourceInterpreter* m_interpreterFound;
			} processor;

			FindParentObject( processor );
			
			if ( processor.m_objFound && processor.m_interpreterFound->IsCategorisedViewUsed( processor.m_objFound ) )
			{
				const THandle< ISerializable >& objFound = processor.m_objFound;
				const CategorisedResourceInterpreter& interpreter = *processor.m_interpreterFound;
				String configFilePath = interpreter.GetConfigFilePath( objFound );
				String resourceExtension = interpreter.GetResourceFileExtension( objFound );
				String rootDirectory = interpreter.GetRootResourceDirectory( objFound );
				CName rootCategory = interpreter.GetRootCategory( objFound );

				String controlType( String::Printf( "CategoryRes:%hs;%hs;%hs;%hs", configFilePath.AsChar(), resourceExtension.AsChar(), rootDirectory.AsChar(), rootCategory.AsChar() ) );
				return RED_NAME( controlType.AsChar() );
			}

			// fallback to base resref editor
			return GetType()->GetName();
		}
	}
}
