#include "emit/codegen.hh"

#include "builtins/builtin.hh"
#include "emit/platform.hh"

#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <type_traits>

namespace jsmm::emit {
namespace {

using types::TypeKind;
using types::TypePtr;

const std::uint64_t oneBits = 0x3FF0000000000000ULL;

constexpr long long unionTagUndefined = 0;
constexpr long long unionTagNull = 1;
constexpr long long unionTagPayload = 2;
constexpr long long unionCellSize = 16;

long long alignFrame(long long bytes) { return (bytes + 15) / 16 * 16; }

std::string typeofName(TypePtr type) {
  switch (type->kind) {
  case TypeKind::Number: return "number";
  case TypeKind::String: return "string";
  case TypeKind::Boolean: return "boolean";
  case TypeKind::Null: return "object";
  case TypeKind::Undefined: return "undefined";
  case TypeKind::Never: return "undefined";
  case TypeKind::Function: return "function";
  case TypeKind::Struct: return "object";
  case TypeKind::Array: return "object";
  case TypeKind::Union: return "object";
  default: return "undefined";
  }
}

} // namespace

x64::Assembly &CodeGenerator::out() { return *out_; }

std::string CodeGenerator::newLabel(const std::string &prefix) {
  return prefix + std::to_string(labelCounter_++);
}

std::string CodeGenerator::internString(const std::string &content) {
  const auto found = stringLiterals_.find(content);
  if (found != stringLiterals_.end()) return found->second;
  const std::string label = newLabel(".Lstr");
  std::vector<std::uint8_t> data(content.begin(), content.end());
  rodataStrings_.emplace_back(label, data);
  stringLiterals_.emplace(content, label);
  return label;
}

void CodeGenerator::beginScope() { scopes_.emplace_back(); }

void CodeGenerator::endScope() { scopes_.pop_back(); }

CodeGenerator::Slot *CodeGenerator::lookupSlot(const std::string &name) {
  for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
    const auto found = scope->slots.find(name);
    if (found != scope->slots.end()) return &found->second;
  }
  return nullptr;
}

long long CodeGenerator::topLevelOffset(const std::string &name) {
  const auto found = topLevelOffsets_.find(name);
  if (found == topLevelOffsets_.end()) return -1;
  return found->second;
}

bool CodeGenerator::topLevelIsNumber(const std::string &name) {
  const auto found = topLevelIsNumber_.find(name);
  if (found == topLevelIsNumber_.end()) return false;
  return found->second;
}

long long CodeGenerator::allocateSlot() {
  if (!freeSlots_.empty()) {
    const long long offset = freeSlots_.back();
    freeSlots_.pop_back();
    return offset;
  }
  ++slotCount_;
  return -8 * slotCount_;
}

void CodeGenerator::releaseSlot(long long offset) { freeSlots_.push_back(offset); }

long long CodeGenerator::pushTemp() {
  const long long offset = allocateSlot();
  temps_.push_back(offset);
  return offset;
}

void CodeGenerator::popTemp() {
  releaseSlot(temps_.back());
  temps_.pop_back();
}

long long CodeGenerator::tempOffset(std::size_t depth) const {
  return temps_[temps_.size() - 1 - depth];
}

void CodeGenerator::storeValue(long long offset, TypePtr type) {
  if (isNumber(type)) out().movsdOffsetXmm(x64::Reg::Rbp, offset, x64::Xmm::X0);
  else
    out().movOffsetReg(x64::Reg::Rbp, offset, x64::Reg::Rax);
}

void CodeGenerator::loadGp(long long offset, x64::Reg reg) {
  out().movRegOffset(reg, x64::Reg::Rbp, offset);
}

void CodeGenerator::loadXmm(long long offset, x64::Xmm reg) {
  out().movsdXmmOffset(reg, x64::Reg::Rbp, offset);
}

void CodeGenerator::emitZeroValue(TypePtr type) {
  const TypePtr resolved = context_->resolve(std::move(type));
  if (resolved == nullptr) {
    out().xorRegReg(x64::Reg::Rax, x64::Reg::Rax);
    return;
  }
  switch (resolved->kind) {
  case TypeKind::Number:
    out().xorRegReg(x64::Reg::Rax, x64::Reg::Rax);
    out().pxor(x64::Xmm::X0, x64::Xmm::X0);
    return;
  case TypeKind::String:
    helpers_.use("literals");
    out().leaLabel(x64::Reg::Rax, "jsmm_literal_empty_string");
    return;
  case TypeKind::Array: {
    helpers_.use(std::string(allocSymbol));
    out().movRegImm(x64::Reg::Rdi, 8);
    out().call(allocSymbol);
    out().movRegImm(x64::Reg::Rcx, 0);
    out().movOffsetReg(x64::Reg::Rax, 0, x64::Reg::Rcx);
    return;
  }
  case TypeKind::Struct: {
    const std::size_t fieldCount = resolved->fields.size();
    helpers_.use(std::string(allocSymbol));
    out().movRegImm(x64::Reg::Rdi, 8 * fieldCount);
    out().call(allocSymbol);
    const long long base = pushTemp();
    out().movOffsetReg(x64::Reg::Rbp, base, x64::Reg::Rax);
    for (std::size_t index = 0; index < fieldCount; ++index) {
      const TypePtr &fieldType = resolved->fields[index].second;
      emitZeroValue(fieldType);
      loadGp(base, x64::Reg::R11);
      const long long displacement = static_cast<long long>(8 * index);
      if (isNumber(fieldType)) out().movsdOffsetXmm(x64::Reg::R11, displacement, x64::Xmm::X0);
      else
        out().movOffsetReg(x64::Reg::R11, displacement, x64::Reg::Rax);
    }
    loadGp(base, x64::Reg::Rax);
    popTemp();
    return;
  }
  case TypeKind::Union: {
    long long tag = unionTagPayload;
    for (const TypePtr &member : resolved->members) {
      const TypePtr memberResolved = context_->resolve(member);
      if (memberResolved == nullptr) continue;
      if (memberResolved->kind == TypeKind::Undefined) {
        tag = unionTagUndefined;
        break;
      }
      if (memberResolved->kind == TypeKind::Null && tag == unionTagPayload) tag = unionTagNull;
    }
    emitNullishCell(tag);
    return;
  }
  default: out().xorRegReg(x64::Reg::Rax, x64::Reg::Rax); return;
  }
}

void CodeGenerator::emitBox(TypePtr payloadType) {
  const bool numeric = isNumber(payloadType);
  const long long temp = pushTemp();
  if (numeric) out().movsdOffsetXmm(x64::Reg::Rbp, temp, x64::Xmm::X0);
  else
    out().movOffsetReg(x64::Reg::Rbp, temp, x64::Reg::Rax);
  helpers_.use(std::string(allocSymbol));
  out().movRegImm(x64::Reg::Rdi, unionCellSize);
  out().call(allocSymbol);
  out().movRegReg(x64::Reg::R10, x64::Reg::Rax);
  if (numeric) {
    out().movsdXmmOffset(x64::Xmm::X0, x64::Reg::Rbp, temp);
    out().movqRegXmm(x64::Reg::Rax, x64::Xmm::X0);
  } else {
    loadGp(temp, x64::Reg::Rax);
  }
  out().movOffsetReg(x64::Reg::R10, 8, x64::Reg::Rax);
  out().movRegImm(x64::Reg::Rax, unionTagPayload);
  out().movOffsetReg(x64::Reg::R10, 0, x64::Reg::Rax);
  out().movRegReg(x64::Reg::Rax, x64::Reg::R10);
  popTemp();
}

