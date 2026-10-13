/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include <functional>

#include "rttiSystem.h"
#include "rttiFundamentalTypes.h"

#include "scriptExpressionParserPath.h"
#include "scriptExpressionParser.h"

#include "scriptStackFrame.h"
#include "scriptDebuggerLocalsBase.h"

#include "scriptExpressionParserPathElement.h"
#include "scriptExpressionParserGeneratedVariable.h"
#include "scriptExpressionParserEnumVariable.h"
#include "scriptDebuggerLocalsFrame.h"

#include "scriptExpressionParser_bison.cxx.h"

#include "rttiEnum.h"
#include "stringRTTI.h"
#include "rttiPointer.h"
#include "rttiPointerTypes.h"

namespace script
{
	namespace expression
	{
		Bool GetValue( Element& element, script::debug::IVariablePtr& parent, String& value );

		namespace comparison
		{
			//////////////////////////////////////////////////////////////////////////
			// Functions for discerning the type of the current token
			Bool IsInteger( const Element& element )
			{
				switch ( element.token.id )
				{
				case TOKEN_INTEGER:
					return true;

				case CUSTOM_TOKEN_GENVAR:
				case TOKEN_IDENT:
					CName elementTypeName = element.variable->GetType()->GetName();

					return elementTypeName == TTypeName< Uint8 >::GetTypeName() ||
						elementTypeName == TTypeName< Uint16 >::GetTypeName() ||
						elementTypeName == TTypeName< Uint32 >::GetTypeName() ||
						elementTypeName == TTypeName< Uint64 >::GetTypeName() ||
						elementTypeName == TTypeName< Int8 >::GetTypeName() ||
						elementTypeName == TTypeName< Int16 >::GetTypeName() ||
						elementTypeName == TTypeName< Int32 >::GetTypeName() ||
						elementTypeName == TTypeName< Int64 >::GetTypeName();
				}

				return false;
			}

			Bool IsFloat( const Element& element )
			{
				switch ( element.token.id )
				{
				case TOKEN_FLOAT:
					return true;

				case CUSTOM_TOKEN_GENVAR:
				case TOKEN_IDENT:
					CName elementTypeName = element.variable->GetType()->GetName();

					return elementTypeName == TTypeName< Float >::GetTypeName() || elementTypeName == TTypeName< Double >::GetTypeName();
				}

				return false;
			}

			Bool IsBoolean( const Element& element )
			{
				switch ( element.token.id )
				{
				case TOKEN_BOOL_TRUE:
				case TOKEN_BOOL_FALSE:
					return true;

				case CUSTOM_TOKEN_GENVAR:
				case TOKEN_IDENT:
					CName elementTypeName = element.variable->GetType()->GetName();

					return elementTypeName == TTypeName< Bool >::GetTypeName();
				}

				return false;
			}

			Bool IsEnum( const Element& element )
			{
				switch ( element.token.id )
				{
				case CUSTOM_TOKEN_ENUMTYPE:
				case TOKEN_IDENT:
					return element.variable->GetType()->GetType() == ERTTITypeType::RT_Enum;
				}

				return false;
			}

			Bool IsStringOrName( const Element& element )
			{
				switch ( element.token.id )
				{
				case TOKEN_NAME:
				case TOKEN_STRING:
					return true;

				case TOKEN_IDENT:
					return element.variable->GetType()->GetType() == ERTTITypeType::RT_Name ||
						element.variable->GetType()->GetName() == TTypeName< String >::GetTypeName();
				}

				return false;
			}

			Bool IsObjectOrHandle( const Element& element )
			{
				switch( element.token.id )
				{
				case TOKEN_NULL:
					return true;

				case TOKEN_THIS:
				case TOKEN_IDENT:
					return element.variable->GetType()->GetType() == ERTTITypeType::RT_Class ||
						element.variable->GetType()->GetType() == ERTTITypeType::RT_Handle ||
						element.variable->GetType()->GetType() == ERTTITypeType::RT_WeakHandle;
				}

				return false;
			}

