#include "emit/irgen.hh"

#include "errors/errors.hh"

#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/TargetParser/Triple.h>

#include <cstdlib>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

namespace jsmm::emit {
namespace {

using types::TypeKind;
using types::TypePtr;

double parseNumber(const std::string &spelling) {
  if (spelling.size() > 2 && spelling[0] == '0' && (spelling[1] == 'x' || spelling[1] == 'X'))
    return static_cast<double>(std::strtoull(spelling.c_str() + 2, nullptr, 16));
  if (spelling.size() > 2 && spelling[0] == '0' && (spelling[1] == 'b' || spelling[1] == 'B')) {
    unsigned long long result = 0;
    for (std::size_t index = 2; index < spelling.size(); ++index)
      result = result * 2ULL + static_cast<unsigned long long>(spelling[index] - '0');
    return static_cast<double>(result);
  }
  if (spelling.size() > 2 && spelling[0] == '0' && (spelling[1] == 'o' || spelling[1] == 'O')) {
    unsigned long long result = 0;
    for (std::size_t index = 2; index < spelling.size(); ++index)
      result = result * 8ULL + static_cast<unsigned long long>(spelling[index] - '0');
    return static_cast<double>(result);
  }
  return std::strtod(spelling.c_str(), nullptr);
}

constexpr long long unionTagUndefined = 0;
constexpr long long unionTagNull = 1;
constexpr long long unionTagPayload = 2;

}

types::TypePtr IrGenerator::typeOf(const reader::ExprPtr &expression) const {
  return annotations_->expressionTypes.at(expression.get());
}

types::TypePtr IrGenerator::resolve(types::TypePtr type) const { return types_->resolve(std::move(type)); }

bool IrGenerator::isNumber(types::TypePtr type) const {
  const TypePtr resolved = resolve(std::move(type));
  return resolved != nullptr && resolved->kind == TypeKind::Number;
}

types::TypePtr IrGenerator::unionPayload(types::TypePtr type) const {
  type = resolve(std::move(type));
  if (type == nullptr || type->kind != TypeKind::Union) return nullptr;
  for (const TypePtr &member : type->members) {
    const TypePtr resolved = resolve(member);
    if (resolved == nullptr) continue;
    if (resolved->kind == TypeKind::Null || resolved->kind == TypeKind::Undefined) continue;
    return resolved;
  }
  return nullptr;
}

std::string IrGenerator::typeofName(types::TypePtr type) const {
  type = resolve(std::move(type));
  if (type == nullptr) return "undefined";
  switch (type->kind) {
  case TypeKind::Number: return "number";
  case TypeKind::String: return "string";
  case TypeKind::Boolean: return "boolean";
  case TypeKind::Null: return "object";
  case TypeKind::Undefined:
  case TypeKind::Never: return "undefined";
  case TypeKind::Function: return "function";
  case TypeKind::Struct:
  case TypeKind::Array:
  case TypeKind::Union: return "object";
  case TypeKind::Variable: return "undefined";
  }
  return "undefined";
}

llvm::Constant *IrGenerator::i64(std::uint64_t value) const {
  return llvm::ConstantInt::get(i64Type_, value, false);
}

llvm::Constant *IrGenerator::i32(std::uint64_t value) const {
  return llvm::ConstantInt::get(i32Type_, value, false);
}

IrGenerator::EmitValue IrGenerator::numberValue(double value) const {
  return EmitValue{llvm::ConstantFP::get(doubleType_, value), true};
}

IrGenerator::EmitValue IrGenerator::rawValue(llvm::Value *value) const {
  return EmitValue{value, false};
}

llvm::Value *IrGenerator::asRaw(EmitValue value) const {
  if (!value.number) return value.value;
  return builder_->CreateBitCast(value.value, i64Type_);
}

llvm::Value *IrGenerator::toInt32(llvm::Value *value) const {
  llvm::Value *wide = builder_->CreateIntrinsic(
      llvm::Intrinsic::fptosi_sat, {i64Type_, doubleType_}, {value});
  return builder_->CreateTrunc(wide, i32Type_);
}

IrGenerator::EmitValue IrGenerator::asTyped(llvm::Value *bits, types::TypePtr type) const {
  if (isNumber(type)) return EmitValue{builder_->CreateBitCast(bits, doubleType_), true};
  return EmitValue{bits, false};
}

llvm::Value *IrGenerator::bytePointer(llvm::Value *base, std::uint64_t offset) const {
  llvm::Value *pointer = builder_->CreateIntToPtr(base, ptrType_);
  if (offset == 0) return pointer;
  return builder_->CreateGEP(i8Type_, pointer, i64(offset));
}

llvm::Value *IrGenerator::dynamicPointer(llvm::Value *base, llvm::Value *offset) const {
  return builder_->CreateGEP(i8Type_, builder_->CreateIntToPtr(base, ptrType_), offset);
}

llvm::Value *IrGenerator::allocate(std::uint64_t bytes) {
  return builder_->CreateCall(allocFunction(), {i64(bytes)});
}

IrGenerator::EmitValue IrGenerator::loadSlot(llvm::Value *address, types::TypePtr type) const {
  llvm::Value *bits = builder_->CreateLoad(i64Type_, address);
  if (isNumber(type)) return EmitValue{builder_->CreateBitCast(bits, doubleType_), true};
  return EmitValue{bits, false};
}

void IrGenerator::storeSlot(llvm::Value *address, EmitValue value) const {
  builder_->CreateStore(asRaw(value), address);
}

bool IrGenerator::terminated() const {
  const llvm::BasicBlock *block = builder_->GetInsertBlock();
  return block != nullptr && block->hasTerminator();
}

llvm::BasicBlock *IrGenerator::createBlock(const llvm::Twine &name) {
  return llvm::BasicBlock::Create(*context_, name, currentFunction_);
}

void IrGenerator::beginScope() { scopes_.emplace_back(); }

void IrGenerator::endScope() { scopes_.pop_back(); }

IrGenerator::Slot *IrGenerator::lookupSlot(const std::string &name) {
  for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
    const auto found = scope->slots.find(name);
    if (found != scope->slots.end()) return &found->second;
  }
  return nullptr;
}

long long IrGenerator::allocateSlot() {
  if (!freeSlots_.empty()) {
    const long long index = freeSlots_.back();
    freeSlots_.pop_back();
    return index;
  }
  return slotCount_++;
}

void IrGenerator::releaseSlot(long long index) { freeSlots_.push_back(index); }

long long IrGenerator::pushTemp() {
  const long long index = allocateSlot();
  temps_.push_back(index);
  return index;
}

void IrGenerator::popTemp() {
  releaseSlot(temps_.back());
  temps_.pop_back();
}

llvm::AllocaInst *IrGenerator::slotAlloca(long long index) {
  if (static_cast<std::size_t>(index) < slotAllocas_.size() && slotAllocas_[index] != nullptr)
    return slotAllocas_[index];
  if (slotAllocas_.size() <= static_cast<std::size_t>(index)) slotAllocas_.resize(index + 1, nullptr);
  llvm::BasicBlock &entry = currentFunction_->getEntryBlock();
  llvm::IRBuilder<> allocator(&entry, entry.begin());
  llvm::AllocaInst *alloca = allocator.CreateAlloca(i64Type_);
  alloca->setAlignment(llvm::Align(8));
  slotAllocas_[index] = alloca;
  return alloca;
}

llvm::AllocaInst *IrGenerator::createEntryAlloca() {
  llvm::BasicBlock &entry = currentFunction_->getEntryBlock();
  llvm::IRBuilder<> allocator(&entry, entry.begin());
  llvm::AllocaInst *alloca = allocator.CreateAlloca(i64Type_);
  alloca->setAlignment(llvm::Align(8));
  return alloca;
}

