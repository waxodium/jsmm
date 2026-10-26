#pragma once

#include <llvm/IR/Module.h>

#include <memory>
#include <string>

namespace jsmm::emit {

void emitExecutable(std::unique_ptr<llvm::Module> program, const std::string &path);

}