			//////////////////////////////////////////////////////////////////////////
			// Utility function to unify the different object formats into a single comparable type
			rtti::Pointer ToPointer( const Element& element )
			{
				if( element.token.id == TOKEN_NULL )
					return rtti::Pointer();

				const void* raw = element.variable->GetRaw();

				switch( element.variable->GetType()->GetType() )
				{
				case ERTTITypeType::RT_Handle:
				case ERTTITypeType::RT_WeakHandle:
				{
					const rtti::IBasePointerType* type = static_cast< const rtti::IBasePointerType* >( element.variable->GetType() );

					return type->GetPointer( raw );
				}

				case ERTTITypeType::RT_Class:
					return rtti::Pointer( const_cast< void* >( raw ), static_cast< const rtti::ClassType* >( element.variable->GetType() ) );
				}

				RED_FATAL( "Type cannot be converted to pointer: %hs", element.variable->GetType()->GetName().AsChar() );

				return rtti::Pointer();
			}

			//////////////////////////////////////////////////////////////////////////
			// Creates a new IVariable wrapper for the result and pushes it onto the token stack
			// ready to be used by future operations
			template< typename T >
			void StoreResult( red::DynArray< Element >& tokenStack, const T& result )
			{
				String resultStr = ToStringDirect( result );

				Element calculated( expression::ReadOnlyToken { red::StringView(), CUSTOM_TOKEN_GENVAR } );
				calculated.variable = red::CreateUniquePtr< GeneratedVariable >( resultStr, GetTypeObject< T >() );
				tokenStack.PushBack( std::move( calculated ) );
			}

			//////////////////////////////////////////////////////////////////////////
			// Unary functions

			template< typename TVal, template< typename > class Operation >
			struct UnaryOperationInternal
			{
				static Bool Call( red::DynArray< Element >& tokenStack, const TVal& value )
				{
					auto result = Operation< TVal >()( value );
					StoreResult( tokenStack, result );

					return true;
				}
			};

			// The default template
			template< typename TVal, template< typename > class Operation >
			struct UnaryOperation
			{
				static Bool Call( red::DynArray< Element >&, const TVal& )
				{
					return false;
				}
			};

			template< template< typename > class Operation >
			struct UnaryOperation< Int64, Operation >
			{
				static Bool Call( red::DynArray< Element >& tokenStack, const Int64& value )
				{
					return UnaryOperationInternal< Int64, Operation >::Call( tokenStack, value );
				}
			};

			template< template< typename > class Operation >
			struct UnaryOperation< Bool, Operation >
			{
				static Bool Call( red::DynArray< Element >& tokenStack, const Bool& value )
				{
					return UnaryOperationInternal< Bool, Operation >::Call( tokenStack, value );
				}
			};

// Requires c++14 support on orbis
#ifndef RED_PLATFORM_ORBIS
			template<>
			struct UnaryOperation< Bool, std::bit_not >
			{
				static Bool Call( red::DynArray< Element >&, const Bool& )
				{
					return false;
				}
			};

			template<>
			struct UnaryOperation< Double, std::bit_not >
			{
				static Bool Call( red::DynArray< Element >&, const Double& )
				{
					return false;
				}
			};
#endif // RED_PLATFORM_ORBIS

			template< template< typename > class Operation >
			struct UnaryOperation< Double, Operation >
			{
				static Bool Call( red::DynArray< Element >& tokenStack, const Double& value )
				{
					return UnaryOperationInternal< Double, Operation >::Call( tokenStack, value );
				}
			};

			//////////////////////////////////////////////////////////////////////////
			// Binary functions

			template< typename TVal, template< typename > class Operation >
			struct BinaryOperationInternal
			{
				static Bool Call( red::DynArray< Element >& tokenStack, const TVal& left, const TVal& right )
				{
					auto result = Operation< TVal >()( left, right );
					StoreResult( tokenStack, result );

					return true;
				}
			};