void CodeGenerator::emitNullishCell(long long tag) {
  helpers_.use(std::string(allocSymbol));
  out().movRegImm(x64::Reg::Rdi, unionCellSize);
  out().call(allocSymbol);
  out().movRegReg(x64::Reg::R10, x64::Reg::Rax);
  out().xorRegReg(x64::Reg::Rax, x64::Reg::Rax);
  out().movOffsetReg(x64::Reg::R10, 8, x64::Reg::Rax);
  out().movRegImm(x64::Reg::Rax, static_cast<std::uint64_t>(tag));
  out().movOffsetReg(x64::Reg::R10, 0, x64::Reg::Rax);
  out().movRegReg(x64::Reg::Rax, x64::Reg::R10);
}

void CodeGenerator::emitCoerce(TypePtr destinationType, TypePtr valueType) {
  const TypePtr destination = context_->resolve(std::move(destinationType));
  const TypePtr value = context_->resolve(std::move(valueType));
  if (destination == nullptr || value == nullptr) return;
  if (destination->kind != TypeKind::Union) return;
  if (value->kind == TypeKind::Union) return;
  if (value->kind == TypeKind::Null) {
    emitNullishCell(unionTagNull);
    return;
  }
  if (value->kind == TypeKind::Undefined) {
    emitNullishCell(unionTagUndefined);
    return;
  }
  emitBox(unionPayload(destination));
}

TypePtr CodeGenerator::typeOf(const reader::ExprPtr &expr) const {
  return annotations_->expressionTypes.at(expr.get());
}

bool CodeGenerator::isNumber(TypePtr type) const {
  type = context_->resolve(std::move(type));
  return type != nullptr && type->kind == TypeKind::Number;
}

TypePtr CodeGenerator::unionPayload(TypePtr type) const {
  type = context_->resolve(std::move(type));
  if (type == nullptr || type->kind != TypeKind::Union) return nullptr;
  for (const TypePtr &member : type->members) {
    const TypePtr resolved = context_->resolve(member);
    if (resolved == nullptr) continue;
    if (resolved->kind == TypeKind::Null || resolved->kind == TypeKind::Undefined) continue;
    return resolved;
  }
  return nullptr;
}

std::uint64_t CodeGenerator::numberBits(const std::string &spelling) {
  double value = 0.0;
  if (spelling.size() > 2 && spelling[0] == '0' && (spelling[1] == 'x' || spelling[1] == 'X')) {
    value = static_cast<double>(std::strtoull(spelling.c_str() + 2, nullptr, 16));
  } else if (spelling.size() > 2 && spelling[0] == '0' &&
             (spelling[1] == 'b' || spelling[1] == 'B')) {
    unsigned long long result = 0;
    for (std::size_t i = 2; i < spelling.size(); ++i)
      result = result * 2ULL + static_cast<unsigned long long>(spelling[i] - '0');
    value = static_cast<double>(result);
  } else if (spelling.size() > 2 && spelling[0] == '0' &&
             (spelling[1] == 'o' || spelling[1] == 'O')) {
    unsigned long long result = 0;
    for (std::size_t i = 2; i < spelling.size(); ++i)
      result = result * 8ULL + static_cast<unsigned long long>(spelling[i] - '0');
    value = static_cast<double>(result);
  } else {
    value = std::strtod(spelling.c_str(), nullptr);
  }
  std::uint64_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

long long CodeGenerator::slotForDeclaration(const std::string &name) {
  if (generatingEntry_ && scopes_.size() == 1) {
    const auto found = entrySlots_.find(name);
    if (found != entrySlots_.end()) return found->second;
  }
  return allocateSlot();
}

void CodeGenerator::generateDeclaration(const reader::VariableDeclaration &declaration) {
  for (const auto &declarator : declaration.declarations) {
    Scope &target = (declaration.kind == "var" && varTargetScopeIndex_ >= 0)
                        ? scopes_[varTargetScopeIndex_]
                        : scopes_.back();
    const auto existing = target.slots.find(declarator.name);
    const bool reuseExisting = declaration.kind == "var" && existing != target.slots.end();
    const long long offset =
        reuseExisting ? existing->second.offset : slotForDeclaration(declarator.name);
    if (declarator.initializer != nullptr) {
      generateExpression(declarator.initializer);
      TypePtr storedType = typeOf(declarator.initializer);
      const auto declared = annotations_->declaredTypes.find(&declarator);
      if (declared != annotations_->declaredTypes.end()) {
        emitCoerce(declared->second, storedType);
        storedType = declared->second;
      }
      target.slots.insert_or_assign(declarator.name, Slot{offset, declaration.kind == "const"});
      storeValue(offset, storedType);
      if (generatingEntry_) {
        topLevelOffsets_.emplace(declarator.name, offset);
        topLevelIsNumber_.emplace(declarator.name, isNumber(storedType));
      }
    } else {
      target.slots.insert_or_assign(declarator.name, Slot{offset, false});
      TypePtr declaredType;
      const auto found = annotations_->declarationTypes.find(&declarator);
      if (found != annotations_->declarationTypes.end()) declaredType = found->second;
      if (declaredType != nullptr) emitZeroValue(declaredType);
      else
        out().xorRegReg(x64::Reg::Rax, x64::Reg::Rax);
      storeValue(offset, declaredType != nullptr ? declaredType : context_->undefined());
      if (generatingEntry_) {
        topLevelOffsets_.emplace(declarator.name, offset);
        topLevelIsNumber_.emplace(declarator.name, isNumber(declaredType));
      }
    }
  }
}

void CodeGenerator::generateStatements(const reader::StatementList &statements) {
  for (const auto &statement : statements) generateStatement(statement);
}

void CodeGenerator::generateStatement(const reader::Statement &statement) {
  std::visit(
      [&](const auto &node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, reader::VariableDeclaration>) {
          generateDeclaration(node);
        } else if constexpr (std::is_same_v<T, reader::ExpressionStatement>) {
          generateExpression(node.expression);
        } else if constexpr (std::is_same_v<T, reader::FunctionDeclaration>) {
        } else if constexpr (std::is_same_v<T, reader::ReturnStatement>) {
          if (node.argument != nullptr) {
            generateExpression(node.argument);
            emitCoerce(state_.resultType, typeOf(node.argument));
            if (isNumber(state_.resultType)) out().movqRegXmm(x64::Reg::Rax, x64::Xmm::X0);
          } else {
            out().xorRegReg(x64::Reg::Rax, x64::Reg::Rax);
          }
          out().jmp(state_.returnLabel);
        } else if constexpr (std::is_same_v<T, reader::BlockStatement>) {
          beginScope();
          generateStatements(node.statements);
          endScope();
        } else if constexpr (std::is_same_v<T, reader::IfStatement>) {
          const std::string elseLabel = newLabel(".Lelse");
          const std::string endLabel = newLabel(".Lendif");
          generateExpression(node.condition);
          out().testRegReg(x64::Reg::Rax, x64::Reg::Rax);
          out().jcc(x64::Cond::Z, node.alternate.empty() ? endLabel : elseLabel);
          beginScope();
          generateStatements(node.consequent);
          endScope();
          if (!node.alternate.empty()) {
            out().jmp(endLabel);
            out().label(elseLabel);
            beginScope();
            generateStatements(node.alternate);
            endScope();
          }
          out().label(endLabel);
        } else if constexpr (std::is_same_v<T, reader::WhileStatement>) {
          const std::string conditionLabel = newLabel(".Lwhile");
          const std::string endLabel = newLabel(".Lwhileend");
          out().label(conditionLabel);
          generateExpression(node.condition);
          out().testRegReg(x64::Reg::Rax, x64::Reg::Rax);
          out().jcc(x64::Cond::Z, endLabel);
          state_.loops.push_back({endLabel, conditionLabel});
          beginScope();
          generateStatements(node.body);
          endScope();
          state_.loops.pop_back();
          out().jmp(conditionLabel);
          out().label(endLabel);
        } else if constexpr (std::is_same_v<T, reader::ForStatement>) {
          const std::string conditionLabel = newLabel(".Lfor");
          const std::string bodyLabel = newLabel(".Lforbody");
          const std::string updateLabel = newLabel(".Lforupdate");
          const std::string endLabel = newLabel(".Lforend");
          beginScope();
          if (node.init != nullptr) generateDeclaration(*node.init);
          out().label(conditionLabel);
          if (node.condition != nullptr) {
            generateExpression(node.condition);
            out().testRegReg(x64::Reg::Rax, x64::Reg::Rax);
            out().jcc(x64::Cond::Z, endLabel);
          } else {
            out().jmp(bodyLabel);
          }
          out().label(bodyLabel);
          state_.loops.push_back({endLabel, updateLabel});
          generateStatements(node.body);
          state_.loops.pop_back();
          out().label(updateLabel);
          if (node.update != nullptr) generateExpression(node.update);
          out().jmp(conditionLabel);
          out().label(endLabel);
          endScope();
        } else if constexpr (std::is_same_v<T, reader::BreakStatement>) {
          out().jmp(state_.loops.back().breakLabel);
        } else if constexpr (std::is_same_v<T, reader::ContinueStatement>) {
          out().jmp(state_.loops.back().continueLabel);
        }
      },
      statement.node);
}