llvm::GlobalVariable *IrGenerator::internString(const std::string &text) {
  const auto found = strings_.find(text);
  if (found != strings_.end()) return found->second;
  llvm::ArrayType *arrayType = llvm::ArrayType::get(i8Type_, text.size());
  llvm::StructType *structType = llvm::StructType::get(*context_, {i64Type_, arrayType});
  std::vector<std::uint8_t> bytes(text.begin(), text.end());
  llvm::Constant *data = llvm::ConstantDataArray::get(*context_, bytes);
  std::vector<llvm::Constant *> elements{i64(text.size()), data};
  llvm::Constant *initializer = llvm::ConstantStruct::get(structType, elements);
  auto *global = new llvm::GlobalVariable(*module_, structType, true,
                                          llvm::GlobalValue::InternalLinkage, initializer,
                                          "jsmm.str." + std::to_string(strings_.size()));
  global->setAlignment(llvm::Align(8));
  global->setDSOLocal(true);
  strings_[text] = global;
  return global;
}

llvm::GlobalVariable *IrGenerator::globalFor(const std::string &name) {
  const auto found = globals_.find(name);
  if (found != globals_.end()) return found->second;
  auto *global = new llvm::GlobalVariable(*module_, i64Type_, false,
                                          llvm::GlobalValue::InternalLinkage, i64(0),
                                          "jsmm.g." + name);
  global->setAlignment(llvm::Align(8));
  global->setDSOLocal(true);
  globals_[name] = global;
  return global;
}

llvm::Function *IrGenerator::runtimeFunction(llvm::StringRef name, llvm::Type *result,
                                             llvm::ArrayRef<llvm::Type *> parameters) {
  const auto found = runtime_.find(name.str());
  if (found != runtime_.end()) return found->second;
  llvm::FunctionType *type = llvm::FunctionType::get(result, parameters, false);
  llvm::Function *function =
      llvm::Function::Create(type, llvm::Function::ExternalLinkage, name, *module_);
  function->setDSOLocal(true);
  runtime_[name.str()] = function;
  return function;
}

llvm::Function *IrGenerator::allocFunction() {
  return runtimeFunction("jsmm_alloc", i64Type_, {i64Type_});
}

llvm::Function *IrGenerator::stringWriter() {
  return runtimeFunction("jsmm_cat_str", voidType_, {i64Type_});
}

llvm::Function *IrGenerator::numberWriter() {
  return runtimeFunction("jsmm_write_number", voidType_, {doubleType_});
}

llvm::Function *IrGenerator::boolWriter() {
  return runtimeFunction("jsmm_write_bool", voidType_, {i64Type_});
}

llvm::Function *IrGenerator::nullWriter() {
  return runtimeFunction("jsmm_write_null", voidType_, {});
}

llvm::Function *IrGenerator::undefinedWriter() {
  return runtimeFunction("jsmm_write_undefined", voidType_, {});
}

llvm::Function *IrGenerator::lbracketWriter() {
  return runtimeFunction("jsmm_write_lbracket", voidType_, {});
}

llvm::Function *IrGenerator::separatorWriter() {
  return runtimeFunction("jsmm_write_separator", voidType_, {});
}

llvm::Function *IrGenerator::rbracketWriter() {
  return runtimeFunction("jsmm_write_rbracket", voidType_, {});
}

llvm::Function *IrGenerator::moduloFunction() {
  return runtimeFunction("jsmm_mod", doubleType_, {doubleType_, doubleType_});
}

llvm::Function *IrGenerator::powerFunction() {
  return runtimeFunction("jsmm_pow", doubleType_, {doubleType_, doubleType_});
}

llvm::Function *IrGenerator::boundsFunction() {
  return runtimeFunction("jsmm_oob", voidType_, {});
}

IrGenerator::EmitValue IrGenerator::emitZeroValue(types::TypePtr type) {
  const TypePtr resolved = resolve(std::move(type));
  if (resolved == nullptr) return rawValue(i64(0));
  switch (resolved->kind) {
  case TypeKind::Number: return numberValue(0.0);
  case TypeKind::String:
    return rawValue(builder_->CreatePtrToInt(emptyString_, i64Type_));
  case TypeKind::Array: {
    llvm::Value *base = allocate(8);
    builder_->CreateStore(i64(0), bytePointer(base, 0));
    return rawValue(base);
  }
  case TypeKind::Struct: {
    llvm::Value *base = allocate(8 * resolved->fields.size());
    for (std::size_t index = 0; index < resolved->fields.size(); ++index) {
      const TypePtr &fieldType = resolved->fields[index].second;
      const EmitValue field = emitZeroValue(fieldType);
      storeSlot(bytePointer(base, 8 * index), field);
    }
    return rawValue(base);
  }
  case TypeKind::Union: {
    long long tag = unionTagPayload;
    bool foundNull = false;
    bool foundUndefined = false;
    for (const TypePtr &member : resolved->members) {
      const TypePtr current = resolve(member);
      if (current == nullptr) continue;
      if (current->kind == TypeKind::Undefined) foundUndefined = true;
      if (current->kind == TypeKind::Null) foundNull = true;
    }
    if (foundUndefined) tag = unionTagUndefined;
    else if (foundNull) tag = unionTagNull;
    return emitNullishCell(tag);
  }
  default: return rawValue(i64(0));
  }
}

IrGenerator::EmitValue IrGenerator::emitNullishCell(long long tag) {
  llvm::Value *base = allocate(16);
  builder_->CreateStore(i64(static_cast<std::uint64_t>(tag)), bytePointer(base, 0));
  builder_->CreateStore(i64(0), bytePointer(base, 8));
  return rawValue(base);
}

IrGenerator::EmitValue IrGenerator::emitBox(types::TypePtr payloadType, EmitValue value) {
  (void)payloadType;
  llvm::Value *base = allocate(16);
  builder_->CreateStore(i64(unionTagPayload), bytePointer(base, 0));
  builder_->CreateStore(asRaw(value), bytePointer(base, 8));
  return rawValue(base);
}

bool IrGenerator::sameLayout(types::TypePtr left, types::TypePtr right) const {
  left = resolve(std::move(left));
  right = resolve(std::move(right));
  if (left == nullptr || right == nullptr || left == right) return true;
  if (left->kind != right->kind || left->kind != TypeKind::Struct) return true;
  if (left->fields.size() != right->fields.size()) return true;
  for (std::size_t index = 0; index < left->fields.size(); ++index) {
    if (left->fields[index].first != right->fields[index].first) return false;
    if (!sameLayout(left->fields[index].second, right->fields[index].second)) return false;
  }
  return true;
}

IrGenerator::EmitValue IrGenerator::convertStruct(types::TypePtr destination,
                                                   types::TypePtr source, EmitValue value) {
  const TypePtr target = resolve(std::move(destination));
  const TypePtr origin = resolve(std::move(source));
  if (target == nullptr || origin == nullptr || target->kind != TypeKind::Struct ||
      origin->kind != TypeKind::Struct)
    return value;
  if (sameLayout(target, origin)) return value;
  llvm::Value *sourceBase = asRaw(value);
  llvm::Value *targetBase = allocate(8 * target->fields.size());
  for (std::size_t index = 0; index < target->fields.size(); ++index) {
    const std::string &name = target->fields[index].first;
    const TypePtr &targetField = target->fields[index].second;
    std::size_t sourceIndex = origin->fields.size();
    TypePtr sourceField;
    for (std::size_t candidate = 0; candidate < origin->fields.size(); ++candidate)
      if (origin->fields[candidate].first == name) {
        sourceIndex = candidate;
        sourceField = origin->fields[candidate].second;
        break;
      }
    if (sourceIndex >= origin->fields.size()) continue;
    llvm::Value *bits = builder_->CreateLoad(
        i64Type_, bytePointer(sourceBase, 8 * sourceIndex));
    EmitValue field = rawValue(bits);
    const TypePtr resolvedSource = resolve(sourceField);
    if (resolvedSource != nullptr && resolvedSource->kind == TypeKind::Struct)
      field = convertStruct(targetField, sourceField, field);
    storeSlot(bytePointer(targetBase, 8 * index), field);
  }
  return rawValue(targetBase);
}