			// The default template
			template< typename Left, typename Right, template< typename > class Operation >
			struct BinaryOperation
			{
				static Bool Call( red::DynArray< Element >&, const Left&, const Right& )
				{
					// This means we haven't created any sort of comparison function for this
					// combination of types and so it will place an error in the watch window
					return false;
				}
			};

			// Cannot perform bitwise operations on floating point numbers
			template<>
			struct BinaryOperation< Double, Double, std::bit_and >
			{
				static Bool Call( red::DynArray< Element >&, Double, Double )
				{
					return false;
				}
			};

			template<>
			struct BinaryOperation< Double, Double, std::bit_or >
			{
				static Bool Call( red::DynArray< Element >&, Double, Double )
				{
					return false;
				}
			};

			template<>
			struct BinaryOperation< Double, Double, std::bit_xor >
			{
				static Bool Call( red::DynArray< Element >&, Double, Double )
				{
					return false;
				}
			};

			// Number comparison
			template< template< typename > class Operation >
			struct BinaryOperation< Int64, Int64, Operation >
			{
				static Bool Call( red::DynArray< Element >& tokenStack, Int64 left, Int64 right )
				{
					return BinaryOperationInternal< Int64, Operation >::Call( tokenStack, left, right );
				}
			};

			template< template< typename > class Operation >
			struct BinaryOperation< Double, Double, Operation >
			{
				static Bool Call( red::DynArray< Element >& tokenStack, Double left, Double right )
				{
					return BinaryOperationInternal< Double, Operation >::Call( tokenStack, left, right );
				}
			};

			template< template< typename > class Operation >
			struct BinaryOperation< Double, Int64, Operation >
			{
				static Bool Call( red::DynArray< Element >& tokenStack, Double left, Int64 right )
				{
					return BinaryOperation< Double, Double, Operation >::Call( tokenStack, left, static_cast< Double >( right ) );
				}
			};

			template< template< typename > class Operation >
			struct BinaryOperation< Int64, Double, Operation >
			{
				static Bool Call( red::DynArray< Element >& tokenStack, Int64 left, Double right )
				{
					return BinaryOperation< Double, Double, Operation >::Call( tokenStack, static_cast<Double>( left ), right );
				}
			};

			// Booleans and Strings only support == and !=, so we explicitly define these operator functions
			template<>
			struct BinaryOperation< String, String, std::equal_to >
			{
				static Bool Call( red::DynArray< Element >& tokenStack, const String& left, const String& right )
				{
					return BinaryOperationInternal< String, std::equal_to >::Call( tokenStack, left, right );
				}
			};

			template<>
			struct BinaryOperation< String, String, std::not_equal_to >
			{
				static Bool Call( red::DynArray< Element >& tokenStack, const String& left, const String& right )
				{
					return BinaryOperationInternal< String, std::not_equal_to >::Call( tokenStack, left, right );
				}
			};

			template<>
			struct BinaryOperation< Bool, Bool, std::equal_to >
			{
				static Bool Call( red::DynArray< Element >& tokenStack, Bool left, Bool right )
				{
					return BinaryOperationInternal< Bool, std::equal_to >::Call( tokenStack, left, right );
				}
			};

			template<>
			struct BinaryOperation< Bool, Bool, std::not_equal_to >
			{
				static Bool Call( red::DynArray< Element >& tokenStack, Bool left, Bool right )
				{
					return BinaryOperationInternal< Bool, std::not_equal_to >::Call( tokenStack, left, right );
				}
			};

			// Booleans also support logical operators || and &&
			template<>
			struct BinaryOperation< Bool, Bool, std::logical_and >
			{
				static Bool Call( red::DynArray< Element >& tokenStack, Bool left, Bool right )
				{
					return BinaryOperationInternal< Bool, std::logical_and >::Call( tokenStack, left, right );
				}
			};

