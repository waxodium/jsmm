#pragma once

#include "ast.hh"
#include "engine/lexer.hh"

#include <string>
#include <unordered_map>
#include <vector>

namespace rma::compiler {

class Parser final {
public:
  explicit Parser(std::vector<syntax::Token> tokens);
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

  const syntax::Token &current() const;
  bool eat(const std::string &lexeme);
  const syntax::Token &consume();
  [[noreturn]] void fail(const std::string &message) const;
  void optionalSemicolon();

  Statement parseStatement();
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
  ExprPtr parsePrimary();
  ExprPtr parseObjectLiteral();
  ExprPtr parseArrayLiteral();
  ExprPtr parsePostfix(ExprPtr expression);
  ExprPtr parseCallPostfix(ExprPtr expression);
  ExprPtr parseMemberPostfix(ExprPtr expression);
  ExprPtr parseComputedMemberPostfix(ExprPtr expression);
  ExprPtr parseUpdatePostfix(ExprPtr expression);
  ExprPtr parseBinaryInfix(ExprPtr left, const std::string &op, OperatorInfo info);
  ExprPtr parseAssignmentInfix(ExprPtr left, const std::string &op, OperatorInfo info);
  static const std::unordered_map<std::string, InfixParselet> &infixParselets();
  static const std::unordered_map<std::string, PrefixParser> &prefixParselets();
  static const std::unordered_map<std::string, PostfixParser> &postfixParselets();

  std::vector<syntax::Token> tokens_;
  std::size_t index_ = 0;
};

} // namespace rma::compiler