void CodeGenerator::generateExpression(const reader::ExprPtr &expr) {
  std::visit(
      [&](const auto &node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, reader::NumberLiteralExpr>) {
          out().movRegImm(x64::Reg::Rax, numberBits(node.spelling));
          out().movqXmmReg(x64::Xmm::X0, x64::Reg::Rax);
        } else if constexpr (std::is_same_v<T, reader::StringLiteralExpr>) {
          out().leaLabel(x64::Reg::Rax, internString(node.value));
        } else if constexpr (std::is_same_v<T, reader::BooleanLiteralExpr>) {
          out().movRegImm(x64::Reg::Rax, node.value ? 1 : 0);
        } else if constexpr (std::is_same_v<T, reader::NullLiteralExpr>) {
          out().xorRegReg(x64::Reg::Rax, x64::Reg::Rax);
        } else if constexpr (std::is_same_v<T, reader::UndefinedLiteralExpr>) {
          out().xorRegReg(x64::Reg::Rax, x64::Reg::Rax);
        } else if constexpr (std::is_same_v<T, reader::IdentifierExpr>) {
          if (Slot *slot = lookupSlot(node.name)) {
            if (isNumber(typeOf(expr))) loadXmm(slot->offset, x64::Xmm::X0);
            else
              loadGp(slot->offset, x64::Reg::Rax);
          } else {
            long long toff = topLevelOffset(node.name);
            if (toff != -1) {
              out().leaLabel(x64::Reg::R11, entryFrameSymbol);
              out().movRegOffset(x64::Reg::R11, x64::Reg::R11, 0);
              if (topLevelIsNumber(node.name))
                out().movsdXmmOffset(x64::Xmm::X0, x64::Reg::R11, toff);
              else
                out().movRegOffset(x64::Reg::Rax, x64::Reg::R11, toff);
            } else {
              out().leaLabel(x64::Reg::Rax, functions_.at(node.name));
            }
          }
        } else if constexpr (std::is_same_v<T, reader::UnaryExpr>) {
          generateUnary(node);
        } else if constexpr (std::is_same_v<T, reader::BinaryExpr>) {
          generateBinary(node);
        } else if constexpr (std::is_same_v<T, reader::UpdateExpr>) {
          generateUpdate(node);
        } else if constexpr (std::is_same_v<T, reader::AssignmentExpr>) {
          generateAssignment(node);
        } else if constexpr (std::is_same_v<T, reader::MemberExpr>) {
          generateMemberAddress(node);
          out().movRegReg(x64::Reg::R11, x64::Reg::Rax);
          if (isNumber(typeOf(expr))) out().movsdXmmOffset(x64::Xmm::X0, x64::Reg::R11, 0);
          else
            out().movRegOffset(x64::Reg::Rax, x64::Reg::R11, 0);
        } else if constexpr (std::is_same_v<T, reader::ObjectExpr>) {
          const std::uint64_t size = 8 * node.properties.size();
          out().movRegImm(x64::Reg::Rdi, size);
          helpers_.use(std::string(allocSymbol));
          out().call(allocSymbol);
          const long long temp = pushTemp();
          out().movOffsetReg(x64::Reg::Rbp, temp, x64::Reg::Rax);
          const TypePtr structType = context_->resolve(typeOf(expr));
          for (std::size_t i = 0; i < node.properties.size(); ++i) {
            generateExpression(node.properties[i].value);
            TypePtr fieldType = typeOf(node.properties[i].value);
            if (structType != nullptr && structType->kind == TypeKind::Struct &&
                i < structType->fields.size()) {
              emitCoerce(structType->fields[i].second, fieldType);
              fieldType = structType->fields[i].second;
            }
            loadGp(temp, x64::Reg::R11);
            const long long displacement = static_cast<long long>(8 * i);
            if (isNumber(fieldType))
              out().movsdOffsetXmm(x64::Reg::R11, displacement, x64::Xmm::X0);
            else
              out().movOffsetReg(x64::Reg::R11, displacement, x64::Reg::Rax);
          }
          loadGp(temp, x64::Reg::Rax);
          popTemp();
        } else if constexpr (std::is_same_v<T, reader::ArrayExpr>) {
          const std::uint64_t size = 8 + 8 * node.elements.size();
          out().movRegImm(x64::Reg::Rdi, size);
          helpers_.use(std::string(allocSymbol));
          out().call(allocSymbol);
          out().movRegImm(x64::Reg::Rcx, node.elements.size());
          out().movOffsetReg(x64::Reg::Rax, 0, x64::Reg::Rcx);
          const long long temp = pushTemp();
          out().movOffsetReg(x64::Reg::Rbp, temp, x64::Reg::Rax);
          const TypePtr arrayType = context_->resolve(typeOf(expr));
          const TypePtr elementType = arrayType != nullptr && arrayType->kind == TypeKind::Array
                                          ? arrayType->element
                                          : nullptr;
          for (std::size_t i = 0; i < node.elements.size(); ++i) {
            generateExpression(node.elements[i]);
            TypePtr storedType = typeOf(node.elements[i]);
            if (elementType != nullptr) {
              emitCoerce(elementType, storedType);
              storedType = elementType;
            }
            loadGp(temp, x64::Reg::R11);
            const long long displacement = static_cast<long long>(8 + 8 * i);
            if (isNumber(storedType))
              out().movsdOffsetXmm(x64::Reg::R11, displacement, x64::Xmm::X0);
            else
              out().movOffsetReg(x64::Reg::R11, displacement, x64::Reg::Rax);
          }
          loadGp(temp, x64::Reg::Rax);
          popTemp();
        } else if constexpr (std::is_same_v<T, reader::CallExpr>) {
          generateCall(expr, node);
        }
      },
      expr->node);
}

