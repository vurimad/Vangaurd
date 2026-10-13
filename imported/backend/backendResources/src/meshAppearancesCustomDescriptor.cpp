#include "build.h"

#include "../../backendEngine/include/batchTool.h"
#include "../../backendEngine/include/editableCollectionCustomPropertyEditor.h"
#include "../../backendMesh/include/meshDataView.h"

#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/resourceMesh/include/meshAppearance.h"
#include "../../../common/resourceMaterial/include/material.h"
#include "../../../common/resourceMaterial/include/materialInstance.h"
#include "../../../common/redReflection/include/serializableValueNotifier.h"
#include "../../../common/resourceMaterial/include/materialTemplate.h"
#include "../../../common/resourceMaterial/include/materialParameter.h"
#include "../../../common/redReflection/include/rttiPathParser.h"
#include "../../../common/redReflection/include/cloningUtils.h"
#include "../../../common/redReflection/include/gatheredResource.h"

namespace tools
{
	res::TStaticResource< IMaterial > s_defaultMaterial( RED_CONST_RESOURCEPATH( "engine\\materials\\metal_base.remt" ), GRF_Backend | GRF_Startup );

	struct SortFunction
	{
		RED_INLINE Bool operator()( const CName& entry1, const CName& entry2 ) const
		{
			return entry1.AsStringView() < entry2.AsStringView();
		}
	};

	Bool ValidateName( const String& name, String& output )
	{
		for ( Uint32 i = 0; i < name.Length(); ++i )
		{
			const Char& letter = name[i];
			if ( !( letter >= 'a' && letter <= 'z' ) && !( letter >= 'A' && letter <= 'Z' )
				&& !( letter >= '0' && letter <= '9' ) && letter != '_' && letter != ':' )
			{
				output = String::Printf( "Name cannot contain \'%c\'", letter );
				return false;
			}
		}
		return true;
	}

	class ComponentAvailability : public ISerializable
	{
		RTTI_DECLARE_TYPE( ComponentAvailability );

		red::Set< CName, SortFunction > m_components;
		red::HashMap< CName, Bool > m_componentsAvailability;
		red::Set< CName > m_readOnly;
		red::Set< CName > m_initiallySet; 						// these were common for all assets on initialization
		Bool m_selectAllValue;

		std::function<Bool( const CName& )> OnNewAction;

	public:

		ComponentAvailability::ComponentAvailability()
			: m_selectAllValue{ false }
		{
			m_readOnly.Insert( RED_NAME_CONSTEXPR( "addNew" ) );
		}

		virtual void GetCustomEditableProperties( rtti::EditableProperties& outRootProperties ) const override
		{
			rtti::ClassEditablePropertyInfo propertyInfo;

			propertyInfo.m_type = GetTypeObject< Bool >();
			propertyInfo.m_name = RED_NAME_CONSTEXPR( "selectAll" );
			outRootProperties.Add( propertyInfo );

			for ( const CName& appName : m_components )
			{
				propertyInfo.m_name = appName;
				outRootProperties.Add( propertyInfo );
			}

			propertyInfo.m_name = RED_NAME_CONSTEXPR( "addNew" );
			propertyInfo.m_type = GetTypeObject< String >();
			outRootProperties.Add( propertyInfo );
		}

