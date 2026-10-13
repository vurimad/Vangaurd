// Copyright (c) 2018-2019 CD Projekt Red. All Rights Reserved.

#include "build.h"
#include "transformMatrixCustomEditor.h"

#include "../../backendEngine/include/editableCollectionInterop.h"


RTTI_BEGIN_TYPE_IN_NAMESPACE(CustomPropertyEditor_TransformMatrix, tools );
	RTTI_PARENT_TYPE( tools::IEditableCustomProperty );
RTTI_END_TYPE();

namespace tools
{
	CName CustomPropertyEditor_TransformMatrix::s_customType = RED_NAME_CONSTEXPR( "transformMatrix" );

	class VectorCompoundObject : public interop::ICompoundObject
	{
	public:
		VectorCompoundObject( prv::EditableCollectionRootItem* root, const rtti::AccessPath& path, const interop::InteropNotifier& onChangedNotifier, prv::TReset resetFunc )
			: m_root( root )
			, m_path( path )
			, m_onChangedNotifier( onChangedNotifier )
			, m_resetFunction(resetFunc)
		{
			InitializeInteropChildren();
		}

		virtual ~VectorCompoundObject() override
		{
			for ( auto it : m_children )
			{
				m_root->UnregisterItem( it.Value().second.Get() );
			}
		}

		virtual void GetChildrenInfo( interop::IContext& ctx, red::DynArray<interop::ChildWithCategory>& outInfo ) const override
		{
			outInfo.Reserve( m_children.Size() );
			for ( auto it : m_children )
			{
				outInfo.PushBack( { it.Key(), it.Value().first } );
			}
		}

		virtual interop::ObjectPtr GetChild( interop::IContext& ctx, CName name ) const override
		{
			auto it = m_children.Find( name );
			return it != m_children.End() ? it.Value().second : interop::ObjectPtr();
		}

		virtual interop::ObjectPtr GetUnlistedChild( interop::IContext& ctx, const String& name ) const override
		{
			if ( name == "_onChanged" )
			{
				return interop::ObjectPtr( RED_NEW( interop::utils::Notifier )( m_onChangedNotifier ) );
			}
			if( name == "_reset" )
			{
				m_resetFunction(ctx);
			}
			return interop::ICompoundObject::GetUnlistedChild( ctx, name );
		}

		virtual void Get( interop::IContext& ctx, interop::InteropString& outValue ) const override
		{
			rtti::ValueHolderPtr matrixHolder;
			m_root->ReadValue(IRTTIContext::GetDefault(), m_path, matrixHolder);
			if( matrixHolder )
			{
				Matrix transformMatrix;
				ClassID<Matrix>()->WriteDirectValue(IRTTIContext::GetDefault(), &transformMatrix, *matrixHolder, false);			
			
				const Vector3& translation = transformMatrix.GetTranslation();
				rtti::ValueHolderPtr translationHolder = rtti::ValueHolder::CreateStructure();
				translationHolder->SetStructElement( RED_NAME_CONSTEXPR( "X" ), rtti::ValueHolder::CreateSingle( ToStringDirect( translation.X ).AsChar() ) );
				translationHolder->SetStructElement( RED_NAME_CONSTEXPR( "Y" ), rtti::ValueHolder::CreateSingle( ToStringDirect( translation.Y ).AsChar() ) );
				translationHolder->SetStructElement( RED_NAME_CONSTEXPR( "Z" ), rtti::ValueHolder::CreateSingle( ToStringDirect( translation.Z ).AsChar() ) );
				outValue = translationHolder->ToString();
			}
			else
			{
				ctx.ThrowException( "Cannot obtain object's localToWorld matrix!" );
			}
		}

		virtual void Set( interop::IContext& ctx, const interop::InteropString& newValue ) const override
		{
			rtti::ValueHolderPtr translationHolder = rtti::ValueHolder::Parse( newValue.AsChar() );
			if ( translationHolder->IsStructure() )
			{
				Vector3 value;
				if ( value.GetClass()->WriteDirectValue( IRTTIContext::GetDefault(), &value, *translationHolder, false ) )
				{
					if ( WriteValue( value ) )
					{
						return;
					}
				}
			}
		}

		virtual const CName GetType() const override
		{
			return GetTypeObject<Vector3>()->GetName();
		}