IrGenerator::EmitValue IrGenerator::coerce(types::TypePtr destination, types::TypePtr source,
                                           EmitValue value) {
  const TypePtr target = resolve(std::move(destination));
  const TypePtr origin = resolve(std::move(source));
  if (target == nullptr || origin == nullptr) return value;
  if (target->kind == TypeKind::Union) {
    if (origin->kind == TypeKind::Union) return value;
    if (origin->kind == TypeKind::Null) return emitNullishCell(unionTagNull);
    if (origin->kind == TypeKind::Undefined) return emitNullishCell(unionTagUndefined);
    return emitBox(unionPayload(target), value);
  }
  if (target->kind == TypeKind::Struct && origin->kind == TypeKind::Struct)
    return convertStruct(target, origin, value);
  return value;
}

void IrGenerator::collectVarDeclarations(
    const reader::StatementList &statements,
    std::vector<const reader::VariableDeclarator *> &out) const {
  for (const auto &statement : statements) {
    if (const auto *declaration = std::get_if<reader::VariableDeclaration>(&statement.node)) {
      if (declaration->kind != "var") continue;
      for (const auto &declarator : declaration->declarations) out.push_back(&declarator);
    } else if (const auto *block = std::get_if<reader::BlockStatement>(&statement.node)) {
      collectVarDeclarations(block->statements, out);
    } else if (const auto *branch = std::get_if<reader::IfStatement>(&statement.node)) {
      collectVarDeclarations(branch->consequent, out);
      collectVarDeclarations(branch->alternate, out);
    } else if (const auto *loop = std::get_if<reader::WhileStatement>(&statement.node)) {
      collectVarDeclarations(loop->body, out);
    } else if (const auto *loop = std::get_if<reader::ForStatement>(&statement.node)) {
      if (loop->init != nullptr && loop->init->kind == "var")
        for (const auto &declarator : loop->init->declarations) out.push_back(&declarator);
      collectVarDeclarations(loop->body, out);
    }
  }
}

void IrGenerator::hoistVarSlots(const reader::StatementList &statements) {
  std::vector<const reader::VariableDeclarator *> declarators;
  collectVarDeclarations(statements, declarators);
  for (const reader::VariableDeclarator *declarator : declarators) {
    if (scopes_.back().slots.contains(declarator->name)) continue;
    llvm::Value *address = generatingEntry_ && scopes_.size() == 1
                               ? static_cast<llvm::Value *>(globalFor(declarator->name))
                               : static_cast<llvm::Value *>(slotAlloca(allocateSlot()));
    scopes_.back().slots.insert_or_assign(declarator->name, Slot{address, false});
    TypePtr type;
    const auto declared = annotations_->declaredTypes.find(declarator);
    if (declared != annotations_->declaredTypes.end()) {
      type = declared->second;
    } else {
      const auto inferred = annotations_->declarationTypes.find(declarator);
      if (inferred != annotations_->declarationTypes.end()) type = inferred->second;
    }
    storeSlot(address, emitZeroValue(type));
  }
}

void IrGenerator::generateDeclaration(const reader::VariableDeclaration &declaration) {
  for (const auto &declarator : declaration.declarations) {
    Scope &target = (declaration.kind == "var" && varTargetScopeIndex_ >= 0)
                        ? scopes_[varTargetScopeIndex_]
                        : scopes_.back();
    const auto existing = target.slots.find(declarator.name);
    const bool reuseExisting = declaration.kind == "var" && existing != target.slots.end();
    llvm::Value *address = reuseExisting ? existing->second.address : nullptr;
    if (address == nullptr)
      address = (generatingEntry_ && scopes_.size() == 1)
                    ? static_cast<llvm::Value *>(globalFor(declarator.name))
                    : static_cast<llvm::Value *>(slotAlloca(allocateSlot()));

    if (declarator.initializer != nullptr) {
      EmitValue value = generateExpression(declarator.initializer);
      TypePtr storedType = typeOf(declarator.initializer);
      const auto declared = annotations_->declaredTypes.find(&declarator);
      if (declared != annotations_->declaredTypes.end()) {
        value = coerce(declared->second, storedType, value);
        storedType = declared->second;
      }
      target.slots.insert_or_assign(declarator.name,
                                    Slot{address, declaration.kind == "const"});
      storeSlot(address, value);
      (void)storedType;
    } else if (declaration.kind == "var" && reuseExisting) {
    } else {
      target.slots.insert_or_assign(declarator.name, Slot{address, false});
      TypePtr declaredType;
      const auto declared = annotations_->declaredTypes.find(&declarator);
      if (declared != annotations_->declaredTypes.end()) {
        declaredType = declared->second;
      } else {
        const auto inferred = annotations_->declarationTypes.find(&declarator);
        if (inferred != annotations_->declarationTypes.end()) declaredType = inferred->second;
      }
      storeSlot(address, emitZeroValue(declaredType));
    }
  }
}

void IrGenerator::generateStatements(const reader::StatementList &statements) {
  for (const auto &statement : statements) {
    if (terminated()) return;
    generateStatement(statement);
  }
}

void IrGenerator::generateStatement(const reader::Statement &statement) {
  std::visit(
      [&](const auto &node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, reader::VariableDeclaration>) {
          generateDeclaration(node);
        } else if constexpr (std::is_same_v<T, reader::ExpressionStatement>) {
          (void)generateExpression(node.expression);
        } else if constexpr (std::is_same_v<T, reader::FunctionDeclaration>) {
        } else if constexpr (std::is_same_v<T, reader::ReturnStatement>) {
          EmitValue value = rawValue(i64(0));
          if (node.argument != nullptr) {
            value = generateExpression(node.argument);
            value = coerce(returnType_, typeOf(node.argument), value);
          } else {
            value = coerce(returnType_, types_->undefined(), value);
          }
          storeSlot(returnSlot_, value);
          builder_->CreateBr(returnBlock_);
        } else if constexpr (std::is_same_v<T, reader::BlockStatement>) {
          beginScope();
          generateStatements(node.statements);
          endScope();
        } else if constexpr (std::is_same_v<T, reader::IfStatement>) {
          EmitValue condition = generateExpression(node.condition);
          llvm::Value *test = builder_->CreateICmpNE(asRaw(condition), i64(0));
          llvm::BasicBlock *thenBlock = createBlock("if.then");
          llvm::BasicBlock *elseBlock =
              node.alternate.empty() ? nullptr : createBlock("if.else");
          llvm::BasicBlock *endBlock = createBlock("if.end");
          builder_->CreateCondBr(test, thenBlock, elseBlock != nullptr ? elseBlock : endBlock);
          builder_->SetInsertPoint(thenBlock);
          beginScope();
          generateStatements(node.consequent);
          endScope();
          if (!terminated()) builder_->CreateBr(endBlock);
          if (elseBlock != nullptr) {
            builder_->SetInsertPoint(elseBlock);
            beginScope();
            generateStatements(node.alternate);
            endScope();
            if (!terminated()) builder_->CreateBr(endBlock);
          }
          builder_->SetInsertPoint(endBlock);
        } else if constexpr (std::is_same_v<T, reader::WhileStatement>) {
          llvm::BasicBlock *conditionBlock = createBlock("while.cond");
          llvm::BasicBlock *bodyBlock = createBlock("while.body");
          llvm::BasicBlock *endBlock = createBlock("while.end");
          builder_->CreateBr(conditionBlock);
          builder_->SetInsertPoint(conditionBlock);
          EmitValue condition = generateExpression(node.condition);
          builder_->CreateCondBr(builder_->CreateICmpNE(asRaw(condition), i64(0)), bodyBlock,
                                 endBlock);
          builder_->SetInsertPoint(bodyBlock);
          loops_.push_back(Loop{endBlock, conditionBlock});
          beginScope();
          generateStatements(node.body);
          endScope();
          loops_.pop_back();
          if (!terminated()) builder_->CreateBr(conditionBlock);
          builder_->SetInsertPoint(endBlock);
        } else if constexpr (std::is_same_v<T, reader::ForStatement>) {
          llvm::BasicBlock *conditionBlock = createBlock("for.cond");
          llvm::BasicBlock *bodyBlock = createBlock("for.body");
          llvm::BasicBlock *updateBlock = createBlock("for.update");
          llvm::BasicBlock *endBlock = createBlock("for.end");
          beginScope();
          if (node.init != nullptr) generateDeclaration(*node.init);
          if (node.initExpression != nullptr) (void)generateExpression(node.initExpression);
          builder_->CreateBr(conditionBlock);
          builder_->SetInsertPoint(conditionBlock);
          if (node.condition != nullptr) {
            EmitValue condition = generateExpression(node.condition);
            builder_->CreateCondBr(builder_->CreateICmpNE(asRaw(condition), i64(0)), bodyBlock,
                                   endBlock);
          } else {
            builder_->CreateBr(bodyBlock);
          }
          builder_->SetInsertPoint(bodyBlock);
          loops_.push_back(Loop{endBlock, updateBlock});
          beginScope();
          generateStatements(node.body);
          endScope();
          loops_.pop_back();
          if (!terminated()) builder_->CreateBr(updateBlock);
          builder_->SetInsertPoint(updateBlock);
          if (node.update != nullptr) (void)generateExpression(node.update);
          if (!terminated()) builder_->CreateBr(conditionBlock);
          builder_->SetInsertPoint(endBlock);
          endScope();
        } else if constexpr (std::is_same_v<T, reader::BreakStatement>) {
          builder_->CreateBr(loops_.back().breakBlock);
        } else if constexpr (std::is_same_v<T, reader::ContinueStatement>) {
          builder_->CreateBr(loops_.back().continueBlock);
        }
      },
      statement.node);
}