			template<>
			struct BinaryOperation< Bool, Bool, std::logical_or >
			{
				static Bool Call( red::DynArray< Element >& tokenStack, Bool left, Bool right )
				{
					return BinaryOperationInternal< Bool, std::logical_or >::Call( tokenStack, left, right );
				}
			};

			// Comparison of enum values is done as an integer comparison, so we use this wrapper
			// to ensure that the enum types match so that comparison of two unrelated enums' first
			// option (integer value 0) will not equate to true
			struct EnumWrapper
			{
				CName type;
				String value;
			};

			template< template< typename > class Operation >
			struct BinaryOperation< EnumWrapper, EnumWrapper, Operation >
			{
				static Bool Call( red::DynArray< Element >& tokenStack, const EnumWrapper& left, const EnumWrapper& right )
				{
					if ( left.type != right.type )
						return false;

					return BinaryOperation< String, String, Operation >::Call( tokenStack, left.value, right.value );
				}
			};

			// It also makes sense to only allow ==/!= comparisons for pointers as well
			template<>
			struct BinaryOperation< rtti::Pointer, rtti::Pointer, std::equal_to >
			{
				static Bool Call( red::DynArray< Element >& tokenStack, const rtti::Pointer& left, const rtti::Pointer& right )
				{
					return BinaryOperationInternal< rtti::Pointer, std::equal_to >::Call( tokenStack, left, right );
				}
			};

			template<>
			struct BinaryOperation< rtti::Pointer, rtti::Pointer, std::not_equal_to >
			{
				static Bool Call( red::DynArray< Element >& tokenStack, const rtti::Pointer& left, const rtti::Pointer& right )
				{
					return BinaryOperationInternal< rtti::Pointer, std::not_equal_to >::Call( tokenStack, left, right );
				}
			};

			//////////////////////////////////////////////////////////////////////////
			// These structs help reduce redundancy by enabling the same templated function
			// to be called twice for both the left and right parts of the expression
			template< typename T >
			struct None {};

			template< typename T >
			struct UnaryNone {};

			template< typename T >
			struct Right
			{
				T right;
			};

			template< template< typename > class Operation, template< typename > class TContainer, typename TPreviousValue >
			Bool PerformOperation( red::DynArray< Element >& tokenStack, script::debug::IVariablePtr& top, const TContainer< TPreviousValue >& previousValue );

			// The default case, probably shouldn't ever execute
			template< template< typename > class TContainer, template< typename > class Operation, typename TValue, typename TPreviousValue >
			struct PerformOperationInternal
			{
				static Bool Call( red::DynArray< Element >&, script::debug::IVariablePtr&, const TValue&, const TContainer< TPreviousValue >& )
				{
					return false;
				}
			};

			// When the first token (right side of the expression off the tokenStack has been processed, we enter this
			// function, which allows us to reenter PerformOperation() once again to process the second token (left side)
			template< template< typename > class Operation, typename TValue, typename TPreviousValue >
			struct PerformOperationInternal< None, Operation, TValue, TPreviousValue >
			{
				static Bool Call( red::DynArray< Element >& tokenStack, script::debug::IVariablePtr& top, const TValue& rightValue, const None< TPreviousValue >& )
				{
					Right< TValue > right { rightValue };
					return PerformOperation< Operation, Right, TValue >( tokenStack, top, right );
				}
			};

			// Both sides of the expression have been processed, so now it's time to perform the operation
			template< template< typename > class Operation, typename TValue, typename TPreviousValue >
			struct PerformOperationInternal< Right, Operation, TValue, TPreviousValue >
			{
				static Bool Call( red::DynArray< Element >& tokenStack, script::debug::IVariablePtr&, const TValue& leftValue, const Right< TPreviousValue >& prev )
				{
					return BinaryOperation< TValue, TPreviousValue, Operation >::Call( tokenStack, leftValue, prev.right );
				}
			};

