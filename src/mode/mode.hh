#pragma once

#include <string>
#include <string_view>

namespace jsmm::mode {

enum class Mode { Unsafe };

struct Capabilities {
  bool pointerArithmetic = false;
  bool pointerToPointer = false;
  bool uncheckedDereference = false;
};

struct ModePolicy {
  Mode mode;
  Capabilities capabilities;
};

const ModePolicy &policyFor(Mode mode);
bool directiveNamesMode(std::string_view raw, Mode &mode);
bool looksLikeDirective(std::string_view raw);
std::string_view directiveName(std::string_view raw);
std::string availableDirectives();

} // namespace jsmm::mode
