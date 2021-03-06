#include <fstream>
#include <iostream>
#include <vector>
#include <cstring>
#include "ELFLoader.h"
#include "ELF.h"
#include "sim/except.h"

using namespace std;
using namespace Simulator;

static const int PAGE_SIZE = 4096;

// Throws an exception if the expression is false
#define Verify(Expr, Message)                                      \
    if (!(Expr)) {                                                 \
        throw runtime_error(msg_prefix + ": " + (Message));        \
    }

// Load the program image into the memory
std::pair<MemAddr, bool>
LoadProgram(const std::string& msg_prefix, vector<ActiveROM::LoadableRange>& ranges, IMemoryAdmin& memory, char* data, MemSize size, bool verbose)
{
    Verify(size >= sizeof(Elf_Ehdr), "ELF file too short or truncated");

    Elf_Ehdr& ehdr = *static_cast<Elf_Ehdr*>(static_cast<void*>(data));

    // Unmarshall header
    ehdr.e_type      = elftohh(ehdr.e_type);
    ehdr.e_machine   = elftohh(ehdr.e_machine);
    ehdr.e_version   = elftohw(ehdr.e_version);
    ehdr.e_entry     = elftoha(ehdr.e_entry);
    ehdr.e_phoff     = elftoho(ehdr.e_phoff);
    ehdr.e_shoff     = elftoho(ehdr.e_shoff);
    ehdr.e_flags     = elftohw(ehdr.e_flags);
    ehdr.e_ehsize    = elftohh(ehdr.e_ehsize);
    ehdr.e_phentsize = elftohh(ehdr.e_phentsize);
    ehdr.e_phnum     = elftohh(ehdr.e_phnum);
    ehdr.e_shentsize = elftohh(ehdr.e_shentsize);
    ehdr.e_shnum     = elftohh(ehdr.e_shnum);
    ehdr.e_shstrndx  = elftohh(ehdr.e_shstrndx);

    // Check file signature
    Verify(ehdr.e_ident[EI_MAG0] == ELFMAG0 && ehdr.e_ident[EI_MAG1] == ELFMAG1 &&
           ehdr.e_ident[EI_MAG2] == ELFMAG2 && ehdr.e_ident[EI_MAG3] == ELFMAG3,
           "invalid ELF file signature");

    // Check that this file is for our 'architecture'
    Verify(ehdr.e_ident[EI_VERSION] == EV_CURRENT,  "ELF version mismatch");
    Verify(ehdr.e_ident[EI_CLASS]   == ELFCLASS,    "file is not of proper bitsize");
    Verify(ehdr.e_ident[EI_DATA]    == ELFDATA,     "file is not of proper endianness");
    Verify(ehdr.e_machine == MACHINE_NORMAL ||
           ehdr.e_machine == MACHINE_LEGACY,       "target architecture is not supported");
    Verify(ehdr.e_type              == ET_EXEC,    "file is not an executable file");
    Verify(ehdr.e_phoff != 0 && ehdr.e_phnum != 0, "file has no program header");
    Verify(ehdr.e_phentsize == sizeof(Elf_Phdr),   "file has an invalid program header");
    Verify(ehdr.e_phoff + ehdr.e_phnum * ehdr.e_phentsize <= size, "file has an invalid program header");
    Verify(ehdr.e_shentsize == sizeof(Elf_Shdr),   "file has an invalid section header");
    Verify(ehdr.e_shoff + ehdr.e_shnum * ehdr.e_shentsize <= size, "file has an invalid section header");

    Elf_Shdr* shdr = static_cast<Elf_Shdr*>(static_cast<void*>(data + ehdr.e_shoff));

    // Load section information
    for (Elf_Half i = 0; i < ehdr.e_shnum; ++i)
    {
        shdr[i].sh_name      = elftohw (shdr[i].sh_name);
        shdr[i].sh_type      = elftohw (shdr[i].sh_type);
        shdr[i].sh_addr      = elftoha (shdr[i].sh_addr);
        shdr[i].sh_offset    = elftoho (shdr[i].sh_offset);
        shdr[i].sh_link      = elftohw (shdr[i].sh_link);
        shdr[i].sh_info      = elftohw (shdr[i].sh_info);
#if ELFCLASS == ELFCLASS64
        shdr[i].sh_flags     = elftohxw(shdr[i].sh_flags);
        shdr[i].sh_size      = elftohxw(shdr[i].sh_size);
        shdr[i].sh_addralign = elftohxw(shdr[i].sh_addralign);
        shdr[i].sh_entsize   = elftohxw(shdr[i].sh_entsize);
#else
        shdr[i].sh_flags     = elftohw (shdr[i].sh_flags);
        shdr[i].sh_size      = elftohw (shdr[i].sh_size);
        shdr[i].sh_addralign = elftohw (shdr[i].sh_addralign);
        shdr[i].sh_entsize   = elftohw (shdr[i].sh_entsize);
#endif
    }


    // Find symbol table & corresponding string table
    Elf_Sym* elf_sym_table = 0;
    size_t elf_sym_table_len = 0;
    const char *str_table = 0;
    size_t str_table_len = 0;

    for (Elf_Half i = 0; i < ehdr.e_shnum; ++i)
    {
        if (shdr[i].sh_type == SHT_SYMTAB)
        {
            Verify(shdr[i].sh_entsize == sizeof(Elf_Sym), "file has an invalid symtable");
            Verify(shdr[i].sh_offset + shdr[i].sh_size <= size, "file has an invalid symtable");

            elf_sym_table = static_cast<Elf_Sym*>(static_cast<void*>(data + shdr[i].sh_offset));
            elf_sym_table_len = shdr[i].sh_size / sizeof(Elf_Sym);

            Verify(shdr[i].sh_link != 0 /*SHN_UNDEF*/, "symtable has no string table");
            Verify(shdr[i].sh_link < ehdr.e_shnum, "symtable has an invalid string table");

            Elf_Shdr& strsh = shdr[shdr[i].sh_link];

            Verify(strsh.sh_type == SHT_STRTAB, "file has an invalid string table");
            Verify(strsh.sh_offset + strsh.sh_size <= size, "file has an invalid string table");

            str_table = data + strsh.sh_offset;
            str_table_len = strsh.sh_size;

            break;
        }
    }

    SymbolTable& symtable = memory.GetSymbolTable();
    for (size_t i = 1 /* first entry is unused */; i < elf_sym_table_len; ++i)
    {
        elf_sym_table[i].st_name = elftohw(elf_sym_table[i].st_name);
        elf_sym_table[i].st_value = elftoha(elf_sym_table[i].st_value);
#if ELFCLASS == ELFCLASS64
        elf_sym_table[i].st_size = elftohxw(elf_sym_table[i].st_size);
#else
        elf_sym_table[i].st_size = elftohw(elf_sym_table[i].st_size);
#endif

        Verify(elf_sym_table[i].st_name < str_table_len, "file specifies an invalid symbol");

        const char* name = str_table + elf_sym_table[i].st_name;
        MemAddr addr = elf_sym_table[i].st_value;
        if (addr != 0 && strlen(name) > 0)
        {
            symtable.AddSymbol(addr, name, elf_sym_table[i].st_size);
        }
    }

    Elf_Phdr* phdr = static_cast<Elf_Phdr*>(static_cast<void*>(data + ehdr.e_phoff));

    // Determine base address and check for loadable segments
    bool     hasLoadable = false;
    Elf_Addr base = 0;
    for (Elf_Half i = 0; i < ehdr.e_phnum; ++i)
    {
        phdr[i].p_type   = elftohw (phdr[i].p_type);
        phdr[i].p_flags  = elftohw (phdr[i].p_flags);
        phdr[i].p_offset = elftoho (phdr[i].p_offset);
        phdr[i].p_vaddr  = elftoha (phdr[i].p_vaddr);
        phdr[i].p_paddr  = elftoha (phdr[i].p_paddr);
        phdr[i].p_filesz = elftohxw(phdr[i].p_filesz);
        phdr[i].p_memsz  = elftohxw(phdr[i].p_memsz);
        phdr[i].p_align  = elftohxw(phdr[i].p_align);

        if (phdr[i].p_type == PT_LOAD)
        {
            if (!hasLoadable || phdr[i].p_vaddr < base) {
                base = phdr[i].p_vaddr;
            }
            hasLoadable = true;
        }
    }
    // Verify(hasLoadable, "file has no loadable segments");
    base = base & -PAGE_SIZE;

    // Then copy the LOAD segments into their right locations
    for (Elf_Half i = 0; i < ehdr.e_phnum; ++i)
    {
        if (phdr[i].p_type == PT_LOAD && phdr[i].p_memsz > 0)
        {
            Verify(phdr[i].p_memsz >= phdr[i].p_filesz, "file has an invalid segment");

            int perm = 0;
            if (phdr[i].p_flags & PF_R) perm |= IMemory::PERM_READ|IMemory::PERM_DCA_READ;
            if (phdr[i].p_flags & PF_W) perm |= IMemory::PERM_WRITE|IMemory::PERM_DCA_WRITE;
            if (phdr[i].p_flags & PF_X) perm |= IMemory::PERM_EXECUTE;

            if (phdr[i].p_filesz > 0)
            {
                Verify(phdr[i].p_offset + phdr[i].p_filesz <= size, "file has an invalid segment");

                ActiveROM::LoadableRange r;
                r.rom_offset = phdr[i].p_offset;
                r.rom_size = phdr[i].p_filesz;
                r.vaddr = phdr[i].p_vaddr;
                r.vsize = phdr[i].p_memsz;
                r.perm = (IMemory::Permissions)perm;
                ranges.push_back(r);

                // We do not reserve here because this
                // will be taken care of by the ActiveROM during loading.
                if (verbose)
                {
                    clog << msg_prefix << ": " << dec << phdr[i].p_filesz << " bytes loadable at virtual address 0x" << hex << phdr[i].p_vaddr << endl;
                }
            }
            else
            {
                memory.Reserve(phdr[i].p_vaddr, phdr[i].p_memsz, 0, perm);
                if (verbose)
                {
                    clog << msg_prefix << ": " << dec << phdr[i].p_memsz << " bytes reserved at virtual address 0x" << hex << phdr[i].p_vaddr << endl;
                }
            }
        }
    }

    if (verbose)
    {
        const char* type = (ehdr.e_machine == MACHINE_LEGACY)
            ? "legacy"
            : "microthreaded";

        clog << msg_prefix << ": loaded " << type << " ELF binary with virtual base address 0x" << hex << base
             << ", entry point at 0x" << hex << ehdr.e_entry << endl;
    }
    return make_pair(ehdr.e_entry, ehdr.e_machine == MACHINE_LEGACY);
}

