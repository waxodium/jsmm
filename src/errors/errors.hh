#pragma once

#include "reader/ast.hh"

#include <cstdint>
#include <string>
#include <string_view>

namespace jsmm::errors {


enum class ErrorCode : std::uint16_t {
  LEX_UNTERMINATED_STRING,
  LEX_UNESCAPED_LINE_TERMINATOR,
  LEX_INVALID_ESCAPE_SEQUENCE,
  LEX_INVALID_HEX_ESCAPE,
  LEX_INVALID_UNICODE_ESCAPE,
  LEX_INVALID_CHARACTER_AFTER_NUMERIC,
  LEX_EXPECTED_DIGIT_IN_NUMERIC,
  LEX_UNEXPECTED_CHARACTER,
  LEX_UNTERMINATED_BLOCK_COMMENT,

  PARSE_UNEXPECTED_END_OF_INPUT,
  PARSE_UNSUPPORTED_KEYWORD,
  PARSE_EXPECTED_BINDING_NAME,
  PARSE_CONST_REQUIRES_INITIALIZER,
  PARSE_EXPECTED_FUNCTION_NAME,
  PARSE_EXPECTED_LPAREN_AFTER_FUNCTION_NAME,
  PARSE_EXPECTED_PARAMETER_NAME,
  PARSE_EXPECTED_RPAREN_AFTER_PARAMETERS,
  PARSE_EXPECTED_LBRACE_BEFORE_FUNCTION_BODY,
  PARSE_EXPECTED_LPAREN_AFTER_IF,
  PARSE_EXPECTED_RPAREN_AFTER_IF_CONDITION,
  PARSE_EXPECTED_LBRACE_FOR_IF_CONSEQUENT,
  PARSE_EXPECTED_LPAREN_AFTER_WHILE,
  PARSE_EXPECTED_RPAREN_AFTER_WHILE_CONDITION,
  PARSE_EXPECTED_LBRACE_FOR_WHILE_BODY,
  PARSE_EXPECTED_LPAREN_AFTER_FOR,
  PARSE_EXPECTED_SEMICOLON_AFTER_FOR_INIT,
  PARSE_EXPECTED_SEMICOLON_AFTER_FOR_CONDITION,
  PARSE_EXPECTED_RPAREN_AFTER_FOR_CLAUSES,
  PARSE_EXPECTED_LBRACE_FOR_FOR_BODY,
  PARSE_BREAK_OUTSIDE_LOOP,
  PARSE_CONTINUE_OUTSIDE_LOOP,
  PARSE_EXPECTED_RBRACE_TO_CLOSE_BLOCK,
  PARSE_EXPECTED_RPAREN_AFTER_CALL_ARGUMENTS,
  PARSE_EXPECTED_PROPERTY_NAME_AFTER_DOT,
  PARSE_EXPECTED_RBRACKET_AFTER_COMPUTED_PROPERTY,
  PARSE_EXPECTED_RPAREN_AFTER_EXPRESSION,
  PARSE_EXPECTED_OBJECT_PROPERTY_NAME,
  PARSE_EXPECTED_COLON_AFTER_OBJECT_PROPERTY_NAME,
  PARSE_EXPECTED_RBRACE_AFTER_OBJECT_LITERAL,
  PARSE_EXPECTED_RBRACKET_AFTER_ARRAY_LITERAL,
  PARSE_EXPECTED_EXPRESSION,
  PARSE_EXPECTED_TYPE_NAME,

  TYPE_FUNCTION_DECLARATIONS_MUST_BE_TOP_LEVEL,
  TYPE_RETURN_OUTSIDE_FUNCTION,
  TYPE_BREAK_OUTSIDE_LOOP,
  TYPE_CONTINUE_OUTSIDE_LOOP,
  TYPE_BUILTIN_CANNOT_BE_USED_AS_VALUE,
  TYPE_UNDEFINED_IDENTIFIER,
  TYPE_DELETE_NOT_SUPPORTED,
  TYPE_UNSUPPORTED_OPERATOR,
  TYPE_CANNOT_UNIFY,
  TYPE_EXPECTED_KIND,
  TYPE_CANNOT_ASSIGN_TO_CONST,
  TYPE_DUPLICATE_BINDING,
  TYPE_INVALID_ASSIGNMENT_TARGET,
  TYPE_ARRAY_ELEMENT_ACCESS_REQUIRES_COMPUTED_INDEX,
  TYPE_MEMBER_ACCESS_NOT_SUPPORTED,
  TYPE_BUILTIN_EXPECTS_ARGUMENTS,
  TYPE_BUILTIN_EXPECTS_AT_LEAST_ARGUMENTS,
  TYPE_CALL_TARGET_EXPECTS_ARGUMENTS,
  TYPE_DUPLICATE_PROPERTY,
  TYPE_FUNCTION_WITHOUT_GUARANTEED_RETURN,
  TYPE_UNKNOWN_TYPE_NAME,
  TYPE_UNION_REQUIRES_SINGLE_PAYLOAD,
  TYPE_NULLABLE_COMPARISON_REQUIRES_NULLISH,