	private:
		Bool GetValue( const CName& name, Float& outValue ) const
		{
			rtti::ValueHolderPtr matrixHolder;			
			if( m_root->ReadValue(IRTTIContext::GetDefault(), m_path, matrixHolder) )
			{
				Matrix transformMatrix;
				if( ClassID<Matrix>()->WriteDirectValue(IRTTIContext::GetDefault(), &transformMatrix, *matrixHolder, false) )
				{
					rtti::ValuePtr valueHolder;
					const Vector3& translation = transformMatrix.GetTranslation();
					if( translation.GetClass()->ReadValue(IRTTIContext::GetDefault(), &translation, rtti::AccessPath(name.AsChar()), valueHolder) && valueHolder->IsSingle() )
					{
						if( FromString(valueHolder->GetSingleValue(), outValue) )
						{
							return true;
						}
					}
				}
			}

			return false;
		}

		Bool SetValue( const CName& name, const Float& value ) const
		{
			rtti::ValueHolderPtr matrixHolder;
			if( m_root->ReadValue(IRTTIContext::GetDefault(), m_path, matrixHolder) )
			{
				Matrix transformMatrix;
				if( ClassID<Matrix>()->WriteDirectValue(IRTTIContext::GetDefault(), &transformMatrix, *matrixHolder, false) )
				{
					Vector3 translation = transformMatrix.GetTranslation();
					rtti::ValueHolderPtr translationCoordHolder = rtti::ValueHolder::Parse(ToStringDirect<Float>(value).AsChar());
					if( translation.GetClass()->WriteValue(IRTTIContext::GetDefault(), &translation, rtti::AccessPath(name.AsChar()), *translationCoordHolder, false) )
					{
						return WriteValue(translation);
					}
				}
			}
			return false;
		}

		Bool WriteValue( const Vector3& value ) const
		{
			rtti::ValueHolderPtr matrixHolder;	
			if( m_root->ReadValue(IRTTIContext::GetDefault(), m_path, matrixHolder) )
			{
				Matrix transformMatrix;
				ClassID<Matrix>()->WriteDirectValue(IRTTIContext::GetDefault(), &transformMatrix, *matrixHolder, false);
				transformMatrix.SetTranslation(value);

				rtti::ValueHolderPtr newMatrixHolder;
				if( !ClassID<Matrix>()->ReadDirectValue(IRTTIContext::GetDefault(), &transformMatrix, newMatrixHolder) )
				{
					return false;
				}

				if( m_root->WriteValue(IRTTIContext::GetDefault(), m_path, newMatrixHolder) )
				{
					return true;
				}
			}
			
			return false;
		}

		void InitializeInteropChildren()
		{
			rtti::EditableProperties properties;
			ClassID<Vector3>()->GetEditableProperties( properties );

			for ( const auto& prop : properties.Get() )
			{
				const CName name = prop.m_name;
				if( name == RED_NAME_CONSTEXPR_NOREG( "X" ) ||
					name == RED_NAME_CONSTEXPR_NOREG( "Y" ) ||
					name == RED_NAME_CONSTEXPR_NOREG( "Z" ) )
				{
					interop::ObjectPtr object = interop::ObjectPtr( RED_NEW( interop::utils::ValueObject<Float> )(
						[this, name]( interop::IContext& ctx, Float& outValue )
					{
						if ( !GetValue( name, outValue ) )
						{
							ctx.ThrowException( "Couldn't obtain value for translation property '%hs'", name.AsChar() );
						}
					},
						[this, name]( interop::IContext& ctx, const Float& value )
					{
						if ( !SetValue( name, value ) )
						{
							ctx.ThrowException( "Couldn't set value for translation property '%hs'", name.AsChar() );
						}
					} ) );

					m_children.Insert( prop.m_name, std::make_pair( prop.m_category, object ) );
					m_root->RegisterItem( object.Get() );
				}
			}
		}

		red::HashMap< CName, std::pair< CName, interop::ObjectPtr > > m_children;

		prv::EditableCollectionRootItem* m_root;
		prv::TReset m_resetFunction;

		const rtti::AccessPath m_path;
		const interop::InteropNotifier& m_onChangedNotifier;
	};

	//----------------------------------------------------------------------------------------------

	CustomPropertyEditor_TransformMatrix::CustomPropertyEditor_TransformMatrix() = default;

	CustomPropertyEditor_TransformMatrix::~CustomPropertyEditor_TransformMatrix()
	{
		for ( auto it : m_children )
		{
			GetRoot()->UnregisterItem( it.Value().second.Get() );
		}
	}

