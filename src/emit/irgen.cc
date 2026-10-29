#include "emit/irgen.hh"

#include "errors/errors.hh"

#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Verifier.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/Triple.h>

#include <algorithm>
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

constexpr long long unionTagNull = 0;
constexpr long long unionTagNumber = 1;
constexpr long long unionTagString = 2;
constexpr long long unionTagBoolean = 3;
constexpr long long unionTagPayload = 4;

} // namespace

types::TypePtr IrGenerator::typeOf(const reader::ExprPtr &expression) const {
  return annotations_->expressionTypes.at(expression.get());
}

types::TypePtr IrGenerator::resolve(types::TypePtr type) const {
  return types_->resolve(std::move(type));
}

bool IrGenerator::isNumber(types::TypePtr type) const {
  const TypePtr resolved = resolve(std::move(type));
  if (resolved == nullptr) return false;
  return resolved->kind == TypeKind::Number || resolved->kind == TypeKind::Byte;
}

bool IrGenerator::isString(types::TypePtr type) const {
  const TypePtr resolved = resolve(std::move(type));
  return resolved != nullptr && resolved->kind == TypeKind::String;
}

types::TypePtr IrGenerator::unionPayload(types::TypePtr type) const {
  type = resolve(std::move(type));
  if (type == nullptr || type->kind != TypeKind::Union) return nullptr;
  for (const TypePtr &member : type->members) {
    const TypePtr resolved = resolve(member);
    if (resolved == nullptr) continue;
    if (resolved->kind == TypeKind::Null || resolved->kind == TypeKind::Never) continue;
    return resolved;
  }
  return nullptr;
}

std::string IrGenerator::typeofName(types::TypePtr type) const {
  type = resolve(type);
  if (type == nullptr) return "null";
  switch (type->kind) {
  case TypeKind::Number: return "number";
  case TypeKind::String: return "string";
  case TypeKind::Boolean: return "boolean";
  case TypeKind::Byte: return "byte";
  case TypeKind::Bytes: return "bytes";
  case TypeKind::Null: return "null";
  case TypeKind::Never: return "null";
  case TypeKind::Function: return "function";
  case TypeKind::Pointer: return "pointer";
  case TypeKind::Struct:
  case TypeKind::Array:
  case TypeKind::Union: return "object";
  case TypeKind::Variable: return "null";
  }
  return "null";
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
  llvm::Value *wide =
      builder_->CreateIntrinsic(llvm::Intrinsic::fptosi_sat, {i64Type_, doubleType_}, {value});
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
  if (slotAllocas_.size() <= static_cast<std::size_t>(index))
    slotAllocas_.resize(index + 1, nullptr);
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
  auto *global =
      new llvm::GlobalVariable(*module_, structType, true, llvm::GlobalValue::InternalLinkage,
                               initializer, "jsmm.str." + std::to_string(strings_.size()));
  global->setAlignment(llvm::Align(8));
  global->setDSOLocal(true);
  strings_[text] = global;
  return global;
}

