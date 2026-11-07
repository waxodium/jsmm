#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace jsmm::types {

enum class TypeKind {
  Number,
  String,
  Boolean,
  Byte,
  Bytes,
  Integer,
  Null,
  Never,
  Variable,
  Function,
  Struct,
  Array,
  Pointer,
  Union
};

struct Type;
using TypePtr = std::shared_ptr<Type>;

struct Type {
  bool isVariadic = false;
  TypePtr variadicElement;
  TypeKind kind;
  int id = 0;
  std::size_t length = 0;
  bool isSigned = true;
  TypePtr link;
  std::vector<TypePtr> parameters;
  TypePtr result;
  std::vector<std::pair<std::string, TypePtr>> fields;
  TypePtr element;
  TypePtr pointed;
  std::vector<TypePtr> members;
};

struct TypeLayout {
  std::uint64_t size = 0;
  std::uint64_t alignment = 1;
  bool defined = false;
};

TypeLayout layoutOf(TypePtr type);
std::uint64_t fieldOffset(TypePtr type, std::size_t index);

class TypeContext final {
public:
  TypeContext();

  TypePtr number() const;
  TypePtr string() const;
  TypePtr boolean() const;
  TypePtr byteType() const;
  TypePtr bytesType() const;
  TypePtr integerType(std::size_t bits, bool isSigned);
  TypePtr nullType() const;
  TypePtr never() const;
  TypePtr variable();
  TypePtr functionType(std::vector<TypePtr> parameters, TypePtr result,
                       bool isVariadic = false, TypePtr variadicElement = nullptr);
  TypePtr structType(std::vector<std::pair<std::string, TypePtr>> fields);
  TypePtr arrayType(TypePtr element, std::size_t length = 0);
  TypePtr pointerType(TypePtr pointed);
  TypePtr unionType(std::vector<TypePtr> members);

  TypePtr resolve(TypePtr type) const;
  std::string show(TypePtr type);

private:
  std::string showType(TypePtr type);

  int nextId_;
  int showCounter_;
  TypePtr number_;
  TypePtr string_;
  TypePtr boolean_;
  TypePtr byte_;
  TypePtr bytes_;
  TypePtr null_;
  TypePtr never_;
};

} // namespace jsmm::types
