/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "tagSystem.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE(TagSystem, red);
RTTI_END_TYPE();


namespace Config
{
	TConfigVar< Int32 > cvTagListInitialCount("Gameplay", "TagListInitialCount", 1000);
}

namespace red
{
	TagSystem::TagSystem()
		: m_tagToObjList( red::PoolEngine() )
		, m_tagToListenersList( red::PoolEngine() )
	{
		m_tagToObjList.Reserve(Config::cvTagListInitialCount.Get());
	}

	TagSystem::~TagSystem()
	{
		Clear();
		ClearListeners();
	}

	void TagSystem::Clear()
	{
		// @todo MS event on tags removed to all listeners
		// PMTODO if Clear gets called only in destructor, I don't think we should notify all listeners about unregistered tags
		// hopefully we're not gonna have separate worlds and will not clear tag manager when changing worlds
		
		{
			RED_SCOPE_LOCK( m_tagsLock );
			m_tagToObjList.Clear();
		}
	}

	void TagSystem::ClearListeners()
	{
		{
			RED_SCOPE_LOCK( m_listenersLock );
			m_tagToListenersList.Clear();
		}
	}

	//////////////////////////////////////////////////////////////////////////////////////////////////
	
	void TagSystem::Register( const IdHash hash, const CName& tag )
	{
		if( tag == CName::NONE() )
		{
			RED_FATAL( "Trying to register object in Tag System with NONE() tag" );
			return;
		}

		RED_SCOPE_LOCK( m_tagsLock );

		Bool keyExists = m_tagToObjList.KeyExist( tag );
		auto& bucket = m_tagToObjList[ tag ].m_hashList;
		if( !keyExists )
		{
			NotifyTagRegisterListeners( tag );
		}
		auto iter = std::find( bucket.Begin(), bucket.End(), hash );	// @todo MS maybe can be optimised, maybe no need to check, only in debug ?
		if( iter == bucket.End() )
		{
			bucket.PushBack( hash );

			NotifyTaggedObjectRegisterListeners( tag );
		}
	}

	void TagSystem::Register( const IdHash hash, const TagList& tags )
	{
		for( const CName& tag : tags.GetTagNames() )
		{
			Register( hash, tag );
		}
	}

	void TagSystem::Unregister( const IdHash hash, const CName& tag )
	{
		RED_SCOPE_LOCK( m_tagsLock );

		auto iter = m_tagToObjList.Find( tag );
		if( iter == m_tagToObjList.End() )
		{
			RED_WARNING( false, "Trying to unregister object from Tag System with a tag that was not registered, tag '%hs'", tag.AsChar() );
			return;
		}

		auto& bucket = iter.Value().m_hashList;
		auto bucketIter = std::find( bucket.Begin(), bucket.End(), hash );
		if( bucketIter != bucket.End() )
		{
			bucket.RemoveReorder( bucketIter );

			NotifyTaggedObjectUnregisterListeners( tag );

			if( bucket.Empty() )									// @todo MS is this really needed? How much memory do we loose by not doing this ?
			{
				m_tagToObjList.Remove( iter );
				NotifyTagUnregisterListeners( tag );
			}

		}
	}

	void TagSystem::Unregister( const IdHash hash, const TagList& tags )
	{
		for( const CName& tag : tags.GetTagNames() )
		{
			Unregister( hash, tag );
		}
	}

	void TagSystem::Unregister( const IdHash hash )
	{
		red::DynArray< CName > tagsToRemove { red::PoolFrame() };
		tagsToRemove.Reserve( 10 );

		for( auto iter = m_tagToObjList.Begin(); iter != m_tagToObjList.End(); ++iter )
		{
			iter.Value().m_hashList.RemoveReorder( hash );
			if( iter.Value().m_hashList.Empty() )
			{
				tagsToRemove.PushBack( iter.Key() );
			}
		}

		for( const auto& tag : tagsToRemove )
		{
			m_tagToObjList.Remove( tag );
		}
	}

	//////////////////////////////////////////////////////////////////////////////////////////////////
	void TagSystem::RegisterListener( const red::SharedPtr< red::ITagSystemListener >& listener, CName tag )
	{
		if ( !tag )
		{
			RED_FATAL("Trying to register listener with empty tag");
			return;
		}

		RED_SCOPE_LOCK( m_listenersLock );

		auto& bucket = m_tagToListenersList.GetRef( tag, red::PoolEngine() );
		RED_FATAL_ASSERT( std::find(bucket.Begin(), bucket.End(), listener ) == bucket.End(), "Listener already registered" );
		bucket.PushBack( listener );
	}

	void TagSystem::RegisterListener( const red::SharedPtr< red::ITagSystemListener >& listener,  const red::TagList& tags )
	{
		for ( const CName& tag : tags.GetTagNames() )
		{
			RegisterListener( listener, tag );
		}
	}