		virtual Bool ReadCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, red::SharedPtr<rtti::ValueHolder>& outValue ) const override
		{
			if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "addNew" ) )
			{
				outValue = rtti::ValueHolder::CreateSingle( String::EMPTY().AsChar() );
				return true;
			}
			else if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "selectAll" ) )
			{
				outValue = rtti::ValueHolder::Parse( ToStringDirect( m_selectAllValue ).AsChar() );
				return true;
			}
			else if ( m_components.Exist( propertyName ) )
			{
				Bool available = false;
				if ( m_componentsAvailability.KeyExist( propertyName ) && m_componentsAvailability[propertyName] )
				{
					available = true;
				}
				return GetTypeObject<Bool>()->ReadValue( ctx, &available, restOfThePath, outValue );
			}
			return false;
		}

		virtual Bool WriteCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, const red::SharedPtr<rtti::ValueHolder>& newValue ) override
		{
			if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "addNew" ) )
			{
				String errorString;
				const String newValueString = newValue->GetSingleValue();
				if ( !ValidateName( newValueString, errorString ) )
				{
					ctx.ReportError( nullptr, errorString.AsChar() );
					return false;
				}

				CName newComponentName = RED_NAME( newValueString );
				if ( m_components.Exist( newComponentName ) || !OnNewAction( newComponentName ) )
				{
					ctx.ReportError( nullptr, "Couldn't create new item" );
					return false;
				}

				m_components.Insert( newComponentName );
				m_componentsAvailability.Insert( newComponentName, true );
				return true;
			}
			else if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "selectAll" ) )
			{
				m_selectAllValue = FromStringDirect<Bool>( newValue->GetSingleValue() );

				if ( !m_selectAllValue )
				{
					m_componentsAvailability.Clear();
				}
				else
				{
					for ( const auto &component : m_components )
					{
						m_componentsAvailability[ component ] = m_selectAllValue;
					}
				}

				return true;
			}
			else if ( m_components.Exist( propertyName ) )
			{
				if ( !m_readOnly.Exist( propertyName ) )
				{
					return GetTypeObject<Bool>()->WriteValue( ctx, &m_componentsAvailability[propertyName], restOfThePath, *newValue.Get() );
				}
				ctx.ReportError( nullptr, "Cannot remove existing items!" );
			}
			return false;
		}
	};

	class AppearancesMaterialMapping : public ISerializable
	{
		RTTI_DECLARE_TYPE( AppearancesMaterialMapping );

	public:
		AppearancesMaterialMapping() = default;
		AppearancesMaterialMapping( const CName& name, const THandle< ComponentAvailability >& componentsAvailability )
			: m_name( name )
			, m_previousName( name )
			, m_initialName( name )
			, m_componentsAvailability( componentsAvailability )
		{
		}

		virtual void GetCustomEditableProperties( rtti::EditableProperties& outRootProperties ) const override
		{
			rtti::ClassEditablePropertyInfo propertyInfo;
			propertyInfo.m_type = GetTypeObject< CName >();
			propertyInfo.m_name = RED_NAME_CONSTEXPR( "name" );
			outRootProperties.Add( propertyInfo );

			propertyInfo.m_customTypeName = RED_NAME_CONSTEXPR_NOREG( "AppearanceMappingMaterialSelector" );

			for ( const CName& usedMat : m_usedMaterials )
			{
				propertyInfo.m_name = usedMat;
				outRootProperties.Add( propertyInfo );
			}
		}


		virtual Bool ReadCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, red::SharedPtr<rtti::ValueHolder>& outValue ) const override
		{
			if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "name" ) )
			{
				outValue = rtti::ValueHolder::CreateSingle( m_name.AsChar() );
				return true;
			}
			else if ( m_usedMaterials.Exist( propertyName ) )
			{
				if ( m_mappedMaterials.KeyExist( propertyName ) )
				{
					outValue = rtti::ValueHolder::CreateSingle( m_mappedMaterials[propertyName].AsChar() );
				}
				else
				{
					outValue = rtti::ValueHolder::CreateSingle( String::EMPTY().AsChar() );
				}
				return true;
			}
			return false;
		}


		virtual Bool WriteCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, const red::SharedPtr<rtti::ValueHolder>& newValue ) override
		{
			if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "name" ) )
			{
				const String newValueString = newValue->GetSingleValue();
				const CName newName = RED_NAME( newValueString );

				if ( m_componentsAvailability->m_components.Exist( newName ) )
				{
					ctx.ReportError( nullptr, "'%hs' already exists", newName.AsChar() );
					return false;
				}

				String errorString;
				if ( !ValidateName( newValueString, errorString ) )
				{
					ctx.ReportError( nullptr, errorString.AsChar() );
					return false;
				}

				m_previousName = m_name;
				m_name = newName;
				return true;
			}
			else if ( m_usedMaterials.Exist( propertyName ) )
			{
				CName mappedMaterial = RED_NAME( newValue->GetSingleValue() );
				m_mappedMaterials[propertyName] = mappedMaterial;
				return true;
			}
			return false;
		}

		CName m_name;
		const CName m_initialName;
		CName m_previousName;

		red::Set< CName, SortFunction > m_usedMaterials;
		red::Map< CName, CName > m_mappedMaterials;
		red::DynArray< THandle< mesh::MeshAppearance > > m_spottedAppearances;

	private:

		THandle< ComponentAvailability > m_componentsAvailability;
	};

	class CommonAppearances : public ISerializable
	{
		RTTI_DECLARE_TYPE( CommonAppearances );

	public:
		CommonAppearances() = default;
		CommonAppearances( const THandle< ComponentAvailability >& componentsAvailability )
			: m_componentsAvailability( componentsAvailability )
		{
		}

		virtual void GetCustomEditableProperties( rtti::EditableProperties& outRootProperties ) const override
		{
			rtti::ClassEditablePropertyInfo propertyInfo;
			propertyInfo.m_type = GetTypeObject< THandle< AppearancesMaterialMapping > >();
			propertyInfo.m_isInlined = true;
			propertyInfo.m_canClear = false;
			propertyInfo.m_canSelect = false;

			for ( const CName& appName : m_appearances )
			{
				if ( m_componentsAvailability->m_componentsAvailability[appName] )
				{
					propertyInfo.m_name = appName;
					outRootProperties.Add( propertyInfo );
				}
			}
		}

		virtual Bool ReadCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, red::SharedPtr<rtti::ValueHolder>& outValue ) const override
		{
			if ( m_appearances.Exist( propertyName ) )
			{
				const rtti::IType* propType = GetTypeObject< THandle< AppearancesMaterialMapping > >();
				return propType->ReadValue( ctx, &m_mappings[propertyName], restOfThePath, outValue );
			}
			return false;
		}

		virtual Bool WriteCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, const red::SharedPtr<rtti::ValueHolder>& newValue ) override
		{
			if ( m_appearances.Exist( propertyName ) )
			{
				const rtti::IType* propType = GetTypeObject< THandle< AppearancesMaterialMapping > >();
				return propType->WriteValue( ctx, &m_mappings[propertyName], restOfThePath, *newValue.Get() );
			}
			return false;
		}

		red::Set< CName, SortFunction > m_appearances;
		red::HashMap< CName, THandle< AppearancesMaterialMapping > > m_mappings;

	private:
		THandle< ComponentAvailability > m_componentsAvailability;
	};

	class MaterialDescriptor : public ISerializable
	{
		RTTI_DECLARE_TYPE( MaterialDescriptor );

	public:
		MaterialDescriptor() = default;

		MaterialDescriptor( const CName& name, const THandle< ComponentAvailability >& componentsAvailability )
			: m_currentName( name )
			, m_previousName( name )
			, m_initialName( name )
			, m_componentsAvailability( componentsAvailability )
		{}

		virtual void GetCustomEditableProperties( rtti::EditableProperties& outRootProperties ) const override
		{
			rtti::ClassEditablePropertyInfo propertyInfo;
			propertyInfo.m_type = GetTypeObject< CName >();
			propertyInfo.m_name = RED_NAME_CONSTEXPR( "name" );
			outRootProperties.Add( propertyInfo );

			if ( m_materialInstance && m_materialInstance->GetBaseMaterial() )
			{
				m_materialInstance->GetCustomEditableProperties( outRootProperties );
				m_materialInstance->GetClass()->GetEditableProperties( outRootProperties );
				return;
			}

			propertyInfo.m_type = GetTypeObject< TResRef< IMaterial > >();
			propertyInfo.m_name = RED_NAME_CONSTEXPR( "material" );
			outRootProperties.Add( propertyInfo );
		}

		virtual Bool ReadCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, red::SharedPtr<rtti::ValueHolder>& outValue ) const override
		{
			if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "name" ) )
			{
				outValue = rtti::ValueHolder::CreateSingle( m_currentName.AsChar() );
				return true;
			}

			if ( m_materialInstance && m_materialInstance->GetBaseMaterial() )
			{
				String wholePath( propertyName.AsChar() );
				if ( !restOfThePath.IsEmpty() )
				{
					wholePath = String::Printf( "%hs.%hs", wholePath.AsChar(), restOfThePath.ToString().AsChar() );
				}

				return m_materialInstance->ReadCustomEditableProperty( ctx, propertyName, restOfThePath, outValue )
					|| m_materialInstance->GetClass()->ReadValue( ctx, m_materialInstance.Get(), rtti::AccessPath( wholePath.AsChar() ), outValue );
			}

			if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "material" ) )
			{
				const rtti::IType* propertyType = GetTypeObject< TResRef< IMaterial > >();
				return propertyType->ReadValue( ctx, &m_materialReference, restOfThePath, outValue );
			}

			return false;
		}

		virtual Bool WriteCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, const red::SharedPtr<rtti::ValueHolder>& newValue ) override
		{
			if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "name" ) )
			{
				String newValueString = newValue->GetSingleValue();
				CName newName = RED_NAME( newValueString );

				if ( m_componentsAvailability->m_components.Exist( newName ) )
				{
					ctx.ReportError( nullptr, "'%hs' already exists", newName.AsChar() );
					return false;
				}

				String errorString;
				if ( !ValidateName( newValueString, errorString ) )
				{
					ctx.ReportError( nullptr, errorString.AsChar() );
					return false;
				}
				m_previousName = m_currentName;
				m_currentName = newName;
				return true;
			}

			if ( m_materialInstance && m_materialInstance->GetBaseMaterial() )
			{
				return WriteInstanceParameter( ctx, propertyName, restOfThePath, newValue );
			}

			if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "material" ) )
			{
				return SetMaterial( ctx, restOfThePath, newValue );
			}
			return false;
		}

		const CName m_initialName;
		CName m_previousName;
		CName m_currentName;

		THandle< CMaterialInstance > m_materialInstance;
		TResRef< IMaterial > m_materialReference;

	private:
		Bool WriteInstanceParameter( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, const red::SharedPtr<rtti::ValueHolder>& newValue )
		{
			String wholePath( propertyName.AsChar() );
			if ( !restOfThePath.IsEmpty() )
			{
				wholePath = String::Printf( "%hs.%hs", wholePath.AsChar(), restOfThePath.ToString().AsChar() );
			}

			Bool result = m_materialInstance->WriteCustomEditableProperty( ctx, propertyName, restOfThePath, newValue )
				|| m_materialInstance->GetClass()->WriteValue( ctx, m_materialInstance.Get(), rtti::AccessPath( wholePath.AsChar() ), *newValue.Get(), true );

			if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "baseMaterial" ) )
			{
				NotifyPropertyChanged( rtti::AccessPath( propertyName.AsChar() ) );
			}
			return result;
		}

		Bool SetMaterial( IRTTIContext& ctx, const rtti::AccessPath& restOfThePath, const red::SharedPtr<rtti::ValueHolder>& newValue )
		{
			TResRef< IMaterial > newMaterial;
			if ( GetTypeObject< TResRef< IMaterial > >()->WriteValue( ctx, &newMaterial, restOfThePath, *newValue.Get() ) )
			{
				if ( m_materialInstance )
				{
					m_materialInstance->SetBaseMaterial( newMaterial.Get() );
					NotifyPropertyChanged( rtti::AccessPath( "baseMaterial" ) );
				}
				else
				{
					m_materialReference = newMaterial;
				}
				return true;
			}

			return false;
		}

		THandle< ComponentAvailability > m_componentsAvailability;
	};

	typedef red::Map< CName, THandle< MaterialDescriptor >, SortFunction > SharedMaterialsMap;

	class CommonMaterials : public ISerializable
	{
		RTTI_DECLARE_TYPE( CommonMaterials );

		SharedMaterialsMap m_commonLocalMaterials;
		SharedMaterialsMap m_commonExternalMaterials;

	public:
		CommonMaterials() = default;
		CommonMaterials( const THandle< ComponentAvailability >& componentsAvailability )
			: m_componentsAvailability( componentsAvailability )
		{
		}

		virtual void GetCustomEditableProperties( rtti::EditableProperties& outRootProperties ) const override
		{
			rtti::ClassEditablePropertyInfo propertyInfo;
			propertyInfo.m_type = GetTypeObject< THandle< MaterialDescriptor > >();
			propertyInfo.m_isInlined = true;
			propertyInfo.m_canClear = false;
			propertyInfo.m_canSelect = false;

			for ( const auto& externalMat : m_commonExternalMaterials )
			{
				if ( m_componentsAvailability->m_componentsAvailability[externalMat.Key()] )
				{
					propertyInfo.m_name = externalMat.Key();
					outRootProperties.Add( propertyInfo );
				}
			}

			for ( const auto& internalMat : m_commonLocalMaterials )
			{
				if ( m_componentsAvailability->m_componentsAvailability[internalMat.Key()] )
				{
					propertyInfo.m_name = internalMat.Key();
					outRootProperties.Add( propertyInfo );
				}
			}
		}

		virtual Bool ReadCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, red::SharedPtr<rtti::ValueHolder>& outValue ) const override
		{
			THandle< MaterialDescriptor > material;
			if ( m_commonExternalMaterials.KeyExist( propertyName ) )
			{
				material = m_commonExternalMaterials[propertyName];
			}
			else if ( m_commonLocalMaterials.KeyExist( propertyName ) )
			{
				material = m_commonLocalMaterials[propertyName];
			}

			if ( material )
			{
				if ( restOfThePath.IsEmpty() )
				{
					outValue = rtti::ValueHolder::CreateHandle( ::ToStringDirect<Uint64>( material->GetID().Get() ).AsChar() );
					return true;
				}
				return GetTypeObject< MaterialDescriptor >()->ReadValue( ctx, material.Get(), restOfThePath, outValue );
			}
			return false;
		}

		virtual Bool WriteCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, const red::SharedPtr<rtti::ValueHolder>& newValue ) override
		{
			THandle< MaterialDescriptor > material;
			if ( m_commonExternalMaterials.KeyExist( propertyName ) )
			{
				material = m_commonExternalMaterials[propertyName];
			}
			else if ( m_commonLocalMaterials.KeyExist( propertyName ) )
			{
				material = m_commonLocalMaterials[propertyName];
			}

			if ( material )
			{
				return GetTypeObject< THandle< MaterialDescriptor > >()->WriteValue( ctx, material.Get(), restOfThePath, *newValue.Get() );
			}

			return false;
		}

		void ClearInstanceParameters( const SerializableID& id )
		{
			for ( const auto& localMat : m_commonLocalMaterials )
			{
				const THandle< MaterialDescriptor >& materialDesc = localMat.Value();
				if ( materialDesc->GetID() == id )
				{
					materialDesc->m_materialInstance->ClearInstanceParameters();
					return;
				}
			}
		}

		Bool ShouldClearParameters() const
		{
			return m_clearInstanceParameters;
		}

	private:
		THandle< ComponentAvailability > m_componentsAvailability;
		Bool m_clearInstanceParameters = false;
	};

	class MeshCustomPropertiesDescriptor_Appearances;
	struct AppearancesSetup
	{
		RTTI_DECLARE_TYPE( AppearancesSetup );

		AppearancesSetup();
		AppearancesSetup( MeshCustomPropertiesDescriptor_Appearances& descriptor );

		void InitializeAppearances( const red::HashMap< CName, Uint32 >& appearancesCount );
		void InitializeMaterials( const red::HashMap< CName, Uint32 >& materialsCount );

		const SharedMaterialsMap& GetCommonExternalMaterials() const
		{
			return m_sharedMaterials->m_commonExternalMaterials;
		}

		const SharedMaterialsMap& GetCommonLocalMaterials() const
		{
			return m_sharedMaterials->m_commonLocalMaterials;
		}

		const TResRef< IMaterial >& GetCommonExternalMaterial( const CName& matName ) const
		{
			if ( m_sharedMaterials->m_commonExternalMaterials.KeyExist( matName ) )
			{
				return m_sharedMaterials->m_commonExternalMaterials[matName]->m_materialReference;
			}
			return TResRef< IMaterial >::Null();
		}

		const THandle< CMaterialInstance >& GetCommonLocalMaterial( const CName& matName ) const
		{
			if ( m_sharedMaterials->m_commonLocalMaterials.KeyExist( matName ) )
			{
				return m_sharedMaterials->m_commonLocalMaterials[matName]->m_materialInstance;
			}

			static THandle< CMaterialInstance > nullHandle;
			return nullHandle;
		}

		Bool ShouldClearParameters() const
		{
			return m_sharedMaterials->ShouldClearParameters();
		}

		const red::HashMap< CName, THandle< AppearancesMaterialMapping > >& GetAppearancesMappings() const
		{
			return m_sharedAppearancesMaterials->m_mappings;
		}

		const THandle<AppearancesMaterialMapping>& GetAppearanceMapping( const CName& appearanceName ) const
		{
			return m_sharedAppearancesMaterials->m_mappings[appearanceName];
		}

		THandle< ComponentAvailability > m_appearancesAvailability;
		THandle< ComponentAvailability > m_materialsAvailability;

	private:
		void InitializeMapping( const CName& appName );
		void InitializeCommonMaterial( const CName& matName );
		void CollectUsedMaterials( const CName& materialName, red::Set< THandle< IMaterial > >& externalMaterials, red::Set< THandle< IMaterial > >& localMaterialsBases ) const;
		void SetupLocalMaterial( const CName& matName, const THandle< CMaterialInstance >& material );
		Bool FindCommonParamData( const CName& matName, const THandle< CMaterialParameter >& param, red::UniqueBuffer& outParamData ) const;

		void OnAppearanceAvailabilityChanged( const CName& appearanceName );
		void OnMaterialAvailabilityChanged( const CName& materialName );
		void OnBaseMaterialChanged( const SerializableID& id );
		void OnAppearanceNameChanged( const THandle< AppearancesMaterialMapping >&appMapping );
		void OnMaterialNameChanged( const THandle< MaterialDescriptor >& materialDesc );

		void OnPropertyChanged( const SerializableID& id, const rtti::AccessPath& path );

		THandle< CommonAppearances > m_sharedAppearancesMaterials;
		THandle< CommonMaterials > m_sharedMaterials;

		SerializableValueNotifier m_serializableNotifier;

		MeshCustomPropertiesDescriptor_Appearances* m_descriptorFallbackPtr;
	};

	class MeshCustomPropertiesDescriptor_Appearances : public IResourceCustomPropertiesDescriptor
	{
		RTTI_DECLARE_TYPE( MeshCustomPropertiesDescriptor_Appearances );

	public:
		MeshCustomPropertiesDescriptor_Appearances()
			: m_setup( *this )
		{
			m_switches.Insert( RED_NAME_CONSTEXPR( "setup.appearancesAvailability.addNew" ), true );
			m_switches.Insert( RED_NAME_CONSTEXPR( "setup.materialsAvailability.addNew" ), true );
			m_switches.Insert( RED_NAME_CONSTEXPR( "setup.appearancesAvailability.selectAll" ), false );
			m_switches.Insert( RED_NAME_CONSTEXPR( "setup.materialsAvailability.selectAll" ), false );
		}

		virtual void Initialize( const red::DynArray< THandle< CResource > >& resources ) override;
		virtual void ApplyChanges( const job::RunContext& runContext ) const override;
		virtual Bool RequiresMainThread() const override { return true; }

		virtual Bool SupportsResource( const rtti::ClassType* resourceClass ) const;
		virtual const String& GetTitle() const override;

		const AppearancesSetup& GetSetup() const
		{
			return m_setup;
		}

	private:
		void ApplyChangesToMesh( const THandle< CMesh >& mesh ) const;
		void UpdateMaterial( const THandle< CMesh >& mesh, const CName& material, const String& propPath ) const;
		void MapMaterials( const THandle< CMesh >& mesh, const CName& appearance, const red::Set< CName >& materials ) const;

		void ApplyMaterialsAvailability( const String& switchPath, const THandle<CMesh>& mesh, red::Set< CName >& materialsToRemove ) const;
		void ApplyAppearancesAvailability( const String& switchPath, const THandle<CMesh>& mesh, red::Set< CName >& appearancesToRemove ) const;
		void GatherSharedMaterialsProperties( const String& switchPath, const THandle<CMesh>& mesh, red::HashMap< CName, red::DynArray< String > >& commonMaterialsProps ) const;
		void GatherSharedAppearancesMappings( const String& switchPath, const THandle<CMesh>& mesh, red::HashMap< CName, red::Set< CName > >& commonAppearancesMaterials ) const;

		AppearancesSetup m_setup;
	};



	class AppearanceMaterialSelector : public tools::IEditableCustomProperty
	{
		RTTI_DECLARE_TYPE( AppearanceMaterialSelector );
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		AppearanceMaterialSelector()
			: m_materials{ red::PoolBackend() }
		{
		}

		virtual void Initialize( const CName customPropertyType, tools::EditableCollection* collection, tools::prv::EditableCollectionRootItem* root,
			const rtti::AccessPath& pathSoFar, const rtti::IType* type, const Bool isInlined, const Bool isReadOnly )
		{
			IEditableCustomProperty::Initialize( customPropertyType, collection, root, pathSoFar, type, isInlined, isReadOnly );
			RefreshOptions();
		}

		virtual const Bool HandlesCustomType( const CName customTypeName, const tools::prv::EditableCollectionRootItem* /*root*/ ) const override final
		{
			return customTypeName == RED_NAME_CONSTEXPR_NOREG( "AppearanceMappingMaterialSelector" );
		}

		virtual Bool HasOptions() const override final
		{
			return true;
		}

		virtual const red::DynArray<String>& GetOptions() const override
		{
			RefreshOptions();
			return m_materials;
		}

		virtual CName GetControlType() const override final
		{
			return RED_NAME( red::String::Printf( "%hs:Restricted", ::GetTypeName<CName>().AsChar() ) );
		}

	private:

		void RefreshOptions() const
		{
			THandle< MeshCustomPropertiesDescriptor_Appearances > desc = FindParentObject< MeshCustomPropertiesDescriptor_Appearances >();
			m_materials.Clear();

			const AppearancesSetup& setup = desc->GetSetup();

			red::DynArray< CName > externalMats;
			setup.GetCommonExternalMaterials().GetKeys( externalMats );

			for ( const CName& externalMat : externalMats )
			{
				if ( setup.m_materialsAvailability->m_componentsAvailability[externalMat] )
				{
					m_materials.PushBack( externalMat.AsChar() );
				}
			}

			red::DynArray< CName > localMats;
			setup.GetCommonLocalMaterials().GetKeys( localMats );

			for ( const CName& localMat : localMats )
			{
				if ( setup.m_materialsAvailability->m_componentsAvailability[localMat] )
				{
					m_materials.PushBack( localMat.AsChar() );
				}
			}
		}

		mutable red::DynArray< String > m_materials;
	};

	void MeshCustomPropertiesDescriptor_Appearances::Initialize( const red::DynArray<THandle<CResource>>& resources )
	{
		IResourceCustomPropertiesDescriptor::Initialize( resources );

		red::HashMap< CName, Uint32 > appearances;
		red::HashMap< CName, Uint32 > materials;

		for ( const THandle<CResource>& resource : resources )
		{
			if ( THandle< CMesh > mesh = Cast< CMesh >( resource ) )
			{
				red::ArraySpan< const THandle< mesh::MeshAppearance > >  apps = mesh->GetAllAppearances();
				for ( const THandle< mesh::MeshAppearance >& app : apps )
				{
					appearances[app->GetName()]++;
				}

				auto& lock = mesh->m_materialEntriesLock;
				RED_SCOPE_SHARED_LOCK(lock);
				for ( const CMesh::MaterialEntry& mat : mesh->m_materialEntries)
				{
					materials[mat.GetName()]++;
				}
			}
		}

		m_setup.InitializeAppearances( appearances );
		m_setup.InitializeMaterials( materials );
	}


	void MeshCustomPropertiesDescriptor_Appearances::ApplyChanges( const job::RunContext& runContext ) const
	{
		if ( m_switches.Empty() )
		{
			return;
		}

		for ( const THandle<CResource>& resource : m_resources )
		{
			if ( THandle< CMesh > mesh = Cast< CMesh >( resource ) )
			{
				ApplyChangesToMesh( mesh );
			}
		}
	}

	void MeshCustomPropertiesDescriptor_Appearances::ApplyChangesToMesh( const THandle<CMesh>& mesh ) const
	{
		red::HashMap< CName, red::DynArray< String > > commonMaterialsProps;
		red::HashMap< CName, red::Set< CName > > commonAppearancesMaterials;

		red::Set< CName > materialsToRemove;
		red::Set< CName > appearancesToRemove;

		for ( const auto &currentSwitch : m_switches )
		{
			if ( !currentSwitch.Value() )
			{
				continue;
			}

			const String propPath( currentSwitch.Key().AsChar() );

			if ( propPath.EndsWith( ".addNew" ) || propPath.EndsWith( ".selectAll" ) )
			{
				continue;
			}

			if ( propPath.BeginsWith( "setup.sharedMaterials" ) )
			{
				GatherSharedMaterialsProperties( propPath, mesh, commonMaterialsProps );
			}
			else if ( propPath.BeginsWith( "setup.sharedAppearancesMaterials." ) )
			{
				GatherSharedAppearancesMappings( propPath, mesh, commonAppearancesMaterials );
			}
		}

		for ( const auto &currentSwitch : m_switches )
		{
			if ( !currentSwitch.Value() )
			{
				continue;
			}

			const String propPath( currentSwitch.Key().AsChar() );

			if ( propPath.EndsWith( ".addNew" ) || propPath.EndsWith( ".selectAll" ) )
			{
				continue;
			}

			if ( propPath.BeginsWith( "setup.materialsAvailability." ) )
			{
				ApplyMaterialsAvailability( propPath, mesh, materialsToRemove );
			}
		}

		for ( const auto &currentSwitch : m_switches )
		{
			if ( !currentSwitch.Value() )
			{
				continue;
			}

			const String propPath( currentSwitch.Key().AsChar() );

			if ( propPath.EndsWith( ".addNew" ) || propPath.EndsWith( ".selectAll" ) )
			{
				continue;
			}

			if ( propPath.BeginsWith( "setup.appearancesAvailability." ) )
			{
				ApplyAppearancesAvailability( propPath, mesh, appearancesToRemove );
			}
		}

		if ( m_setup.ShouldClearParameters() )
		{
			for ( const auto& commonMaterial : commonMaterialsProps )
			{
				if ( const THandle< CMaterialInstance >& localMat = m_setup.GetCommonLocalMaterial( commonMaterial.Key() ) )
				{
					mesh::DataView meshView( *mesh );
					if ( THandle< CMaterialInstance > meshMaterial = Cast< CMaterialInstance >( meshView.GetMaterialDataByName( commonMaterial.Key() ) ) )
					{
						meshMaterial->ClearInstanceParameters();
					}
				}
			}
		}

		for ( const auto& commonMaterial : commonMaterialsProps )
		{
			const CName materialName = commonMaterial.Key();
			for ( const String& propPath : commonMaterial.Value() )
			{
				UpdateMaterial( mesh, materialName, propPath );
			}
		}

		for ( const auto& commonApp : commonAppearancesMaterials )
		{
			const CName appearanceName = commonApp.Key();
			MapMaterials( mesh, appearanceName, commonApp.Value() );
		}

		mesh::DataView meshView( *mesh.Get() );
		for ( const CName& appearanceName : appearancesToRemove )
		{
			meshView.RemoveAppearance( appearanceName );
		}

		for ( const CName& materialName : materialsToRemove )
		{
			Int32 materialIndex = meshView.GetMaterialIndex( materialName );
			if ( materialIndex >= 0 )
			{
				meshView.RemoveMaterial( materialIndex );
			}
		}

		meshView.ForceRecreateMaterialSetup();
	}

	void MeshCustomPropertiesDescriptor_Appearances::ApplyMaterialsAvailability( const String& switchPath, const THandle<CMesh>& mesh, red::Set< CName >& materialsToRemove ) const
	{
		const CName materialName = RED_NAME( switchPath.StringAfter( "setup.materialsAvailability." ) );
		if ( m_setup.m_materialsAvailability->m_readOnly.Exist( materialName ) )
		{
			return;
		}

		CName initialMaterialName = materialName;
		if ( m_setup.GetCommonLocalMaterials().KeyExist( materialName ) )
		{
			initialMaterialName = m_setup.GetCommonLocalMaterials()[materialName]->m_initialName;
		}
		else if ( m_setup.GetCommonExternalMaterials().KeyExist( materialName ) )
		{
			initialMaterialName = m_setup.GetCommonExternalMaterials()[materialName]->m_initialName;
		}

		mesh::DataView meshView( *mesh.Get() );
		const Int32 materialIndex = meshView.GetMaterialIndex( initialMaterialName );

		if ( m_setup.m_materialsAvailability->m_componentsAvailability[materialName] )
		{
			if ( materialIndex >= 0 )
			{
				if ( materialName != initialMaterialName )
				{
					meshView.SetMaterialName( materialIndex, materialName );
				}

				if ( m_setup.ShouldClearParameters() )
				{
					if ( THandle< CMaterialInstance > materialInstance = Cast< CMaterialInstance >( meshView.GetMaterialData( materialIndex ) ) )
					{
						materialInstance->ClearInstanceParameters();
					}
				}

				return;
			}

			if ( const THandle< CMaterialInstance >& localMat = m_setup.GetCommonLocalMaterial( materialName ) )
			{
				const String materialSwitchPath = String::Printf( "setup.sharedMaterials.%hs", materialName.AsChar() );

				for ( const auto& switchInfo : m_switches )
				{
					if ( switchInfo.Value() )
					{
						const String switchPath( switchInfo.Key().AsChar() );

						if ( switchPath.EndsWith( ".addNew" ) || switchPath.EndsWith( ".selectAll" ) )
						{
							continue;
						}

						if ( switchPath.BeginsWith( materialSwitchPath ) && !switchPath.EndsWith( "baseMaterial" ) && switchPath.Length() > materialSwitchPath.Length() )
						{
							THandle< CMaterialInstance > meshMaterial = Cast< CMaterialInstance >( CloneObject( localMat ) );
							meshMaterial->SetParent( mesh.Get() );
							meshView.AddMaterial( meshMaterial, materialName );
							break;
						}
					}
				}

				if ( meshView.GetMaterialIndex( materialName ) >= 0 )
				{
					return;
				}

				THandle< CMaterialInstance > meshMaterial = Cast< CMaterialInstance >( CloneObject( localMat ) );
				meshMaterial->SetParent( mesh.Get() );
				meshView.AddMaterial( meshMaterial, materialName );

				return;
			}

			TResRef< IMaterial > externalMat = m_setup.GetCommonExternalMaterial( materialName );
			if ( externalMat.Empty() )
			{
				externalMat = s_defaultMaterial.Get();
			}
			meshView.AddMaterial( externalMat.Get(), materialName );
		}
		else if ( materialIndex >= 0 &&	( initialMaterialName == materialName ||
			!m_setup.m_materialsAvailability->m_components.Exist( initialMaterialName ) ) )
		{
			if ( !meshView.RemoveMaterial( materialIndex ) )
			{
				materialsToRemove.Insert( materialName );
			}
		}
	}

	Bool EnsureMaterialExists( mesh::DataView& meshView, const mesh::DataView& spottedMeshView, const CName& materialName )
	{
		if ( meshView.GetMaterialIndex( materialName ) >= 0 )
		{
			return true;
		}

		THandle< IMaterial > sourceMaterial = spottedMeshView.GetMaterialData( spottedMeshView.GetMaterialIndex( materialName ) );
		if ( !sourceMaterial )
		{
			return false;
		}

		THandle< IMaterial > meshMaterial = Cast< IMaterial >( CloneObject( sourceMaterial ) );
		meshMaterial->SetParent( meshView.GetMesh() );
		return meshView.AddMaterial( meshMaterial, materialName );
	}

	void MeshCustomPropertiesDescriptor_Appearances::ApplyAppearancesAvailability( const String& switchPath, const THandle<CMesh>& mesh, red::Set< CName >& appearancesToRemove ) const
	{
		const CName appearanceName = RED_NAME( switchPath.StringAfter( "setup.appearancesAvailability." ) );
		if ( m_setup.m_appearancesAvailability->m_readOnly.Exist( appearanceName ) )
		{
			return;
		}

		CName initialAppearanceName = appearanceName;
		if ( m_setup.GetAppearancesMappings().KeyExist( appearanceName ) )
		{
			initialAppearanceName = m_setup.GetAppearanceMapping( appearanceName )->m_initialName;
		}

		mesh::DataView meshView( *mesh.Get() );
		if ( m_setup.m_appearancesAvailability->m_componentsAvailability[appearanceName] )
		{
			if ( mesh->GetAppearanceInternal( initialAppearanceName ) )
			{
				if ( initialAppearanceName != appearanceName )
				{
					meshView.RenameAppearance( meshView.GetAppearanceIndex( initialAppearanceName ), appearanceName );
				}
				return;
			}

			THandle< mesh::MeshAppearance > appearance = CreateHandle< mesh::MeshAppearance >( *mesh->GetDefaultAppearance().Get(), appearanceName );

			THandle< mesh::MeshAppearance > spottedAppearance; // select (among all spotted appearances) the appearance that uses most materials on LOD0
			if ( const THandle<AppearancesMaterialMapping>& mapping = m_setup.GetAppearanceMapping( appearanceName ) )
			{
					Uint32 maxMaterialsCount = 0;
					for ( const THandle< mesh::MeshAppearance >& appearance : mapping->m_spottedAppearances )
					{
						red::Set< CName > lod0Materials;
						mesh::DataViewRO tempView( *( appearance->GetMesh() ) );
						const red::DynArray< Uint16 > chunks = tempView.GetLODChunkIndices( 0 );

						for ( const Uint16& chunk : chunks )
						{
							lod0Materials.Insert( appearance->GetChunkMaterialName( chunk ) );
						}

						if ( lod0Materials.Size() > maxMaterialsCount )
						{
							maxMaterialsCount = lod0Materials.Size();
							spottedAppearance = appearance;
						}
					}
			}

			if ( spottedAppearance )
			{
				const Uint32 defaultNumChunks = appearance->GetNumChunks();
				const Uint32 spottedNumChunks = spottedAppearance->GetNumChunks();

				mesh::DataView spottedMeshView( *spottedAppearance->GetMesh() );
				if ( spottedAppearance != appearance && defaultNumChunks == spottedNumChunks )
				{
					for ( const auto &materialName : m_setup.GetAppearanceMapping( appearanceName )->m_usedMaterials )
					{
						EnsureMaterialExists( meshView, spottedMeshView, materialName );
					}
					appearance = CreateHandle< mesh::MeshAppearance >( *spottedAppearance.Get(), appearanceName, mesh.Get() );
				}

				// try to map materials by chunk's source material uuids in case chunks order is different
				red::HashMap< String, red::DynArray< CName > > spottedChunksIdsMaterials;
				red::HashMap< CName, red::DynArray< CName > > spottedDefaultChunksMaterials;
				const THandle< mesh::MeshAppearance >& spottedDefaultAppearance = spottedMeshView.GetDefaultAppearanceData();
				for ( Uint32 lodInd = 0; lodInd < spottedMeshView.GetNumLODs(); ++lodInd )
				{
					red::DynArray< Uint16 > chunks = spottedMeshView.GetLODChunkIndices( lodInd );
					for ( Uint16 i : chunks )
					{
						const String& uuid = spottedMeshView.GetChunkSourceMaterialUUID( i );
						if ( !uuid.Empty() )
						{
							spottedChunksIdsMaterials[uuid].Resize( spottedMeshView.GetNumLODs() );
							spottedChunksIdsMaterials[uuid][lodInd] = spottedAppearance->GetChunkMaterialName( i );
						}

						spottedDefaultChunksMaterials[spottedDefaultAppearance->GetChunkMaterialName( i )].Resize( spottedMeshView.GetNumLODs() );
						spottedDefaultChunksMaterials[spottedDefaultAppearance->GetChunkMaterialName( i )][lodInd] = spottedAppearance->GetChunkMaterialName( i );
					}
				}

				meshView.AddAppearance( appearance );
				red::Set< Uint32 > matchedIds;
				Int32 appearanceIndex = meshView.GetAppearanceIndex( appearanceName );
				THandle< mesh::MeshAppearance > defaultApp = meshView.GetDefaultAppearanceData();
				for ( Uint32 lodInd = 0; lodInd < meshView.GetNumLODs(); ++lodInd )
				{
					red::DynArray< Uint16 > chunks = meshView.GetLODChunkIndices( lodInd );
					for ( Uint16 i : chunks )
					{
						Uint32 sourceLod = Min( lodInd, (Uint32)spottedMeshView.GetNumLODs() - 1 );
						const String& uuid = meshView.GetChunkSourceMaterialUUID( i );
						if ( spottedChunksIdsMaterials.KeyExist( uuid ) )
						{
							CName materialName = spottedChunksIdsMaterials[uuid][sourceLod];
							if ( EnsureMaterialExists( meshView, spottedMeshView, materialName ) )
							{
								meshView.SetChunkMaterial( materialName, i, appearanceIndex );
								matchedIds.Insert( i );
							}
						}
						else if ( const red::DynArray< CName >* availableNames = spottedDefaultChunksMaterials.FindPtr( defaultApp->GetChunkMaterialName( i ) ) )
						{
							CName materialName = ( *availableNames )[sourceLod];
							if ( EnsureMaterialExists( meshView, spottedMeshView, materialName ) )
							{
								meshView.SetChunkMaterial( materialName, i, appearanceIndex );
								matchedIds.Insert( i );
							}
						}
						else
						{
							ReportMessage( String::Printf(
								"Couldn't match neither chunk's source material uuid, nor its default appearance material. Chunk %d, appearance %hs, mesh %hs",
								i, appearanceName.AsChar(), mesh->GetPath().ToString().AsChar() ) );
						}
					}
				}

				if ( spottedNumChunks != defaultNumChunks && matchedIds.Empty() )
				{
					// well now that's slightly creepy. Try to map materials somehow.
					// Get all materials used in both appearances
					red::DynArray< CName > materials, spottedMaterials;
					for ( Uint32 i = 0; i < defaultNumChunks; ++i )
					{
						CName materialName = appearance->GetChunkMaterialName( i );
						if ( !materials.Exist( materialName ) )
						{
							materials.PushBack( materialName );
						}
					}

					for ( Uint32 i = 0; i < spottedNumChunks; ++i )
					{
						CName materialName = spottedAppearance->GetChunkMaterialName( i );
						if ( !spottedMaterials.Exist( materialName ) )
						{
							spottedMaterials.PushBack( materialName );
						}
					}

					// and use them in the same order with respect to destination mesh default appearance layout
					meshView.AddAppearance( appearance );
					Int32 appearanceIndex = meshView.GetAppearanceIndex( appearanceName );
					for ( Uint32 i = 0; i < defaultNumChunks; ++i )
					{
						Uint32 materialIndex = materials.GetIndex( appearance->GetChunkMaterialName( i ) );
						if ( materialIndex < spottedMaterials.Size() )
						{
							if ( EnsureMaterialExists( meshView, spottedMeshView, spottedMaterials[materialIndex] ) )
							{
								meshView.SetChunkMaterial( spottedMaterials[materialIndex], i, appearanceIndex );
							}
						}
					}
				}
			}

			if ( meshView.GetAppearanceIndex( appearanceName ) < 0 )
			{
				meshView.AddAppearance( appearance );
			}
			meshView.ForceRecreateMaterialSetup();
		}
		else if ( initialAppearanceName == appearanceName ||
			!m_setup.m_appearancesAvailability->m_components.Exist( initialAppearanceName ) )
		{
			if ( meshView.RemoveAppearance( initialAppearanceName ) )
			{
				meshView.ForceRecreateMaterialSetup();
			}
			else
			{
				appearancesToRemove.Insert( appearanceName );
			}
		}
	}

	void MeshCustomPropertiesDescriptor_Appearances::GatherSharedMaterialsProperties( const String& switchPath,
		const THandle<CMesh>& mesh, red::HashMap< CName, red::DynArray< String > >& commonMaterialsProps ) const
	{
		String propPath = switchPath.StringAfter( "setup.sharedMaterials." );

		CName materialName = RED_NAME( propPath.StringBefore( "." ) );
		if ( materialName.Empty() )
		{
			materialName = RED_NAME( propPath );
		}

		propPath = propPath.StringAfter( "." );
		if ( propPath.EqualsNC( "name" ) )
		{
			CName initialMaterialName = materialName;
			if ( m_setup.GetCommonLocalMaterials().KeyExist( materialName ) )
			{
				initialMaterialName = m_setup.GetCommonLocalMaterials()[materialName]->m_initialName;
			}
			else if ( m_setup.GetCommonExternalMaterials().KeyExist( materialName ) )
			{
				initialMaterialName = m_setup.GetCommonExternalMaterials()[materialName]->m_initialName;
			}

			mesh::DataView meshView( *mesh.Get() );
			Int32 materialIndex = meshView.GetMaterialIndex( initialMaterialName );

			if ( initialMaterialName != materialName && materialIndex >= 0
				&& !m_setup.GetCommonExternalMaterials().KeyExist( initialMaterialName )
				&& !m_setup.GetCommonLocalMaterials().KeyExist( initialMaterialName ) )
			{
				meshView.SetMaterialName( materialIndex, materialName );
			}
		}
		else if ( propPath.BeginsWith( "Material." ) )
		{
			commonMaterialsProps[materialName].PushBack( propPath.StringAfter( "Material." ) );
		}
		else
		{
			commonMaterialsProps[materialName].PushBack( propPath );
		}
	}

	void MeshCustomPropertiesDescriptor_Appearances::GatherSharedAppearancesMappings( const String& switchPath,
		const THandle<CMesh>& mesh, red::HashMap<CName, red::Set<CName>>& commonAppearancesMaterials ) const
	{
		String propPath = switchPath.StringAfter( "setup.sharedAppearancesMaterials." );

		CName appearanceName = RED_NAME( propPath.StringBefore( "." ) );
		propPath = propPath.StringAfter( "." );

		if ( propPath.EqualsNC( "name" ) )
		{
			CName initialAppearanceName = appearanceName;
			if ( m_setup.GetAppearancesMappings().KeyExist( appearanceName ) )
			{
				initialAppearanceName = m_setup.GetAppearanceMapping( appearanceName )->m_initialName;
			}

			mesh::DataView meshView( *mesh.Get() );
			Int32 appearanceIndex = meshView.GetAppearanceIndex( initialAppearanceName );

			if ( initialAppearanceName != appearanceName && appearanceIndex >= 0
				&& !m_setup.GetAppearancesMappings().KeyExist( initialAppearanceName ) )
			{
				meshView.RenameAppearance( appearanceIndex, appearanceName );
			}
		}
		else
		{
			CName materialName = RED_NAME( propPath );
			commonAppearancesMaterials[appearanceName].Insert( materialName );
		}
	}

	void MeshCustomPropertiesDescriptor_Appearances::UpdateMaterial( const THandle<CMesh>& mesh, const CName& materialName, const String& propPath ) const
	{
		if ( !m_setup.m_materialsAvailability->m_componentsAvailability[materialName] )
		{
			return;
		}

		mesh::DataView meshView( *mesh );
		Int32 materialIndex = meshView.GetMaterialIndex( materialName );

		if ( propPath.Empty() )
		{
			const TResRef< IMaterial >& externalMat = m_setup.GetCommonExternalMaterial( materialName );
			if ( externalMat.Empty() )
			{
				return;
			}

			if ( materialIndex >= 0 )
			{
				meshView.SetMaterialData( materialIndex, externalMat.Get() );
			}
		}
		else if ( const THandle< CMaterialInstance >& localMat = m_setup.GetCommonLocalMaterial( materialName ) )
		{
			if ( materialIndex < 0 || !meshView.IsMaterialLocalInstance( materialIndex ) )
			{
				return;
			}

			rtti::ValuePtr propValue;
			rtti::AccessPath path( propPath.AsChar() );

			rtti::PathParser parser( path );
			CName propName;

			THandle< CMaterialInstance > meshMaterial = Cast< CMaterialInstance >( meshView.GetMaterialDataByName( materialName ) );
			if ( localMat->GetClass()->ReadValue( IRTTIContext::GetDefault(), localMat.Get(), path, propValue ) )
			{
				meshMaterial->GetClass()->WriteValue( IRTTIContext::GetDefault(), meshMaterial.Get(), path, *propValue.Get(), true );
			}
			else if ( parser.EatName( propName ) && localMat->ReadCustomEditableProperty( IRTTIContext::GetDefault(), propName, parser.GetUneatenPath(), propValue ) )
			{
				meshMaterial->WriteCustomEditableProperty( IRTTIContext::GetDefault(), propName, parser.GetUneatenPath(), propValue );
			}
		}
	}

	void MeshCustomPropertiesDescriptor_Appearances::MapMaterials( const THandle<CMesh>& mesh, const CName& appearanceName, const red::Set< CName >& materialsNames ) const
	{
		if ( !m_setup.m_appearancesAvailability->m_componentsAvailability[appearanceName] )
		{
			return;
		}

		const THandle< AppearancesMaterialMapping >& mapping = m_setup.GetAppearanceMapping( appearanceName );
		THandle< mesh::MeshAppearance > appearance = mesh->GetAppearanceInternal( appearanceName );
		if ( !appearance )
		{
			return;
		}

		mesh::DataView meshView( *mesh.Get() );
		for ( Uint32 i = 0; i < appearance->GetNumChunks(); ++i )
		{
			const CName currentMaterialName = appearance->GetChunkMaterialName( i );
			if ( !materialsNames.Exist( currentMaterialName ) )
			{
				continue;
			}

			const CName mapTo = mapping->m_mappedMaterials[currentMaterialName];
			if ( mapTo.Empty() )
			{
				continue;
			}

			meshView.SetChunkMaterial( mapTo, i, meshView.GetAppearanceIndex( appearanceName ) );
		}
		meshView.ForceRecreateMaterialSetup();
	}

	Bool MeshCustomPropertiesDescriptor_Appearances::SupportsResource( const rtti::ClassType* resourceClass ) const
	{
		return resourceClass == ClassID< CMesh >();
	}

	const String& MeshCustomPropertiesDescriptor_Appearances::GetTitle() const
	{
		static String title( "Appearances setup" );
		return title;
	}

	AppearancesSetup::AppearancesSetup()
		: m_sharedAppearancesMaterials( CreateHandle< CommonAppearances >() )
		, m_appearancesAvailability( CreateHandle< ComponentAvailability >() )
		, m_sharedMaterials( CreateHandle< CommonMaterials >() )
		, m_materialsAvailability( CreateHandle< ComponentAvailability >() )
		, m_descriptorFallbackPtr( nullptr )
	{
	}

	AppearancesSetup::AppearancesSetup( MeshCustomPropertiesDescriptor_Appearances& descriptor )
		: m_appearancesAvailability( CreateHandle< ComponentAvailability >() )
		, m_sharedAppearancesMaterials( CreateHandle< CommonAppearances >( m_appearancesAvailability ) )
		, m_materialsAvailability( CreateHandle< ComponentAvailability >() )
		, m_sharedMaterials( CreateHandle< CommonMaterials >( m_materialsAvailability ) )
		, m_descriptorFallbackPtr( &descriptor )
	{
		m_serializableNotifier.SetCallback( [this] ( const SerializableID& id, const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& targetValue )
		{
			OnPropertyChanged( id, path );
		} );

		m_appearancesAvailability->OnNewAction = [this] ( const CName& newAppearanceName )
		{
			m_sharedAppearancesMaterials->m_appearances.Insert( newAppearanceName );

			String appearanceAvailabilityPath = String::Printf( "setup.appearancesAvailability.%hs", newAppearanceName.AsChar() );
			m_descriptorFallbackPtr->GetSwitches().Insert( RED_NAME( appearanceAvailabilityPath ), true );

			InitializeMapping( newAppearanceName );

			m_descriptorFallbackPtr->NotifyPropertyChanged( rtti::AccessPath( "setup.sharedAppearancesMaterials" ) );
			m_descriptorFallbackPtr->NotifyPropertyChanged( rtti::AccessPath( "setup.appearancesAvailability" ) );
			return true;
		};

		m_materialsAvailability->OnNewAction = [this] ( const CName& newMaterialName )
		{
			THandle< MaterialDescriptor > newMaterialDesc = CreateHandle< MaterialDescriptor >( newMaterialName, m_materialsAvailability );
			newMaterialDesc->m_materialInstance = CreateHandle< CMaterialInstance >();
			m_sharedMaterials->m_commonLocalMaterials.Insert( newMaterialName, newMaterialDesc );

			String materialAvailabilityPath = String::Printf( "setup.materialsAvailability.%hs", newMaterialName.AsChar() );
			m_descriptorFallbackPtr->GetSwitches().Insert( RED_NAME( materialAvailabilityPath ), true );

			m_descriptorFallbackPtr->NotifyPropertyChanged( rtti::AccessPath( "setup.sharedMaterials" ) );
			m_descriptorFallbackPtr->NotifyPropertyChanged( rtti::AccessPath( "setup.materialsAvailability" ) );
			return true;
		};
	}

	void AppearancesSetup::InitializeAppearances( const red::HashMap<CName, Uint32>& appearancesCount )
	{
		for ( auto appCount : appearancesCount )
		{
			const CName& appName = appCount.Key();
			m_appearancesAvailability->m_components.Insert( appName );

			if ( appCount.Value() == m_descriptorFallbackPtr->GetResources().Size() )
			{
				m_appearancesAvailability->m_componentsAvailability[appName] = true;
				m_appearancesAvailability->m_initiallySet.Insert( appName );
				InitializeMapping( appName );
			}
		}
	}

	void AppearancesSetup::InitializeMapping( const CName& appName )
	{
		THandle< AppearancesMaterialMapping > mapping = CreateHandle< AppearancesMaterialMapping >( appName, m_appearancesAvailability );

		m_sharedAppearancesMaterials->m_appearances.Insert( appName );
		m_sharedAppearancesMaterials->m_mappings.Insert( appName, mapping );

		for ( const THandle<CResource>& resource : m_descriptorFallbackPtr->GetResources() )
		{
			if ( THandle< CMesh > mesh = Cast< CMesh >( resource ) )
			{
				THandle< mesh::MeshAppearance > app = mesh->GetAppearanceInternal( appName );
				if ( !app || appName != app->GetName() )
				{
					continue;
				}

				if ( !mapping->m_spottedAppearances.Exist( app ) )
				{
					mapping->m_spottedAppearances.PushBack( app );
				}

				for ( Uint32 i = 0; i < app->GetNumChunks(); ++i )
				{
					mapping->m_usedMaterials.Insert( app->GetChunkMaterialName( i ) );
				}
			}
		}
	}

	void AppearancesSetup::InitializeMaterials( const red::HashMap<CName, Uint32>& materialsCount )
	{
		for ( auto matCount : materialsCount )
		{
			const CName& matName = matCount.Key();
			m_materialsAvailability->m_components.Insert( matName );

			if ( matCount.Value() == m_descriptorFallbackPtr->GetResources().Size() )
			{
				m_materialsAvailability->m_componentsAvailability[matName] = true;
				m_materialsAvailability->m_initiallySet.Insert( matName );
				InitializeCommonMaterial( matName );
			}
		}
	}

	void AppearancesSetup::InitializeCommonMaterial( const CName& matName )
	{
		red::Set< THandle< IMaterial > > externalMaterials;
		red::Set< THandle< IMaterial > > baseMaterials;
		CollectUsedMaterials( matName, externalMaterials, baseMaterials );

		if ( !externalMaterials.Empty() )
		{
			if ( !m_sharedMaterials->m_commonExternalMaterials.KeyExist( matName ) )
			{
				m_sharedMaterials->m_commonExternalMaterials.Insert( matName, CreateHandle< MaterialDescriptor >( matName, m_materialsAvailability ) );
			}

			if ( externalMaterials.Size() > 1 )
			{
				m_sharedMaterials->m_commonExternalMaterials[matName]->m_materialReference = TResRef<IMaterial>();
			}
			else if ( baseMaterials.Empty() || ( baseMaterials.Size() == 1 && *baseMaterials.Begin() == *externalMaterials.Begin() ) )
			{
				m_sharedMaterials->m_commonExternalMaterials[matName]->m_materialReference = *externalMaterials.Begin();
			}
			else
			{
				m_sharedMaterials->m_commonExternalMaterials[matName]->m_materialReference = TResRef<IMaterial>();
			}
		}
		else
		{
			THandle< CMaterialInstance > commonMaterial = CreateHandle< CMaterialInstance >();

			if ( !m_sharedMaterials->m_commonLocalMaterials.KeyExist( matName ) )
			{
				m_sharedMaterials->m_commonLocalMaterials.Insert( matName, CreateHandle< MaterialDescriptor >( matName, m_materialsAvailability ) );
			}

			m_sharedMaterials->m_commonLocalMaterials[matName]->m_materialInstance = commonMaterial;

			if ( baseMaterials.Size() == 1 )
			{
				commonMaterial->SetBaseMaterial( *baseMaterials.Begin() );
			}

			SetupLocalMaterial( matName, commonMaterial );
		}
	}

	void AppearancesSetup::CollectUsedMaterials( const CName& materialName, red::Set<THandle<IMaterial>>& externalMaterials, red::Set<THandle<IMaterial>>& localMaterialsBases ) const
	{
		for ( const THandle<CResource>& resource : m_descriptorFallbackPtr->GetResources() )
		{
			if ( THandle< CMesh > mesh = Cast< CMesh >( resource ) )
			{
				auto& lock = mesh->m_materialEntriesLock;
				RED_SCOPE_SHARED_LOCK(lock);

				for ( const CMesh::MaterialEntry& mat : mesh->m_materialEntries )
				{
					if ( mat.GetName() == materialName )
					{
						if ( mat.IsLocalInstance() )
						{
							localMaterialsBases.Insert( HandleFromPtr( mat.GetMaterial()->GetBaseMaterial() ) );
						}
						else
						{
							externalMaterials.Insert( mat.GetMaterial() );
						}
						break;
					}
				}
			}
		}
	}

	void AppearancesSetup::SetupLocalMaterial( const CName& matName, const THandle<CMaterialInstance>& material )
	{
		const CMaterialTemplate* matTemplate = Cast< const CMaterialTemplate >( material->GetMaterialDefinition() );
		if ( matTemplate == nullptr )
		{
			return;
		}

		for ( Int32 target = MSH_First; target < MSH_MAX; ++target )
		{
			for ( const THandle< CMaterialParameter >& param : matTemplate->GetParameters( static_cast<EMaterialShaderTarget>( target ) ) )
			{
				if ( !param || !param->GetName() || !param->GetProperty() )
				{
					continue;
				}

				if ( param->IsHidden() )
				{
					continue;
				}

				red::UniqueBuffer paramData;
				if ( FindCommonParamData( matName, param, paramData ) )
				{
					material->WriteParameterRaw( param->GetName(), param->GetProperty()->GetType(), paramData.Get() );
				}

				if ( paramData )
				{
					param->GetProperty()->GetType()->Destruct( paramData.Get() );
				}
			}
		}
	}

	Bool AppearancesSetup::FindCommonParamData( const CName& matName, const THandle< CMaterialParameter >& param, red::UniqueBuffer& outParamData ) const
	{
		for ( const THandle<CResource>& resource : m_descriptorFallbackPtr->GetResources() )
		{
			THandle< CMesh > mesh = Cast< CMesh >( resource );
			if ( !mesh )
			{
				continue;
			}

			auto& lock = mesh->m_materialEntriesLock;
			RED_SCOPE_SHARED_LOCK(lock);

			for ( const CMesh::MaterialEntry& mat : mesh->m_materialEntries )
			{
				if ( mat.GetName() != matName )
				{
					continue;
				}

				const rtti::IType* paramType = param->GetProperty()->GetType();

				red::UniqueBuffer data = red::CreateUniqueBuffer< red::PoolBackend >( paramType->GetSize(), DEFAULT_MATERIAL_PARAMETER_INSTANCE_ALIGNMENT );

				red::Memset( data.Get(), 0, paramType->GetSize() );
				paramType->Construct( data.Get() );

				if ( mat.GetMaterial()->ReadParameterRaw( param->GetName(), paramType, data.Get() ) )
				{
					if ( !outParamData )
					{
						outParamData.Swap( data );
						break;
					}
					else if ( !paramType->Compare( outParamData.Get(), data.Get(), 0 ) )
					{
						paramType->Destruct( data.Get() );
						return false;
					}
				}

				paramType->Destruct( data.Get() );
				break;
			}
		}
		return true;
	}

	void AppearancesSetup::OnAppearanceAvailabilityChanged( const CName& appearanceName )
	{
		const auto InsertAppearanceName = [ this ]( const CName &name )
		{
			if ( m_appearancesAvailability->m_componentsAvailability[ name ] && !m_sharedAppearancesMaterials->m_appearances.Exist( name ) )
			{
				m_sharedAppearancesMaterials->m_appearances.Insert( name );
				InitializeMapping( name );
			}
			/*else
			{
				m_sharedAppearancesMaterials->m_appearances.Remove( name );
				m_sharedAppearancesMaterials->m_mappings.Remove( name );
			}*/
		};

		const Bool isSelectAll = appearanceName == RED_NAME_CONSTEXPR_NOREG( "selectAll" );

		if ( !m_appearancesAvailability->m_components.Exist( appearanceName ) && !isSelectAll )
		{
			return;
		}

		if ( isSelectAll )
		{
			for ( const auto &appName : m_appearancesAvailability->m_components )
			{
				InsertAppearanceName( appName );
			}

			m_descriptorFallbackPtr->NotifyPropertyChanged( rtti::AccessPath( "setup.appearancesAvailability" ) );
		}
		else
		{
			InsertAppearanceName( appearanceName );
		}

		m_descriptorFallbackPtr->NotifyPropertyChanged( rtti::AccessPath( "setup.sharedAppearancesMaterials" ) );
	}

	void AppearancesSetup::OnMaterialAvailabilityChanged( const CName& materialName )
	{
		const Bool isSelectAll = materialName == RED_NAME_CONSTEXPR_NOREG( "selectAll" );

		if ( !m_materialsAvailability->m_components.Exist( materialName ) && !isSelectAll )
		{
			return;
		}

		const auto SetupMappedMaterials = [ this ]( const CName &name )
		{

			if ( m_materialsAvailability->m_componentsAvailability[ name ]
				&& !m_sharedMaterials->m_commonExternalMaterials.KeyExist( name )
				&& !m_sharedMaterials->m_commonLocalMaterials.KeyExist( name ) )
			{
				InitializeCommonMaterial( name );
			}
			else
			{
				/*m_sharedMaterials->m_commonExternalMaterials.Remove( name );
				m_sharedMaterials->m_commonLocalMaterials.Remove( name );*/

				for ( const auto& item : m_sharedAppearancesMaterials->m_mappings )
				{
					const THandle< AppearancesMaterialMapping >& mapping = item.Value();
					for ( auto& mappedMaterial : mapping->m_mappedMaterials )
					{
						if ( mappedMaterial.Value() == name )
						{
							mapping->m_mappedMaterials[ mappedMaterial.Key() ] = CName::NONE();

							String changedPath = String::Printf( "setup.sharedAppearancesMaterials.%hs.%hs", item.Key().AsChar(), mappedMaterial.Key().AsChar() );
							m_descriptorFallbackPtr->NotifyPropertyChanged( rtti::AccessPath( changedPath.AsChar() ) );
						}
					}
				}
			}
		};

		if ( isSelectAll )
		{
			for ( const auto &matName : m_materialsAvailability->m_components )
			{
				SetupMappedMaterials( matName );
			}

			m_descriptorFallbackPtr->NotifyPropertyChanged( rtti::AccessPath( "setup.materialsAvailability" ) );
		}
		else
		{
			SetupMappedMaterials( materialName );
		}

		m_descriptorFallbackPtr->NotifyPropertyChanged( rtti::AccessPath( "setup.sharedAppearancesMaterials" ) );
		m_descriptorFallbackPtr->NotifyPropertyChanged( rtti::AccessPath( "setup.sharedMaterials" ) );
	}

	void AppearancesSetup::OnBaseMaterialChanged( const SerializableID& id )
	{
		auto material = std::find_if( m_sharedMaterials->m_commonLocalMaterials.Begin(), m_sharedMaterials->m_commonLocalMaterials.End(),
			[id] ( const auto& pair )
		{
			return pair.Value()->GetID() == id;
		} );

		if ( material != m_sharedMaterials->m_commonLocalMaterials.End() )
		{
			String materialName( ( *material ).Key().AsChar() );
			m_descriptorFallbackPtr->GetSwitches()[RED_NAME( String::Printf( "setup.sharedMaterials.%hs.baseMaterial", materialName.AsChar() ) )] = true;
			m_sharedMaterials->NotifyPropertyChanged( rtti::AccessPath( materialName.AsChar() ) );
		}
	}

	void AppearancesSetup::OnAppearanceNameChanged( const THandle<AppearancesMaterialMapping>&appMapping )
	{
		m_sharedAppearancesMaterials->m_appearances.Remove( appMapping->m_previousName );
		m_sharedAppearancesMaterials->m_appearances.Insert( appMapping->m_name );

		m_sharedAppearancesMaterials->m_mappings.Remove( appMapping->m_previousName );
		m_sharedAppearancesMaterials->m_mappings.Insert( appMapping->m_name, appMapping );

		m_appearancesAvailability->m_componentsAvailability.Remove( appMapping->m_previousName );
		m_appearancesAvailability->m_components.Remove( appMapping->m_previousName );

		m_appearancesAvailability->m_componentsAvailability.Insert( appMapping->m_name, true );
		m_appearancesAvailability->m_components.Insert( appMapping->m_name );

		red::DynArray< CName > switchesPaths;
		m_descriptorFallbackPtr->GetSwitches().GetKeys( switchesPaths );

		const String sharedAppsPath = String::Printf( "setup.sharedAppearancesMaterials.%hs", appMapping->m_previousName.AsChar() );
		const String appsAvailabilityPath = String::Printf( "setup.appearancesAvailability.%hs", appMapping->m_previousName.AsChar() );

		const String newSharedAppsPath = String::Printf( "setup.sharedAppearancesMaterials.%hs", appMapping->m_name.AsChar() );
		const String newAppsAvailabilityPath = String::Printf( "setup.appearancesAvailability.%hs", appMapping->m_name.AsChar() );

		for ( const CName& path : switchesPaths )
		{
			String pathString( path.AsChar() );
			if ( pathString.BeginsWith( sharedAppsPath ) )
			{
				String newPath = pathString;
				newPath.Replace( sharedAppsPath, newSharedAppsPath );

				Bool value = m_descriptorFallbackPtr->GetSwitches()[path];
				m_descriptorFallbackPtr->GetSwitches().Insert( RED_NAME( newPath ), value );

				m_descriptorFallbackPtr->GetSwitches().Remove( path );
			}
			else if ( pathString.BeginsWith( appsAvailabilityPath ) )
			{
				String newPath = pathString;
				newPath.Replace( appsAvailabilityPath, newAppsAvailabilityPath );

				Bool value = m_descriptorFallbackPtr->GetSwitches()[path];
				m_descriptorFallbackPtr->GetSwitches().Insert( RED_NAME( newPath ), value );

				m_descriptorFallbackPtr->GetSwitches().Remove( path );
			}
		}

		m_descriptorFallbackPtr->NotifyPropertyChanged( rtti::AccessPath( "setup.sharedAppearancesMaterials" ) );
		m_descriptorFallbackPtr->NotifyPropertyChanged( rtti::AccessPath( "setup.appearancesAvailability" ) );
	}

	void AppearancesSetup::OnMaterialNameChanged( const THandle<MaterialDescriptor>& materialDesc )
	{
		if ( m_sharedMaterials->m_commonLocalMaterials.Remove( materialDesc->m_previousName ).IsSuccessful() )
		{
			m_sharedMaterials->m_commonLocalMaterials.Insert( materialDesc->m_currentName, materialDesc );
		}

		if ( m_sharedMaterials->m_commonExternalMaterials.Remove( materialDesc->m_previousName ).IsSuccessful() )
		{
			m_sharedMaterials->m_commonExternalMaterials.Insert( materialDesc->m_currentName, materialDesc );
		}

		m_materialsAvailability->m_componentsAvailability.Remove( materialDesc->m_previousName );
		m_materialsAvailability->m_components.Remove( materialDesc->m_previousName );

		m_materialsAvailability->m_componentsAvailability.Insert( materialDesc->m_currentName, true );
		m_materialsAvailability->m_components.Insert( materialDesc->m_currentName );

		if ( m_materialsAvailability->m_readOnly.Remove( materialDesc->m_previousName ).IsSuccessful() )
		{
			m_materialsAvailability->m_readOnly.Insert( materialDesc->m_currentName );
		}

		if ( m_materialsAvailability->m_initiallySet.Remove( materialDesc->m_previousName ).IsSuccessful() )
		{
			m_materialsAvailability->m_initiallySet.Insert( materialDesc->m_currentName );
		}

		red::DynArray< CName > switchesPaths;
		m_descriptorFallbackPtr->GetSwitches().GetKeys( switchesPaths );

		const String sharedMaterialsPath = String::Printf( "setup.sharedMaterials.%hs", materialDesc->m_previousName.AsChar() );
		const String materialsAvailabilityPath = String::Printf( "setup.materialsAvailability.%hs", materialDesc->m_previousName.AsChar() );

		const String newSharedMaterialsPath = String::Printf( "setup.sharedMaterials.%hs", materialDesc->m_currentName.AsChar() );
		const String newMaterialsAvailabilityPath = String::Printf( "setup.materialsAvailability.%hs", materialDesc->m_currentName.AsChar() );

		for ( const CName& path : switchesPaths )
		{
			String pathString( path.AsChar() );
			if ( pathString.BeginsWith( sharedMaterialsPath ) )
			{
				String newPath = pathString;
				newPath.Replace( sharedMaterialsPath, newSharedMaterialsPath );

				Bool value = m_descriptorFallbackPtr->GetSwitches()[path];
				m_descriptorFallbackPtr->GetSwitches().Insert( RED_NAME( newPath ), value );

				m_descriptorFallbackPtr->GetSwitches().Remove( path );
			}
			else if ( pathString.BeginsWith( materialsAvailabilityPath ) )
			{
				String newPath = pathString;
				newPath.Replace( materialsAvailabilityPath, newMaterialsAvailabilityPath );

				Bool value = m_descriptorFallbackPtr->GetSwitches()[path];
				m_descriptorFallbackPtr->GetSwitches().Insert( RED_NAME( newPath ), value );

				m_descriptorFallbackPtr->GetSwitches().Remove( path );
			}
		}

		m_descriptorFallbackPtr->NotifyPropertyChanged( rtti::AccessPath( "setup.sharedMaterials" ) );
		m_descriptorFallbackPtr->NotifyPropertyChanged( rtti::AccessPath( "setup.materialsAvailability" ) );
	}

	void AppearancesSetup::OnPropertyChanged( const SerializableID& id, const rtti::AccessPath& path )
	{
		if ( id == m_appearancesAvailability->GetID() )
		{
			const CName appearanceName = RED_NAME( path.ToString() );
			OnAppearanceAvailabilityChanged( appearanceName );
		}
		else if ( id == m_materialsAvailability->GetID() )
		{
			const CName materialName = RED_NAME( path.ToString() );
			OnMaterialAvailabilityChanged( materialName );
		}
		else if ( path.ToString().EqualsNC( "baseMaterial" ) )
		{
			OnBaseMaterialChanged( id );
		}
		else if ( path.ToString().EqualsNC( "name" ) )
		{
			auto appearanceIt = std::find_if( m_sharedAppearancesMaterials->m_mappings.Begin(), m_sharedAppearancesMaterials->m_mappings.End(),
				[id] ( const auto& pair ) { return pair.Value()->GetID() == id;	} );

			if ( appearanceIt != m_sharedAppearancesMaterials->m_mappings.End() )
			{
				THandle< AppearancesMaterialMapping > appMapping = appearanceIt.Value();
				OnAppearanceNameChanged( appMapping );
				return;
			}

			THandle< MaterialDescriptor > materialDesc;
			auto materialIt = std::find_if( m_sharedMaterials->m_commonLocalMaterials.Begin(), m_sharedMaterials->m_commonLocalMaterials.End(),
				[id] ( const auto& pair ) { return pair.Value()->GetID() == id;	} );

			if ( materialIt != m_sharedMaterials->m_commonLocalMaterials.End() )
			{
				materialDesc = materialIt.Value();
			}
			else
			{
				materialIt = std::find_if( m_sharedMaterials->m_commonExternalMaterials.Begin(), m_sharedMaterials->m_commonExternalMaterials.End(),
					[id] ( const auto& pair ) {	return pair.Value()->GetID() == id;	} );

				if ( materialIt != m_sharedMaterials->m_commonExternalMaterials.End() )
				{
					materialDesc = materialIt.Value();
				}
			}

			if ( materialDesc )
			{
				OnMaterialNameChanged( materialDesc );
			}
		}
		else if ( path.ToString().EqualsNC( "instanceParameters" ) )
		{
			m_sharedMaterials->ClearInstanceParameters( id );
		}
	}
}


