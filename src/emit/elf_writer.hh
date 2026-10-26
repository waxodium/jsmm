#pragma once

#include <llvm/ADT/StringRef.h>
#include <llvm/Support/MemoryBufferRef.h>

#include <string>

namespace jsmm::emit {

std::string buildExecutable(llvm::MemoryBufferRef object, llvm::StringRef entrySymbol);

}