IrGenerator::EmitValue IrGenerator::generateExpression(const reader::ExprPtr &expression) {
  if (terminated()) {
    const TypePtr type = resolve(typeOf(expression));
    if (type != nullptr && type->kind == TypeKind::Number) return numberValue(0.0);
    return rawValue(i64(0));
  }
  return std::visit(
      [&](const auto &node) -> EmitValue {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, reader::NumberLiteralExpr>) {
          return numberValue(parseNumber(node.spelling));
        } else if constexpr (std::is_same_v<T, reader::StringLiteralExpr>) {
          return rawValue(builder_->CreatePtrToInt(internString(node.value), i64Type_));
        } else if constexpr (std::is_same_v<T, reader::BooleanLiteralExpr>) {
          return rawValue(i64(node.value ? 1 : 0));
        } else if constexpr (std::is_same_v<T, reader::NullLiteralExpr>) {
          return rawValue(i64(0));
        } else if constexpr (std::is_same_v<T, reader::UndefinedLiteralExpr>) {
          return rawValue(i64(0));
        } else if constexpr (std::is_same_v<T, reader::IdentifierExpr>) {
          if (Slot *slot = lookupSlot(node.name)) return loadSlot(slot->address, typeOf(expression));
          const auto global = globals_.find(node.name);
          if (global != globals_.end()) return loadSlot(global->second, typeOf(expression));
          const auto function = functions_.find(node.name);
          if (function != functions_.end())
            return rawValue(builder_->CreatePtrToInt(function->second, i64Type_));
          throw errors::ErrorException{errors::ErrorStatus{
              errors::ErrorCode::TYPE_UNDEFINED_IDENTIFIER, node.name, expression->range}};
        } else if constexpr (std::is_same_v<T, reader::UnaryExpr>) {
          return generateUnary(node);
        } else if constexpr (std::is_same_v<T, reader::BinaryExpr>) {
          return generateBinary(node);
        } else if constexpr (std::is_same_v<T, reader::UpdateExpr>) {
          return generateUpdate(node);
        } else if constexpr (std::is_same_v<T, reader::AssignmentExpr>) {
          return generateAssignment(node);
        } else if constexpr (std::is_same_v<T, reader::MemberExpr>) {
          llvm::Value *address = generateMemberAddress(node);
          return loadSlot(builder_->CreateIntToPtr(address, ptrType_), typeOf(expression));
        } else if constexpr (std::is_same_v<T, reader::ObjectExpr>) {
          llvm::Value *base = allocate(8 * node.properties.size());
          const TypePtr structType = resolve(typeOf(expression));
          for (std::size_t index = 0; index < node.properties.size(); ++index) {
            std::size_t fieldIndex = index;
            TypePtr declaredField;
            if (structType != nullptr && structType->kind == TypeKind::Struct) {
              for (std::size_t field = 0; field < structType->fields.size(); ++field)
                if (structType->fields[field].first == node.properties[index].key) {
                  fieldIndex = field;
                  declaredField = structType->fields[field].second;
                  break;
                }
            }
            EmitValue value = generateExpression(node.properties[index].value);
            if (declaredField != nullptr)
              value = coerce(declaredField, typeOf(node.properties[index].value), value);
            storeSlot(bytePointer(base, 8 * fieldIndex), value);
          }
          return rawValue(base);
        } else if constexpr (std::is_same_v<T, reader::ArrayExpr>) {
          llvm::Value *base = allocate(8 + 8 * node.elements.size());
          builder_->CreateStore(i64(node.elements.size()), bytePointer(base, 0));
          const TypePtr arrayType = resolve(typeOf(expression));
          TypePtr elementType = arrayType != nullptr && arrayType->kind == TypeKind::Array
                                    ? arrayType->element
                                    : nullptr;
          for (std::size_t index = 0; index < node.elements.size(); ++index) {
            EmitValue value = generateExpression(node.elements[index]);
            if (elementType != nullptr)
              value = coerce(elementType, typeOf(node.elements[index]), value);
            storeSlot(bytePointer(base, 8 + 8 * index), value);
          }
          return rawValue(base);
        } else if constexpr (std::is_same_v<T, reader::CallExpr>) {
          return generateCall(expression, node);
        }
        return rawValue(i64(0));
      },
      expression->node);
}

IrGenerator::EmitValue IrGenerator::generateUnary(const reader::UnaryExpr &unary) {
  if (unary.op == "!") {
    EmitValue operand = generateExpression(unary.operand);
    llvm::Value *result = builder_->CreateICmpEQ(asRaw(operand), i64(0));
    return rawValue(builder_->CreateZExt(result, i64Type_));
  }
  if (unary.op == "~") {
    EmitValue operand = generateExpression(unary.operand);
    llvm::Value *value = toInt32(operand.value);
    llvm::Value *inverted = builder_->CreateNot(value);
    llvm::Value *extended = builder_->CreateSExt(inverted, i64Type_);
    return EmitValue{builder_->CreateSIToFP(extended, doubleType_), true};
  }
  if (unary.op == "-") {
    EmitValue operand = generateExpression(unary.operand);
    return EmitValue{builder_->CreateFNeg(operand.value), true};
  }
  if (unary.op == "+") return generateExpression(unary.operand);
  if (unary.op == "typeof") {
    const TypePtr operandType = resolve(typeOf(unary.operand));
    EmitValue operand = generateExpression(unary.operand);
    if (operandType != nullptr && operandType->kind == TypeKind::Union) {
      llvm::Value *tag = builder_->CreateLoad(i64Type_, bytePointer(asRaw(operand), 0));
      llvm::Value *undefinedText =
          builder_->CreatePtrToInt(internString("undefined"), i64Type_);
      llvm::Value *nullText = builder_->CreatePtrToInt(internString("object"), i64Type_);
      llvm::Value *payloadText = builder_->CreatePtrToInt(
          internString(typeofName(unionPayload(operandType))), i64Type_);
      llvm::Value *nullish = builder_->CreateSelect(
          builder_->CreateICmpEQ(tag, i64(unionTagUndefined)), undefinedText, nullText);
      llvm::Value *result = builder_->CreateSelect(
          builder_->CreateICmpULT(tag, i64(unionTagPayload)), nullish, payloadText);
      return rawValue(result);
    }
    return rawValue(builder_->CreatePtrToInt(internString(typeofName(operandType)), i64Type_));
  }
  if (unary.op == "void") {
    (void)generateExpression(unary.operand);
    return rawValue(i64(0));
  }
  throw errors::ErrorException{errors::ErrorStatus{
      errors::ErrorCode::TYPE_UNSUPPORTED_OPERATOR, unary.op, {}}};
}