RTTI_BEGIN_TYPE_IN_NAMESPACE( AppearancesSetup, tools );
	RTTI_PROPERTY( m_sharedAppearancesMaterials ).editable().inlined().clearable( false ).selectable( false );
	RTTI_PROPERTY( m_appearancesAvailability ).editable().inlined().clearable( false ).selectable( false );
	RTTI_PROPERTY( m_sharedMaterials ).editable().inlined().clearable( false ).selectable( false );
	RTTI_PROPERTY( m_materialsAvailability ).editable().inlined().clearable( false ).selectable( false );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( ComponentAvailability, tools );
	RTTI_PARENT_TYPE( ISerializable );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( MeshCustomPropertiesDescriptor_Appearances, tools );
	RTTI_PARENT_TYPE( tools::IResourceCustomPropertiesDescriptor );
	RTTI_PROPERTY( m_setup ).editable();
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( CommonAppearances, tools );
	RTTI_PARENT_TYPE( ISerializable );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( AppearancesMaterialMapping, tools );
	RTTI_PARENT_TYPE( ISerializable );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( MaterialDescriptor, tools );
	RTTI_PARENT_TYPE( ISerializable );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( CommonMaterials, tools );
	RTTI_PARENT_TYPE( ISerializable );
	RTTI_PROPERTY( m_clearInstanceParameters ).editable();
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( AppearanceMaterialSelector, tools );
	RTTI_PARENT_TYPE( tools::IEditableCustomProperty );
RTTI_END_TYPE();
