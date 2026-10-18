#pragma once

#include "../engine/lexer.hh"
#include "ast.hh"
#include <string>
#include <unordered_map>
#include <vector>

namespace rma::compiler {

class Parser final {
public:
  explicit Parser(std::vector<edon::ecma::Token> tokens);
  Program parse();

private:
  const edon::ecma::Token &current() const;
  bool eat(const std::string &lexeme);
  const edon::ecma::Token &consume();
  [[noreturn]] void fail(const std::string &message) const;
  edon::ecma::Value parseExpression(int minimumPrecedence = 0);
  edon::ecma::Value parseUnary();
  edon::ecma::Value parsePrimary();
  edon::ecma::Value applyBinary(const std::string &op, const edon::ecma::Value &left,
                                const edon::ecma::Value &right);
  int precedence(const std::string &op) const;
  void parseDeclaration();
  void parseConsoleCall();
  void parseAssignment();
  void optionalSemicolon();

  std::vector<edon::ecma::Token> tokens_;
  std::size_t index_ = 0;
  struct Binding {
    edon::ecma::Value value;
    bool mutableBinding = true;
  };
  std::unordered_map<std::string, Binding> bindings_;
  Program program_;
};

} // namespace rma::compiler