IrGenerator::EmitValue IrGenerator::generateBinary(const reader::BinaryExpr &binary) {
  if (binary.op == "==" || binary.op == "!=" || binary.op == "===" || binary.op == "!==")
    return generateEquality(binary);
  if (binary.op == "&&" || binary.op == "||" || binary.op == "??")
    return generateLogical(binary);
  if (binary.op == "+" || binary.op == "-" || binary.op == "*" || binary.op == "/" ||
      binary.op == "%" || binary.op == "**")
    return generateArithmetic(binary);
  if (binary.op == "<" || binary.op == ">" || binary.op == "<=" || binary.op == ">=")
    return generateRelational(binary);
  return generateBitwise(binary);
}

IrGenerator::EmitValue IrGenerator::generateArithmetic(const reader::BinaryExpr &binary) {
  EmitValue left = generateExpression(binary.left);
  EmitValue right = generateExpression(binary.right);
  if (binary.op == "+") return EmitValue{builder_->CreateFAdd(left.value, right.value), true};
  if (binary.op == "-") return EmitValue{builder_->CreateFSub(left.value, right.value), true};
  if (binary.op == "*") return EmitValue{builder_->CreateFMul(left.value, right.value), true};
  if (binary.op == "/") return EmitValue{builder_->CreateFDiv(left.value, right.value), true};
  if (binary.op == "%")
    return EmitValue{builder_->CreateCall(moduloFunction(), {left.value, right.value}), true};
  return EmitValue{builder_->CreateCall(powerFunction(), {left.value, right.value}), true};
}

IrGenerator::EmitValue IrGenerator::generateBitwise(const reader::BinaryExpr &binary) {
  EmitValue left = generateExpression(binary.left);
  EmitValue right = generateExpression(binary.right);
  llvm::Value *leftValue = toInt32(left.value);
  llvm::Value *rightValue = toInt32(right.value);
  const bool shift = binary.op == "<<" || binary.op == ">>" || binary.op == ">>>";
  if (shift) rightValue = builder_->CreateAnd(rightValue, i32(31));
  if (binary.op == "&") {
    llvm::Value *value = builder_->CreateAnd(leftValue, rightValue);
    return EmitValue{builder_->CreateSIToFP(builder_->CreateSExt(value, i64Type_), doubleType_),
                     true};
  }
  if (binary.op == "|") {
    llvm::Value *value = builder_->CreateOr(leftValue, rightValue);
    return EmitValue{builder_->CreateSIToFP(builder_->CreateSExt(value, i64Type_), doubleType_),
                     true};
  }
  if (binary.op == "^") {
    llvm::Value *value = builder_->CreateXor(leftValue, rightValue);
    return EmitValue{builder_->CreateSIToFP(builder_->CreateSExt(value, i64Type_), doubleType_),
                     true};
  }
  if (binary.op == "<<") {
    llvm::Value *value = builder_->CreateShl(leftValue, rightValue);
    return EmitValue{builder_->CreateSIToFP(builder_->CreateSExt(value, i64Type_), doubleType_),
                     true};
  }
  if (binary.op == ">>") {
    llvm::Value *value = builder_->CreateAShr(leftValue, rightValue);
    return EmitValue{builder_->CreateSIToFP(builder_->CreateSExt(value, i64Type_), doubleType_),
                     true};
  }
  llvm::Value *value = builder_->CreateLShr(leftValue, rightValue);
  return EmitValue{builder_->CreateUIToFP(builder_->CreateZExt(value, i64Type_), doubleType_),
                   true};
}

IrGenerator::EmitValue IrGenerator::generateRelational(const reader::BinaryExpr &binary) {
  EmitValue left = generateExpression(binary.left);
  EmitValue right = generateExpression(binary.right);
  llvm::Value *comparison = nullptr;
  if (binary.op == "<")
    comparison = builder_->CreateFCmpOLT(left.value, right.value);
  else if (binary.op == ">")
    comparison = builder_->CreateFCmpOGT(left.value, right.value);
  else if (binary.op == "<=")
    comparison = builder_->CreateFCmpOLE(left.value, right.value);
  else
    comparison = builder_->CreateFCmpOGE(left.value, right.value);
  return rawValue(builder_->CreateZExt(comparison, i64Type_));
}

IrGenerator::EmitValue IrGenerator::generateEquality(const reader::BinaryExpr &binary) {
  const bool negated = binary.op == "!=" || binary.op == "!==";
  const TypePtr leftType = resolve(typeOf(binary.left));
  const TypePtr rightType = resolve(typeOf(binary.right));

  if (leftType != nullptr && leftType->kind == TypeKind::Number) {
    EmitValue left = generateExpression(binary.left);
    EmitValue right = generateExpression(binary.right);
    llvm::Value *comparison = negated ? builder_->CreateFCmpUNE(left.value, right.value)
                                      : builder_->CreateFCmpOEQ(left.value, right.value);
    return rawValue(builder_->CreateZExt(comparison, i64Type_));
  }

  const bool leftUnion = leftType != nullptr && leftType->kind == TypeKind::Union;
  const bool rightUnion = rightType != nullptr && rightType->kind == TypeKind::Union;
  if (leftUnion || rightUnion) {
    if (leftUnion && rightUnion) {
      EmitValue left = generateExpression(binary.left);
      EmitValue right = generateExpression(binary.right);
      llvm::Value *leftTag = builder_->CreateLoad(i64Type_, bytePointer(asRaw(left), 0));
      llvm::Value *rightTag = builder_->CreateLoad(i64Type_, bytePointer(asRaw(right), 0));
      llvm::BasicBlock *payloadBlock = createBlock("eq.payload");
      llvm::BasicBlock *unequalBlock = createBlock("eq.unequal");
      llvm::BasicBlock *endBlock = createBlock("eq.end");
      builder_->CreateCondBr(builder_->CreateICmpEQ(leftTag, rightTag), payloadBlock,
                             unequalBlock);
      builder_->SetInsertPoint(unequalBlock);
      llvm::Value *unequalValue = negated ? i64(1) : i64(0);
      builder_->CreateBr(endBlock);
      builder_->SetInsertPoint(payloadBlock);
      llvm::Value *payloadResult = nullptr;
      if (isNumber(unionPayload(leftType))) {
        llvm::Value *leftPayload = builder_->CreateBitCast(
            builder_->CreateLoad(i64Type_, bytePointer(asRaw(left), 8)), doubleType_);
        llvm::Value *rightPayload = builder_->CreateBitCast(
            builder_->CreateLoad(i64Type_, bytePointer(asRaw(right), 8)), doubleType_);
        llvm::Value *comparison =
            negated ? builder_->CreateFCmpUNE(leftPayload, rightPayload)
                    : builder_->CreateFCmpOEQ(leftPayload, rightPayload);
        payloadResult = builder_->CreateZExt(comparison, i64Type_);
      } else {
        llvm::Value *leftPayload =
            builder_->CreateLoad(i64Type_, bytePointer(asRaw(left), 8));
        llvm::Value *rightPayload =
            builder_->CreateLoad(i64Type_, bytePointer(asRaw(right), 8));
        llvm::Value *comparison =
            negated ? builder_->CreateICmpNE(leftPayload, rightPayload)
                    : builder_->CreateICmpEQ(leftPayload, rightPayload);
        payloadResult = builder_->CreateZExt(comparison, i64Type_);
      }
      builder_->CreateBr(endBlock);
      builder_->SetInsertPoint(endBlock);
      llvm::PHINode *phi = builder_->CreatePHI(i64Type_, 2);
      phi->addIncoming(payloadResult, payloadBlock);
      phi->addIncoming(unequalValue, unequalBlock);
      return rawValue(phi);
    }
    const bool unionOnLeft = leftUnion;
    const reader::ExprPtr &unionSide = unionOnLeft ? binary.left : binary.right;
    const reader::ExprPtr &otherSide = unionOnLeft ? binary.right : binary.left;
    const TypePtr otherType = unionOnLeft ? rightType : leftType;
    EmitValue unionValue = generateExpression(unionSide);
    (void)generateExpression(otherSide);
    llvm::Value *tag = builder_->CreateLoad(i64Type_, bytePointer(asRaw(unionValue), 0));
    const std::uint64_t expected =
        otherType != nullptr && otherType->kind == TypeKind::Null ? unionTagNull
                                                                  : unionTagUndefined;
    llvm::Value *comparison = negated
                                  ? builder_->CreateICmpNE(tag, i64(expected))
                                  : builder_->CreateICmpEQ(tag, i64(expected));
    return rawValue(builder_->CreateZExt(comparison, i64Type_));
  }

  EmitValue left = generateExpression(binary.left);
  EmitValue right = generateExpression(binary.right);
  llvm::Value *comparison = negated
                                ? builder_->CreateICmpNE(asRaw(left), asRaw(right))
                                : builder_->CreateICmpEQ(asRaw(left), asRaw(right));
  return rawValue(builder_->CreateZExt(comparison, i64Type_));
}