void CodeGenerator::generateUnary(const reader::UnaryExpr &unary) {
  if (unary.op == "!") {
    generateExpression(unary.operand);
    out().testRegReg(x64::Reg::Rax, x64::Reg::Rax);
    out().setCond(x64::Cond::E, x64::Reg::Rax);
    out().movzxByte(x64::Reg::Rax, x64::Reg::Rax);
    return;
  }
  if (unary.op == "~") {
    generateExpression(unary.operand);
    out().cvttsd2si(x64::Reg::Rax, x64::Xmm::X0);
    out().notReg(x64::Reg::Rax);
    out().cvtsi2sd(x64::Xmm::X0, x64::Reg::Rax);
    return;
  }
  if (unary.op == "-") {
    generateExpression(unary.operand);
    out().movqRegXmm(x64::Reg::Rax, x64::Xmm::X0);
    out().btcRegImm(x64::Reg::Rax, 63);
    out().movqXmmReg(x64::Xmm::X0, x64::Reg::Rax);
    return;
  }
  if (unary.op == "+") {
    generateExpression(unary.operand);
    return;
  }
  if (unary.op == "typeof") {
    generateExpression(unary.operand);
    const TypePtr operandType = context_->resolve(typeOf(unary.operand));
    out().leaLabel(x64::Reg::Rax, internString(typeofName(operandType)));
    return;
  }
  if (unary.op == "void") {
    generateExpression(unary.operand);
    out().xorRegReg(x64::Reg::Rax, x64::Reg::Rax);
  }
}

void CodeGenerator::generateBinary(const reader::BinaryExpr &binary) {
  if (binary.op == "==" || binary.op == "!=" || binary.op == "===" || binary.op == "!==") {
    generateEquality(binary);
    return;
  }
  if (binary.op == "&&" || binary.op == "||" || binary.op == "??") {
    generateLogical(binary);
    return;
  }
  static const std::vector<std::string> arithmetic = {"+", "-", "*", "/", "%", "**"};
  for (const auto &op : arithmetic)
    if (binary.op == op) {
      generateArithmetic(binary);
      return;
    }
  static const std::vector<std::string> relational = {"<", ">", "<=", ">="};
  for (const auto &op : relational)
    if (binary.op == op) {
      generateRelational(binary);
      return;
    }
  generateBitwise(binary);
}

void CodeGenerator::generateArithmetic(const reader::BinaryExpr &binary) {
  generateExpression(binary.left);
  const long long temp = pushTemp();
  storeValue(temp, typeOf(binary.left));
  generateExpression(binary.right);
  out().movsdXmmXmm(x64::Xmm::X1, x64::Xmm::X0);
  loadXmm(temp, x64::Xmm::X0);
  popTemp();
  if (binary.op == "+") out().addsd(x64::Xmm::X0, x64::Xmm::X1);
  if (binary.op == "-") out().subsd(x64::Xmm::X0, x64::Xmm::X1);
  if (binary.op == "*") out().mulsd(x64::Xmm::X0, x64::Xmm::X1);
  if (binary.op == "/") out().divsd(x64::Xmm::X0, x64::Xmm::X1);
  if (binary.op == "%") {
    helpers_.use(std::string(moduloSymbol));
    out().call(moduloSymbol);
  }
  if (binary.op == "**") {
    helpers_.use(std::string(powerSymbol));
    out().call(powerSymbol);
  }
}

void CodeGenerator::generateRelational(const reader::BinaryExpr &binary) {
  generateExpression(binary.left);
  const long long temp = pushTemp();
  storeValue(temp, typeOf(binary.left));
  generateExpression(binary.right);
  out().movsdXmmXmm(x64::Xmm::X1, x64::Xmm::X0);
  loadXmm(temp, x64::Xmm::X0);
  popTemp();
  out().ucomisd(x64::Xmm::X0, x64::Xmm::X1);
  if (binary.op == "<") {
    out().setCond(x64::Cond::B, x64::Reg::Rax);
    out().setCond(x64::Cond::NP, x64::Reg::Rcx);
    out().andRegReg(x64::Reg::Rax, x64::Reg::Rcx);
    out().movzxByte(x64::Reg::Rax, x64::Reg::Rax);
  } else if (binary.op == "<=") {
    out().setCond(x64::Cond::BE, x64::Reg::Rax);
    out().setCond(x64::Cond::NP, x64::Reg::Rcx);
    out().andRegReg(x64::Reg::Rax, x64::Reg::Rcx);
    out().movzxByte(x64::Reg::Rax, x64::Reg::Rax);
  } else if (binary.op == ">") {
    out().setCond(x64::Cond::A, x64::Reg::Rax);
    out().movzxByte(x64::Reg::Rax, x64::Reg::Rax);
  } else {
    out().setCond(x64::Cond::AE, x64::Reg::Rax);
    out().movzxByte(x64::Reg::Rax, x64::Reg::Rax);
  }
}

void CodeGenerator::generateBitwise(const reader::BinaryExpr &binary) {
  generateExpression(binary.left);
  const long long temp = pushTemp();
  storeValue(temp, typeOf(binary.left));
  generateExpression(binary.right);
  out().cvttsd2si(x64::Reg::Rcx, x64::Xmm::X0);
  loadXmm(temp, x64::Xmm::X0);
  out().cvttsd2si(x64::Reg::R10, x64::Xmm::X0);
  popTemp();
  if (binary.op == "&") out().andRegReg(x64::Reg::R10, x64::Reg::Rcx);
  if (binary.op == "|") out().orRegReg(x64::Reg::R10, x64::Reg::Rcx);
  if (binary.op == "^") out().xorRegReg(x64::Reg::R10, x64::Reg::Rcx);
  if (binary.op == "<<") out().shlCl(x64::Reg::R10);
  if (binary.op == ">>") out().sarCl(x64::Reg::R10);
  if (binary.op == ">>>") out().shrCl(x64::Reg::R10);
  out().cvtsi2sd(x64::Xmm::X0, x64::Reg::R10);
}