			template< template< typename > class Operation, typename TValue, typename TPreviousValue >
			struct PerformOperationInternal< UnaryNone, Operation, TValue, TPreviousValue >
			{
				static Bool Call( red::DynArray< Element >& tokenStack, script::debug::IVariablePtr&, const TValue& value, const UnaryNone< TPreviousValue >& )
				{
					return UnaryOperation< TValue, Operation >::Call( tokenStack, value );
				}
			};

			// This is the main workhorse for operations. It will take the top token off the stack and figure out what to do with it based on it's type
			template< template< typename > class Operation, template< typename > class TContainer, typename TPreviousValue >
			Bool PerformOperation( red::DynArray< Element >& tokenStack, script::debug::IVariablePtr& top, const TContainer< TPreviousValue >& previousValue )
			{
				if ( tokenStack.Size() < 1 )
					return false;

				Element element = tokenStack.PopBack();

				String valueStr;
				if ( !GetValue( element, top, valueStr ) )
					return false;

				if( IsInteger( element ) )
				{
					Int64 value;
					if( !FromString( valueStr, value ) )
						return false;

					if( !PerformOperationInternal< TContainer, Operation, Int64, TPreviousValue >::Call( tokenStack, top, value, previousValue ) )
						return false;
				}
				else if( IsFloat( element ) )
				{
					Double value;
					if( !FromString( valueStr, value ) )
						return false;

					if ( !PerformOperationInternal< TContainer, Operation, Double, TPreviousValue >::Call( tokenStack, top, value, previousValue ) )
						return false;
				}
				else if( IsBoolean( element ) )
				{
					Bool value;
					if ( !FromString( valueStr, value ) )
						return false;

					if ( !PerformOperationInternal< TContainer, Operation, Bool, TPreviousValue >::Call( tokenStack, top, value, previousValue ) )
						return false;
				}
				else if( IsEnum( element ) )
				{
					RED_FATAL_ASSERT( element.variable->GetType()->GetType() == ERTTITypeType::RT_Enum );

					const rtti::EnumType* type = static_cast< const rtti::EnumType* >( element.variable->GetType() );

					EnumWrapper container;
					container.type = type->GetName();
					container.value = std::move( valueStr );

					if ( !PerformOperationInternal< TContainer, Operation, EnumWrapper, TPreviousValue >::Call( tokenStack, top, container, previousValue ) )
						return false;
				}
				else if( IsStringOrName( element ) )
				{
					if ( !PerformOperationInternal< TContainer, Operation, String, TPreviousValue >::Call( tokenStack, top, valueStr, previousValue ) )
						return false;
				}
				else if( IsObjectOrHandle( element ) )
				{
					rtti::Pointer object = ToPointer( element );

					if ( !PerformOperationInternal< TContainer, Operation, rtti::Pointer, TPreviousValue >::Call( tokenStack, top, object, previousValue ) )
						return false;
				}
				else
				{
					return false;
				}

				return true;
			}

			// Entrypoint, this is the function called by the main Resolve() loop
			template< template< typename > class Operation >
			Bool PerformOperation( red::DynArray< Element >& tokenStack, script::debug::IVariablePtr& top )
			{
				None< std::nullptr_t > none;
				return PerformOperation< Operation, None, std::nullptr_t >( tokenStack, top, none );
			}

			// Entrypoint, this is the function called by the main Resolve() loop
			template< template< typename > class Operation >
			Bool PerformUnaryOperation( red::DynArray< Element >& tokenStack, script::debug::IVariablePtr& top )
			{
				UnaryNone< std::nullptr_t > none;
				return PerformOperation< Operation, UnaryNone, std::nullptr_t >( tokenStack, top, none );
			}
		}

		Path::Path() = default;
		Path::~Path() = default;

		void Path::Add( const red::StringView& text, Uint32 id )
		{
			m_tokens.PushBack( ReadOnlyToken { text, id } );
		}

		Bool Path::Build( const String& data )
		{
			expression::Parser parser;

			return parser.Parse( data.AsChar(), *this );
		}