IrGenerator::EmitValue IrGenerator::generateLogical(const reader::BinaryExpr &binary) {
  if (binary.op == "??") {
    const TypePtr leftType = resolve(typeOf(binary.left));
    if (leftType != nullptr && leftType->kind == TypeKind::Union) {
      EmitValue left = generateExpression(binary.left);
      TypePtr resultType = unionPayload(leftType);
      if (resultType == nullptr) resultType = resolve(typeOf(binary.right));
      llvm::Value *base = asRaw(left);
      llvm::Value *slot = slotAlloca(pushTemp());
      storeSlot(slot, rawValue(base));
      llvm::Value *tag = builder_->CreateLoad(i64Type_, bytePointer(base, 0));
      llvm::BasicBlock *payloadBlock = createBlock("coalesce.payload");
      llvm::BasicBlock *rightBlock = createBlock("coalesce.right");
      llvm::BasicBlock *endBlock = createBlock("coalesce.end");
      builder_->CreateCondBr(builder_->CreateICmpUGE(tag, i64(unionTagPayload)), payloadBlock,
                             rightBlock);
      builder_->SetInsertPoint(payloadBlock);
      llvm::Value *payload = builder_->CreateLoad(i64Type_, bytePointer(base, 8));
      storeSlot(slot, asTyped(payload, resultType));
      builder_->CreateBr(endBlock);
      builder_->SetInsertPoint(rightBlock);
      EmitValue right = generateExpression(binary.right);
      right = coerce(resultType, typeOf(binary.right), right);
      storeSlot(slot, right);
      builder_->CreateBr(endBlock);
      builder_->SetInsertPoint(endBlock);
      EmitValue result = loadSlot(slot, resultType);
      popTemp();
      return result;
    }
    if (leftType != nullptr &&
        (leftType->kind == TypeKind::Null || leftType->kind == TypeKind::Undefined)) {
      (void)generateExpression(binary.left);
      return generateExpression(binary.right);
    }
    return generateExpression(binary.left);
  }

  EmitValue left = generateExpression(binary.left);
  llvm::Value *slot = slotAlloca(pushTemp());
  storeSlot(slot, rawValue(asRaw(left)));
  llvm::BasicBlock *rightBlock = createBlock("logical.right");
  llvm::BasicBlock *shortBlock = createBlock("logical.short");
  llvm::BasicBlock *endBlock = createBlock("logical.end");
  llvm::Value *test = builder_->CreateICmpNE(asRaw(left), i64(0));
  if (binary.op == "&&")
    builder_->CreateCondBr(test, rightBlock, shortBlock);
  else
    builder_->CreateCondBr(test, shortBlock, rightBlock);
  builder_->SetInsertPoint(rightBlock);
  EmitValue right = generateExpression(binary.right);
  storeSlot(slot, rawValue(asRaw(right)));
  builder_->CreateBr(endBlock);
  builder_->SetInsertPoint(shortBlock);
  builder_->CreateBr(endBlock);
  builder_->SetInsertPoint(endBlock);
  EmitValue result = loadSlot(slot, nullptr);
  popTemp();
  return result;
}

IrGenerator::EmitValue IrGenerator::generateUpdate(const reader::UpdateExpr &update) {
  llvm::Value *address = nullptr;
  if (const auto *member = std::get_if<reader::MemberExpr>(&update.target->node)) {
    address = generateMemberAddress(*member);
    address = builder_->CreateIntToPtr(address, ptrType_);
  } else {
    const auto &identifier = std::get<reader::IdentifierExpr>(update.target->node);
    if (Slot *slot = lookupSlot(identifier.name)) {
      address = slot->address;
    } else {
      const auto global = globals_.find(identifier.name);
      if (global != globals_.end()) address = global->second;
    }
  }
  if (address == nullptr)
    throw errors::ErrorException{errors::ErrorStatus{
        errors::ErrorCode::TYPE_INVALID_ASSIGNMENT_TARGET, {}, update.target->range}};
  EmitValue previous = loadSlot(address, types_->number());
  EmitValue one = numberValue(1.0);
  EmitValue updated{builder_->CreateFSub(previous.value, one.value), true};
  if (update.op == "++") updated.value = builder_->CreateFAdd(previous.value, one.value);
  storeSlot(address, updated);
  if (update.prefix) return updated;
  return previous;
}

IrGenerator::EmitValue IrGenerator::generateAssignment(const reader::AssignmentExpr &assignment) {
  const auto *member = std::get_if<reader::MemberExpr>(&assignment.target->node);
  llvm::Value *address = nullptr;
  if (member != nullptr) {
    address = builder_->CreateIntToPtr(generateMemberAddress(*member), ptrType_);
  } else {
    const auto &identifier = std::get<reader::IdentifierExpr>(assignment.target->node);
    if (Slot *slot = lookupSlot(identifier.name)) {
      address = slot->address;
    } else {
      const auto global = globals_.find(identifier.name);
      if (global != globals_.end()) address = global->second;
    }
  }
  if (address == nullptr)
    throw errors::ErrorException{errors::ErrorStatus{
        errors::ErrorCode::TYPE_INVALID_ASSIGNMENT_TARGET, {}, assignment.target->range}};

  if (assignment.op == "=") {
    const TypePtr targetType = typeOf(assignment.target);
    EmitValue value = generateExpression(assignment.value);
    value = coerce(targetType, typeOf(assignment.value), value);
    storeSlot(address, value);
    return asTyped(asRaw(value), targetType);
  }

  EmitValue previous = loadSlot(address, types_->number());
  EmitValue right = generateExpression(assignment.value);
  llvm::Value *result = nullptr;
  if (assignment.op == "+=")
    result = builder_->CreateFAdd(previous.value, right.value);
  else if (assignment.op == "-=")
    result = builder_->CreateFSub(previous.value, right.value);
  else if (assignment.op == "*=")
    result = builder_->CreateFMul(previous.value, right.value);
  else if (assignment.op == "/=")
    result = builder_->CreateFDiv(previous.value, right.value);
  else
    result = builder_->CreateCall(moduloFunction(), {previous.value, right.value});
  EmitValue stored{result, true};
  storeSlot(address, stored);
  return stored;
}