void CodeGenerator::generateEquality(const reader::BinaryExpr &binary) {
  const bool negated = binary.op == "!=" || binary.op == "!==";
  const TypePtr leftType = context_->resolve(typeOf(binary.left));
  const TypePtr rightType = context_->resolve(typeOf(binary.right));
  if (leftType != nullptr && leftType->kind == TypeKind::Number) {
    generateExpression(binary.left);
    const long long temp = pushTemp();
    storeValue(temp, typeOf(binary.left));
    generateExpression(binary.right);
    out().movsdXmmXmm(x64::Xmm::X1, x64::Xmm::X0);
    loadXmm(temp, x64::Xmm::X0);
    popTemp();
    out().ucomisd(x64::Xmm::X0, x64::Xmm::X1);
    if (negated) {
      out().setCond(x64::Cond::NE, x64::Reg::Rax);
      out().setCond(x64::Cond::P, x64::Reg::Rcx);
      out().orRegReg(x64::Reg::Rax, x64::Reg::Rcx);
      out().movzxByte(x64::Reg::Rax, x64::Reg::Rax);
    } else {
      out().setCond(x64::Cond::E, x64::Reg::Rax);
      out().setCond(x64::Cond::NP, x64::Reg::Rcx);
      out().andRegReg(x64::Reg::Rax, x64::Reg::Rcx);
      out().movzxByte(x64::Reg::Rax, x64::Reg::Rax);
    }
    return;
  }
  const bool leftUnion = leftType != nullptr && leftType->kind == TypeKind::Union;
  const bool rightUnion = rightType != nullptr && rightType->kind == TypeKind::Union;
  if (leftUnion || rightUnion) {
    const bool unionOnLeft = leftUnion;
    const reader::ExprPtr &unionSide = unionOnLeft ? binary.left : binary.right;
    const reader::ExprPtr &otherSide = unionOnLeft ? binary.right : binary.left;
    const TypePtr otherType = unionOnLeft ? rightType : leftType;
    const TypePtr unionType = unionOnLeft ? leftType : rightType;
    generateExpression(unionSide);
    const long long temp = pushTemp();
    storeValue(temp, typeOf(unionSide));
    generateExpression(otherSide);
    out().movRegReg(x64::Reg::R11, x64::Reg::Rax);
    loadGp(temp, x64::Reg::R10);
    popTemp();
    const std::string unequalLabel = newLabel(".Lequnequal");
    const std::string endLabel = newLabel(".Leqend");
    if (otherType != nullptr && otherType->kind == TypeKind::Union) {
      out().movRegOffset(x64::Reg::Rcx, x64::Reg::R10, 0);
      out().movRegOffset(x64::Reg::Rdx, x64::Reg::R11, 0);
      out().cmpRegReg(x64::Reg::Rcx, x64::Reg::Rdx);
      out().jcc(x64::Cond::NE, unequalLabel);
      const TypePtr payload = unionPayload(unionType);
      if (isNumber(payload)) {
        out().movRegOffset(x64::Reg::Rcx, x64::Reg::R10, 8);
        out().movqXmmReg(x64::Xmm::X0, x64::Reg::Rcx);
        out().movRegOffset(x64::Reg::Rcx, x64::Reg::R11, 8);
        out().movqXmmReg(x64::Xmm::X1, x64::Reg::Rcx);
        out().ucomisd(x64::Xmm::X0, x64::Xmm::X1);
        if (negated) {
          out().setCond(x64::Cond::NE, x64::Reg::Rax);
          out().setCond(x64::Cond::P, x64::Reg::Rcx);
          out().orRegReg(x64::Reg::Rax, x64::Reg::Rcx);
          out().movzxByte(x64::Reg::Rax, x64::Reg::Rax);
        } else {
          out().setCond(x64::Cond::E, x64::Reg::Rax);
          out().setCond(x64::Cond::NP, x64::Reg::Rcx);
          out().andRegReg(x64::Reg::Rax, x64::Reg::Rcx);
          out().movzxByte(x64::Reg::Rax, x64::Reg::Rax);
        }
      } else {
        out().movRegOffset(x64::Reg::Rcx, x64::Reg::R10, 8);
        out().movRegOffset(x64::Reg::Rdx, x64::Reg::R11, 8);
        out().cmpRegReg(x64::Reg::Rcx, x64::Reg::Rdx);
        out().setCond(negated ? x64::Cond::NE : x64::Cond::E, x64::Reg::Rax);
        out().movzxByte(x64::Reg::Rax, x64::Reg::Rax);
      }
      out().jmp(endLabel);
      out().label(unequalLabel);
      out().movRegImm(x64::Reg::Rax, negated ? 1 : 0);
      out().label(endLabel);
      return;
    }
    const long long expected = otherType != nullptr && otherType->kind == TypeKind::Null
                                   ? unionTagNull
                                   : unionTagUndefined;
    out().movRegOffset(x64::Reg::Rcx, x64::Reg::R10, 0);
    out().cmpRegImm(x64::Reg::Rcx, expected);
    out().setCond(negated ? x64::Cond::NE : x64::Cond::E, x64::Reg::Rax);
    out().movzxByte(x64::Reg::Rax, x64::Reg::Rax);
    return;
  }
  generateExpression(binary.left);
  const long long temp = pushTemp();
  storeValue(temp, typeOf(binary.left));
  generateExpression(binary.right);
  out().movRegReg(x64::Reg::R11, x64::Reg::Rax);
  loadGp(temp, x64::Reg::R10);
  popTemp();
  out().cmpRegReg(x64::Reg::R10, x64::Reg::R11);
  out().setCond(negated ? x64::Cond::NE : x64::Cond::E, x64::Reg::Rax);
  out().movzxByte(x64::Reg::Rax, x64::Reg::Rax);
}

void CodeGenerator::generateLogical(const reader::BinaryExpr &binary) {
  const TypePtr leftType = context_->resolve(typeOf(binary.left));
  if (binary.op == "??") {
    if (leftType != nullptr && leftType->kind == TypeKind::Union) {
      const std::string rightLabel = newLabel(".Lcoalesce");
      const std::string endLabel = newLabel(".Lcoalesceend");
      generateExpression(binary.left);
      const long long temp = pushTemp();
      storeValue(temp, typeOf(binary.left));
      out().movRegOffset(x64::Reg::Rcx, x64::Reg::Rax, 0);
      out().cmpRegImm(x64::Reg::Rcx, unionTagPayload);
      out().jcc(x64::Cond::B, rightLabel);
      loadGp(temp, x64::Reg::Rax);
      out().movRegOffset(x64::Reg::Rax, x64::Reg::Rax, 8);
      const TypePtr payload = unionPayload(leftType);
      if (isNumber(payload)) out().movqXmmReg(x64::Xmm::X0, x64::Reg::Rax);
      out().jmp(endLabel);
      out().label(rightLabel);
      generateExpression(binary.right);
      out().label(endLabel);
      popTemp();
      return;
    }
    if (leftType != nullptr &&
        (leftType->kind == TypeKind::Null || leftType->kind == TypeKind::Undefined)) {
      generateExpression(binary.left);
      generateExpression(binary.right);
      return;
    }
    generateExpression(binary.left);
    return;
  }
  const std::string fallbackLabel = newLabel(".Llogical");
  const std::string endLabel = newLabel(".Llogicalend");
  generateExpression(binary.left);
  const long long temp = pushTemp();
  storeValue(temp, typeOf(binary.left));
  out().testRegReg(x64::Reg::Rax, x64::Reg::Rax);
  out().jcc(binary.op == "&&" ? x64::Cond::Z : x64::Cond::NZ, fallbackLabel);
  generateExpression(binary.right);
  out().jmp(endLabel);
  out().label(fallbackLabel);
  loadGp(temp, x64::Reg::Rax);
  out().label(endLabel);
  popTemp();
}

void CodeGenerator::generateUpdate(const reader::UpdateExpr &update) {
  const auto *member = std::get_if<reader::MemberExpr>(&update.target->node);
  const bool decrement = update.op == "--";
  const bool memberTarget = member != nullptr;
  long long addressTemp = 0;
  long long targetOffset = 0;
  x64::Reg baseReg = x64::Reg::Rbp;
  if (!memberTarget) {
    const auto &identifier = std::get<reader::IdentifierExpr>(update.target->node);
    if (Slot *slot = lookupSlot(identifier.name)) {
      targetOffset = slot->offset;
    } else {
      long long toff = topLevelOffset(identifier.name);
      if (toff != -1) {
        loadEntryFrame(x64::Reg::R11);
        targetOffset = toff;
        baseReg = x64::Reg::R11;
      } else {
        targetOffset = 0;
      }
    }
  } else {
    generateMemberAddress(*member);
    addressTemp = pushTemp();
    out().movOffsetReg(x64::Reg::Rbp, addressTemp, x64::Reg::Rax);
  }

  if (memberTarget) {
    loadGp(addressTemp, x64::Reg::R11);
    out().movsdXmmOffset(x64::Xmm::X0, x64::Reg::R11, 0);
  } else {
    out().movsdXmmOffset(x64::Xmm::X0, baseReg, targetOffset);
  }
  const long long oldTemp = pushTemp();
  storeValue(oldTemp, context_->number());
  out().movRegImm(x64::Reg::Rax, oneBits);
  out().movqXmmReg(x64::Xmm::X2, x64::Reg::Rax);
  out().movsdXmmXmm(x64::Xmm::X1, x64::Xmm::X0);
  if (decrement) out().subsd(x64::Xmm::X1, x64::Xmm::X2);
  else
    out().addsd(x64::Xmm::X1, x64::Xmm::X2);
  if (memberTarget) {
    loadGp(addressTemp, x64::Reg::R11);
    out().movsdOffsetXmm(x64::Reg::R11, 0, x64::Xmm::X1);
  } else {
    out().movsdOffsetXmm(baseReg, targetOffset, x64::Xmm::X1);
  }
  popTemp();
  if (memberTarget) { popTemp(); }
  if (update.prefix) out().movsdXmmXmm(x64::Xmm::X0, x64::Xmm::X1);
}