llvm::GlobalVariable *IrGenerator::globalFor(const std::string &name) {
  const auto found = globals_.find(name);
  if (found != globals_.end()) return found->second;
  auto *global = new llvm::GlobalVariable(
      *module_, i64Type_, false, llvm::GlobalValue::InternalLinkage, i64(0), "jsmm.g." + name);
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

llvm::Function *IrGenerator::boundsFunction() { return runtimeFunction("jsmm_oob", voidType_, {}); }

llvm::Function *IrGenerator::stringEqualFunction() {
  return runtimeFunction("jsmm_str_eq", i64Type_, {i64Type_, i64Type_});
}

llvm::Function *IrGenerator::stringConcatFunction() {
  return runtimeFunction("jsmm_str_cat", i64Type_, {i64Type_, i64Type_});
}

llvm::Function *IrGenerator::stringCompareFunction() {
  return runtimeFunction("jsmm_str_cmp", i64Type_, {i64Type_, i64Type_});
}

llvm::Function *IrGenerator::throwFunction() {
  return runtimeFunction("jsmm_throw", voidType_, {});
}

llvm::Value *IrGenerator::stringEquals(llvm::Value *left, llvm::Value *right) {
  llvm::Value *result = builder_->CreateCall(stringEqualFunction(), {left, right});
  return builder_->CreateICmpNE(result, i64(0));
}

long long IrGenerator::unionTagFor(types::TypePtr type) const {
  const TypePtr resolved = resolve(std::move(type));
  if (resolved == nullptr) return unionTagPayload;
  switch (resolved->kind) {
  case TypeKind::Null: return unionTagNull;
  case TypeKind::Number:
  case TypeKind::Byte: return unionTagNumber;
  case TypeKind::String: return unionTagString;
  case TypeKind::Boolean: return unionTagBoolean;
  default: return unionTagPayload;
  }
}

llvm::Value *IrGenerator::emitTruthy(EmitValue value, types::TypePtr type) {
  const TypePtr resolved = resolve(std::move(type));
  llvm::Value *zeroFloat = llvm::ConstantFP::get(doubleType_, 0.0);
  if (resolved == nullptr) return builder_->CreateICmpNE(asRaw(value), i64(0));
  switch (resolved->kind) {
  case TypeKind::Null:
  case TypeKind::Never: return llvm::ConstantInt::getFalse(*context_);
  case TypeKind::Number:
  case TypeKind::Byte: return builder_->CreateFCmpONE(value.value, zeroFloat);
  case TypeKind::String: {
    llvm::Value *length = builder_->CreateLoad(i64Type_, bytePointer(asRaw(value), 0));
    return builder_->CreateICmpNE(length, i64(0));
  }
  case TypeKind::Union: {
    llvm::Value *base = asRaw(value);
    llvm::Value *tag = builder_->CreateLoad(i64Type_, bytePointer(base, 0));
    llvm::Value *payload = builder_->CreateLoad(i64Type_, bytePointer(base, 8));
    llvm::Value *isNumber = builder_->CreateICmpEQ(tag, i64(unionTagNumber));
    llvm::Value *isString = builder_->CreateICmpEQ(tag, i64(unionTagString));
    llvm::Value *stringPointer =
        builder_->CreateSelect(isString, payload, builder_->CreatePtrToInt(emptyString_, i64Type_));
    llvm::Value *length =
        builder_->CreateLoad(i64Type_, builder_->CreateIntToPtr(stringPointer, ptrType_));
    llvm::Value *stringTruthy = builder_->CreateICmpNE(length, i64(0));
    llvm::Value *numberTruthy =
        builder_->CreateFCmpONE(builder_->CreateBitCast(payload, doubleType_), zeroFloat);
    llvm::Value *payloadTruthy = builder_->CreateICmpNE(payload, i64(0));
    llvm::Value *truthy = builder_->CreateSelect(
        isNumber, numberTruthy, builder_->CreateSelect(isString, stringTruthy, payloadTruthy));
    return builder_->CreateAnd(builder_->CreateICmpNE(tag, i64(unionTagNull)), truthy);
  }
  default: return builder_->CreateICmpNE(asRaw(value), i64(0));
  }
}

llvm::Value *IrGenerator::emitNullish(EmitValue value, types::TypePtr type) {
  const TypePtr resolved = resolve(std::move(type));
  if (resolved == nullptr) return llvm::ConstantInt::getFalse(*context_);
  switch (resolved->kind) {
  case TypeKind::Null:
  case TypeKind::Never: return llvm::ConstantInt::getTrue(*context_);
  case TypeKind::Union: {
    llvm::Value *tag = builder_->CreateLoad(i64Type_, bytePointer(asRaw(value), 0));
    return builder_->CreateICmpEQ(tag, i64(unionTagNull));
  }
  default: return llvm::ConstantInt::getFalse(*context_);
  }
}

llvm::Value *IrGenerator::emitUnionEquality(EmitValue left, types::TypePtr leftType,
                                            EmitValue right, types::TypePtr rightType) {
  llvm::Value *leftTag = nullptr;
  llvm::Value *leftPayload = nullptr;
  if (leftType != nullptr && leftType->kind == TypeKind::Union) {
    llvm::Value *base = asRaw(left);
    leftTag = builder_->CreateLoad(i64Type_, bytePointer(base, 0));
    leftPayload = builder_->CreateLoad(i64Type_, bytePointer(base, 8));
  } else {
    leftTag = i64(static_cast<std::uint64_t>(unionTagFor(leftType)));
    leftPayload = asRaw(left);
  }
  llvm::Value *rightTag = nullptr;
  llvm::Value *rightPayload = nullptr;
  if (rightType != nullptr && rightType->kind == TypeKind::Union) {
    llvm::Value *base = asRaw(right);
    rightTag = builder_->CreateLoad(i64Type_, bytePointer(base, 0));
    rightPayload = builder_->CreateLoad(i64Type_, bytePointer(base, 8));
  } else {
    rightTag = i64(static_cast<std::uint64_t>(unionTagFor(rightType)));
    rightPayload = asRaw(right);
  }

  llvm::Type *boolType = builder_->getInt1Ty();
  llvm::BasicBlock *notEqualBlock = createBlock("union.eq.false");
  llvm::BasicBlock *kindBlock = createBlock("union.eq.kind");
  llvm::BasicBlock *numberCheckBlock = createBlock("union.eq.check.number");
  llvm::BasicBlock *numberBlock = createBlock("union.eq.number");
  llvm::BasicBlock *stringCheckBlock = createBlock("union.eq.check.string");
  llvm::BasicBlock *stringBlock = createBlock("union.eq.string");
  llvm::BasicBlock *valueBlock = createBlock("union.eq.value");
  llvm::BasicBlock *mergeBlock = createBlock("union.eq.end");

  builder_->CreateCondBr(builder_->CreateICmpEQ(leftTag, rightTag), kindBlock, notEqualBlock);

  builder_->SetInsertPoint(notEqualBlock);
  builder_->CreateBr(mergeBlock);

  builder_->SetInsertPoint(kindBlock);
  builder_->CreateCondBr(builder_->CreateICmpEQ(leftTag, i64(unionTagNull)), mergeBlock,
                         numberCheckBlock);

  builder_->SetInsertPoint(numberCheckBlock);
  builder_->CreateCondBr(builder_->CreateICmpEQ(leftTag, i64(unionTagNumber)), numberBlock,
                         stringCheckBlock);

  builder_->SetInsertPoint(numberBlock);
  llvm::Value *numberEqual =
      builder_->CreateFCmpOEQ(builder_->CreateBitCast(leftPayload, doubleType_),
                              builder_->CreateBitCast(rightPayload, doubleType_));
  builder_->CreateBr(mergeBlock);

  builder_->SetInsertPoint(stringCheckBlock);
  builder_->CreateCondBr(builder_->CreateICmpEQ(leftTag, i64(unionTagString)), stringBlock,
                         valueBlock);

  builder_->SetInsertPoint(stringBlock);
  llvm::Value *stringEqual = stringEquals(leftPayload, rightPayload);
  builder_->CreateBr(mergeBlock);

  builder_->SetInsertPoint(valueBlock);
  llvm::Value *valueEqual = builder_->CreateICmpEQ(leftPayload, rightPayload);
  builder_->CreateBr(mergeBlock);

  builder_->SetInsertPoint(mergeBlock);
  auto *phi = builder_->CreatePHI(boolType, 5, "union.eq");
  phi->addIncoming(numberEqual, numberBlock);
  phi->addIncoming(stringEqual, stringBlock);
  phi->addIncoming(valueEqual, valueBlock);
  phi->addIncoming(llvm::ConstantInt::getTrue(*context_), kindBlock);
  phi->addIncoming(llvm::ConstantInt::getFalse(*context_), notEqualBlock);
  return phi;
}

IrGenerator::EmitValue IrGenerator::emitZeroValue(types::TypePtr type) {
  const TypePtr resolved = resolve(std::move(type));
  if (resolved == nullptr) return rawValue(i64(0));
  switch (resolved->kind) {
  case TypeKind::Number: return numberValue(0.0);
  case TypeKind::String: return rawValue(builder_->CreatePtrToInt(emptyString_, i64Type_));
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
    return emitNullishCell(unionTagNull);
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
  llvm::Value *base = allocate(16);
  long long tag = unionTagPayload;
  if (payloadType != nullptr) {
    const TypePtr resolved = resolve(std::move(payloadType));
    if (resolved != nullptr) {
      switch (resolved->kind) {
      case TypeKind::Number:
      case TypeKind::Byte: tag = unionTagNumber; break;
      case TypeKind::String: tag = unionTagString; break;
      case TypeKind::Boolean: tag = unionTagBoolean; break;
      case TypeKind::Null: tag = unionTagNull; break;
      default: tag = unionTagPayload; break;
      }
    }
  }
  builder_->CreateStore(i64(tag), bytePointer(base, 0));
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

IrGenerator::EmitValue IrGenerator::convertStruct(types::TypePtr destination, types::TypePtr source,
                                                  EmitValue value) {
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
    llvm::Value *bits = builder_->CreateLoad(i64Type_, bytePointer(sourceBase, 8 * sourceIndex));
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
    return emitBox(origin, value);
  }
  if (origin->kind == TypeKind::Null) return emitZeroValue(target);
  if (target->kind == TypeKind::Struct && origin->kind == TypeKind::Struct)
    return convertStruct(target, origin, value);
  return value;
}

void IrGenerator::collectVarDeclarations(
    const reader::StatementList &statements,
    std::vector<const reader::VariableDeclarator *> &out) const {
  for (const auto &statement : statements) collectVarDeclarations(statement, out);
}

void IrGenerator::collectVarDeclarations(
    const std::unique_ptr<reader::Statement> &stmt,
    std::vector<const reader::VariableDeclarator *> &out) const {
  if (stmt == nullptr) return;
  std::visit(
      [&](const auto &node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, reader::VariableDeclaration>) {
          if (node.kind == "var")
            for (const auto &declarator : node.declarations) out.push_back(&declarator);
        } else if constexpr (std::is_same_v<T, reader::BlockStatement>) {
          collectVarDeclarations(node.statements, out);
        } else if constexpr (std::is_same_v<T, reader::IfStatement>) {
          collectVarDeclarations(node.consequent, out);
          collectVarDeclarations(node.alternate, out);
        } else if constexpr (std::is_same_v<T, reader::WhileStatement>) {
          collectVarDeclarations(node.body, out);
        } else if constexpr (std::is_same_v<T, reader::ForStatement>) {
          if (node.init != nullptr && node.init->kind == "var")
            for (const auto &declarator : node.init->declarations) out.push_back(&declarator);
          collectVarDeclarations(node.body, out);
        } else if constexpr (std::is_same_v<T, reader::LabeledStatement>) {
          if (node.body != nullptr) collectVarDeclarations(node.body, out);
        }
      },
      stmt->node);
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
    const auto declared = annotations_->declarationTypes.find(declarator);
    if (declared != annotations_->declarationTypes.end()) { type = declared->second; }
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
      const auto declared = annotations_->declarationTypes.find(&declarator);
      if (declared != annotations_->declarationTypes.end()) {
        value = coerce(declared->second, storedType, value);
        storedType = declared->second;
      }
      target.slots.insert_or_assign(declarator.name, Slot{address, declaration.kind == "const"});
      storeSlot(address, value);
      (void)storedType;
    } else if (declaration.kind == "var" && reuseExisting) {
    } else {
      target.slots.insert_or_assign(declarator.name, Slot{address, false});
      TypePtr declaredType;
      const auto declared = annotations_->declarationTypes.find(&declarator);
      if (declared != annotations_->declarationTypes.end()) { declaredType = declared->second; }
      storeSlot(address, emitZeroValue(declaredType));
    }
  }
}

