#include "emit/writebin.hh"

#include "emit/elf_writer.hh"
#include "emit/runtime.hh"
#include "errors/errors.hh"
#include "io/platform.hh"

#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Linker/Linker.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Triple.h>

#include <sys/stat.h>

namespace jsmm::emit {
namespace {

[[noreturn]] void fail(errors::ErrorCode code, const std::string &detail) {
  throw errors::ErrorException{errors::ErrorStatus{code, detail}};
}

std::unique_ptr<llvm::TargetMachine> createTargetMachine(const llvm::Module &module) {
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  llvm::InitializeNativeTargetAsmParser();

  const llvm::Triple triple(module.getTargetTriple());
  std::string message;
  const llvm::Target *target = llvm::TargetRegistry::lookupTarget(triple, message);
  if (target == nullptr)
    fail(errors::ErrorCode::CODEGEN_TARGET_UNAVAILABLE,
         "no native target for '" + triple.str() + "': " + message);

  llvm::TargetOptions options;
  options.ExceptionModel = llvm::ExceptionHandling::None;
  std::unique_ptr<llvm::TargetMachine> machine(target->createTargetMachine(
      triple, "generic", "", options, llvm::Reloc::Static, llvm::CodeModel::Small,
      llvm::CodeGenOptLevel::None));
  if (machine == nullptr)
    fail(errors::ErrorCode::CODEGEN_TARGET_UNAVAILABLE,
         "cannot create a target machine for '" + triple.str() + "'");
  return machine;
}

std::string emitObject(llvm::Module &module) {
  const std::unique_ptr<llvm::TargetMachine> machine = createTargetMachine(module);
  module.setDataLayout(machine->createDataLayout());

  llvm::SmallVector<char, 0> buffer;
  llvm::raw_svector_ostream stream(buffer);
  llvm::legacy::PassManager passes;
  if (machine->addPassesToEmitFile(passes, stream, nullptr,
                                   llvm::CodeGenFileType::ObjectFile, false))
    fail(errors::ErrorCode::CODEGEN_OBJECT_EMISSION_FAILED,
         "the target cannot emit object files");
  passes.run(module);
  return std::string(buffer.begin(), buffer.end());
}

}

void emitExecutable(std::unique_ptr<llvm::Module> program, const std::string &path) {
  std::unique_ptr<llvm::Module> runtime = createRuntimeModule(program->getContext());
  runtime->setDataLayout(program->getDataLayout());
  runtime->setTargetTriple(program->getTargetTriple());

  if (llvm::Linker::linkModules(*program, std::move(runtime)))
    fail(errors::ErrorCode::CODEGEN_LINK_FAILED, "cannot link the runtime module");

  const std::string objectData = emitObject(*program);
  const llvm::MemoryBufferRef objectBuffer(llvm::StringRef(objectData), "jsmm.object");
  const std::string executable = buildExecutable(objectBuffer, "_start");
  io::writeFile(path, executable);
  (void)::chmod(path.c_str(), 0755);
}

}
