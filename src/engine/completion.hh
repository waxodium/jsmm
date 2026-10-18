#pragma once

#include "value.hh"
#include <string>
#include <utility>

namespace edon::ecma {

enum class CompletionType { Normal, Return, Break, Continue, Throw };

struct Completion {
  CompletionType type = CompletionType::Normal;
  Value value = Value::undefined();
  std::string target;

  static Completion normal(Value value = Value::undefined()) {
    return {CompletionType::Normal, std::move(value), {}};
  }
  static Completion returned(Value value = Value::undefined()) {
    return {CompletionType::Return, std::move(value), {}};
  }
  static Completion thrown(Value value) { return {CompletionType::Throw, std::move(value), {}}; }
  static Completion broken(std::string target = {}) {
    return {CompletionType::Break, Value::undefined(), std::move(target)};
  }
  static Completion continued(std::string target = {}) {
    return {CompletionType::Continue, Value::undefined(), std::move(target)};
  }
  bool isAbrupt() const noexcept { return type != CompletionType::Normal; }
};

} // namespace edon::ecma