	void TagSystem::UnregisterListener( const red::SharedPtr< red::ITagSystemListener >& listener, CName tag )
	{
		RED_SCOPE_LOCK( m_listenersLock );

		auto iter = m_tagToListenersList.Find( tag );
		if ( iter == m_tagToListenersList.End() )
		{
			RED_FATAL_ASSERT( false, "Trying to unregister listener from Tag System with a tag that was not registered, tag '%hs'", tag.AsChar() );
			return;
		}

		auto& bucket = iter.Value();
		auto bucketIter = std::find(bucket.Begin(), bucket.End(), listener );
		RED_FATAL_ASSERT( bucketIter != bucket.End(), "Listener was not registered (or was already unregistered)" );

		bucket.RemoveReorder( bucketIter );
		if ( bucket.Empty() )
		{
			m_tagToListenersList.Remove(iter);
		}
	}

	void TagSystem::UnregisterListener( const red::SharedPtr< red::ITagSystemListener >& listener,  const red::TagList& tags )
	{
		for ( const CName& tag : tags.GetTagNames() )
		{
			UnregisterListener( listener, tag );
		}
	}

	//////////////////////////////////////////////////////////////////////////////////////////////////

	Bool TagSystem::GetAnyMatching( const CName& tag, IdHash& foundElem ) const
	{
		RED_SCOPE_SHARED_LOCK( m_tagsLock );

		auto iter = m_tagToObjList.Find( tag );
		if( iter != m_tagToObjList.End() )
		{
			auto& bucket = iter.Value().m_hashList;
			if( bucket.Empty() )
			{
				return false;            // Bucket is present, but empty. This might happen, since tags hash map is always growing.
			}

			foundElem = bucket.Front();
			return true;
		}

		return false;
	}

	Bool TagSystem::GetAnyMatching( const TagList& tags, TagMatch matchOption, IdHash& foundElem ) const
	{
		if( tags.Empty() )
		{
			return false;
		}

		RED_SCOPE_SHARED_LOCK( m_tagsLock );

		if( tags.Size() == 1 || matchOption == TagMatch::Any ) // Match single tag
		{
			for( const CName& tag : tags.GetTagNames() )
			{
				auto bucketIter = m_tagToObjList.Find( tag );
				if( bucketIter != m_tagToObjList.End() )
				{
					auto& bucket = bucketIter.Value().m_hashList;
					if( bucket.Empty() )
					{
						continue;
					}

					foundElem = bucket.Front();
					return true;
				}
			}
		}
		else if( matchOption == TagMatch::All ) // Match many tags
		{
			red::DynArray< IdHash > matchedElements { red::PoolEngine() };
			Bool firstInit = true;

			// Algorithm is - fill the matches on the first tag, then subtract those that don't match the consecutive ones

			for( const CName& tag : tags.GetTagNames() )
			{
				auto bucketIter = m_tagToObjList.Find( tag );
				if( bucketIter != m_tagToObjList.End() )
				{
					auto& bucket = bucketIter.Value().m_hashList;
					if( bucket.Empty() )
					{
						return false;           // Whole bucket for this tag is empty, we won't be able to match all.
					}

					if( firstInit == true )
					{
						matchedElements.PushBack( bucket );
						firstInit = false;
					}
					else
					{
						for( Int32 i = matchedElements.Size() - 1; i >= 0; --i )
						{
							Bool containsElem = bucket.Exist( matchedElements[ i ] );
							if( containsElem == false )
							{
								matchedElements.RemoveAtReorder( i );
							}
						}
					}

					if( matchedElements.Empty() ) // at this point nothing matched, no point in further checks
					{
						return false;
					}
				}
			}

			if( matchedElements.Empty() ) // if nothing matched
			{
				return false;
			}

			foundElem = matchedElements.Front();
			return true;
		}

		return false;
	}

	//////////////////////////////////////////////////////////////////////////////////////////////////
	
	void TagSystem::GetAllMatching( const CName& tag, red::DynArray< IdHash >& gos ) const
	{
		RED_SCOPE_SHARED_LOCK( m_tagsLock );
		
		auto iter = m_tagToObjList.Find( tag );
		if ( iter != m_tagToObjList.End() )
		{
			gos.PushBack( iter.Value().m_hashList );
		}
	}

	void TagSystem::GetAllMatching( const TagList& tags, TagMatch matchOption, red::DynArray< IdHash >& gos ) const
	{
		ForEach( tags, matchOption, [ &gos ]( IdHash elem )
		{
			red::alg::PushBackUnique< red::DynArray< IdHash >, IdHash >( gos, elem );
		});
	}

