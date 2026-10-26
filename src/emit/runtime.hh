#pragma once

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <memory>

namespace jsmm::emit {

std::unique_ptr<llvm::Module> createRuntimeModule(llvm::LLVMContext &context);

}