  CODEGEN_UNKNOWN_RUNTIME_HELPER,
  CODEGEN_TARGET_UNAVAILABLE,
  CODEGEN_OBJECT_EMISSION_FAILED,
  CODEGEN_EXECUTABLE_LINK_FAILED,
  CODEGEN_LINK_FAILED,

  IO_CANNOT_OPEN_SOURCE_FILE,
  IO_CANNOT_OPEN_FILE_FOR_WRITING,
  IO_CANNOT_WRITE_FILE,
  IO_EMPTY_COMMAND,
  IO_CANNOT_FORK,
  IO_COMMAND_TERMINATED_ABNORMALLY,

  UNIFY_TYPE_CYCLE_DETECTED,

  CLI_LEX_ERROR,
  CLI_ASSEMBLER_FAILED,
  CLI_LINKER_FAILED,

  ErrorCodeCount  // sentinel
};

constexpr std::string_view errorMessage(ErrorCode code) {
  switch (code) {
    case ErrorCode::LEX_UNTERMINATED_STRING:
      return "unterminated string literal";
    case ErrorCode::LEX_UNESCAPED_LINE_TERMINATOR:
      return "unescaped line terminator in string literal";
    case ErrorCode::LEX_INVALID_ESCAPE_SEQUENCE:
      return "invalid escape sequence";
    case ErrorCode::LEX_INVALID_HEX_ESCAPE:
      return "invalid \\x escape sequence";
    case ErrorCode::LEX_INVALID_UNICODE_ESCAPE:
      return "invalid \\u escape sequence";
    case ErrorCode::LEX_INVALID_CHARACTER_AFTER_NUMERIC:
      return "invalid character after numeric literal";
    case ErrorCode::LEX_EXPECTED_DIGIT_IN_NUMERIC:
      return "expected digit in numeric literal";
    case ErrorCode::LEX_UNEXPECTED_CHARACTER:
      return "unexpected character";
    case ErrorCode::LEX_UNTERMINATED_BLOCK_COMMENT:
      return "unterminated block comment";

    case ErrorCode::PARSE_UNEXPECTED_END_OF_INPUT:
      return "unexpected end of input";
    case ErrorCode::PARSE_UNSUPPORTED_KEYWORD:
      return "unsupported keyword";
    case ErrorCode::PARSE_EXPECTED_BINDING_NAME:
      return "expected binding name";
    case ErrorCode::PARSE_CONST_REQUIRES_INITIALIZER:
      return "const declaration requires an initializer";
    case ErrorCode::PARSE_EXPECTED_FUNCTION_NAME:
      return "expected function name";
    case ErrorCode::PARSE_EXPECTED_LPAREN_AFTER_FUNCTION_NAME:
      return "expected '(' after function name";
    case ErrorCode::PARSE_EXPECTED_PARAMETER_NAME:
      return "expected parameter name";
    case ErrorCode::PARSE_EXPECTED_RPAREN_AFTER_PARAMETERS:
      return "expected ')' after parameters";
    case ErrorCode::PARSE_EXPECTED_LBRACE_BEFORE_FUNCTION_BODY:
      return "expected '{' before function body";
    case ErrorCode::PARSE_EXPECTED_LPAREN_AFTER_IF:
      return "expected '(' after 'if'";
    case ErrorCode::PARSE_EXPECTED_RPAREN_AFTER_IF_CONDITION:
      return "expected ')' after if condition";
    case ErrorCode::PARSE_EXPECTED_LBRACE_FOR_IF_CONSEQUENT:
      return "expected '{' for if consequent";
    case ErrorCode::PARSE_EXPECTED_LPAREN_AFTER_WHILE:
      return "expected '(' after 'while'";
    case ErrorCode::PARSE_EXPECTED_RPAREN_AFTER_WHILE_CONDITION:
      return "expected ')' after while condition";
    case ErrorCode::PARSE_EXPECTED_LBRACE_FOR_WHILE_BODY:
      return "expected '{' for while body";
    case ErrorCode::PARSE_EXPECTED_LPAREN_AFTER_FOR:
      return "expected '(' after 'for'";
    case ErrorCode::PARSE_EXPECTED_SEMICOLON_AFTER_FOR_INIT:
      return "expected ';' after for-init";
    case ErrorCode::PARSE_EXPECTED_SEMICOLON_AFTER_FOR_CONDITION:
      return "expected ';' after for-condition";
    case ErrorCode::PARSE_EXPECTED_RPAREN_AFTER_FOR_CLAUSES:
      return "expected ')' after for clauses";
    case ErrorCode::PARSE_EXPECTED_LBRACE_FOR_FOR_BODY:
      return "expected '{' for for body";
    case ErrorCode::PARSE_BREAK_OUTSIDE_LOOP:
      return "'break' outside of a loop";
    case ErrorCode::PARSE_CONTINUE_OUTSIDE_LOOP:
      return "'continue' outside of a loop";
    case ErrorCode::PARSE_EXPECTED_RBRACE_TO_CLOSE_BLOCK:
      return "expected '}' to close block";
    case ErrorCode::PARSE_EXPECTED_RPAREN_AFTER_CALL_ARGUMENTS:
      return "expected ')' after call arguments";
    case ErrorCode::PARSE_EXPECTED_PROPERTY_NAME_AFTER_DOT:
      return "expected property name after '.'";
    case ErrorCode::PARSE_EXPECTED_RBRACKET_AFTER_COMPUTED_PROPERTY:
      return "expected ']' after computed property";
    case ErrorCode::PARSE_EXPECTED_RPAREN_AFTER_EXPRESSION:
      return "expected ')' after expression";
    case ErrorCode::PARSE_EXPECTED_OBJECT_PROPERTY_NAME:
      return "expected object property name";
    case ErrorCode::PARSE_EXPECTED_COLON_AFTER_OBJECT_PROPERTY_NAME:
      return "expected ':' after object property name";
    case ErrorCode::PARSE_EXPECTED_RBRACE_AFTER_OBJECT_LITERAL:
      return "expected '}' after object literal";
    case ErrorCode::PARSE_EXPECTED_RBRACKET_AFTER_ARRAY_LITERAL:
      return "expected ']' after array literal";
    case ErrorCode::PARSE_EXPECTED_EXPRESSION:
      return "expected expression";
    case ErrorCode::PARSE_EXPECTED_TYPE_NAME:
      return "expected a type name";

    case ErrorCode::TYPE_FUNCTION_DECLARATIONS_MUST_BE_TOP_LEVEL:
      return "function declarations must appear at the top level";
    case ErrorCode::TYPE_RETURN_OUTSIDE_FUNCTION:
      return "'return' outside of a function body";
    case ErrorCode::TYPE_BREAK_OUTSIDE_LOOP:
      return "'break' outside of a loop";
    case ErrorCode::TYPE_CONTINUE_OUTSIDE_LOOP:
      return "'continue' outside of a loop";
    case ErrorCode::TYPE_BUILTIN_CANNOT_BE_USED_AS_VALUE:
      return "builtin cannot be used as a value";
    case ErrorCode::TYPE_UNDEFINED_IDENTIFIER:
      return "undefined identifier";
    case ErrorCode::TYPE_DELETE_NOT_SUPPORTED:
      return "'delete' is not supported in the static subset";
    case ErrorCode::TYPE_UNSUPPORTED_OPERATOR:
      return "unsupported operator";
    case ErrorCode::TYPE_CANNOT_UNIFY:
      return "cannot unify";
    case ErrorCode::TYPE_EXPECTED_KIND:
      return "expected type";
    case ErrorCode::TYPE_CANNOT_ASSIGN_TO_CONST:
      return "cannot assign to constant binding";
    case ErrorCode::TYPE_DUPLICATE_BINDING:
      return "duplicate binding";
    case ErrorCode::TYPE_INVALID_ASSIGNMENT_TARGET:
      return "invalid assignment target";
    case ErrorCode::TYPE_ARRAY_ELEMENT_ACCESS_REQUIRES_COMPUTED_INDEX:
      return "array element access requires a computed index";
    case ErrorCode::TYPE_MEMBER_ACCESS_NOT_SUPPORTED:
      return "member access is not supported";
    case ErrorCode::TYPE_BUILTIN_EXPECTS_ARGUMENTS:
      return "expects arguments";
    case ErrorCode::TYPE_BUILTIN_EXPECTS_AT_LEAST_ARGUMENTS:
      return "expects at least arguments";
    case ErrorCode::TYPE_CALL_TARGET_EXPECTS_ARGUMENTS:
      return "call target expects arguments";
    case ErrorCode::TYPE_DUPLICATE_PROPERTY:
      return "duplicate property in object literal";
    case ErrorCode::TYPE_FUNCTION_WITHOUT_GUARANTEED_RETURN:
      return "function without a guaranteed return unifies its result with undefined";
    case ErrorCode::TYPE_UNKNOWN_TYPE_NAME:
      return "unknown type name";
    case ErrorCode::TYPE_UNION_REQUIRES_SINGLE_PAYLOAD:
      return "a union may hold at most one non-nullable member";
    case ErrorCode::TYPE_NULLABLE_COMPARISON_REQUIRES_NULLISH:
      return "a nullable value may only be compared with null or undefined";

    case ErrorCode::CODEGEN_UNKNOWN_RUNTIME_HELPER:
      return "unknown runtime helper";
    case ErrorCode::CODEGEN_TARGET_UNAVAILABLE:
      return "target unavailable";
    case ErrorCode::CODEGEN_OBJECT_EMISSION_FAILED:
      return "object emission failed";
    case ErrorCode::CODEGEN_EXECUTABLE_LINK_FAILED:
      return "executable link failed";
    case ErrorCode::CODEGEN_LINK_FAILED:
      return "module link failed";

    case ErrorCode::IO_CANNOT_OPEN_SOURCE_FILE:
      return "cannot open source file";
    case ErrorCode::IO_CANNOT_OPEN_FILE_FOR_WRITING:
      return "cannot open file for writing";
    case ErrorCode::IO_CANNOT_WRITE_FILE:
      return "cannot write file";
    case ErrorCode::IO_EMPTY_COMMAND:
      return "empty command";
    case ErrorCode::IO_CANNOT_FORK:
      return "cannot fork";
    case ErrorCode::IO_COMMAND_TERMINATED_ABNORMALLY:
      return "command terminated abnormally";

    case ErrorCode::UNIFY_TYPE_CYCLE_DETECTED:
      return "type cycle detected";

    case ErrorCode::CLI_LEX_ERROR:
      return "lex error";
    case ErrorCode::CLI_ASSEMBLER_FAILED:
      return "assembler failed";
    case ErrorCode::CLI_LINKER_FAILED:
      return "linker failed";

    default:
      return "unknown error";
  }
}

/*
Carries error code, 

- optional detail text
- source range.
*/
struct ErrorStatus {
  ErrorCode code = ErrorCode::ErrorCodeCount;
  std::string detail;
  reader::SourceRange range{};
};

// `jsmm: <code> <message> [<detail>] at <file>:<line>:<col>`.
class ErrorException : public std::runtime_error {
public:
  explicit ErrorException(ErrorStatus status)
      : std::runtime_error(buildMessage(status)), status_(std::move(status)) {}

  const ErrorStatus &status() const noexcept { return status_; }

private:
  static std::string buildMessage(const ErrorStatus &s) {
    std::string out = std::string(errorMessage(s.code));
    if (!s.detail.empty()) out += ": " + s.detail;
    return out;
  }

  ErrorStatus status_;
};

inline std::string formatError(const ErrorStatus &status) {
  std::string out = "jsmm: ";
  out += std::string(errorMessage(status.code));
  if (!status.detail.empty()) out += ": " + status.detail;
  out += " at ";
  out += std::to_string(status.range.start.line);
  out += ":";
  out += std::to_string(status.range.start.column);
  return out;
}

} // namespace jsmm::errors
