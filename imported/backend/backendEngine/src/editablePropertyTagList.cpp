/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "editablePropertyTagList.h"

#include "editableCollectionInterop.h"

#include "../../common/core/include/tagList.h"

#include "../../common/redReflection/include/editableView.h"
#include "resourceEditorUserContext.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( TagListEditableCustomProperty, tools );
	RTTI_PARENT_TYPE( IEditableCustomProperty );
RTTI_END_TYPE();

namespace tools
{
	const CName c_tagTypeName = RED_NAME_CONSTEXPR( "Tag" );

	TagListEditableCustomProperty::TagListEditableCustomProperty()
	{
	}

	const Bool TagListEditableCustomProperty::HandlesCustomType( const CName customTypeName, const prv::EditableCollectionRootItem* root ) const
	{
		return customTypeName == c_tagTypeName;
	}

	CName TagListEditableCustomProperty::GetControlType() const
	{
		return c_tagTypeName;
	}

	Bool TagListEditableCustomProperty::HasUnlistedChildren() const
	{
		return true;
	}

	interop::ObjectPtr TagListEditableCustomProperty::GetUnlistedChild( interop::IContext& ctx, const String& name ) const
	{
		using FunctorType = interop::utils::Functor1< void, CName >;

		if ( name == "_add" )
		{
			return interop::ObjectPtr( RED_NEW( FunctorType )(
				[this]( interop::IContext& ctx, const CName& tagName )
				{
					const_cast< TagListEditableCustomProperty* >( this )->AddTag( ctx, tagName );
				}
			) );
		}
		else if ( name == "_remove" )
		{
			return interop::ObjectPtr( RED_NEW( FunctorType )(
				[this]( interop::IContext& ctx, const CName& tagName )
				{
					const_cast<TagListEditableCustomProperty*>( this )->RemoveTag( ctx, tagName );
				}
			) );
		}
		else if ( name == "_includes" )
		{
			if ( THandle< ResourceEditorUserContext > userContext = Cast< ResourceEditorUserContext >( GetRoot()->GetNegotiatedView()->GetUserContext() ) )
			{
				if ( IIncludedTagsProvider* provider = userContext->GetIncludedTagsProvider() )
				{
					red::TagList includedTags = provider->GetIncludedTags( GetPathSoFar() );
					return interop::ObjectPtr( RED_NEW( interop::utils::ConstValueObject< red::TagList > )( includedTags ) );
				}
			}
		}

		return IEditableCustomProperty::GetUnlistedChild( ctx, name );
	}

	void TagListEditableCustomProperty::AddTag( interop::IContext& ctx, const CName tagName )
	{
		rtti::ValuePtr value;
		if ( !GetValue( IRTTIContext::GetDefault(), GetPathSoFar(), value ) )
		{
			ctx.ThrowException( "TagListEditableCustomProperty: Couldn't read value" );
			return;
		}

		red::TagList tagList;
		if ( !GetTypeObject< red::TagList >()->WriteValue( IRTTIContext::GetDefault(), &tagList, rtti::AccessPath(), *value.Get() ) )
		{
			ctx.ThrowException( "TagListEditableCustomProperty: Couldn't serialize read value to a typed object" );
			return;
		}

		tagList.AddTag( tagName );

		if ( !GetTypeObject< red::TagList >()->ReadValue( IRTTIContext::GetDefault(), &tagList, rtti::AccessPath(), value ) )
		{
			ctx.ThrowException( "TagListEditableCustomProperty: Couldn't serialize read value from a typed object" );
			return;
		}

		if ( !SetValue( IRTTIContext::GetDefault(), GetPathSoFar(), value ) )
		{
			ctx.ThrowException( "TagListEditableCustomProperty: Couldn't write value" );
		}
	}

	void TagListEditableCustomProperty::RemoveTag( interop::IContext& ctx, const CName tagName )
	{
		rtti::ValuePtr value;
		if ( !GetValue( IRTTIContext::GetDefault(), GetPathSoFar(), value ) )
		{
			ctx.ThrowException( "TagListEditableCustomProperty: Couldn't read value" );
			return;
		}

		red::TagList tagList;
		if ( !GetTypeObject< red::TagList >()->WriteValue( IRTTIContext::GetDefault(), &tagList, rtti::AccessPath(), *value.Get() ) )
		{
			ctx.ThrowException( "TagListEditableCustomProperty: Couldn't serialize read value to a typed object" );
			return;
		}

		tagList.RemoveTag( tagName );

		if ( !GetTypeObject< red::TagList >()->ReadValue( IRTTIContext::GetDefault(), &tagList, rtti::AccessPath(), value ) )
		{
			ctx.ThrowException( "TagListEditableCustomProperty: Couldn't serialize read value from a typed object" );
			return;
		}

		if ( !SetValue( IRTTIContext::GetDefault(), GetPathSoFar(), value ) )
		{
			ctx.ThrowException( "TagListEditableCustomProperty: Couldn't write value" );
		}
	}
}