	void CustomPropertyEditor_TransformMatrix::Initialize( const CName customPropertyType, EditableCollection* collection, prv::EditableCollectionRootItem* root, const rtti::AccessPath& pathSoFar, const rtti::IType* type, const Bool isInlined, const Bool isReadOnly )
	{
		IEditableCustomProperty::Initialize( customPropertyType, collection, root, pathSoFar, type, isInlined, isReadOnly );

		m_serializableNotifier.SetCallback( [this]( const SerializableID& id, const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& targetValue )
		{
			OnNotifyPropertyChange( id, path );
		} );

		InitializeInteropChildren();
	}

	CName CustomPropertyEditor_TransformMatrix::GetControlType() const
	{
		return ClassID<Transform>()->GetName();
	}

	const Bool CustomPropertyEditor_TransformMatrix::HandlesCustomType( const CName customTypeName, const prv::EditableCollectionRootItem* /*root*/ ) const
	{
		return s_customType == customTypeName;
	}

	Bool CustomPropertyEditor_TransformMatrix::HasUnlistedChildren() const
	{
		return true;
	}

	interop::ObjectPtr CustomPropertyEditor_TransformMatrix::GetUnlistedChild( interop::IContext& ctx, const String& name ) const
	{
		return interop::ObjectPtr();
	}

	interop::ObjectPtr CustomPropertyEditor_TransformMatrix::GetChild( interop::IContext& ctx, const CName& name ) const
	{
		auto it = m_children.Find( name );
		if ( it != m_children.End() )
		{
			return it.Value().second;
		}

		return interop::ObjectPtr();
	}

	Bool CustomPropertyEditor_TransformMatrix::GetValue(IRTTIContext& ctx, const rtti::AccessPath& path, red::SharedPtr<rtti::ValueHolder>& outValue) const
	{
		rtti::ValueHolderPtr matrixHolder;
		if( GetRoot()->ReadValue(IRTTIContext::GetDefault(), GetPathSoFar(), matrixHolder) )
		{
			Matrix transformMatrix;
			if( ClassID<Matrix>()->WriteDirectValue(IRTTIContext::GetDefault(), &transformMatrix, *matrixHolder, false) )
			{
				rtti::ValuePtr valueHolder;
				const Transform& transform = transformMatrix.ToXform();
				if( ClassID< Transform>()->ReadValue(IRTTIContext::GetDefault(), &transform, rtti::AccessPath(), outValue) )
				{
					return true;
				}
			}
		}
		return false;
	}

	Bool CustomPropertyEditor_TransformMatrix::SetValue(IRTTIContext& ctx, const rtti::AccessPath& path, const red::SharedPtr<rtti::ValueHolder>& newValue) const
	{
		Transform transform = Transform::IDENTITY();
		if( ClassID<Transform>()->WriteValue(IRTTIContext::GetDefault(), &transform, path, *newValue, false) )
		{
			Matrix transformMatrix = transform.ToMatrix();
			transformMatrix.SetTranslation(transform.GetPosition());
			rtti::ValueHolderPtr matrixHolder;
			if( !ClassID<Matrix>()->ReadDirectValue(IRTTIContext::GetDefault(), &transformMatrix, matrixHolder) )
			{
				return false;
			}

			if( !GetRoot()->WriteValue(IRTTIContext::GetDefault(), GetPathSoFar(), matrixHolder) )
			{
				return false;
			}

			return true;
		}
		return false;
	}

	void CustomPropertyEditor_TransformMatrix::OnNotifyPropertyChange(const SerializableID& id, const rtti::AccessPath& path)
	{
	}