		script::debug::IVariablePtr ResolveStackVariable( const Element& element, script::debug::IVariablePtr& parent )
		{
			const red::StringView& name = element.token.text;

			script::debug::IVariablePtr variable = parent->FindChild( name );

			if ( variable )
				return variable;

			if ( name != "this" )
			{
				script::debug::IVariablePtr root = parent->FindChild( "this" );

				if ( root )
					return root->FindChild( name );
			}

			return nullptr;
		}

		script::debug::IVariablePtr ResolveIdent( const Element& element, script::debug::IVariablePtr& parent )
		{
			script::debug::IVariablePtr variable = ResolveStackVariable( element, parent );

			if( !variable )
			{
				CName enumName = RED_NAME_NOREG( element.token.text );
				const rtti::EnumType* type = GetRttiSystem().FindEnum( enumName );

				if( type )
				{
					variable = red::CreateUniquePtr< EnumVariable >( type );
				}
			}

			return variable;
		}

		script::debug::IVariablePtr ResolveVariable( const Element& element, script::debug::IVariablePtr& parent )
		{
			switch ( element.token.id )
			{
			case TOKEN_IDENT:
				return ResolveIdent( element, parent );

			case TOKEN_INTEGER:
				return red::CreateUniquePtr< GeneratedVariable >( element.token.text.ToString(), GetTypeObject< Int32 >() );

			case TOKEN_FLOAT:
				return red::CreateUniquePtr< GeneratedVariable >( element.token.text.ToString(), GetTypeObject< Float >() );

			case TOKEN_NULL:
				return red::CreateUniquePtr< GeneratedVariable >( element.token.text.ToString(), "Null" );

			case TOKEN_BOOL_TRUE:
			case TOKEN_BOOL_FALSE:
				return red::CreateUniquePtr< GeneratedVariable >( element.token.text.ToString(), GetTypeObject< Bool >() );

			case TOKEN_NAME:
				return red::CreateUniquePtr< GeneratedVariable >( element.token.text.ToString(), GetTypeObject< CName >() );

			case TOKEN_STRING:
				return red::CreateUniquePtr< GeneratedVariable >( element.token.text.ToString(), GetTypeObject< String >() );
			}

			return nullptr;
		}

		Bool GetValue( Element& element, script::debug::IVariablePtr& parent, String& value )
		{
			if ( !element.variable )
			{
				element.variable = ResolveVariable( element, parent );

				if ( !element.variable )
					return false;
			}

			switch ( element.token.id )
			{
			case TOKEN_INTEGER:
			case TOKEN_FLOAT:
			case TOKEN_NULL:
			case TOKEN_BOOL_TRUE:
			case TOKEN_BOOL_FALSE:
			case TOKEN_NAME:
			case TOKEN_STRING:
				value.Set( element.token.text.Data(), element.token.text.Length() );
				return true;

			case TOKEN_THIS:
			case TOKEN_IDENT:
			case CUSTOM_TOKEN_GENVAR:
				value = element.variable->GetValue();
				return true;
			}

			return false;
		}