void IrGenerator::generateStatements(const reader::StatementList &statements) {
  for (const auto &statement : statements) {
    if (terminated()) return;
    generateStatement(*statement);
  }
}

void IrGenerator::generateStatement(const reader::Statement &statement) {
  std::visit(
      [&](const auto &node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, reader::VariableDeclaration>) {
          generateDeclaration(node);
        } else if constexpr (std::is_same_v<T, reader::ExpressionStatement>) {
          if (node.expression != nullptr) (void)generateExpression(node.expression);
        } else if constexpr (std::is_same_v<T, reader::FunctionDeclaration>) {
        } else if constexpr (std::is_same_v<T, reader::ReturnStatement>) {
          EmitValue value = rawValue(i64(0));
          if (node.argument != nullptr) {
            value = generateExpression(node.argument);
            value = coerce(returnType_, typeOf(node.argument), value);
          } else {
            value = coerce(returnType_, types_->nullType(), value);
          }
          storeSlot(returnSlot_, value);
          builder_->CreateBr(returnBlock_);
        } else if constexpr (std::is_same_v<T, reader::BlockStatement>) {
          beginScope();
          generateStatements(node.statements);
          endScope();
        } else if constexpr (std::is_same_v<T, reader::IfStatement>) {
          EmitValue condition = generateExpression(node.condition);
          llvm::Value *test = emitTruthy(condition, typeOf(node.condition));
          llvm::BasicBlock *thenBlock = createBlock("if.then");
          llvm::BasicBlock *elseBlock = !node.alternate.empty() ? createBlock("if.else") : nullptr;
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
          builder_->CreateCondBr(emitTruthy(condition, typeOf(node.condition)), bodyBlock,
                                 endBlock);
          builder_->SetInsertPoint(bodyBlock);
          loops_.push_back(Loop{endBlock, conditionBlock});
          const bool labelled = !pendingLoopLabel_.empty();
          if (labelled) {
            labels_.push_back(Label{pendingLoopLabel_, endBlock, conditionBlock});
            pendingLoopLabel_.clear();
          }
          beginScope();
          generateStatements(node.body);
          endScope();
          loops_.pop_back();
          if (labelled) labels_.pop_back();
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
            builder_->CreateCondBr(emitTruthy(condition, typeOf(node.condition)), bodyBlock,
                                   endBlock);
          } else {
            builder_->CreateBr(bodyBlock);
          }
          builder_->SetInsertPoint(bodyBlock);
          loops_.push_back(Loop{endBlock, updateBlock});
          const bool labelled = !pendingLoopLabel_.empty();
          if (labelled) {
            labels_.push_back(Label{pendingLoopLabel_, endBlock, updateBlock});
            pendingLoopLabel_.clear();
          }
          beginScope();
          generateStatements(node.body);
          endScope();
          loops_.pop_back();
          if (labelled) labels_.pop_back();
          if (!terminated()) builder_->CreateBr(updateBlock);
          builder_->SetInsertPoint(updateBlock);
          if (node.update != nullptr) (void)generateExpression(node.update);
          if (!terminated()) builder_->CreateBr(conditionBlock);
          builder_->SetInsertPoint(endBlock);
          endScope();
        } else if constexpr (std::is_same_v<T, reader::BreakStatement>) {
          if (node.label != nullptr) {
            llvm::BasicBlock *target = nullptr;
            for (auto entry = labels_.rbegin(); entry != labels_.rend(); ++entry)
              if (entry->name == node.label->text) {
                target = entry->breakBlock;
                break;
              }
            if (target == nullptr)
              throw errors::ErrorException{errors::ErrorStatus{
                  errors::ErrorCode::TYPE_UNDEFINED_IDENTIFIER, node.label->text, statement.range}};
            builder_->CreateBr(target);
          } else {
            builder_->CreateBr(loops_.back().breakBlock);
          }
        } else if constexpr (std::is_same_v<T, reader::ContinueStatement>) {
          if (node.label != nullptr) {
            llvm::BasicBlock *target = nullptr;
            for (auto entry = labels_.rbegin(); entry != labels_.rend(); ++entry)
              if (entry->name == node.label->text && entry->continueBlock != nullptr) {
                target = entry->continueBlock;
                break;
              }
            if (target == nullptr)
              throw errors::ErrorException{
                  errors::ErrorStatus{errors::ErrorCode::TYPE_CONTINUE_OUTSIDE_LOOP,
                                      node.label->text, statement.range}};
            builder_->CreateBr(target);
          } else {
            builder_->CreateBr(loops_.back().continueBlock);
          }
        } else if constexpr (std::is_same_v<T, reader::ThrowStatement>) {
          if (node.argument != nullptr) (void)generateExpression(node.argument);
          builder_->CreateCall(throwFunction());
          builder_->CreateUnreachable();
        } else if constexpr (std::is_same_v<T, reader::LabeledStatement>) {
          if (node.body == nullptr) return;
          const bool loopBody = std::holds_alternative<reader::WhileStatement>(node.body->node) ||
                                std::holds_alternative<reader::ForStatement>(node.body->node);
          if (loopBody) {
            pendingLoopLabel_ = node.label;
            generateStatement(*node.body);
            pendingLoopLabel_.clear();
          } else {
            llvm::BasicBlock *endBlock = createBlock("label.end");
            labels_.push_back(Label{node.label, endBlock, nullptr});
            generateStatement(*node.body);
            labels_.pop_back();
            if (!terminated()) builder_->CreateBr(endBlock);
            builder_->SetInsertPoint(endBlock);
          }
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
          throw errors::ErrorException{errors::ErrorStatus{
              errors::ErrorCode::TYPE_UNSUPPORTED_OPERATOR, "undefined", expression->range}};
        } else if constexpr (std::is_same_v<T, reader::IdentifierExpr>) {
          if (Slot *slot = lookupSlot(node.name))
            return loadSlot(slot->address, typeOf(expression));
          const auto global = globals_.find(node.name);
          if (global != globals_.end()) return loadSlot(global->second, typeOf(expression));
          const auto function = functions_.find(node.name);
          if (function != functions_.end())
            return rawValue(builder_->CreatePtrToInt(function->second, i64Type_));
          throw errors::ErrorException{errors::ErrorStatus{
              errors::ErrorCode::TYPE_UNDEFINED_IDENTIFIER, node.name, expression->range}};
        } else if constexpr (std::is_same_v<T, reader::UnaryExpr>) {
          return generateUnary(node);
        } else if constexpr (std::is_same_v<T, reader::AddressOfExpr>) {
          // For &member_expr, return the address directly without loading
          if (const auto *member = std::get_if<reader::MemberExpr>(&node.operand->node)) {
            llvm::Value *addr = generateMemberAddress(*member, node.operand->range);
            return rawValue(builder_->CreatePtrToInt(addr, i64Type_));
          }
          // For &*deref_expr, return the pointer value directly
          if (const auto *deref = std::get_if<reader::DereferenceExpr>(&node.operand->node)) {
            EmitValue ptrValue = generateExpression(deref->operand);
            return rawValue(asRaw(ptrValue));
          }
          if (const auto *identifier = std::get_if<reader::IdentifierExpr>(&node.operand->node)) {
            if (Slot *slot = lookupSlot(identifier->name)) {
              return rawValue(builder_->CreatePtrToInt(slot->address, i64Type_));
            }
            const auto global = globals_.find(identifier->name);
            if (global != globals_.end())
              return rawValue(builder_->CreatePtrToInt(global->second, i64Type_));
          }
          EmitValue operand = generateExpression(node.operand);
          return rawValue(asRaw(operand));
        } else if constexpr (std::is_same_v<T, reader::DereferenceExpr>) {
          EmitValue ptrValue = generateExpression(node.operand);
          llvm::Value *pointer = builder_->CreateIntToPtr(asRaw(ptrValue), ptrType_);
          llvm::Value *loaded = builder_->CreateLoad(i64Type_, pointer);
          const TypePtr pointedType = resolve(typeOf(expression));
          if (pointedType != nullptr && isNumber(pointedType))
            return EmitValue{builder_->CreateBitCast(loaded, doubleType_), true};
          return rawValue(loaded);
        } else if constexpr (std::is_same_v<T, reader::BinaryExpr>) {
          return generateBinary(node);
        } else if constexpr (std::is_same_v<T, reader::UpdateExpr>) {
          return generateUpdate(node);
        } else if constexpr (std::is_same_v<T, reader::AssignmentExpr>) {
          return generateAssignment(node);
        } else if constexpr (std::is_same_v<T, reader::MemberExpr>) {
          llvm::Value *address = generateMemberAddress(node, expression->range);
          return loadSlot(builder_->CreateIntToPtr(address, ptrType_), typeOf(expression));
        } else if constexpr (std::is_same_v<T, reader::ObjectExpr>) {
          llvm::Value *base = allocate(8 * node.properties.size());
          const TypePtr structType = resolve(typeOf(expression));
          for (std::size_t index = 0; index < node.properties.size(); ++index) {
            std::size_t fieldIndex = index;
            TypePtr declaredField;
            if (structType != nullptr && structType->kind == TypeKind::Struct) {
              for (std::size_t field = 0; field < structType->fields.size(); ++field)
                if (structType->fields[field].first == node.properties[index].first) {
                  fieldIndex = field;
                  declaredField = structType->fields[field].second;
                  break;
                }
            }
            EmitValue value = generateExpression(node.properties[index].second);
            if (declaredField != nullptr)
              value = coerce(declaredField, typeOf(node.properties[index].second), value);
            storeSlot(bytePointer(base, 8 * fieldIndex), value);
          }
          return rawValue(base);
        } else if constexpr (std::is_same_v<T, reader::ArrayExpr>) {
          // Determine array size: max of element count and highest designated index + 1
          std::size_t arraySize = node.elements.size();
          for (const auto &desig : node.designated)
            if (desig.index + 1 > arraySize) arraySize = desig.index + 1;

          llvm::Value *base = allocate(8 + 8 * arraySize);
          builder_->CreateStore(i64(arraySize), bytePointer(base, 0));
          const TypePtr arrayType = resolve(typeOf(expression));
          TypePtr elementType = arrayType != nullptr && arrayType->kind == TypeKind::Array
                                    ? arrayType->element
                                    : nullptr;

          // Zero-initialize all slots
          for (std::size_t index = 0; index < arraySize; ++index) {
            EmitValue zero = emitZeroValue(elementType != nullptr ? elementType : types_->nullType());
          storeSlot(bytePointer(base, 8 + 8 * index), zero);
          }

          // Store direct elements
          for (std::size_t index = 0; index < node.elements.size(); ++index) {
            EmitValue value = generateExpression(node.elements[index]);
            if (elementType != nullptr)
              value = coerce(elementType, typeOf(node.elements[index]), value);
            storeSlot(bytePointer(base, 8 + 8 * index), value);
          }

          // Store designated initializers
          for (const auto &desig : node.designated) {
            EmitValue value = generateExpression(desig.value);
            if (elementType != nullptr)
              value = coerce(elementType, typeOf(desig.value), value);
            storeSlot(bytePointer(base, 8 + 8 * desig.index), value);
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
    llvm::Value *result = builder_->CreateNot(emitTruthy(operand, typeOf(unary.operand)));
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
  if (unary.op == "+") {
    EmitValue operand = generateExpression(unary.operand);
    return EmitValue{builder_->CreateBitCast(asRaw(operand), doubleType_), true};
  }
  if (unary.op == "typeof") {
    const TypePtr operandType = resolve(typeOf(unary.operand));
    EmitValue operand = generateExpression(unary.operand);
    if (operandType != nullptr && operandType->kind == TypeKind::Union) {
      llvm::Value *tag = builder_->CreateLoad(i64Type_, bytePointer(asRaw(operand), 0));
      llvm::Value *nullText = builder_->CreatePtrToInt(internString("null"), i64Type_);
      llvm::Value *numberText = builder_->CreatePtrToInt(internString("number"), i64Type_);
      llvm::Value *stringText = builder_->CreatePtrToInt(internString("string"), i64Type_);
      llvm::Value *boolText = builder_->CreatePtrToInt(internString("boolean"), i64Type_);
      llvm::Value *payloadText =
          builder_->CreatePtrToInt(internString(typeofName(unionPayload(operandType))), i64Type_);

      // Start with payload as default
      llvm::Value *result = payloadText;
      result = builder_->CreateSelect(
          builder_->CreateICmpEQ(tag, i64(unionTagBoolean)), boolText, result);
      result = builder_->CreateSelect(
          builder_->CreateICmpEQ(tag, i64(unionTagString)), stringText, result);
      result = builder_->CreateSelect(
          builder_->CreateICmpEQ(tag, i64(unionTagNumber)), numberText, result);
      result = builder_->CreateSelect(
          builder_->CreateICmpEQ(tag, i64(unionTagNull)), nullText, result);
      return rawValue(result);
    }
    return rawValue(builder_->CreatePtrToInt(internString(typeofName(operandType)), i64Type_));
  }
  if (unary.op == "void") {
    (void)generateExpression(unary.operand);
    return rawValue(i64(0));
  }
  throw errors::ErrorException{
      errors::ErrorStatus{errors::ErrorCode::TYPE_UNSUPPORTED_OPERATOR, unary.op, {}}};
}

IrGenerator::EmitValue IrGenerator::generateBinary(const reader::BinaryExpr &binary) {
  if (binary.op == "==" || binary.op == "!=" || binary.op == "===" || binary.op == "!==")
    return generateEquality(binary);
  if (binary.op == "&&" || binary.op == "||" || binary.op == "??") return generateLogical(binary);
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
  if (binary.op == "+") {
    if (isString(typeOf(binary.left)) && isString(typeOf(binary.right)))
      return EmitValue{builder_->CreateCall(stringConcatFunction(), {asRaw(left), asRaw(right)}),
                       false};
    return EmitValue{builder_->CreateFAdd(left.value, right.value), true};
  }
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
  if (isString(typeOf(binary.left)) && isString(typeOf(binary.right))) {
    llvm::Value *ordering =
        builder_->CreateCall(stringCompareFunction(), {asRaw(left), asRaw(right)});
    if (binary.op == "<") comparison = builder_->CreateICmpSLT(ordering, i64(0));
    else if (binary.op == ">")
      comparison = builder_->CreateICmpSGT(ordering, i64(0));
    else if (binary.op == "<=")
      comparison = builder_->CreateICmpSLE(ordering, i64(0));
    else
      comparison = builder_->CreateICmpSGE(ordering, i64(0));
  } else if (isNumber(typeOf(binary.left)) && isNumber(typeOf(binary.right))) {
    if (binary.op == "<") comparison = builder_->CreateFCmpOLT(left.value, right.value);
    else if (binary.op == ">")
      comparison = builder_->CreateFCmpOGT(left.value, right.value);
    else if (binary.op == "<=")
      comparison = builder_->CreateFCmpOLE(left.value, right.value);
    else
      comparison = builder_->CreateFCmpOGE(left.value, right.value);
  } else {
    llvm::Value *lhs = asRaw(left);
    llvm::Value *rhs = asRaw(right);
    if (binary.op == "<") comparison = builder_->CreateICmpULT(lhs, rhs);
    else if (binary.op == ">")
      comparison = builder_->CreateICmpUGT(lhs, rhs);
    else if (binary.op == "<=")
      comparison = builder_->CreateICmpULE(lhs, rhs);
    else
      comparison = builder_->CreateICmpUGE(lhs, rhs);
  }
  return rawValue(builder_->CreateZExt(comparison, i64Type_));
}

IrGenerator::EmitValue IrGenerator::generateEquality(const reader::BinaryExpr &binary) {
  EmitValue left = generateExpression(binary.left);
  EmitValue right = generateExpression(binary.right);
  const TypePtr leftType = resolve(typeOf(binary.left));
  const TypePtr rightType = resolve(typeOf(binary.right));
  llvm::Value *equal = nullptr;
  const bool leftUnion = leftType != nullptr && leftType->kind == TypeKind::Union;
  const bool rightUnion = rightType != nullptr && rightType->kind == TypeKind::Union;
  if (leftUnion || rightUnion) {
    equal = emitUnionEquality(left, leftType, right, rightType);
  } else if (isNumber(leftType) && isNumber(rightType)) {
    equal = builder_->CreateFCmpOEQ(left.value, right.value);
  } else if (leftType != nullptr && rightType != nullptr && leftType->kind == TypeKind::String &&
             rightType->kind == TypeKind::String) {
    equal = stringEquals(asRaw(left), asRaw(right));
  } else {
    equal = builder_->CreateICmpEQ(asRaw(left), asRaw(right));
  }
  if (binary.op == "!=" || binary.op == "!==") equal = builder_->CreateNot(equal);
  return rawValue(builder_->CreateZExt(equal, i64Type_));
}

IrGenerator::EmitValue IrGenerator::generateLogical(const reader::BinaryExpr &binary) {
  EmitValue left = generateExpression(binary.left);
  const TypePtr leftType = typeOf(binary.left);
  llvm::Value *leftBits = asRaw(left);
  llvm::BasicBlock *leftBlock = builder_->GetInsertBlock();
  llvm::BasicBlock *rightBlock = createBlock("logic.rhs");
  llvm::BasicBlock *endBlock = createBlock("logic.end");
  const bool andOp = binary.op == "&&";
  const bool nullish = binary.op == "??";
  llvm::Value *isTruthy = emitTruthy(left, leftType);
  llvm::Value *takeLeft = nullish ? builder_->CreateNot(emitNullish(left, leftType))
                                  : (andOp ? builder_->CreateNot(isTruthy) : isTruthy);
  builder_->CreateCondBr(takeLeft, endBlock, rightBlock);
  builder_->SetInsertPoint(rightBlock);
  EmitValue right = generateExpression(binary.right);
  right = coerce(leftType, typeOf(binary.right), right);
  llvm::Value *rightBits = asRaw(right);
  llvm::BasicBlock *rightEnd = builder_->GetInsertBlock();
  if (!terminated()) builder_->CreateBr(endBlock);
  builder_->SetInsertPoint(endBlock);
  auto *result = builder_->CreatePHI(i64Type_, 2, "logic.value");
  result->addIncoming(leftBits, leftBlock);
  result->addIncoming(rightBits, rightEnd);
  return asTyped(result, typeOf(binary.left));
}

IrGenerator::EmitValue IrGenerator::generateUpdate(const reader::UpdateExpr &update) {
  llvm::Value *address = generateLValueAddress(update.operand);
  if (address == nullptr)
    throw errors::ErrorException{errors::ErrorStatus{
        errors::ErrorCode::TYPE_INVALID_ASSIGNMENT_TARGET, update.op, update.operand->range}};
  llvm::Value *previous =
      builder_->CreateBitCast(builder_->CreateLoad(i64Type_, address), doubleType_);
  llvm::Value *one = llvm::ConstantFP::get(doubleType_, 1.0);
  llvm::Value *updated =
      update.op == "++" ? builder_->CreateFAdd(previous, one) : builder_->CreateFSub(previous, one);
  builder_->CreateStore(builder_->CreateBitCast(updated, i64Type_), address);
  if (update.prefix) return EmitValue{updated, true};
  return EmitValue{previous, true};
}

llvm::Value *IrGenerator::generateLValueAddress(const reader::ExprPtr &target) {
  if (const auto *identifier = std::get_if<reader::IdentifierExpr>(&target->node)) {
    if (Slot *slot = lookupSlot(identifier->name)) return slot->address;
    const auto global = globals_.find(identifier->name);
    if (global != globals_.end()) return global->second;
    return nullptr;
  }
  if (const auto *deref = std::get_if<reader::DereferenceExpr>(&target->node))
    return builder_->CreateIntToPtr(asRaw(generateExpression(deref->operand)), ptrType_);
  if (const auto *member = std::get_if<reader::MemberExpr>(&target->node))
    return builder_->CreateIntToPtr(generateMemberAddress(*member, target->range), ptrType_);
  return nullptr;
}

IrGenerator::EmitValue IrGenerator::generateAssignment(const reader::AssignmentExpr &assignment) {
  EmitValue value = generateExpression(assignment.right);
  TypePtr valueType = typeOf(assignment.right);
  TypePtr targetType = typeOf(assignment.left);

  llvm::Value *address = generateLValueAddress(assignment.left);
  if (address == nullptr)
    throw errors::ErrorException{errors::ErrorStatus{
        errors::ErrorCode::TYPE_INVALID_ASSIGNMENT_TARGET, assignment.op, assignment.left->range}};

  if (assignment.op != "=") {
    EmitValue current = loadSlot(address, targetType);
    const bool stringAppend = assignment.op == "+=" && isString(targetType) && isString(valueType);
    if (stringAppend) {
      value =
          EmitValue{builder_->CreateCall(stringConcatFunction(), {asRaw(current), asRaw(value)}),
                    false};
      valueType = types_->string();
    } else if (assignment.op == "+=")
      value = EmitValue{builder_->CreateFAdd(current.value, value.value), true};
    else if (assignment.op == "-=")
      value = EmitValue{builder_->CreateFSub(current.value, value.value), true};
    else if (assignment.op == "*=")
      value = EmitValue{builder_->CreateFMul(current.value, value.value), true};
    else if (assignment.op == "/=")
      value = EmitValue{builder_->CreateFDiv(current.value, value.value), true};
    else if (assignment.op == "%=")
      value = EmitValue{builder_->CreateCall(moduloFunction(), {current.value, value.value}), true};
    else
      throw errors::ErrorException{errors::ErrorStatus{errors::ErrorCode::TYPE_UNSUPPORTED_OPERATOR,
                                                       assignment.op, assignment.left->range}};
    if (!stringAppend) valueType = types_->number();
  }

  value = coerce(targetType, valueType, value);
  storeSlot(address, value);
  return value;
}

llvm::Value *IrGenerator::generateMemberAddress(const reader::MemberExpr &member,
                                                const reader::SourceRange &range) {
  EmitValue object = generateExpression(member.object);
  llvm::Value *base = asRaw(object);
  TypePtr objectType = resolve(typeOf(member.object));

  // Dereference pointer if needed
  if (objectType != nullptr && objectType->kind == TypeKind::Pointer) {
    llvm::Value *pointer = builder_->CreateIntToPtr(base, ptrType_);
    base = builder_->CreateLoad(i64Type_, pointer);
    objectType = objectType->pointed;
  }

  if (member.computedFlag) {
    if (objectType == nullptr ||
        (objectType->kind != TypeKind::Array && objectType->kind != TypeKind::Bytes))
      throw errors::ErrorException{
          errors::ErrorStatus{errors::ErrorCode::TYPE_UNSUPPORTED_OPERATOR, "[]", range}};
    EmitValue indexValue = generateExpression(member.computed);
    llvm::Value *index32 = toInt32(indexValue.value);
    llvm::Value *index = builder_->CreateSExt(index32, i64Type_);

    llvm::Value *lengthPtr = bytePointer(base, 0);
    llvm::Value *length = builder_->CreateLoad(i64Type_, lengthPtr);

    llvm::BasicBlock *oobBlock = createBlock("oob");
    llvm::BasicBlock *okBlock = createBlock("member.ok");
    llvm::BasicBlock *endBlock = createBlock("member.end");
    builder_->CreateCondBr(builder_->CreateICmpUGE(index, length), oobBlock, okBlock);

    builder_->SetInsertPoint(oobBlock);
    builder_->CreateCall(boundsFunction());
    builder_->CreateBr(endBlock);

    builder_->SetInsertPoint(okBlock);
    llvm::Value *elementPtr =
        dynamicPointer(base, builder_->CreateAdd(i64(8), builder_->CreateMul(index, i64(8))));
    llvm::Value *okAddress = builder_->CreatePtrToInt(elementPtr, i64Type_);
    builder_->CreateBr(endBlock);

    builder_->SetInsertPoint(endBlock);
    llvm::PHINode *phi = builder_->CreatePHI(i64Type_, 2, "member.addr");
    phi->addIncoming(okAddress, okBlock);
    phi->addIncoming(builder_->CreatePtrToInt(builder_->CreateIntToPtr(i64(0), ptrType_), i64Type_),
                     oobBlock);
    return phi;
  }

  if (objectType != nullptr && objectType->kind == TypeKind::Struct) {
    for (std::size_t index = 0; index < objectType->fields.size(); ++index) {
      if (objectType->fields[index].first == member.property) {
        return builder_->CreateAdd(base, i64(8 * index));
      }
    }
  }

  throw errors::ErrorException{errors::ErrorStatus{
      errors::ErrorCode::TYPE_MEMBER_ACCESS_NOT_SUPPORTED, member.property, range}};
}

IrGenerator::EmitValue IrGenerator::generateCall(const reader::ExprPtr &expression,
                                                 const reader::CallExpr &call) {
  const TypePtr target = resolve(annotations_->argumentTypes.at(expression.get()));
  if (target == nullptr || target->kind != TypeKind::Function)
    throw errors::ErrorException{errors::ErrorStatus{
        errors::ErrorCode::TYPE_CALL_TARGET_EXPECTS_ARGUMENTS, "call", expression->range}};
  if (const auto *identifier = std::get_if<reader::IdentifierExpr>(&call.callee->node)) {
    if (const builtins::BuiltinSpec *builtin =
            builtins::BuiltinRegistry::instance().find(identifier->name)) {
      builtin->emit(*this, call);
      return lastValue_;
    }
  }
  EmitValue callee = generateExpression(call.callee);
  llvm::Value *functionPointer =
      builder_->CreateIntToPtr(asRaw(callee), llvm::PointerType::getUnqual(*context_));
  std::vector<llvm::Value *> arguments;
  for (std::size_t index = 0; index < call.arguments.size(); ++index) {
    EmitValue argument = generateExpression(call.arguments[index]);
    TypePtr argumentType = typeOf(call.arguments[index]);
    if (index < target->parameters.size())
      argument = coerce(target->parameters[index], argumentType, argument);
    arguments.push_back(asRaw(argument));
  }
  llvm::FunctionType *functionType = llvm::FunctionType::get(
      i64Type_, std::vector<llvm::Type *>(arguments.size(), i64Type_), false);
  return asTyped(builder_->CreateCall(functionType, functionPointer, arguments), target->result);
}

LoweredModule IrGenerator::generate(const reader::Program &program,
                                    const types::Annotations &annotations,
                                    const types::TypeContext &typeContext) {
  auto ownedContext = std::make_shared<llvm::LLVMContext>();
  context_ = ownedContext.get();
  module_ = new llvm::Module("jsmm", *context_);
  module_->setTargetTriple(llvm::Triple(llvm::sys::getDefaultTargetTriple()));

  builder_ = std::make_unique<llvm::IRBuilder<>>(*context_);
  annotations_ = &annotations;
  types_ = const_cast<types::TypeContext *>(&typeContext);
  doubleType_ = llvm::Type::getDoubleTy(*context_);
  i64Type_ = llvm::Type::getInt64Ty(*context_);
  i32Type_ = llvm::Type::getInt32Ty(*context_);
  i8Type_ = llvm::Type::getInt8Ty(*context_);
  ptrType_ = llvm::PointerType::getUnqual(*context_);
  voidType_ = llvm::Type::getVoidTy(*context_);
  globals_.clear();
  functions_.clear();
  strings_.clear();
  runtime_.clear();
  emptyString_ = internString("");
  scopes_.clear();
  freeSlots_.clear();
  temps_.clear();
  slotAllocas_.clear();
  loops_.clear();
  labels_.clear();
  pendingLoopLabel_.clear();
  slotCount_ = 0;

  for (const auto &statement : program.statements) {
    if (const auto *declaration = std::get_if<reader::FunctionDeclaration>(&statement->node)) {
      TypePtr functionType = nullptr;
      for (const auto &[expr, type] : annotations.expressionTypes) {
        (void)expr;
        (void)type;
      }
      std::vector<llvm::Type *> parameters(declaration->parameters.size(), i64Type_);
      llvm::FunctionType *signature = llvm::FunctionType::get(i64Type_, parameters, false);
      llvm::Function *function = llvm::Function::Create(signature, llvm::Function::ExternalLinkage,
                                                        declaration->name, *module_);
      functions_[declaration->name] = function;
      (void)functionType;
    }
  }
  generateEntry(program);
  for (const auto &statement : program.statements) {
    if (const auto *declaration = std::get_if<reader::FunctionDeclaration>(&statement->node))
      generateFunction(*declaration, functions_.at(declaration->name));
  }
  std::string verification;
  llvm::raw_string_ostream output(verification);
  if (llvm::verifyModule(*module_, &output))
    throw std::runtime_error("invalid generated LLVM IR: " + output.str());
  LoweredModule result{ownedContext, std::unique_ptr<llvm::Module>(module_)};
  module_ = nullptr;
  context_ = nullptr;
  return result;
}

void IrGenerator::generateEntry(const reader::Program &program) {
  llvm::FunctionType *signature = llvm::FunctionType::get(i64Type_, false);
  currentFunction_ =
      llvm::Function::Create(signature, llvm::Function::ExternalLinkage, "jsmm_entry", *module_);
  auto *entry = llvm::BasicBlock::Create(*context_, "entry", currentFunction_);
  builder_->SetInsertPoint(entry);
  returnType_ = types_->nullType();
  returnSlot_ = createEntryAlloca();
  storeSlot(returnSlot_, emitZeroValue(returnType_));
  returnBlock_ = createBlock("return");
  generatingEntry_ = true;
  beginScope();
  varTargetScopeIndex_ = 0;
  hoistVarSlots(program.statements);
  generateStatements(program.statements);
  if (!terminated()) builder_->CreateBr(returnBlock_);
  builder_->SetInsertPoint(returnBlock_);
  if (!terminated()) builder_->CreateRet(builder_->CreateLoad(i64Type_, returnSlot_));
  endScope();
  generatingEntry_ = false;
  varTargetScopeIndex_ = -1;
  currentFunction_ = nullptr;
}

void IrGenerator::generateFunction(const reader::FunctionDeclaration &declaration,
                                   llvm::Function *function) {
  currentFunction_ = function;
  llvm::BasicBlock *entry = llvm::BasicBlock::Create(*context_, "entry", function);
  builder_->SetInsertPoint(entry);
  scopes_.clear();
  freeSlots_.clear();
  temps_.clear();
  slotAllocas_.clear();
  loops_.clear();
  labels_.clear();
  pendingLoopLabel_.clear();
  slotCount_ = 0;
  const auto declaredFunction = annotations_->functionTypes.find(declaration.name);
  returnType_ = declaredFunction != annotations_->functionTypes.end()
                    ? resolve(declaredFunction->second->result)
                    : types_->nullType();
  if (returnType_ != nullptr && returnType_->kind == TypeKind::Variable)
    returnType_ = types_->nullType();
  returnSlot_ = createEntryAlloca();
  storeSlot(returnSlot_, emitZeroValue(returnType_));
  returnBlock_ = createBlock("return");
  beginScope();
  auto argument = function->arg_begin();
  for (const auto &parameter : declaration.parameters) {
    llvm::Value *address = slotAlloca(allocateSlot());
    scopes_.back().slots.emplace(parameter.name, Slot{address, false});
    builder_->CreateStore(&*argument, address);
    ++argument;
  }
  hoistVarSlots(declaration.body->statements);
  generateStatements(declaration.body->statements);
  if (!terminated()) builder_->CreateBr(returnBlock_);
  builder_->SetInsertPoint(returnBlock_);
  if (!terminated()) builder_->CreateRet(builder_->CreateLoad(i64Type_, returnSlot_));
  endScope();
  currentFunction_ = nullptr;
}

void IrGenerator::emitArgument(const reader::ExprPtr &argument) {
  lastValue_ = argument ? generateExpression(argument) : rawValue(i64(0));
}
void IrGenerator::moveResultToArgumentRegister() {}
void IrGenerator::clearResult() { lastValue_ = rawValue(i64(0)); }
void IrGenerator::declareHelper(std::string, std::string) {}
void IrGenerator::useHelper(const std::string &) {}
void IrGenerator::callHelper(const std::string &) {}

void IrGenerator::formatValue(const reader::ExprPtr &expression) {
  EmitValue value = generateExpression(expression);
  emitFormatValue(value, typeOf(expression));
  lastValue_ = rawValue(i64(0));
}
void IrGenerator::formatArray(const reader::ExprPtr &expression) {
  EmitValue value = generateExpression(expression);
  emitFormatArray(value, typeOf(expression));
  lastValue_ = rawValue(i64(0));
}
void IrGenerator::formatNewline() {
  builder_->CreateCall(stringWriter(), {builder_->CreatePtrToInt(internString("\n"), i64Type_)});
}

void IrGenerator::emitFormatValue(EmitValue value, TypePtr type) {
  type = resolve(type);
  if (type == nullptr) {
    builder_->CreateCall(nullWriter());
    return;
  }
  switch (type->kind) {
  case TypeKind::Number: builder_->CreateCall(numberWriter(), {value.value}); break;
  case TypeKind::String: builder_->CreateCall(stringWriter(), {asRaw(value)}); break;
  case TypeKind::Boolean: builder_->CreateCall(boolWriter(), {asRaw(value)}); break;
  case TypeKind::Byte: builder_->CreateCall(numberWriter(), {value.value}); break;
  case TypeKind::Bytes: builder_->CreateCall(stringWriter(), {asRaw(value)}); break;
  case TypeKind::Null: builder_->CreateCall(nullWriter()); break;
  case TypeKind::Array: emitFormatArray(value, type); break;
  case TypeKind::Pointer:
    builder_->CreateCall(numberWriter(), {builder_->CreateSIToFP(asRaw(value), doubleType_)});
    break;
  case TypeKind::Union: emitFormatUnion(value, type); break;
  case TypeKind::Struct: builder_->CreateCall(stringWriter(), {asRaw(value)}); break;
  case TypeKind::Function: builder_->CreateCall(stringWriter(), {asRaw(value)}); break;
  case TypeKind::Never: builder_->CreateCall(nullWriter()); break;
  case TypeKind::Variable: builder_->CreateCall(nullWriter()); break;
  }
}
void IrGenerator::emitFormatArray(EmitValue value, TypePtr type) {
  builder_->CreateCall(lbracketWriter());
  llvm::Value *base = asRaw(value);
  llvm::Value *length = builder_->CreateLoad(i64Type_, bytePointer(base, 0));
  llvm::BasicBlock *cond = createBlock("array.print.cond");
  llvm::BasicBlock *body = createBlock("array.print.body");
  llvm::BasicBlock *end = createBlock("array.print.end");
  builder_->CreateBr(cond);
  builder_->SetInsertPoint(cond);
  auto *index = builder_->CreatePHI(i64Type_, 2, "array.index");
  index->addIncoming(i64(0), builder_->GetInsertBlock()->getSinglePredecessor());
  builder_->CreateCondBr(builder_->CreateICmpULT(index, length), body, end);
  builder_->SetInsertPoint(body);
  llvm::Value *notFirst = builder_->CreateICmpNE(index, i64(0));
  llvm::BasicBlock *separatorBlock = createBlock("array.print.separator");
  llvm::BasicBlock *itemBlock = createBlock("array.print.item");
  builder_->CreateCondBr(notFirst, separatorBlock, itemBlock);
  builder_->SetInsertPoint(separatorBlock);
  builder_->CreateCall(separatorWriter());
  builder_->CreateBr(itemBlock);
  builder_->SetInsertPoint(itemBlock);
  llvm::Value *item = builder_->CreateLoad(
      i64Type_,
      dynamicPointer(base, builder_->CreateAdd(i64(8), builder_->CreateMul(index, i64(8)))));
  emitFormatValue(asTyped(item, type->element), type->element);
  llvm::Value *next = builder_->CreateAdd(index, i64(1));
  llvm::BasicBlock *bodyEnd = builder_->GetInsertBlock();
  if (!terminated()) builder_->CreateBr(cond);
  index->addIncoming(next, bodyEnd);
  builder_->SetInsertPoint(end);
  builder_->CreateCall(rbracketWriter());
}
void IrGenerator::emitFormatUnion(EmitValue value, TypePtr type) {
  llvm::Value *base = asRaw(value);
  llvm::Value *tag = builder_->CreateLoad(i64Type_, bytePointer(base, 0));
  llvm::Value *payload = builder_->CreateLoad(i64Type_, bytePointer(base, 8));
  const TypePtr payloadType = unionPayload(type);
  const std::string suffix = std::to_string(unionBlockCounter_++);

  llvm::BasicBlock *nullBlock = createBlock("union.null" + suffix);
  llvm::BasicBlock *numberCheck = createBlock("union.check.number" + suffix);
  llvm::BasicBlock *numberBlock = createBlock("union.number" + suffix);
  llvm::BasicBlock *stringCheck = createBlock("union.check.string" + suffix);
  llvm::BasicBlock *stringBlock = createBlock("union.string" + suffix);
  llvm::BasicBlock *booleanCheck = createBlock("union.check.boolean" + suffix);
  llvm::BasicBlock *booleanBlock = createBlock("union.boolean" + suffix);
  llvm::BasicBlock *payloadBlock = createBlock("union.payload" + suffix);
  llvm::BasicBlock *endBlock = createBlock("union.end" + suffix);

  builder_->CreateCondBr(builder_->CreateICmpEQ(tag, i64(unionTagNull)), nullBlock, numberCheck);

  builder_->SetInsertPoint(nullBlock);
  builder_->CreateCall(nullWriter());
  builder_->CreateBr(endBlock);

  builder_->SetInsertPoint(numberCheck);
  builder_->CreateCondBr(builder_->CreateICmpEQ(tag, i64(unionTagNumber)), numberBlock,
                         stringCheck);

  builder_->SetInsertPoint(numberBlock);
  builder_->CreateCall(numberWriter(), {builder_->CreateBitCast(payload, doubleType_)});
  builder_->CreateBr(endBlock);

  builder_->SetInsertPoint(stringCheck);
  builder_->CreateCondBr(builder_->CreateICmpEQ(tag, i64(unionTagString)), stringBlock,
                         booleanCheck);

  builder_->SetInsertPoint(stringBlock);
  builder_->CreateCall(stringWriter(), {payload});
  builder_->CreateBr(endBlock);

  builder_->SetInsertPoint(booleanCheck);
  builder_->CreateCondBr(builder_->CreateICmpEQ(tag, i64(unionTagBoolean)), booleanBlock,
                         payloadBlock);

  builder_->SetInsertPoint(booleanBlock);
  builder_->CreateCall(boolWriter(), {payload});
  builder_->CreateBr(endBlock);

  builder_->SetInsertPoint(payloadBlock);
  emitFormatValue(asTyped(payload, payloadType), payloadType);
  if (!terminated()) builder_->CreateBr(endBlock);

  builder_->SetInsertPoint(endBlock);
}

} // namespace jsmm::emit