	void CustomPropertyEditor_TransformMatrix::InitializeInteropChildren()
	{
		rtti::EditableProperties properties;
		ClassID<Transform>()->GetEditableProperties( properties );
		
		const rtti::ClassEditablePropertyInfo* positionInfo = properties.Find( RED_NAME_CONSTEXPR_NOREG( "position" ) );
		if ( positionInfo )
		{
			interop::ObjectPtr object = interop::ObjectPtr( RED_NEW( VectorCompoundObject )( GetRoot(), GetPathSoFar(), m_translationChangedNotifier, [this]( interop::IContext& ctx )
			{
				rtti::ValueHolderPtr matrixHolder;
				if ( GetRoot()->ReadValue( IRTTIContext::GetDefault(), GetPathSoFar(), matrixHolder ) )
				{
					Matrix transformMatrix;
					if ( ClassID<Matrix>()->WriteDirectValue( IRTTIContext::GetDefault(), &transformMatrix, *matrixHolder, false ) )
					{
						transformMatrix.SetTranslation( Vector3::ZEROS() );
						rtti::ValueHolderPtr newMatrixHolder;
						if ( ClassID<Matrix>()->ReadDirectValue( IRTTIContext::GetDefault(), &transformMatrix, newMatrixHolder )
							&& GetRoot()->WriteValue( IRTTIContext::GetDefault(), GetPathSoFar(), newMatrixHolder ) )
						{
							return;
						}
					}
				}
			} ) );

			GetRoot()->RegisterItem( object.Get() );
			m_children.Insert( positionInfo->m_name, std::make_pair( positionInfo->m_category, object ) );
		}

		const rtti::ClassEditablePropertyInfo* orientationInfo = properties.Find( RED_NAME_CONSTEXPR_NOREG( "orientation" ) );
		if ( orientationInfo )
		{
			interop::ObjectPtr object = interop::ObjectPtr( RED_NEW( prv::ResetableValueObject< Quaternion > )(
				GetRoot(),
				[this]( interop::IContext& ctx )
				{
					rtti::ValueHolderPtr matrixHolder;
					if ( GetRoot()->ReadValue( IRTTIContext::GetDefault(), GetPathSoFar(), matrixHolder ) )
					{
						Matrix transformMatrix;
						if ( ClassID<Matrix>()->WriteDirectValue( IRTTIContext::GetDefault(), &transformMatrix, *matrixHolder, false ) )
						{
							Matrix newMatrix = Matrix::IDENTITY();
							newMatrix.SetTranslation( transformMatrix.GetTranslation() );
							newMatrix.SetScale33( transformMatrix.GetScale33() );
							rtti::ValueHolderPtr newMatrixHolder;
							if ( ClassID<Matrix>()->ReadDirectValue( IRTTIContext::GetDefault(), &newMatrix, newMatrixHolder )
								&& GetRoot()->WriteValue( IRTTIContext::GetDefault(), GetPathSoFar(), newMatrixHolder ) )
							{
								return;
							}
						}
					}
				},
					[this]( interop::IContext& ctx, Quaternion& outValue )
				{
					rtti::ValueHolderPtr matrixHolder;
					if ( GetRoot()->ReadValue( IRTTIContext::GetDefault(), GetPathSoFar(), matrixHolder ) )
					{
						Matrix transformMatrix;
						if ( ClassID<Matrix>()->WriteDirectValue( IRTTIContext::GetDefault(), &transformMatrix, *matrixHolder, false ) )
						{
							outValue = transformMatrix.ToQuat();
							return;
						}
					}
					ctx.ThrowException( "Couldn't get rotation" );
				},
					[this, name = orientationInfo->m_name]( interop::IContext& ctx, const Quaternion& value )
				{
					rtti::ValueHolderPtr matrixHolder;
					if ( GetRoot()->ReadValue( IRTTIContext::GetDefault(), GetPathSoFar(), matrixHolder ) )
					{
						Matrix transformMatrix;
						if ( ClassID<Matrix>()->WriteDirectValue( IRTTIContext::GetDefault(), &transformMatrix, *matrixHolder, false ) )
						{
							Matrix fromQuat = value.ToMatrix();
							Quaternion fromMat = fromQuat.ToQuat();
							EulerAngles angles = fromMat.ToEulerAngles();
							fromQuat.SetTranslation( transformMatrix.GetTranslation() );
							fromQuat.SetScale33( transformMatrix.GetScale33() );
							rtti::ValueHolderPtr newMatrixHolder;
							if ( ClassID<Matrix>()->ReadDirectValue( IRTTIContext::GetDefault(), &fromQuat, newMatrixHolder )
								&& GetRoot()->WriteValue( IRTTIContext::GetDefault(), GetPathSoFar(), newMatrixHolder ) )
							{
								return;
							}
						}
					}
					ctx.ThrowException( "Couldn't set rotation" );
				},
				const_cast<interop::InteropNotifier*>( &m_rotationChangedNotifier ) 
			) );

			GetRoot()->RegisterItem( object.Get() );
			m_children.Insert( orientationInfo->m_name, std::make_pair( orientationInfo->m_category, object ) );
		}
	}

	Bool CustomPropertyEditor_TransformMatrix::GetChildrenInfo( interop::IContext& ctx, red::DynArray<interop::ChildWithCategory>& outInfo ) const
	{
		outInfo.Reserve( m_children.Size() );
		for ( auto it : m_children )
		{
			outInfo.PushBack( interop::ChildWithCategory{ it.Key(), it.Value().first } );
		}

		return true;
	}

}