void CodeGenerator::loadEntryFrame(x64::Reg reg) {
  out().leaLabel(reg, entryFrameSymbol);
  out().movRegOffset(reg, reg, 0);
}

void CodeGenerator::generateAssignment(const reader::AssignmentExpr &assignment) {
  const auto *member = std::get_if<reader::MemberExpr>(&assignment.target->node);
  if (member == nullptr) {
    const auto &identifier = std::get<reader::IdentifierExpr>(assignment.target->node);
    const Slot *slot = lookupSlot(identifier.name);
    const bool local = slot != nullptr;
    const long long localOffset = local ? slot->offset : 0;
    const long long topOffset = local ? -1 : topLevelOffset(identifier.name);
    if (assignment.op == "=") {
      generateExpression(assignment.value);
      emitCoerce(typeOf(assignment.target), typeOf(assignment.value));
      const TypePtr storedType = typeOf(assignment.target);
      if (local) {
        storeValue(localOffset, storedType);
      } else if (topOffset != -1) {
        loadEntryFrame(x64::Reg::R11);
        if (isNumber(storedType)) out().movsdOffsetXmm(x64::Reg::R11, topOffset, x64::Xmm::X0);
        else
          out().movOffsetReg(x64::Reg::R11, topOffset, x64::Reg::Rax);
      }
      return;
    }
    if (local) {
      loadXmm(localOffset, x64::Xmm::X0);
    } else if (topOffset != -1) {
      loadEntryFrame(x64::Reg::R11);
      out().movsdXmmOffset(x64::Xmm::X0, x64::Reg::R11, topOffset);
    }
    const long long temp = pushTemp();
    storeValue(temp, context_->number());
    generateExpression(assignment.value);
    out().movsdXmmXmm(x64::Xmm::X2, x64::Xmm::X0);
    loadXmm(temp, x64::Xmm::X0);
    popTemp();
    if (assignment.op == "+=") out().addsd(x64::Xmm::X0, x64::Xmm::X2);
    if (assignment.op == "-=") out().subsd(x64::Xmm::X0, x64::Xmm::X2);
    if (assignment.op == "*=") out().mulsd(x64::Xmm::X0, x64::Xmm::X2);
    if (assignment.op == "/=") out().divsd(x64::Xmm::X0, x64::Xmm::X2);
    if (assignment.op == "%=") {
      out().movsdXmmXmm(x64::Xmm::X1, x64::Xmm::X2);
      helpers_.use(std::string(moduloSymbol));
      out().call(moduloSymbol);
    }
    if (local) {
      out().movsdOffsetXmm(x64::Reg::Rbp, localOffset, x64::Xmm::X0);
    } else if (topOffset != -1) {
      loadEntryFrame(x64::Reg::R11);
      out().movsdOffsetXmm(x64::Reg::R11, topOffset, x64::Xmm::X0);
    }
    return;
  }

  generateMemberAddress(*member);
  const long long addressTemp = pushTemp();
  out().movOffsetReg(x64::Reg::Rbp, addressTemp, x64::Reg::Rax);
  if (assignment.op == "=") {
    generateExpression(assignment.value);
    emitCoerce(typeOf(assignment.target), typeOf(assignment.value));
    const TypePtr storedType = typeOf(assignment.target);
    loadGp(addressTemp, x64::Reg::R11);
    if (isNumber(storedType)) out().movsdOffsetXmm(x64::Reg::R11, 0, x64::Xmm::X0);
    else
      out().movOffsetReg(x64::Reg::R11, 0, x64::Reg::Rax);
    popTemp();
    return;
  }
  loadGp(addressTemp, x64::Reg::R11);
  out().movsdXmmOffset(x64::Xmm::X0, x64::Reg::R11, 0);
  const long long temp = pushTemp();
  storeValue(temp, context_->number());
  generateExpression(assignment.value);
  out().movsdXmmXmm(x64::Xmm::X2, x64::Xmm::X0);
  loadXmm(temp, x64::Xmm::X0);
  popTemp();
  if (assignment.op == "+=") out().addsd(x64::Xmm::X0, x64::Xmm::X2);
  if (assignment.op == "-=") out().subsd(x64::Xmm::X0, x64::Xmm::X2);
  if (assignment.op == "*=") out().mulsd(x64::Xmm::X0, x64::Xmm::X2);
  if (assignment.op == "/=") out().divsd(x64::Xmm::X0, x64::Xmm::X2);
  if (assignment.op == "%=") {
    out().movsdXmmXmm(x64::Xmm::X1, x64::Xmm::X2);
    helpers_.use(std::string(moduloSymbol));
    out().call(moduloSymbol);
  }
  loadGp(addressTemp, x64::Reg::R11);
  out().movsdOffsetXmm(x64::Reg::R11, 0, x64::Xmm::X0);
  popTemp();
}

void CodeGenerator::generateMemberAddress(const reader::MemberExpr &member) {
  const TypePtr objectType = context_->resolve(typeOf(member.object));
  if (objectType != nullptr && objectType->kind == TypeKind::Array) {
    generateExpression(member.object);
    const long long temp = pushTemp();
    out().movOffsetReg(x64::Reg::Rbp, temp, x64::Reg::Rax);
    generateExpression(member.computedProperty);
    out().cvttsd2si(x64::Reg::Rax, x64::Xmm::X0);
    loadGp(temp, x64::Reg::R11);
    out().movRegOffset(x64::Reg::Rcx, x64::Reg::R11, 0);
    out().cmpRegReg(x64::Reg::Rax, x64::Reg::Rcx);
    const std::string inBounds = newLabel(".Linbounds");
    out().jcc(x64::Cond::B, inBounds);
    helpers_.use(std::string(boundsSymbol));
    out().call(boundsSymbol);
    out().label(inBounds);
    out().movRegImm(x64::Reg::Rcx, 8);
    out().imulRegReg(x64::Reg::Rax, x64::Reg::Rcx);
    out().addRegReg(x64::Reg::Rax, x64::Reg::R11);
    out().addRegImm(x64::Reg::Rax, 8);
    popTemp();
    return;
  }
  generateExpression(member.object);
  const auto found = annotations_->structFieldIndices.find(&member);
  out().addRegImm(x64::Reg::Rax, static_cast<long long>(8 * found->second));
}

void CodeGenerator::generateArguments(const std::vector<reader::ExprPtr> &arguments) {
  for (const auto &argument : arguments) {
    generateExpression(argument);
    const auto destination = annotations_->argumentTypes.find(argument.get());
    TypePtr storedType = typeOf(argument);
    if (destination != annotations_->argumentTypes.end()) {
      emitCoerce(destination->second, storedType);
      storedType = destination->second;
    }
    const long long temp = pushTemp();
    storeValue(temp, storedType);
  }
  const std::size_t count = arguments.size();
  const bool padded = count % 2 == 1;
  if (padded) out().push(x64::Reg::Rax);
  for (std::size_t i = count; i > 0; --i) {
    loadGp(tempOffset(count - i), x64::Reg::Rax);
    out().push(x64::Reg::Rax);
  }
}

void CodeGenerator::emitArgument(const reader::ExprPtr &argument) { generateExpression(argument); }

void CodeGenerator::moveResultToArgumentRegister() {
  out().movRegReg(x64::Reg::Rdi, x64::Reg::Rax);
}