		Bool Path::Resolve( const CScriptStackFrame* in, script::debug::IVariablePtr& out )
		{
			red::DynArray< Element > tokenStack{ red::PoolScript() };

			script::debug::IVariablePtr top = red::CreateUniquePtr< script::debug::Frame >( in );

			for ( const expression::ReadOnlyToken token : m_tokens )
			{
				switch ( token.id )
				{
				case TOKEN_IDENT:
				case TOKEN_INTEGER:
				case TOKEN_FLOAT:
				case TOKEN_NULL:
				case TOKEN_BOOL_TRUE:
				case TOKEN_BOOL_FALSE:
				case TOKEN_STRING:
				case TOKEN_NAME:
				{
					Element element( token );

					tokenStack.PushBack( std::move( element ) );
					break;
				}

				case TOKEN_THIS:
				{
					Element element( token );
					element.variable = top->FindChild( "this" );

					if ( !element.variable )
						return false;

					tokenStack.PushBack( std::move( element ) );
					break;
				}

				case '.':
				{
					if ( tokenStack.Size() < 2 )
						return false;

					Element right = tokenStack.PopBack();
					Element left = tokenStack.PopBack();

					if ( !left.variable )
						left.variable = ResolveIdent( left, top );

					if ( !left.variable )
						return false;

					right.variable = ResolveIdent( right, left.variable );

					if ( !right.variable )
						return false;

					right.token.id = TOKEN_IDENT;

					tokenStack.PushBack( std::move( right ) );
					break;
				}

				case '+':
				{
					if ( !comparison::PerformOperation< std::plus >( tokenStack, top ) )
						return false;

					break;
				}

				case '-':
				{
					if ( !comparison::PerformOperation< std::minus >( tokenStack, top ) )
						return false;

					break;
				}

				case '*':
				{
					if ( !comparison::PerformOperation< std::multiplies >( tokenStack, top ) )
						return false;

					break;
				}

				case '&':
				{
					if ( !comparison::PerformOperation< std::bit_and >( tokenStack, top ) )
						return false;

					break;
				}

				case '|':
				{
					if ( !comparison::PerformOperation< std::bit_or >( tokenStack, top ) )
						return false;

					break;
				}

				case '^':
				{
					if ( !comparison::PerformOperation< std::bit_xor >( tokenStack, top ) )
						return false;

					break;
				}

				case '/':
				{
					if ( !comparison::PerformOperation< std::divides >( tokenStack, top ) )
						return false;

					break;
				}

				case '>':
				{
					if ( !comparison::PerformOperation< std::greater >( tokenStack, top ) )
						return false;

					break;
				}

				case '<':
				{
					if ( !comparison::PerformOperation< std::less >( tokenStack, top ) )
						return false;

					break;
				}

				case TOKEN_OP_GREQ:
				{
					if ( !comparison::PerformOperation< std::greater_equal >( tokenStack, top ) )
						return false;

					break;
				}

				case TOKEN_OP_LEEQ:
				{
					if ( !comparison::PerformOperation< std::less_equal >( tokenStack, top ) )
						return false;

					break;
				}

				case TOKEN_OP_EQUAL:
				{
					if ( !comparison::PerformOperation< std::equal_to >( tokenStack, top ) )
						return false;

					break;
				}

				case TOKEN_OP_NOTEQUAL:
				{
					if ( !comparison::PerformOperation< std::not_equal_to >( tokenStack, top ) )
						return false;

					break;
				}

				case TOKEN_OP_LOGIC_AND:
				{
					if ( !comparison::PerformOperation< std::logical_and >( tokenStack, top ) )
						return false;

					break;
				}

				case TOKEN_OP_LOGIC_OR:
				{
					if ( !comparison::PerformOperation< std::logical_or >( tokenStack, top ) )
						return false;

					break;
				}

				case TOKEN_UNARY_LOGICAL_NOT:
				{
					if ( !comparison::PerformUnaryOperation< std::logical_not >( tokenStack, top ) )
						return false;

					break;
				}

// Requires c++14 support on orbis
#ifndef RED_PLATFORM_ORBIS
				case TOKEN_UNARY_BITWISE_NOT:
				{
					if ( !comparison::PerformUnaryOperation< std::bit_not >( tokenStack, top ) )
						return false;

					break;
				}
#endif // RED_PLATFORM_ORBIS
				default:
					RED_FATAL( "Unrecognised token type: %.*hs (%u)", token.text.Length(), token.text.Data(), token.id );

				}
			}

			if ( tokenStack.Size() != 1 )
				return false;

			Element element = tokenStack.PopBack();

			if ( !element.variable )
				element.variable = ResolveVariable( element, top );

			if ( !element.variable )
				return false;

			out = std::move( element.variable );

			return true;
		}

		void Path::Clear()
		{
			m_tokens.Clear();
		}
	}
}