llvm::Value *IrGenerator::generateMemberAddress(const reader::MemberExpr &member) {
  const TypePtr objectType = resolve(typeOf(member.object));
  if (objectType != nullptr && objectType->kind == TypeKind::Array) {
    EmitValue object = generateExpression(member.object);
    EmitValue indexValue = generateExpression(member.computedProperty);
    llvm::Value *index = builder_->CreateIntrinsic(llvm::Intrinsic::fptosi_sat,
                                                   {i64Type_, doubleType_}, {indexValue.value});
    llvm::Value *base = asRaw(object);
    llvm::Value *count = builder_->CreateLoad(i64Type_, bytePointer(base, 0));
    llvm::BasicBlock *insideBlock = createBlock("index.inside");
    llvm::BasicBlock *outsideBlock = createBlock("index.outside");
    builder_->CreateCondBr(builder_->CreateICmpULT(index, count), insideBlock, outsideBlock);
    builder_->SetInsertPoint(outsideBlock);
    builder_->CreateCall(boundsFunction(), {});
    builder_->CreateUnreachable();
    builder_->SetInsertPoint(insideBlock);
    llvm::Value *offset =
        builder_->CreateAdd(i64(8), builder_->CreateMul(index, i64(8)));
    llvm::Value *pointer = dynamicPointer(base, offset);
    return builder_->CreatePtrToInt(pointer, i64Type_);
  }
  EmitValue object = generateExpression(member.object);
  const auto found = annotations_->structFieldIndices.find(&member);
  if (found == annotations_->structFieldIndices.end())
    throw errors::ErrorException{errors::ErrorStatus{
        errors::ErrorCode::TYPE_MEMBER_ACCESS_NOT_SUPPORTED, member.property, member.object->range}};
  return builder_->CreateAdd(asRaw(object), i64(8 * found->second));
}

IrGenerator::EmitValue IrGenerator::generateCall(const reader::ExprPtr &expression,
                                                  const reader::CallExpr &call) {
  const auto *identifier = std::get_if<reader::IdentifierExpr>(&call.callee->node);
  const bool direct = identifier != nullptr && lookupSlot(identifier->name) == nullptr &&
                      functions_.contains(identifier->name);
  const builtins::BuiltinSpec *builtin =
      identifier != nullptr && lookupSlot(identifier->name) == nullptr && !direct
          ? builtins::BuiltinRegistry::instance().find(identifier->name)
          : nullptr;

  if (builtin != nullptr) {
    lastValue_ = rawValue(i64(0));
    builtin->emit(*this, call);
    return lastValue_;
  }

  llvm::Value *callee = nullptr;
  if (!direct) callee = asRaw(generateExpression(call.callee));

  std::vector<llvm::Value *> arguments;
  arguments.reserve(call.arguments.size());
  for (const auto &argument : call.arguments) {
    EmitValue value = generateExpression(argument);
    const auto destination = annotations_->argumentTypes.find(argument.get());
    if (destination != annotations_->argumentTypes.end())
      value = coerce(destination->second, typeOf(argument), value);
    arguments.push_back(asRaw(value));
  }

  llvm::Value *result = nullptr;
  if (direct) {
    result = builder_->CreateCall(functions_.at(identifier->name), arguments);
  } else {
    llvm::FunctionType *type =
        llvm::FunctionType::get(i64Type_,
                                std::vector<llvm::Type *>(arguments.size(), i64Type_), false);
    result = builder_->CreateCall(type,
                                  builder_->CreateIntToPtr(callee, ptrType_), arguments);
  }
  return asTyped(result, typeOf(expression));
}

void IrGenerator::emitFormatValue(EmitValue value, types::TypePtr type) {
  const TypePtr resolved = resolve(std::move(type));
  const TypeKind kind = resolved == nullptr ? TypeKind::Undefined : resolved->kind;
  switch (kind) {
  case TypeKind::Number:
    builder_->CreateCall(numberWriter(),
                         {builder_->CreateBitCast(asRaw(value), doubleType_)});
    return;
  case TypeKind::Boolean:
    builder_->CreateCall(boolWriter(), {asRaw(value)});
    return;
  case TypeKind::Null:
    builder_->CreateCall(nullWriter(), {});
    return;
  case TypeKind::Array:
    emitFormatArray(value, resolved);
    return;
  case TypeKind::Union:
    emitFormatUnion(value, resolved);
    return;
  case TypeKind::Undefined:
  case TypeKind::Never:
  case TypeKind::Variable:
    builder_->CreateCall(undefinedWriter(), {});
    return;
  default:
    builder_->CreateCall(stringWriter(), {asRaw(value)});
    return;
  }
}

void IrGenerator::emitFormatArray(EmitValue value, types::TypePtr type) {
  const TypePtr resolved = resolve(std::move(type));
  TypePtr elementType =
      resolved != nullptr && resolved->element != nullptr ? resolved->element : nullptr;
  llvm::Value *base = asRaw(value);
  llvm::Value *indexSlot = slotAlloca(pushTemp());
  builder_->CreateStore(i64(0), indexSlot);

  builder_->CreateCall(lbracketWriter(), {});
  llvm::BasicBlock *loopBlock = createBlock("array.loop");
  llvm::BasicBlock *bodyBlock = createBlock("array.body");
  llvm::BasicBlock *separatorBlock = createBlock("array.separator");
  llvm::BasicBlock *elementBlock = createBlock("array.element");
  llvm::BasicBlock *doneBlock = createBlock("array.done");
  builder_->CreateBr(loopBlock);

  builder_->SetInsertPoint(loopBlock);
  llvm::Value *index = builder_->CreateLoad(i64Type_, indexSlot);
  llvm::Value *count = builder_->CreateLoad(i64Type_, bytePointer(base, 0));
  builder_->CreateCondBr(builder_->CreateICmpULT(index, count), bodyBlock, doneBlock);

  builder_->SetInsertPoint(bodyBlock);
  builder_->CreateCondBr(builder_->CreateICmpNE(index, i64(0)), separatorBlock, elementBlock);

  builder_->SetInsertPoint(separatorBlock);
  builder_->CreateCall(separatorWriter(), {});
  builder_->CreateBr(elementBlock);

  builder_->SetInsertPoint(elementBlock);
  llvm::Value *current = builder_->CreateLoad(i64Type_, indexSlot);
  llvm::Value *offset = builder_->CreateAdd(i64(8), builder_->CreateMul(current, i64(8)));
  llvm::Value *address = dynamicPointer(base, offset);
  llvm::Value *bits = builder_->CreateLoad(i64Type_, address);
  emitFormatValue(asTyped(bits, elementType), elementType);
  llvm::Value *next = builder_->CreateAdd(builder_->CreateLoad(i64Type_, indexSlot), i64(1));
  builder_->CreateStore(next, indexSlot);
  builder_->CreateBr(loopBlock);

  builder_->SetInsertPoint(doneBlock);
  builder_->CreateCall(rbracketWriter(), {});
  popTemp();
}

