#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace jsmm::emit {

inline constexpr std::string_view entrySymbol = "jsmm_entry";
inline constexpr std::string_view startSymbol = "_start";
inline constexpr std::string_view entryFrameSymbol = "jsmm_entry_frame";
inline constexpr std::string_view allocSymbol = "jsmm_alloc";
inline constexpr std::string_view moduloSymbol = "jsmm_mod";
inline constexpr std::string_view powerSymbol = "jsmm_pow";
inline constexpr std::string_view boundsSymbol = "jsmm_oob";
inline constexpr std::string_view arenaSymbol = "jsmm_arena";
inline constexpr std::string_view arenaPointerSymbol = "jsmm_arena_pointer";

inline constexpr std::uint64_t arenaSize = 64ULL * 1024ULL * 1024ULL;

class HelperRegistry final {
public:
  void declare(std::string symbol, std::string text);
  void use(const std::string &symbol);
  [[nodiscard]] bool used(const std::string &symbol) const;
  void reset();
  [[nodiscard]] std::string assemble() const;

private:
  std::unordered_map<std::string, std::string> available_;
  std::vector<std::string> order_;
  std::vector<std::string> used_;
};

} // namespace jsmm::emit
