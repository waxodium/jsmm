#include "emit/elf_writer.hh"

#include "errors/errors.hh"

#include <llvm/BinaryFormat/ELF.h>
#include <llvm/Object/ELFObjectFile.h>
#include <llvm/Object/ObjectFile.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <vector>

namespace jsmm::emit {
namespace {

namespace elf = llvm::ELF;
namespace object = llvm::object;

constexpr std::uint64_t baseAddress = 0x400000ULL;
constexpr std::uint64_t pageSize = 0x1000ULL;

[[noreturn]] void fail(const std::string &detail) {
  throw errors::ErrorException{
      errors::ErrorStatus{errors::ErrorCode::CODEGEN_EXECUTABLE_LINK_FAILED, detail}};
}

std::uint64_t alignUp(std::uint64_t value, std::uint64_t alignment) {
  if (alignment <= 1) return value;
  return (value + alignment - 1) / alignment * alignment;
}

struct Section {
  object::SectionRef ref;
  std::uint64_t index = 0;
  std::uint64_t size = 0;
  std::uint64_t alignment = 1;
  std::uint64_t offset = 0;
  std::uint64_t address = 0;
  bool executable = false;
  bool bss = false;
  std::vector<std::uint8_t> contents;
};

void writeRaw(std::vector<std::uint8_t> &data, std::uint64_t offset, std::uint64_t value,
              unsigned bytes) {
  if (offset + bytes > data.size()) fail("relocation outside of section contents");
  for (unsigned i = 0; i < bytes; ++i)
    data[offset + i] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFU);
}

std::uint64_t resolveSymbol(const object::ObjectFile &file, const object::SymbolRef &symbol,
                            const std::vector<Section> &sections) {
  auto flagsOrError = symbol.getFlags();
  if (!flagsOrError) {
    llvm::consumeError(flagsOrError.takeError());
    fail("cannot read symbol flags");
  }
  auto valueOrError = symbol.getValue();
  if (!valueOrError) {
    llvm::consumeError(valueOrError.takeError());
    fail("cannot read symbol value");
  }
  const std::uint32_t flags = *flagsOrError;
  const std::uint64_t value = *valueOrError;
  if ((flags & object::SymbolRef::SF_Undefined) != 0) fail("undefined symbol reference");
  if ((flags & object::SymbolRef::SF_Absolute) != 0) return value;

  auto sectionOrError = symbol.getSection();
  if (!sectionOrError) {
    llvm::consumeError(sectionOrError.takeError());
    fail("cannot read symbol section");
  }
  const object::section_iterator section = *sectionOrError;
  if (section == file.section_end()) return value;
  const std::uint64_t index = section->getIndex();
  for (const Section &candidate : sections)
    if (candidate.index == index) return candidate.address + value;
  fail("symbol belongs to a section that is not loaded");
}

std::string describe(const object::RelocationRef &relocation) {
  std::string text;
  llvm::SmallVector<char, 16> name;
  relocation.getTypeName(name);
  text.assign(name.begin(), name.end());
  return text;
}

void applyRelocations(Section &section, const object::SectionRef &relocationSection,
                      const object::ObjectFile &file,
                      const std::vector<Section> &sections) {
  for (const object::RelocationRef &relocation : relocationSection.relocations()) {
    if (relocation.getSymbol() == file.symbol_end()) continue;
    const object::SymbolRef symbol = *relocation.getSymbol();
    const std::uint64_t symbolAddress = resolveSymbol(file, symbol, sections);
    const object::ELFRelocationRef elfRelocation(relocation);
    auto addendOrError = elfRelocation.getAddend();
    if (!addendOrError) {
      llvm::consumeError(addendOrError.takeError());
      fail("cannot read relocation addend");
    }
    const std::int64_t addend = *addendOrError;
    const std::uint64_t offset = relocation.getOffset();
    const std::uint64_t place = section.address + offset;
    const std::uint64_t type = relocation.getType();
    switch (type) {
    case elf::R_X86_64_NONE: break;
    case elf::R_X86_64_64:
      writeRaw(section.contents, offset,
               symbolAddress + static_cast<std::uint64_t>(addend), 8);
      break;
    case elf::R_X86_64_PC32:
    case elf::R_X86_64_PLT32:
      writeRaw(section.contents, offset,
               symbolAddress + static_cast<std::uint64_t>(addend) - place, 4);
      break;
    case elf::R_X86_64_32:
      writeRaw(section.contents, offset,
               symbolAddress + static_cast<std::uint64_t>(addend), 4);
      break;
    case elf::R_X86_64_32S:
      writeRaw(section.contents, offset,
               symbolAddress + static_cast<std::uint64_t>(addend), 4);
      break;
    case elf::R_X86_64_PC64:
      writeRaw(section.contents, offset,
               symbolAddress + static_cast<std::uint64_t>(addend) - place, 8);
      break;
    default: fail("unsupported relocation " + describe(relocation));
    }
  }
}

}

