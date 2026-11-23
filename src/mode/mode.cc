#include "mode/mode.hh"
#include <stdexcept>

namespace jsmm::mode {
namespace {

struct DirectiveSpelling {
  std::string_view spelling;
  Mode mode;
};

constexpr DirectiveSpelling spellings[] = {
    {"\"use unsafe\"", Mode::Unsafe},
    {"'use unsafe'", Mode::Unsafe},
    {"\"use safe\"", Mode::Safe},
    {"'use safe'", Mode::Safe},
};

} // namespace

const ModeSpec &policyFor(Mode mode) {
  switch (mode) {
  case Mode::Unsafe: return kUnsafePolicy;
  case Mode::Safe: {
    return kSafePolicy;
  }
  }
  throw std::invalid_argument{"no standard mode registered"};
}

bool directiveNamesMode(std::string_view raw, Mode &mode) {
  for (const auto &entry : spellings) {
    if (entry.spelling != raw) continue;
    mode = entry.mode;
    return true;
  }
  return false;
}

std::string_view directiveName(std::string_view raw) {
  if (!looksLikeDirective(raw)) return {};
  return raw.substr(5, raw.size() - 6);
}

std::string availableDirectives() {
  constexpr std::size_t count = sizeof(spellings) / sizeof(spellings[0]);
  std::string listed;
  for (std::size_t index = 0; index < count; ++index) {
    bool duplicate = false;
    for (std::size_t earlier = 0; earlier < index; ++earlier)
      if (spellings[earlier].mode == spellings[index].mode) duplicate = true;
    if (duplicate) continue;
    if (!listed.empty()) listed += ", ";
    listed += std::string{spellings[index].spelling};
  }
  return listed;
}

bool looksLikeDirective(std::string_view raw) {
  if (raw.size() < 6) return false;
  const char quote = raw.front();
  if ((quote != '"' && quote != '\'') || raw.back() != quote) return false;
  const std::string_view body = raw.substr(1, raw.size() - 2);
  if (body.size() <= 4 || body.substr(0, 4) != "use ") return false;
  const std::string_view name = body.substr(4);
  for (std::size_t index = 0; index < name.size(); ++index) {
    const char character = name[index];
    const bool alpha = (character >= 'a' && character <= 'z') ||
                       (character >= 'A' && character <= 'Z') ||
                       character == '_';
    const bool digit = character >= '0' && character <= '9';
    if (index == 0 ? !alpha : !(alpha || digit)) return false;
  }
  return true;
}

} // namespace jsmm::mode
