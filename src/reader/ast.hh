#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace jsmm::reader {

struct SourcePosition {
  std::uint32_t line = 1;
  std::uint32_t column = 1;
};

struct SourceRange {
  SourcePosition start{};
  SourcePosition end{};
};

struct Identifier {
  std::string text;
  SourceRange range{};
};

struct NumberLiteral {
  std::string text;
  SourceRange range{};
};

struct StringLiteral {
  std::string text;
  bool raw = false;
  SourceRange range{};
};

struct Expression;

using ExprPtr = std::shared_ptr<Expression>;

// Expression node kinds.
struct NumberLiteralExpr {
  std::string spelling;
};

struct StringLiteralExpr {
  std::string value;
};

struct IdentifierExpr {
  std::string name;
};

struct CallExpr {
  ExprPtr callee;
  std::vector<ExprPtr> arguments;
};

struct UnaryExpr {
  std::string op;
  ExprPtr operand;
};

struct AddressOfExpr {
  ExprPtr operand;
};

struct DereferenceExpr {
  ExprPtr operand;
};

struct BinaryExpr {
  std::string op;
  ExprPtr left;
  ExprPtr right;
};

struct UpdateExpr {
  std::string op;
  bool prefix;
  ExprPtr operand;
};

struct AssignmentExpr {
  std::string op;
  ExprPtr left;
  ExprPtr right;
};

struct MemberExpr {
  ExprPtr object;
  std::string property;
  ExprPtr computed;
  bool computedFlag;
};

struct ObjectExpr {
  std::vector<std::pair<std::string, ExprPtr>> properties;
};

struct DesignatedInitializer {
  std::size_t index;
  ExprPtr value;
};

struct ArrayExpr {
  std::vector<ExprPtr> elements;
  std::vector<DesignatedInitializer> designated;
  bool hasDesignated = false;
};

struct BooleanLiteralExpr {
  bool value;
};

struct NullLiteralExpr {};

struct UndefinedLiteralExpr {};

struct TypeExpr {
  std::vector<std::string> names;
  bool pointer = false;
  std::size_t intSize = 0;
  bool intSigned = false;
  bool isIntType = false;
  std::size_t arraySize = 0;
  bool isArray = false;
  SourceRange range{};
};

// Statements.
struct Statement;

struct BlockStatement {
  std::vector<std::unique_ptr<Statement>> statements;
  SourceRange range{};
};

struct IfStatement {
  ExprPtr condition;
  std::vector<std::unique_ptr<Statement>> consequent;
  std::vector<std::unique_ptr<Statement>> alternate;
  SourceRange range{};
};

struct WhileStatement {
  ExprPtr condition;
  std::vector<std::unique_ptr<Statement>> body;
  SourceRange range{};
};

struct VariableDeclarator {
  std::string name;
  ExprPtr initializer = nullptr;
  std::shared_ptr<TypeExpr> annotation = nullptr;
  SourceRange range{};
};

struct VariableDeclaration {
  std::string kind;
  std::vector<VariableDeclarator> declarations;
  SourceRange range{};
};

struct ForStatement {
  std::unique_ptr<VariableDeclaration> init = nullptr;
  ExprPtr initExpression = nullptr;
  ExprPtr condition = nullptr;
  ExprPtr update = nullptr;
  std::vector<std::unique_ptr<Statement>> body;
  SourceRange range{};
};

struct FunctionParameter {
  std::string name;
  SourceRange range{};
};

struct FunctionDeclaration {
  std::string name;
  std::vector<FunctionParameter> parameters;
  std::unique_ptr<BlockStatement> body;
  SourceRange range{};
};

struct BreakStatement {
  std::shared_ptr<Identifier> label = nullptr;
  SourceRange range{};
};

struct ContinueStatement {
  std::shared_ptr<Identifier> label = nullptr;
  SourceRange range{};
};

struct ReturnStatement {
  ExprPtr argument = nullptr;
  SourceRange range{};
};

struct ThrowStatement {
  ExprPtr argument = nullptr;
  SourceRange range{};
};

struct LabeledStatement {
  std::string label;
  std::unique_ptr<Statement> body;
  SourceRange range{};
};

struct ExpressionStatement {
  ExprPtr expression = nullptr;
  SourceRange range{};
};

// The expression type wrapper: carries a typed node plus source range.
struct Expression {
  std::variant<
    NumberLiteralExpr,
    StringLiteralExpr,
    IdentifierExpr,
    CallExpr,
    UnaryExpr,
    AddressOfExpr,
    DereferenceExpr,
    BinaryExpr,
    UpdateExpr,
    AssignmentExpr,
    MemberExpr,
    ObjectExpr,
    ArrayExpr,
    BooleanLiteralExpr,
    NullLiteralExpr,
    UndefinedLiteralExpr
  > node;
  SourceRange range{};
};

// Statement is the wrapper carrying source range.
struct Statement {
  std::variant<
    BlockStatement,
    VariableDeclaration,
    FunctionDeclaration,
    ExpressionStatement,
    IfStatement,
    WhileStatement,
    ForStatement,
    BreakStatement,
    ContinueStatement,
    ReturnStatement,
    ThrowStatement,
    LabeledStatement
  > node;
  SourceRange range{};
};

using StatementList = std::vector<std::unique_ptr<Statement>>;

struct Program {
  StatementList statements;
  SourceRange range{};
};

} // namespace jsmm::reader