void CodeGenerator::declareHelper(std::string symbol, std::string text) {
  helpers_.declare(std::move(symbol), std::move(text));
}

void CodeGenerator::useHelper(const std::string &symbol) { helpers_.use(symbol); }

void CodeGenerator::callHelper(const std::string &symbol) { out().call(symbol); }

void CodeGenerator::clearResult() { out().xorRegReg(x64::Reg::Rax, x64::Reg::Rax); }

void CodeGenerator::formatValue(const reader::ExprPtr &expression) {
  const TypePtr resolved = context_->resolve(typeOf(expression));
  if (resolved != nullptr && resolved->kind == TypeKind::Array) {
    formatArray(expression);
    return;
  }
  generateExpression(expression);
  formatByType(resolved);
}

void CodeGenerator::formatByType(const TypePtr &type) {
  const TypePtr resolved = context_->resolve(type);
  const TypeKind kind = resolved == nullptr ? TypeKind::Undefined : resolved->kind;
  helpers_.use("string_writer");
  switch (kind) {
  case TypeKind::Number:
    helpers_.use("literals");
    helpers_.use("number_writer");
    out().call("jsmm_write_number");
    break;
  case TypeKind::Boolean:
    helpers_.use("literals");
    helpers_.use("literal_writers");
    out().movRegReg(x64::Reg::Rdi, x64::Reg::Rax);
    out().call("jsmm_write_bool");
    break;
  case TypeKind::Null:
    helpers_.use("literals");
    helpers_.use("literal_writers");
    out().call("jsmm_write_null");
    break;
  case TypeKind::Undefined:
  case TypeKind::Variable:
    helpers_.use("literals");
    helpers_.use("literal_writers");
    out().call("jsmm_write_undefined");
    break;
  case TypeKind::Array: formatArrayValue(resolved); break;
  case TypeKind::Union: formatUnionValue(resolved); break;
  default:
    out().movRegReg(x64::Reg::Rdi, x64::Reg::Rax);
    out().call("jsmm_cat_str");
    break;
  }
}

void CodeGenerator::formatUnionValue(const TypePtr &type) {
  const TypePtr payload = unionPayload(type);
  const long long temp = pushTemp();
  out().movOffsetReg(x64::Reg::Rbp, temp, x64::Reg::Rax);
  out().movRegOffset(x64::Reg::Rcx, x64::Reg::Rax, 0);
  out().cmpRegImm(x64::Reg::Rcx, unionTagPayload);
  const std::string nullishLabel = newLabel(".Lunionnullish");
  const std::string undefinedLabel = newLabel(".Lunionundefined");
  const std::string endLabel = newLabel(".Lunionend");
  out().jcc(x64::Cond::B, nullishLabel);
  loadGp(temp, x64::Reg::Rax);
  out().movRegOffset(x64::Reg::Rax, x64::Reg::Rax, 8);
  if (isNumber(payload)) out().movqXmmReg(x64::Xmm::X0, x64::Reg::Rax);
  formatByType(payload);
  out().jmp(endLabel);
  out().label(nullishLabel);
  helpers_.use("literals");
  helpers_.use("literal_writers");
  out().cmpRegImm(x64::Reg::Rcx, unionTagUndefined);
  out().jcc(x64::Cond::E, undefinedLabel);
  out().call("jsmm_write_null");
  out().jmp(endLabel);
  out().label(undefinedLabel);
  out().call("jsmm_write_undefined");
  out().label(endLabel);
  popTemp();
}

void CodeGenerator::formatArray(const reader::ExprPtr &expression) {
  generateExpression(expression);
  formatArrayValue(context_->resolve(typeOf(expression)));
}

void CodeGenerator::formatNewline() {
  helpers_.use("string_writer");
  helpers_.use("literals");
  out().leaLabel(x64::Reg::Rdi, internString("\n"));
  out().call("jsmm_cat_str");
}

void CodeGenerator::formatArrayValue(const TypePtr &type) {
  const TypePtr resolved = context_->resolve(type);
  const TypePtr elementType = resolved != nullptr && resolved->element != nullptr
                                  ? resolved->element
                                  : context_->variable();
  helpers_.use("literals");
  helpers_.use("literal_writers");
  const long long arrayTemp = pushTemp();
  out().movOffsetReg(x64::Reg::Rbp, arrayTemp, x64::Reg::Rax);
  const long long indexTemp = pushTemp();
  out().xorRegReg(x64::Reg::Rax, x64::Reg::Rax);
  out().movOffsetReg(x64::Reg::Rbp, indexTemp, x64::Reg::Rax);

  out().call("jsmm_write_lbracket");
  const std::string loopLabel = newLabel(".Larray");
  const std::string doneLabel = newLabel(".Larraydone");
  const std::string skipLabel = newLabel(".Larraysep");
  out().label(loopLabel);
  loadGp(indexTemp, x64::Reg::Rax);
  loadGp(arrayTemp, x64::Reg::R11);
  out().movRegOffset(x64::Reg::Rcx, x64::Reg::R11, 0);
  out().cmpRegReg(x64::Reg::Rax, x64::Reg::Rcx);
  out().jcc(x64::Cond::AE, doneLabel);
  out().testRegReg(x64::Reg::Rax, x64::Reg::Rax);
  out().jcc(x64::Cond::E, skipLabel);
  out().call("jsmm_write_separator");
  out().label(skipLabel);
  loadGp(indexTemp, x64::Reg::Rax);
  loadGp(arrayTemp, x64::Reg::R11);
  out().movRegReg(x64::Reg::Rcx, x64::Reg::Rax);
  out().movRegImm(x64::Reg::Rdx, 8);
  out().imulRegReg(x64::Reg::Rcx, x64::Reg::Rdx);
  out().addRegReg(x64::Reg::Rcx, x64::Reg::R11);
  out().addRegImm(x64::Reg::Rcx, 8);
  if (isNumber(elementType)) out().movsdXmmOffset(x64::Xmm::X0, x64::Reg::Rcx, 0);
  else
    out().movRegOffset(x64::Reg::Rax, x64::Reg::Rcx, 0);
  formatByType(elementType);
  loadGp(indexTemp, x64::Reg::Rax);
  out().addRegImm(x64::Reg::Rax, 1);
  out().movOffsetReg(x64::Reg::Rbp, indexTemp, x64::Reg::Rax);
  out().jmp(loopLabel);
  out().label(doneLabel);
  out().call("jsmm_write_rbracket");
  popTemp();
  popTemp();
}

void CodeGenerator::generateCall(const reader::ExprPtr &expr, const reader::CallExpr &call) {
  const auto *identifier = std::get_if<reader::IdentifierExpr>(&call.callee->node);
  const bool direct = identifier != nullptr && lookupSlot(identifier->name) == nullptr &&
                      functions_.contains(identifier->name);
  const builtins::BuiltinSpec *builtin =
      identifier != nullptr && lookupSlot(identifier->name) == nullptr && !direct
          ? builtins::BuiltinRegistry::instance().find(identifier->name)
          : nullptr;

  if (builtin != nullptr) {
    builtin->emit(*this, call);
    return;
  }

  long long calleeTemp = 0;
  const bool indirect = !direct;
  if (indirect) {
    generateExpression(call.callee);
    calleeTemp = pushTemp();
    out().movOffsetReg(x64::Reg::Rbp, calleeTemp, x64::Reg::Rax);
  }

  generateArguments(call.arguments);
  if (direct) {
    out().call(functions_.at(identifier->name));
  } else {
    loadGp(calleeTemp, x64::Reg::R11);
    out().callIndirect(x64::Reg::R11);
  }

  const std::size_t count = call.arguments.size();
  const std::size_t pushed = count + (count % 2 == 1 ? 1 : 0);
  out().addRegImm(x64::Reg::Rsp, static_cast<long long>(8 * pushed));
  for (std::size_t i = 0; i < count; ++i) popTemp();
  if (indirect) popTemp();
  if (isNumber(typeOf(expr))) out().movqXmmReg(x64::Xmm::X0, x64::Reg::Rax);
}

