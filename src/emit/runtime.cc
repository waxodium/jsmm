#include "emit/runtime.hh"

#include "emit/helpers.hh"
#include "emit/platform.hh"

#include <string>

namespace jsmm::emit {

std::unique_ptr<llvm::Module> createRuntimeModule(llvm::LLVMContext &context) {
  HelperRegistry helpers;
  registerPlatformHelpers(helpers);

  helpers.use(std::string(startSymbol));
  helpers.use(std::string(allocSymbol));
  helpers.use(std::string(moduloSymbol));
  helpers.use(std::string(powerSymbol));
  helpers.use(std::string(boundsSymbol));
  helpers.use("string_writer");
  helpers.use("string_equal");
  helpers.use("throw");
  helpers.use("literals");
  helpers.use("literal_writers");
  helpers.use("number_writer");

  auto module = std::make_unique<llvm::Module>("jsmm.runtime", context);
  module->setModuleInlineAsm(std::string(".intel_syntax noprefix\n") + helpers.assemble());
  return module;
}

}