	//////////////////////////////////////////////////////////////////////////////////////////////////
	Bool TagSystem::ForEach(const CName& tag, const IteratorLambda& iteratorLambda) const
	{
		RED_SCOPE_SHARED_LOCK( m_tagsLock );

		auto bucketIter = m_tagToObjList.Find( tag );
		if( bucketIter == m_tagToObjList.End() )
		{
			return false;
		}

		auto& bucket = bucketIter.Value().m_hashList;
		if( bucket.Empty() )
		{
			return false;
		}

		for( const auto& elem : bucket )
		{
			iteratorLambda( elem );
		}

		return true;
	}

	Bool TagSystem::ForEach( const TagList& tags, TagMatch matchOption, const IteratorLambda& iteratorLambda ) const
	{
		// No tags to match
		if( tags.Empty() )
		{
			return false;
		}

		RED_SCOPE_SHARED_LOCK( m_tagsLock );

		Bool retValue = false;
		if( tags.Size() == 1 || matchOption == TagMatch::Any ) // Match single tag
		{

			for( const CName& tag : tags.GetTagNames() )
			{
				auto bucketIter = m_tagToObjList.Find( tag );
				if( bucketIter != m_tagToObjList.End() )
				{
					auto& bucket = bucketIter.Value().m_hashList;
					if( bucket.Empty() )
					{
						continue;
					}

					for( const auto& elem : bucket )
					{
						iteratorLambda( elem );
					}

					retValue = true;
				}
			}
		}
		else if( matchOption == TagMatch::All ) // Match many tags
		{
			red::DynArray< IdHash > matchedElements { red::PoolEngine() };
			Bool firstInit = true;

			for( const CName& tag : tags.GetTagNames() )
			{
				auto bucketIter = m_tagToObjList.Find( tag );
				if( bucketIter != m_tagToObjList.End() )
				{
					auto& bucket = bucketIter.Value().m_hashList;
					if( bucket.Empty() )
					{
						return false;           // Whole bucket for this tag is empty, we won't be able to match all.
					}

					if( firstInit == true )
					{
						matchedElements.PushBack( bucket );
						firstInit = false;
					}
					else
					{
						for( Int32 i = matchedElements.Size() - 1; i >= 0; --i )
						{
							Bool containsElem = bucket.Exist( matchedElements[ i ] );
							if( containsElem == false )
							{
								matchedElements.RemoveAtReorder( i );
							}
						}
					}

					if( matchedElements.Empty() ) // at this point nothing matched, no point in further checks
					{
						return false;
					}
				}
				else
				{
					// There is no such tag, nothing can match now
					return false;
				}
			}

			if( matchedElements.Empty() ) // at this point nothing matched, no point in further checks
			{
				return false;
			}

			for( const auto& elem : matchedElements )
			{
				iteratorLambda( elem );
			}
			
			retValue = true;
		}

		return retValue;
	}

	//////////////////////////////////////////////////////////////////////////////////////////////////
	void TagSystem::GetAllTags(red::DynArray< CName >& outTags) const
	{
		RED_SCOPE_SHARED_LOCK( m_tagsLock );
		m_tagToObjList.GetKeys(outTags);
	}

	//////////////////////////////////////////////////////////////////////////////////////////////////
	void TagSystem::NotifyTagRegisterListeners( CName tag ) const
	{
		RED_SCOPE_LOCK( m_listenersLock );

		auto iter = m_tagToListenersList.Find( tag );
		if ( iter != m_tagToListenersList.End() )
		{
			const auto& bucket = iter.Value();
			for ( auto& listener : bucket )
			{
				listener->OnTagRegistered();
			}
		}
	}

	void TagSystem::NotifyTagUnregisterListeners( CName tag ) const
	{
		RED_SCOPE_LOCK( m_listenersLock );

		auto iter = m_tagToListenersList.Find( tag );
		if ( iter != m_tagToListenersList.End() )
		{
			const auto& bucket = iter.Value();
			for ( auto& listener : bucket )
			{
				listener->OnTagUnregistered();
			}
		}
	}

	void TagSystem::NotifyTaggedObjectRegisterListeners( CName tag ) const
	{
		RED_SCOPE_LOCK( m_listenersLock );

		auto iter = m_tagToListenersList.Find( tag );
		if ( iter != m_tagToListenersList.End() )
		{
			const auto& bucket = iter.Value();
			for ( auto& listener : bucket )
			{
				listener->OnTaggedObjectRegistered();
			}
		}
	}

	void TagSystem::NotifyTaggedObjectUnregisterListeners( CName tag ) const
	{
		RED_SCOPE_LOCK( m_listenersLock );

		auto iter = m_tagToListenersList.Find( tag );
		if ( iter != m_tagToListenersList.End() )
		{
			const auto& bucket = iter.Value();
			for ( auto& listener : bucket )
			{
				listener->OnTaggedObjectUnregistered();
			}
		}
	}

} // red