std::string buildExecutable(llvm::MemoryBufferRef objectData, llvm::StringRef entrySymbol) {
  auto objectOrError = object::ObjectFile::createObjectFile(objectData);
  if (!objectOrError) {
    llvm::consumeError(objectOrError.takeError());
    fail("cannot parse generated object file");
  }
  const std::unique_ptr<object::ObjectFile> file = std::move(*objectOrError);

  std::vector<Section> sections;
  for (const object::SectionRef &ref : file->sections()) {
    const bool executable = ref.isText();
    const bool bss = ref.isBSS();
    const bool data = ref.isData();
    if (!executable && !data && !bss) continue;
    Section section;
    section.ref = ref;
    section.index = ref.getIndex();
    section.size = ref.getSize();
    section.alignment = std::max<std::uint64_t>(ref.getAlignment().value(), 1);
    section.executable = executable;
    section.bss = bss;
    if (!bss) {
      auto contentsOrError = ref.getContents();
      if (!contentsOrError) {
        llvm::consumeError(contentsOrError.takeError());
        fail("cannot read section contents");
      }
      const llvm::StringRef contents = *contentsOrError;
      section.contents.assign(contents.begin(), contents.end());
      if (section.contents.size() < section.size) section.contents.resize(section.size, 0);
    }
    sections.push_back(std::move(section));
  }
  if (sections.empty()) fail("generated object has no loadable sections");

  const std::uint64_t headerSize =
      sizeof(elf::Elf64_Ehdr) + 2 * sizeof(elf::Elf64_Phdr);
  std::uint64_t cursor = alignUp(headerSize, 16);
  for (Section &section : sections) {
    if (!section.executable) continue;
    cursor = alignUp(cursor, section.alignment);
    section.offset = cursor;
    section.address = baseAddress + cursor;
    cursor += section.size;
  }
  const std::uint64_t textEnd = cursor;

  cursor = alignUp(textEnd, pageSize);
  const std::uint64_t dataStart = cursor;
  for (Section &section : sections) {
    if (section.executable || section.bss) continue;
    cursor = alignUp(cursor, section.alignment);
    section.offset = cursor;
    section.address = baseAddress + cursor;
    cursor += section.size;
  }
  for (Section &section : sections) {
    if (section.executable || !section.bss) continue;
    cursor = alignUp(cursor, section.alignment);
    section.offset = cursor;
    section.address = baseAddress + cursor;
    cursor += section.size;
  }
  const std::uint64_t memoryEnd = cursor;

  std::uint64_t fileEnd = dataStart;
  for (const Section &section : sections)
    if (!section.bss) fileEnd = std::max(fileEnd, section.offset + section.contents.size());
  if (fileEnd < headerSize) fail("generated executable layout is too small");

  std::uint64_t entryPoint = 0;
  bool entryFound = false;
  for (const object::SymbolRef &symbol : file->symbols()) {
    auto nameOrError = symbol.getName();
    if (!nameOrError) {
      llvm::consumeError(nameOrError.takeError());
      continue;
    }
    if (*nameOrError != entrySymbol) continue;
    entryPoint = resolveSymbol(*file, symbol, sections);
    entryFound = true;
    break;
  }
  if (!entryFound) fail("entry symbol is missing from the generated object");

  for (const object::SectionRef &relocationSection : file->sections()) {
    auto relocatedOrError = relocationSection.getRelocatedSection();
    if (!relocatedOrError) {
      llvm::consumeError(relocatedOrError.takeError());
      continue;
    }
    const object::section_iterator target = *relocatedOrError;
    if (target == file->section_end()) continue;
    for (Section &section : sections) {
      if (section.index != target->getIndex()) continue;
      applyRelocations(section, relocationSection, *file, sections);
      break;
    }
  }

  std::vector<std::uint8_t> image(fileEnd, 0);
  for (const Section &section : sections)
    if (!section.bss && !section.contents.empty())
      std::memcpy(image.data() + section.offset, section.contents.data(),
                  section.contents.size());

  elf::Elf64_Ehdr header{};
  header.e_ident[elf::EI_MAG0] = 0x7F;
  header.e_ident[elf::EI_MAG1] = 'E';
  header.e_ident[elf::EI_MAG2] = 'L';
  header.e_ident[elf::EI_MAG3] = 'F';
  header.e_ident[elf::EI_CLASS] = elf::ELFCLASS64;
  header.e_ident[elf::EI_DATA] = elf::ELFDATA2LSB;
  header.e_ident[elf::EI_VERSION] = elf::EV_CURRENT;
  header.e_ident[elf::EI_OSABI] = elf::ELFOSABI_NONE;
  header.e_type = elf::ET_EXEC;
  header.e_machine = elf::EM_X86_64;
  header.e_version = elf::EV_CURRENT;
  header.e_entry = entryPoint;
  header.e_phoff = sizeof(elf::Elf64_Ehdr);
  header.e_shoff = 0;
  header.e_flags = 0;
  header.e_ehsize = sizeof(elf::Elf64_Ehdr);
  header.e_phentsize = sizeof(elf::Elf64_Phdr);
  header.e_phnum = 2;
  header.e_shentsize = sizeof(elf::Elf64_Shdr);
  header.e_shnum = 0;
  header.e_shstrndx = 0;

  elf::Elf64_Phdr textHeader{};
  textHeader.p_type = elf::PT_LOAD;
  textHeader.p_flags = elf::PF_R | elf::PF_X;
  textHeader.p_offset = 0;
  textHeader.p_vaddr = baseAddress;
  textHeader.p_paddr = baseAddress;
  textHeader.p_filesz = textEnd;
  textHeader.p_memsz = textEnd;
  textHeader.p_align = pageSize;

  elf::Elf64_Phdr dataHeader{};
  dataHeader.p_type = elf::PT_LOAD;
  dataHeader.p_flags = elf::PF_R | elf::PF_W;
  dataHeader.p_offset = dataStart;
  dataHeader.p_vaddr = baseAddress + dataStart;
  dataHeader.p_paddr = baseAddress + dataStart;
  dataHeader.p_filesz = fileEnd - dataStart;
  dataHeader.p_memsz = memoryEnd - dataStart;
  dataHeader.p_align = pageSize;

  std::memcpy(image.data(), &header, sizeof(header));
  std::memcpy(image.data() + sizeof(header), &textHeader, sizeof(textHeader));
  std::memcpy(image.data() + sizeof(header) + sizeof(textHeader), &dataHeader,
              sizeof(dataHeader));

  return std::string(reinterpret_cast<const char *>(image.data()), image.size());
}

}
