#include "../../backendEngine/include/validator.h"

namespace tools
{
	class MultilayerMaskValidator : public IValidator
	{
		RTTI_DECLARE_TYPE( MultilayerMaskValidator );

	public:
		MultilayerMaskValidator();

		virtual Bool Validate( const THandle<CResource>& resource, tools::IMessageReporter& errorHandler ) const override;

	private:
		red::DynArray< std::tuple<String, Uint32, Uint32 > > m_limits;
	};

	//////////////////////////////////////////////////////////////////////////

	class BitmapTextureValidator : public IValidator
	{
		RTTI_DECLARE_TYPE( BitmapTextureValidator );

	public:
		BitmapTextureValidator();

		virtual Bool Validate( const THandle<CResource>& resource, tools::IMessageReporter& errorHandler ) const override;
	};
}