#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace jsmm::types {

enum class TypeKind {
  Number,
  String,
  Boolean,
  Null,
  Undefined,
  Never,
  Variable,
  Function,
  Struct,
  Array,
  Union
};

struct Type;
using TypePtr = std::shared_ptr<Type>;

struct Type {
  TypeKind kind;
  int id = 0;
  TypePtr link;
  std::vector<TypePtr> parameters;
  TypePtr result;
  std::vector<std::pair<std::string, TypePtr>> fields;
  TypePtr element;
  std::vector<TypePtr> members;
};

class TypeContext final {
public:
  TypeContext();

  TypePtr number();
  TypePtr string();
  TypePtr boolean();
  TypePtr null();
  TypePtr undefined();
  TypePtr never();
  TypePtr variable();
  TypePtr functionType(std::vector<TypePtr> parameters, TypePtr result);
  TypePtr structType(std::vector<std::pair<std::string, TypePtr>> fields);
  TypePtr arrayType(TypePtr element);
  TypePtr unionType(std::vector<TypePtr> members);

  TypePtr resolve(TypePtr type);
  std::string show(TypePtr type);

private:
  std::string showType(TypePtr type);

  int nextId_ = 0;
  int showCounter_ = 0;
  TypePtr number_;
  TypePtr string_;
  TypePtr boolean_;
  TypePtr null_;
  TypePtr undefined_;
  TypePtr never_;
};

} // namespace jsmm::types
