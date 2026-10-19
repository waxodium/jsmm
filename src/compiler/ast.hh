#pragma once

#include "engine/lexer.hh"

#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace rma::compiler {

struct SourceRange {
  syntax::SourcePosition start{};
  syntax::SourcePosition end{};
};

struct Expr;
using ExprPtr = std::shared_ptr<Expr>;

struct LiteralExpr {
  enum class Kind { Number, String, Boolean, Null, Undefined };
  Kind kind;
  std::string spelling;
};

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
struct CallExpr {
  ExprPtr callee;
  std::vector<ExprPtr> arguments;
};

struct ArrayExpr {
  std::vector<ExprPtr> elements;
};

struct UpdateExpr {
  std::string op;
  bool prefix;
  ExprPtr target;
};

struct GenericExpr {
  std::string kind;
  std::vector<ExprPtr> children;
};

struct Expr {
  using Node = std::variant<LiteralExpr, IdentifierExpr, UnaryExpr, BinaryExpr, AssignmentExpr,
                            MemberExpr, ObjectExpr, CallExpr, ArrayExpr, UpdateExpr, GenericExpr>;
  Node node;
  SourceRange range;

  template <class T>
  explicit Expr(T value, SourceRange sourceRange = {})
      : node(std::move(value)),
        range(sourceRange) {}
};

struct VariableDeclarator {
  std::string name;
  ExprPtr initializer;
};
struct VariableDeclaration {
  std::string kind;
  std::vector<VariableDeclarator> declarations;
};
struct ExpressionStatement {
  ExprPtr expression;
};
struct BlockStatement;
using StatementList = std::vector<struct Statement>;
struct ReturnStatement {
  ExprPtr argument;
};
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
  ExprPtr condition;
  ExprPtr update;
  StatementList body;
};
struct BreakStatement {
  std::string label;
};
struct ContinueStatement {
  std::string label;
};
struct GenericStatement {
  std::string kind;
  std::vector<ExprPtr> expressions;
  StatementList statements;
};

struct Statement {
  using Node = std::variant<VariableDeclaration, ExpressionStatement, FunctionDeclaration,
                            ReturnStatement, BlockStatement, IfStatement, WhileStatement,
                            ForStatement, BreakStatement, ContinueStatement, GenericStatement>;
  Node node;
  SourceRange range;

  template <class T>
  explicit Statement(T value, SourceRange sourceRange = {})
      : node(std::move(value)),
        range(sourceRange) {}
};

struct Program {
  StatementList statements;
  SourceRange range;
};

} // namespace rma::compiler