std::string CodeGenerator::generateFunction(const reader::FunctionDeclaration &declaration) {
  x64::Assembly prologue;
  x64::Assembly body;
  scopes_.clear();
  slotCount_ = 0;
  freeSlots_.clear();
  temps_.clear();
  state_ = FunctionState{};
  state_.returnLabel = newLabel(".Lret");
  generatingEntry_ = false;
  const auto found = annotations_->functionResults.find(&declaration);
  state_.resultType =
      found != annotations_->functionResults.end() ? found->second : context_->undefined();

  beginScope();
  varTargetScopeIndex_ = static_cast<int>(scopes_.size()) - 1;
  std::vector<long long> parameterOffsets;
  for (const auto &parameter : declaration.parameters) {
    const long long offset = allocateSlot();
    scopes_.back().slots.insert_or_assign(parameter, Slot{offset, true});
    parameterOffsets.push_back(offset);
  }

  prologue.push(x64::Reg::Rbp);
  prologue.movRegReg(x64::Reg::Rbp, x64::Reg::Rsp);
  const long long frame = alignFrame(slotCount_ * 8);
  if (frame > 0) prologue.subRegImm(x64::Reg::Rsp, frame);
  for (std::size_t i = 0; i < parameterOffsets.size(); ++i) {
    prologue.movRegOffset(x64::Reg::Rax, x64::Reg::Rbp, static_cast<long long>(16 + 8 * i));
    prologue.movOffsetReg(x64::Reg::Rbp, parameterOffsets[i], x64::Reg::Rax);
  }

  out_ = &body;
  generateStatements(declaration.body);
  out().xorRegReg(x64::Reg::Rax, x64::Reg::Rax);
  out().label(state_.returnLabel);
  out().movRegReg(x64::Reg::Rsp, x64::Reg::Rbp);
  out().pop(x64::Reg::Rbp);
  out().ret();

  const long long fullFrame = alignFrame(slotCount_ * 8);
  if (fullFrame != frame) {
    x64::Assembly adjusted;
    adjusted.push(x64::Reg::Rbp);
    adjusted.movRegReg(x64::Reg::Rbp, x64::Reg::Rsp);
    if (fullFrame > 0) adjusted.subRegImm(x64::Reg::Rsp, fullFrame);
    for (std::size_t i = 0; i < parameterOffsets.size(); ++i) {
      adjusted.movRegOffset(x64::Reg::Rax, x64::Reg::Rbp, static_cast<long long>(16 + 8 * i));
      adjusted.movOffsetReg(x64::Reg::Rbp, parameterOffsets[i], x64::Reg::Rax);
    }
    prologue = adjusted;
  }

  x64::Assembly function;
  const std::string label = functions_.at(declaration.name);
  function.globl(label);
  function.label(label);
  function.append(prologue);
  function.append(body);
  return function.text();
}

std::string CodeGenerator::generateEntry(const reader::Program &program) {
  x64::Assembly body;
  scopes_.clear();
  slotCount_ = 0;
  freeSlots_.clear();
  temps_.clear();
  entrySlots_.clear();
  state_ = FunctionState{};
  state_.returnLabel = newLabel(".Lret");
  state_.resultType = context_->undefined();
  generatingEntry_ = true;

  out_ = &body;
  out().leaLabel(x64::Reg::R11, entryFrameSymbol);
  out().movOffsetReg(x64::Reg::R11, 0, x64::Reg::Rbp);

  beginScope();
  varTargetScopeIndex_ = static_cast<int>(scopes_.size()) - 1;

  std::vector<std::pair<std::string, long long>> reserved;
  for (const auto &statement : program.statements) {
    const auto *declaration = std::get_if<reader::VariableDeclaration>(&statement.node);
    if (declaration == nullptr) continue;
    for (const auto &declarator : declaration->declarations) {
      const long long offset = allocateSlot();
      reserved.emplace_back(declarator.name, offset);
      entrySlots_.emplace(declarator.name, offset);
    }
  }

  for (const auto &[name, offset] : reserved) {
    TypePtr type = nullptr;
    const auto found = annotations_->topLevelTypes.find(name);
    if (found != annotations_->topLevelTypes.end()) type = found->second;
    emitZeroValue(type);
    storeValue(offset, type != nullptr ? type : context_->undefined());
  }

  generateStatements(program.statements);
  out().xorRegReg(x64::Reg::Rax, x64::Reg::Rax);
  out().label(state_.returnLabel);
  out().movRegReg(x64::Reg::Rsp, x64::Reg::Rbp);
  out().pop(x64::Reg::Rbp);
  out().ret();

  x64::Assembly entry;
  entry.globl(entrySymbol);
  entry.label(entrySymbol);
  entry.push(x64::Reg::Rbp);
  entry.movRegReg(x64::Reg::Rbp, x64::Reg::Rsp);
  const long long frame = alignFrame(slotCount_ * 8);
  if (frame > 0) entry.subRegImm(x64::Reg::Rsp, frame);
  entry.append(body);
  return entry.text();
}

std::string CodeGenerator::generate(const reader::Program &program,
                                    const types::Annotations &annotations,
                                    types::TypeContext &context) {
  annotations_ = &annotations;
  context_ = &context;
  functions_.clear();
  stringLiterals_.clear();
  rodataStrings_.clear();
  labelCounter_ = 0;
  helpers_.reset();
  registerPlatformHelpers(helpers_);
  helpers_.use(std::string(startSymbol));
  helpers_.use(std::string(entryFrameSymbol));

  {
    long long topLevelSlotCount = 0;
    for (const auto &statement : program.statements) {
      const auto *decl = std::get_if<reader::VariableDeclaration>(&statement.node);
      if (decl == nullptr) continue;
      for (const auto &d : decl->declarations) {
        ++topLevelSlotCount;
        const long long offset = -8 * topLevelSlotCount;
        topLevelOffsets_.emplace(d.name, offset);
        bool isNum = false;
        const auto it = annotations_->topLevelTypes.find(d.name);
        if (it != annotations_->topLevelTypes.end()) { isNum = isNumber(it->second); }
        topLevelIsNumber_.emplace(d.name, isNum);
      }
    }
  }

  std::vector<const reader::FunctionDeclaration *> declarations;
  long long index = 0;
  for (const auto &statement : program.statements) {
    const auto *declaration = std::get_if<reader::FunctionDeclaration>(&statement.node);
    if (declaration == nullptr) continue;
    const std::string label = "jsfn_" + declaration->name + "_" + std::to_string(index++);
    functions_.insert_or_assign(declaration->name, label);
    declarations.push_back(declaration);
  }

  std::vector<std::string> functionTexts;
  for (const auto *declaration : declarations)
    functionTexts.push_back(generateFunction(*declaration));
  const std::string entry = generateEntry(program);

  x64::Assembly assembly;
  assembly.directive(".intel_syntax noprefix");
  assembly.section("text");
  assembly.globl(entrySymbol);
  assembly.append(entry);
  for (const auto &function : functionTexts) assembly.append(function);
  if (!rodataStrings_.empty()) {
    assembly.section("rodata");
    for (const auto &[label, data] : rodataStrings_) {
      assembly.label(label);
      assembly.quad(data.size());
      if (!data.empty()) assembly.bytes(data);
    }
  }
  assembly.append(helpers_.assemble());
  return assembly.text();
}

} // namespace jsmm::emit