void IrGenerator::emitFormatUnion(EmitValue value, types::TypePtr type) {
  const TypePtr resolved = resolve(std::move(type));
  const TypePtr payloadType = unionPayload(resolved);
  llvm::Value *base = asRaw(value);
  llvm::Value *tag = builder_->CreateLoad(i64Type_, bytePointer(base, 0));
  llvm::BasicBlock *payloadBlock = createBlock("union.payload");
  llvm::BasicBlock *nullishBlock = createBlock("union.nullish");
  llvm::BasicBlock *nullBlock = createBlock("union.null");
  llvm::BasicBlock *undefinedBlock = createBlock("union.undefined");
  llvm::BasicBlock *endBlock = createBlock("union.end");
  builder_->CreateCondBr(builder_->CreateICmpULT(tag, i64(unionTagPayload)), nullishBlock,
                         payloadBlock);

  builder_->SetInsertPoint(nullishBlock);
  builder_->CreateCondBr(builder_->CreateICmpEQ(tag, i64(unionTagUndefined)), undefinedBlock,
                         nullBlock);

  builder_->SetInsertPoint(undefinedBlock);
  builder_->CreateCall(undefinedWriter(), {});
  builder_->CreateBr(endBlock);

  builder_->SetInsertPoint(nullBlock);
  builder_->CreateCall(nullWriter(), {});
  builder_->CreateBr(endBlock);

  builder_->SetInsertPoint(payloadBlock);
  llvm::Value *payload = builder_->CreateLoad(i64Type_, bytePointer(base, 8));
  emitFormatValue(asTyped(payload, payloadType), payloadType);
  if (!terminated()) builder_->CreateBr(endBlock);

  builder_->SetInsertPoint(endBlock);
}

void IrGenerator::emitArgument(const reader::ExprPtr &) {}

void IrGenerator::moveResultToArgumentRegister() {}

void IrGenerator::clearResult() { lastValue_ = rawValue(i64(0)); }

void IrGenerator::declareHelper(std::string, std::string) {}

void IrGenerator::useHelper(const std::string &) {}

void IrGenerator::callHelper(const std::string &) {}

void IrGenerator::formatValue(const reader::ExprPtr &expression) {
  EmitValue value = generateExpression(expression);
  emitFormatValue(value, typeOf(expression));
}

void IrGenerator::formatArray(const reader::ExprPtr &expression) {
  EmitValue value = generateExpression(expression);
  emitFormatArray(value, typeOf(expression));
}

void IrGenerator::formatNewline() {
  builder_->CreateCall(
      stringWriter(), {builder_->CreatePtrToInt(internString("\n"), i64Type_)});
}

void IrGenerator::generateFunction(const reader::FunctionDeclaration &declaration,
                                   llvm::Function *function) {
  currentFunction_ = function;
  scopes_.clear();
  slotAllocas_.clear();
  slotCount_ = 0;
  freeSlots_.clear();
  temps_.clear();
  loops_.clear();
  builder_->SetInsertPoint(llvm::BasicBlock::Create(*context_, "entry", function));
  beginScope();
  varTargetScopeIndex_ = 0;
  returnBlock_ = llvm::BasicBlock::Create(*context_, "return", function);
  returnSlot_ = createEntryAlloca();
  const auto found = annotations_->functionResults.find(&declaration);
  returnType_ = found != annotations_->functionResults.end() ? found->second
                                                            : types_->undefined();
  for (std::size_t index = 0; index < declaration.parameters.size(); ++index) {
    const long long slot = allocateSlot();
    llvm::AllocaInst *address = slotAlloca(slot);
    builder_->CreateStore(function->getArg(index), address);
    scopes_.back().slots.insert_or_assign(declaration.parameters[index], Slot{address, false});
  }
  hoistVarSlots(declaration.body);
  generateStatements(declaration.body);
  if (!terminated()) {
    EmitValue value = rawValue(i64(0));
    const TypePtr resolved = resolve(returnType_);
    if (resolved != nullptr && resolved->kind == TypeKind::Union)
      value = emitZeroValue(returnType_);
    storeSlot(returnSlot_, value);
    builder_->CreateBr(returnBlock_);
  }
  builder_->SetInsertPoint(returnBlock_);
  builder_->CreateRet(builder_->CreateLoad(i64Type_, returnSlot_));
  currentFunction_ = nullptr;
}

void IrGenerator::generateEntry(const reader::Program &program) {
  llvm::Function *function = llvm::Function::Create(
      llvm::FunctionType::get(voidType_, false), llvm::Function::ExternalLinkage, "jsmm_entry",
      *module_);
  function->setDSOLocal(true);
  currentFunction_ = function;
  scopes_.clear();
  slotAllocas_.clear();
  slotCount_ = 0;
  freeSlots_.clear();
  temps_.clear();
  loops_.clear();
  returnBlock_ = nullptr;
  returnSlot_ = nullptr;
  returnType_ = nullptr;
  generatingEntry_ = true;
  builder_->SetInsertPoint(llvm::BasicBlock::Create(*context_, "entry", function));
  beginScope();
  varTargetScopeIndex_ = 0;
  for (const auto &entry : globals_) scopes_.back().slots.insert_or_assign(
      entry.first, Slot{entry.second, false});
  for (const auto &entry : globals_) {
    TypePtr type;
    const auto found = annotations_->topLevelTypes.find(entry.first);
    if (found != annotations_->topLevelTypes.end()) type = found->second;
    storeSlot(entry.second, emitZeroValue(type));
  }
  hoistVarSlots(program.statements);
  generateStatements(program.statements);
  if (!terminated()) builder_->CreateRetVoid();
  generatingEntry_ = false;
  currentFunction_ = nullptr;
}

LoweredModule IrGenerator::generate(const reader::Program &program,
                                    const types::Annotations &annotations,
                                    types::TypeContext &context) {
  auto shared = std::make_shared<llvm::LLVMContext>();
  context_ = shared.get();
  annotations_ = &annotations;
  types_ = &context;
  module_ = new llvm::Module("jsmm.program", *context_);
  builder_ = std::make_unique<llvm::IRBuilder<>>(*context_);
  doubleType_ = llvm::Type::getDoubleTy(*context_);
  i64Type_ = llvm::Type::getInt64Ty(*context_);
  i32Type_ = llvm::Type::getInt32Ty(*context_);
  i8Type_ = llvm::Type::getInt8Ty(*context_);
  ptrType_ = llvm::PointerType::get(*context_, 0);
  voidType_ = llvm::Type::getVoidTy(*context_);
  module_->setTargetTriple(llvm::Triple("x86_64-unknown-linux-gnu"));
  emptyString_ = internString("");

  std::vector<std::string> globalNames;
  for (const auto &statement : program.statements) {
    const auto *declaration = std::get_if<reader::VariableDeclaration>(&statement.node);
    if (declaration == nullptr) continue;
    for (const auto &declarator : declaration->declarations) globalNames.push_back(declarator.name);
  }
  std::vector<const reader::VariableDeclarator *> hoisted;
  collectVarDeclarations(program.statements, hoisted);
  for (const reader::VariableDeclarator *declarator : hoisted) globalNames.push_back(declarator->name);
  for (const std::string &name : globalNames) (void)globalFor(name);

  std::vector<std::pair<const reader::FunctionDeclaration *, llvm::Function *>> declarations;
  long long index = 0;
  for (const auto &statement : program.statements) {
    const auto *declaration = std::get_if<reader::FunctionDeclaration>(&statement.node);
    if (declaration == nullptr) continue;
    llvm::FunctionType *type = llvm::FunctionType::get(
        i64Type_, std::vector<llvm::Type *>(declaration->parameters.size(), i64Type_), false);
    llvm::Function *function = llvm::Function::Create(
        type, llvm::Function::ExternalLinkage,
        "jsfn_" + declaration->name + "_" + std::to_string(index++), *module_);
    function->setDSOLocal(true);
    for (std::size_t parameter = 0; parameter < declaration->parameters.size(); ++parameter)
      function->getArg(parameter)->setName(declaration->parameters[parameter]);
    functions_.insert_or_assign(declaration->name, function);
    declarations.emplace_back(declaration, function);
  }

  for (const auto &entry : declarations) generateFunction(*entry.first, entry.second);
  generateEntry(program);

  LoweredModule result;
  result.context = shared;
  result.module = std::unique_ptr<llvm::Module>(module_);
  return result;
}

}
