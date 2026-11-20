#include "mode/unsafe/unsafe.hh"

namespace jsmm::mode {
namespace {

constexpr ModePolicy policy{Mode::Unsafe, Capabilities{true, true, true}};

}

const ModePolicy &unsafePolicy() { return policy; }

} // namespace jsmm::mode
