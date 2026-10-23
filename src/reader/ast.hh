#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace jsmm::reader {

struct SourcePosition {
  std::size_t line = 1;
  std::size_t column = 1;
};

struct SourceRange {
  SourcePosition start{};
  SourcePosition end{};
};

struct Expr;
using ExprPtr = std::shared_ptr<Expr>;

struct NumberLiteralExpr {
  std::string spelling;
};

struct StringLiteralExpr {
  std::string value;
};

struct BooleanLiteralExpr {
  bool value = false;
};

struct NullLiteralExpr {};

struct UndefinedLiteralExpr {};

struct IdentifierExpr {
  std::string name;
};

struct UnaryExpr {
  std::string op;
  ExprPtr operand;
};

struct BinaryExpr {
  std::string op;
  ExprPtr left;
  ExprPtr right;
};

struct UpdateExpr {
  std::string op;
  bool prefix = false;
  ExprPtr target;
};

struct AssignmentExpr {
  std::string op;
  ExprPtr target;
  ExprPtr value;
};

struct MemberExpr {
  ExprPtr object;
  std::string property;
  ExprPtr computedProperty;
  bool computed = false;
};

struct ObjectProperty {
  std::string key;
  ExprPtr value;
};

struct ObjectExpr {
  std::vector<ObjectProperty> properties;
};

struct ArrayExpr {
  std::vector<ExprPtr> elements;
};

struct CallExpr {
  ExprPtr callee;
  std::vector<ExprPtr> arguments;
};

struct Expr {
  using Node =
      std::variant<NumberLiteralExpr, StringLiteralExpr, BooleanLiteralExpr, NullLiteralExpr,
                   UndefinedLiteralExpr, IdentifierExpr, UnaryExpr, BinaryExpr, UpdateExpr,
                   AssignmentExpr, MemberExpr, ObjectExpr, ArrayExpr, CallExpr>;
  Node node;
  SourceRange range;

  template <class T>
  explicit Expr(T value, SourceRange sourceRange = {})
      : node(std::move(value)),
        range(sourceRange) {}
};

struct TypeExpr {
  std::vector<std::string> names;
  SourceRange range;
};

struct VariableDeclarator {
  std::string name;
  ExprPtr initializer;
  std::shared_ptr<TypeExpr> annotation;
};

struct VariableDeclaration {
  std::string kind;
  std::vector<VariableDeclarator> declarations;
};

struct ExpressionStatement {
  ExprPtr expression;
};

struct Statement;
using StatementList = std::vector<Statement>;

struct FunctionDeclaration {
  std::string name;
  std::vector<std::string> parameters;
  StatementList body;
};

struct BlockStatement {
  StatementList statements;
};

struct IfStatement {
  ExprPtr condition;
  StatementList consequent;
  StatementList alternate;
};

struct WhileStatement {
  ExprPtr condition;
  StatementList body;
};

struct ForStatement {
  std::unique_ptr<VariableDeclaration> init;
  ExprPtr initExpression;
  ExprPtr condition;
  ExprPtr update;
  StatementList body;
};

struct ReturnStatement {
  ExprPtr argument;
};

struct BreakStatement {};

struct ContinueStatement {};

struct Statement {
  using Node = std::variant<VariableDeclaration, ExpressionStatement, FunctionDeclaration,
                            ReturnStatement, BlockStatement, IfStatement, WhileStatement,
                            ForStatement, BreakStatement, ContinueStatement>;
  Node node;
  SourceRange range;

  template <class T>
  explicit Statement(T value, SourceRange sourceRange = {})
      : node(std::move(value)),
        range(sourceRange) {}
};

struct Program {
  StatementList statements;
};

} // namespace jsmm::reader
