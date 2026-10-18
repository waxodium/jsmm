#pragma once

#include "reference.hh"
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

namespace edon::ecma {

struct Binding {
  Value value = Value::undefined();
  bool mutableBinding = true;
  bool initialized = true;
};

class Environment : public std::enable_shared_from_this<Environment> {
public:
  explicit Environment(std::shared_ptr<Environment> outer = {}) : outer_(std::move(outer)) {}

  bool hasOwnBinding(const std::string &name) const {
    return bindings_.find(name) != bindings_.end();
  }

  bool hasBinding(const std::string &name) const {
    return hasOwnBinding(name) || (outer_ && outer_->hasBinding(name));
  }

  bool createBinding(const std::string &name, bool mutableBinding, bool initialized = false) {
    if (hasOwnBinding(name)) return false;
    bindings_.emplace(name, Binding{Value::undefined(), mutableBinding, initialized});
    return true;
  }

  bool initializeBinding(const std::string &name, Value value) {
    auto it = bindings_.find(name);
    if (it == bindings_.end() || it->second.initialized) return false;
    it->second.value = std::move(value);
    it->second.initialized = true;
    return true;
  }

  bool getBinding(const std::string &name, Value &out) const {
    const auto it = bindings_.find(name);
    if (it != bindings_.end()) {
      if (!it->second.initialized) return false;
      out = it->second.value;
      return true;
    }
    return outer_ && outer_->getBinding(name, out);
  }

  bool setBinding(const std::string &name, Value value, bool strict) {
    auto it = bindings_.find(name);
    if (it != bindings_.end()) {
      if (!it->second.initialized || !it->second.mutableBinding) return false;
      it->second.value = std::move(value);
      return true;
    }
    if (outer_) return outer_->setBinding(name, std::move(value), strict);
    return !strict; 
  }

  std::shared_ptr<Environment> outer() const { return outer_; }

private:
  std::unordered_map<std::string, Binding> bindings_;
  std::shared_ptr<Environment> outer_;
};

} // namespace edon::ecma
