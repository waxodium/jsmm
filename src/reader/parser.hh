#pragma once

#include "errors/errors.hh"
#include "reader/ast.hh"
#include "reader/lexer.hh"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace jsmm::reader {

class Parser final {
public:
  explicit Parser(std::vector<Token> tokens);
  Program parse();

private:
  using StatementParser = Statement (Parser::*)();
  using PrefixParser = ExprPtr (Parser::*)();
  using PostfixParser = ExprPtr (Parser::*)(ExprPtr);
  struct OperatorInfo {
    int bindingPower;
    bool rightAssociative = false;
  };
  using InfixParser = ExprPtr (Parser::*)(ExprPtr, const std::string &, OperatorInfo);
  struct InfixParselet {
    OperatorInfo info;
    InfixParser parse;
    bool assignment = false;
  };

  [[nodiscard]] const Token &current() const;
  bool eat(std::string_view lexeme);
  const Token &consume();
  [[noreturn]] void fail(const std::string &message) const;
  [[noreturn]] void fail(const std::string &message, errors::ErrorCode code) const;
  void optionalSemicolon();

  Statement parseStatement();
  Statement parseEmptyStatement();
  Statement parseExpressionStatement();
  Statement parseDeclaration();
  Statement parseFunctionDeclaration();
  Statement parseReturnStatement();
  Statement parseBlock();
  StatementList parseBlockBody();
  Statement parseIfStatement();
  Statement parseWhileStatement();
  Statement parseForStatement();
  Statement parseBreakStatement();
  Statement parseContinueStatement();

  ExprPtr parseExpression(int minimumBindingPower = 1);
  ExprPtr parsePrefix();
  ExprPtr parseUnaryPrefix();
  ExprPtr parseUpdatePrefix();
  ExprPtr parsePostfix(ExprPtr expression);
  ExprPtr parseCallPostfix(ExprPtr expression);
  ExprPtr parseMemberPostfix(ExprPtr expression);
  ExprPtr parseComputedMemberPostfix(ExprPtr expression);
  ExprPtr parseUpdatePostfix(ExprPtr expression);
  ExprPtr parseBinaryInfix(ExprPtr left, const std::string &op, OperatorInfo info);
  ExprPtr parseAssignmentInfix(ExprPtr left, const std::string &op, OperatorInfo info);
  ExprPtr parsePrimary();
  ExprPtr parseObjectLiteral();
  ExprPtr parseArrayLiteral();
  std::shared_ptr<TypeExpr> parseTypeAnnotation();

  static const std::unordered_map<std::string, InfixParselet> &infixParselets();
  static const std::unordered_map<std::string, PrefixParser> &prefixParselets();
  static const std::unordered_map<std::string, PostfixParser> &postfixParselets();

  std::vector<Token> tokens_;
  std::size_t index_ = 0;
};

} // namespace jsmm::reader
