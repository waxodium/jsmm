#include "ast_printer.hh"

#include <ostream>
#include <string>
#include <type_traits>
#include <variant>

namespace rma::compiler {
namespace {
void indent(std::ostream &out, unsigned depth) {
  for (unsigned i = 0; i < depth; ++i) out << "  ";
}

void range(std::ostream &out, const SourceRange &value, bool enabled) {
  if (!enabled) return;
  out << " @" << value.start.line << ':' << value.start.column << "-" << value.end.line << ':'
      << value.end.column;
}

void printExpr(const ExprPtr &expression, std::ostream &out, unsigned depth, bool showRanges);
void printStatement(const Statement &statement, std::ostream &out, unsigned depth, bool showRanges);

void printExpr(const ExprPtr &expression, std::ostream &out, unsigned depth, bool showRanges) {
  if (!expression) {
    out << "<null-expr>";
    return;
  }
  std::visit(
      [&](const auto &node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, LiteralExpr>) {
          static constexpr const char *names[] = {"Number", "String", "Boolean", "Null",
                                                  "Undefined"};
          out << '(' << names[static_cast<unsigned>(node.kind)] << ' ' << node.spelling;
          range(out, expression->range, showRanges);
          out << ')';
        } else if constexpr (std::is_same_v<T, IdentifierExpr>) {
          out << "(Identifier " << node.name;
          range(out, expression->range, showRanges);
          out << ')';
        } else if constexpr (std::is_same_v<T, UnaryExpr>) {
          out << "(Unary " << node.op << ' ';
          printExpr(node.operand, out, depth + 1, showRanges);
          range(out, expression->range, showRanges);
          out << ')';
        } else if constexpr (std::is_same_v<T, BinaryExpr>) {
          out << "(Binary " << node.op << ' ';
          printExpr(node.left, out, depth + 1, showRanges);
          out << ' ';
          printExpr(node.right, out, depth + 1, showRanges);
          range(out, expression->range, showRanges);
          out << ')';
        } else if constexpr (std::is_same_v<T, AssignmentExpr>) {
          out << "(Assignment " << node.op << ' ';
          printExpr(node.target, out, depth + 1, showRanges);
          out << ' ';
          printExpr(node.value, out, depth + 1, showRanges);
          range(out, expression->range, showRanges);
          out << ')';
        } else if constexpr (std::is_same_v<T, MemberExpr>) {
          out << "(Member ";
          printExpr(node.object, out, depth + 1, showRanges);
          if (node.computed) {
            out << " [";
            printExpr(node.computedProperty, out, depth + 1, showRanges);
            out << ']';
          } else
            out << ' ' << node.property;
          range(out, expression->range, showRanges);
          out << ')';
        } else if constexpr (std::is_same_v<T, CallExpr>) {
          out << "(Call ";
          printExpr(node.callee, out, depth + 1, showRanges);
          for (const auto &argument : node.arguments) {
            out << ' ';
            printExpr(argument, out, depth + 1, showRanges);
          }
          range(out, expression->range, showRanges);
          out << ')';
        } else if constexpr (std::is_same_v<T, GenericExpr>) {
          out << "(GenericExpr " << node.kind;
          for (const auto &child : node.children) {
            out << ' ';
            printExpr(child, out, depth + 1, showRanges);
          }
          range(out, expression->range, showRanges);
          out << ')';
        } else if constexpr (std::is_same_v<T, ArrayExpr>) {
          out << "(Array";
          for (const auto &element : node.elements) {
            out << ' ';
            printExpr(element, out, depth + 1, showRanges);
          }
          range(out, expression->range, showRanges);
          out << ')';
        }
      },
      expression->node);
}

void printStatement(const Statement &statement, std::ostream &out, unsigned depth,
                    bool showRanges) {
  indent(out, depth);
  std::visit(
      [&](const auto &node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, VariableDeclaration>) {
          out << "(Declaration " << node.kind;
          for (const auto &item : node.declarations) {
            out << "\n";
            indent(out, depth + 1);
            out << "(Binding " << item.name;
            if (item.initializer) {
              out << ' ';
              printExpr(item.initializer, out, depth + 2, showRanges);
            }
            out << ')';
          }
          range(out, statement.range, showRanges);
          out << ')';
        } else if constexpr (std::is_same_v<T, ExpressionStatement>) {
          out << "(ExpressionStatement ";
          printExpr(node.expression, out, depth + 1, showRanges);
          range(out, statement.range, showRanges);
          out << ')';
        } else if constexpr (std::is_same_v<T, FunctionDeclaration>) {
          out << "(Function " << node.name << " (Parameters";
          for (const auto &parameter : node.parameters) out << ' ' << parameter;
          out << ')';
          for (const auto &child : node.body) {
            out << '\n';
            printStatement(child, out, depth + 1, showRanges);
          }
          range(out, statement.range, showRanges);
          out << ')';
        } else if constexpr (std::is_same_v<T, ReturnStatement>) {
          out << "(Return";
          if (node.argument) {
            out << ' ';
            printExpr(node.argument, out, depth + 1, showRanges);
          }
          range(out, statement.range, showRanges);
          out << ')';
        } else if constexpr (std::is_same_v<T, BlockStatement>) {
          out << "(Block";
          for (const auto &child : node.statements) {
            out << '\n';
            printStatement(child, out, depth + 1, showRanges);
          }
          range(out, statement.range, showRanges);
          out << ')';
        } else if constexpr (std::is_same_v<T, GenericStatement>) {
          out << "(GenericStatement " << node.kind;
          for (const auto &expression : node.expressions) {
            out << ' ';
            printExpr(expression, out, depth + 1, showRanges);
          }
          for (const auto &child : node.statements) {
            out << '\n';
            printStatement(child, out, depth + 1, showRanges);
          }
          range(out, statement.range, showRanges);
          out << ')';
        } else if constexpr (std::is_same_v<T, IfStatement>) {
          out << "(If";
          out << '\n';
          indent(out, depth + 1);
          out << "(Condition ";
          printExpr(node.condition, out, depth + 2, showRanges);
          out << ')';
          for (const auto &child : node.consequent) {
            out << '\n';
            printStatement(child, out, depth + 1, showRanges);
          }
          if (!node.alternate.empty()) {
            out << '\n';
            indent(out, depth + 1);
            out << "(Alternate";
            for (const auto &child : node.alternate) {
              out << '\n';
              printStatement(child, out, depth + 2, showRanges);
            }
            out << ')';
          }
          range(out, statement.range, showRanges);
          out << ')';
        } else if constexpr (std::is_same_v<T, WhileStatement>) {
          out << "(While";
          out << '\n';
          indent(out, depth + 1);
          out << "(Condition ";
          printExpr(node.condition, out, depth + 2, showRanges);
          out << ')';
          for (const auto &child : node.body) {
            out << '\n';
            printStatement(child, out, depth + 1, showRanges);
          }
          range(out, statement.range, showRanges);
          out << ')';
        } else if constexpr (std::is_same_v<T, ForStatement>) {
          out << "(For";
          if (node.init) {
            out << '\n';
            indent(out, depth + 1);
            out << "(Init ";
            const auto &decl = *node.init;
            out << decl.kind;
            for (const auto &item : decl.declarations) {
              out << ' ' << item.name;
              if (item.initializer) {
                out << '=';
                printExpr(item.initializer, out, depth + 2, showRanges);
              }
            }
            out << ')';
          }
          if (node.condition) {
            out << '\n';
            indent(out, depth + 1);
            out << "(Condition ";
            printExpr(node.condition, out, depth + 2, showRanges);
            out << ')';
          }
          if (node.update) {
            out << '\n';
            indent(out, depth + 1);
            out << "(Update ";
            printExpr(node.update, out, depth + 2, showRanges);
            out << ')';
          }
          for (const auto &child : node.body) {
            out << '\n';
            printStatement(child, out, depth + 1, showRanges);
          }
          range(out, statement.range, showRanges);
          out << ')';
        } else if constexpr (std::is_same_v<T, BreakStatement>) {
          out << "(Break";
          if (!node.label.empty()) out << ' ' << node.label;
          range(out, statement.range, showRanges);
          out << ')';
        } else if constexpr (std::is_same_v<T, ContinueStatement>) {
          out << "(Continue";
          if (!node.label.empty()) out << ' ' << node.label;
          range(out, statement.range, showRanges);
          out << ')';
        }
      },
      statement.node);
}
} // namespace

void AstPrinter::print(const Program &program, std::ostream &out, bool showRanges) const {
  out << "(Program";
  for (const auto &statement : program.statements) {
    out << '\n';
    printStatement(statement, out, 1, showRanges);
  }
  range(out, program.range, showRanges);
  out << ")\n";
}

} // namespace rma::compiler
