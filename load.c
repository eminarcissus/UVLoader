/*
 * load.c - Parses and loads an ELF to memory
 * Copyright 2012 Yifan Lu
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *    http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "load.h"
#include "resolve.h"
#include "scefuncs.h"
#include "utils.h"

/********************************************//**
 *  \brief Loads file to memory
 *  
 *  \returns Zero on success, otherwise error
 ***********************************************/
int
uvl_load_file (const char *filename,    ///< File to load
                     void **data,       ///< Output pointer to data
                  PsvSSize *size)       ///< Output pointer to data size
{
    PsvUID fd;
    PsvUID memblock;
    void *base;

    fd = sceIoOpen (filename, PSP2_O_RDONLY, 0);
    if (fd < 0)
    {
        LOG ("Failed to open %s for reading.", filename);
        return -1;
    }
    memblock = sceKernelAllocMemBlock ("UVLTemp", 0xC20D060, UVL_BIN_MAX_SIZE, NULL);
    if (memblock < 0)
    {
        LOG ("Failed allocate %u bytes of memory.", memblock);
        return -1;
    }
    if (sceKernelGetMemBlockBase (memblock, &base) < 0)
    {
        LOG ("Failed to locate base for block 0x%08X.", memblock);
        return -1;
    }
    *size = sceIoRead (fd, base, UVL_BIN_MAX_SIZE);
    if (*size < 0)
    {
        LOG ("Failed to read %s: 0x%08X", filename, *size);
        return -1;
    }
    if (*size >= UVL_BIN_MAX_SIZE)
    {
        LOG ("Warning. Max homebrew size of %u bytes reached. File could be truncated.", UVL_BIN_MAX_SIZE);
    }
    IF_DEBUG LOG ("Read %u bytes from %s", *size, filename);
    if (sceIoClose (fd) < 0)
    {
        LOG ("Failed to close file.");
        return -1;
    }

    *data = base;

    return 0;
}

/********************************************//**
 *  \brief Frees data pointer created by load
 *  
 *  \returns Zero on success, otherwise error
 ***********************************************/
static inline int
uvl_free_data (void *data)      ///< Data allocated by @c uvl_load_file
{
    PsvUID block;

    block = sceKernelFindMemBlockByAddr (data, 0);
    if (block < 0)
    {
        LOG ("Cannot find block id: 0x%08X", block);
    }
    if (sceKernelFreeMemBlock (block) < 0)
    {
        LOG ("Cannot free block: 0x%08X", block);
        return -1;
    }
    return 0;
}

/********************************************//**
 *  \brief Loads an supported executable
 *  
 *  This function identifies and loads a 
 *  executable at the given file.
 *  Currently supports ELF and SCE executable.
 *  \returns Zero on success, otherwise error
 ***********************************************/
int
uvl_load_exe (const char *filename, ///< Absolute path to executable
                    void **entry)   ///< Returned pointer to entry pointer
{
    void *data;
    PsvSSize size;
    char *magic;

    *entry = NULL;
    IF_DEBUG LOG ("Opening %s for reading.", filename);
    if (uvl_load_file (filename, &data, &size) < 0)
    {
        LOG ("Cannot load file.");
        return -1;
    }

    magic = (char*)data;
    IF_VERBOSE LOG ("Magic number: 0x%02X 0x%02X 0x%02X 0x%02X", magic[0], magic[1], magic[2], magic[3]);

    if (magic[0] == ELFMAG0)
    {
        if (magic[1] == ELFMAG1 && magic[2] == ELFMAG2 && magic[3] == ELFMAG3)
        {
            IF_DEBUG LOG ("Found a ELF, loading.");
            if (uvl_load_elf (data, entry) < 0)
            {
                LOG ("Cannot load ELF.");
                return -1;
            }
        }
    }
    else if (magic[0] == SCEMAG0)
    {
        if (magic[1] == SCEMAG1 && magic[2] == SCEMAG2 && magic[3] == SCEMAG3)
        {
            IF_DEBUG LOG ("Loading SELF.");
            if (uvl_load_elf ((void*)((u32_t)data + SCEHDR_LEN), entry) < 0)
            {
                LOG ("Cannot load SELF.");
                return -1;
            }
        }
    }
    else
    {
        LOG ("Invalid magic.");
        return -1;
    }

    // free data
    if (uvl_free_data (data) < 0)
    {
        LOG ("Cannot free data");
        return -1;
    }
    return 0;
}

/********************************************//**
 *  \brief Changes import table's offsets 
 *  to loaded file in memory.
 *  
 *  This is only used when NIDs needs to be 
 *  resolved before the program is loaded to 
 *  it's proper location.
 ***********************************************/
static inline void
uvl_offset_import (module_imports_t *import,   ///< Import table to modify
                                int addend)    ///< Positive or negative number to add to all offsets
{
    int i;
    IF_VERBOSE LOG ("Modifying import table offsets for 0x%08X", (u32_t)import);
    import->lib_name = (void*)((u32_t)import->lib_name + addend);
    import->func_nid_table = (void*)((u32_t)import->func_nid_table + addend);
    import->func_entry_table = (void*)((u32_t)import->func_entry_table + addend);
    import->var_nid_table = (void*)((u32_t)import->var_nid_table + addend);
    import->var_entry_table = (void*)((u32_t)import->var_entry_table + addend);
    import->tls_nid_table = (void*)((u32_t)import->tls_nid_table + addend);
    import->tls_entry_table = (void*)((u32_t)import->tls_entry_table + addend);
    IF_VERBOSE LOG ("Modifying import table entries offsets for 0x%08X", (u32_t)import);
    for (i = 0; i < import->num_functions; i++)
    {
        import->func_entry_table[i] = (void*)((u32_t)import->func_entry_table[i] + addend);
    }
    for (i = 0; i < import->num_vars; i++)
    {
        import->var_entry_table[i] = (void*)((u32_t)import->var_entry_table[i] + addend);
    }
    for (i = 0; i < import->num_tls_vars; i++)
    {
        import->tls_entry_table[i] = (void*)((u32_t)import->tls_entry_table[i] + addend);
    }
}

/********************************************//**
 *  \brief Loads an ELF file
 *  
 *  Performs both loading and resolving NIDs
 *  \returns Zero on success, otherwise error
 ***********************************************/
int 
uvl_load_elf (void *data,           ///< ELF data start
              void **entry)         ///< Returned pointer to entry pointer
{
    Elf32_Ehdr_t *elf_hdr;
    u32_t i;
    int addend;
    *entry = NULL;

    // get headers
    IF_VERBOSE LOG ("Reading headers.");
    elf_hdr = data;
    IF_DEBUG LOG ("Checking headers.");
    if (uvl_elf_check_header (elf_hdr) < 0)
    {
        LOG ("Check header failed.");
        return -1;
    }

    // get program headers
    Elf32_Phdr_t *prog_hdrs;
    IF_VERBOSE LOG ("Reading program headers.");
    prog_hdrs = (void*)((u32_t)data + elf_hdr->e_phoff);

    // get mod_info
    module_info_t *mod_info;
    IF_DEBUG LOG ("Getting module info.");
    if (uvl_elf_get_module_info (data, elf_hdr, &mod_info) < 0)
    {
        LOG ("Cannot find module info section.");
        return -1;
    }
    IF_DEBUG LOG ("Module name: %s, export table offset: 0x%08X, import table offset: 0x%08X", mod_info->modname, mod_info->ent_top, mod_info->stub_top);

    // free memory
    IF_DEBUG LOG ("Cleaning up memory.");
    if (uvl_elf_free_memory (prog_hdrs, elf_hdr->e_phnum) < 0)
    {
        LOG ("Error freeing memory.");
        return -1;
    }

    // actually load the ELF
    PsvUID memblock;
    void *blockaddr;
    u32_t length;
    if (elf_hdr->e_phnum < 1)
    {
        LOG ("No program sections to load!");
        return -1;
    }
    IF_DEBUG LOG ("Loading %u program sections.", elf_hdr->e_phnum);
    for (i = 0; i < elf_hdr->e_phnum; i++)
    {
        if (prog_hdrs[i].p_type != PT_LOAD || prog_hdrs[i].p_vaddr == 0)
        {
            IF_DEBUG LOG ("Section %u is not loadable. Skipping.", i);
            continue;
        }
        length = prog_hdrs[i].p_memsz;
        length = (length + 0xFFFFF) & ~0xFFFFF; // Align to 1MB
        if (prog_hdrs[i].p_flags & PF_X == PF_X) // executable section
        {
            memblock = sceKernelAllocCodeMemBlock ("UVLHomebrew", length);
        }
        else // data section
        {
            memblock = sceKernelAllocMemBlock ("UVLHomebrew", 0xC20D060, length, NULL);
        }
        if (memblock < 0)
        {
            LOG ("Error allocating memory. 0x%08X", memblock);
            return -1;
        }
        if (sceKernelGetMemBlockBase (memblock, &blockaddr) < 0)
        {
            LOG ("Error getting memory block address.");
        }
        if ((u32_t)blockaddr != (u32_t)prog_hdrs[i].p_vaddr)
        {
            LOG ("Error, section %u wants to be loaded to 0x%08X but we allocated 0x%08X", i, (u32_t)prog_hdrs[i].p_vaddr, (u32_t)blockaddr);
            //return -1;
        }

        IF_DEBUG LOG ("Allocated memory at 0x%08X, attempting to load section %u.", (u32_t)blockaddr, i);
        psvUnlockMem ();
        memcpy (blockaddr, (void*)((u32_t)data + prog_hdrs[i].p_offset), prog_hdrs[i].p_filesz);
        IF_DEBUG LOG ("Zeroing %u remainder of memory.", prog_hdrs[i].p_memsz - prog_hdrs[i].p_filesz);
        memset ((void*)((u32_t)blockaddr + prog_hdrs[i].p_filesz), 0, prog_hdrs[i].p_memsz - prog_hdrs[i].p_filesz);
        psvLockMem ();
    }

    // resolve NIDs
    module_imports_t *import;
    void  *end;
    import = (void*)(prog_hdrs[0].p_vaddr + mod_info->stub_top);
    end = (void*)(prog_hdrs[0].p_vaddr + mod_info->stub_end);
    for (i = 0; (void*)&import[i] < end; i++)
    {
        IF_DEBUG LOG ("Loading module for %s", import[i].lib_name);
        if (uvl_load_module_for_lib (import[i].lib_name) < 0)
        {
            LOG ("Cannot load required module for %s. May still be possible to resolve with cached entries. Continuing.", import[i].lib_name);
            continue;
        }
        IF_DEBUG LOG ("Resolving imports for %s", import[i].lib_name);
        if (uvl_resolve_imports (&import[i]) < 0)
        {
            LOG ("Failed to resolve imports for %s", import[i].lib_name);
            return -1;
        }
    }

    // find the entry point
    module_exports_t *export;
    u32_t j;
    export = (void*)(prog_hdrs[0].p_vaddr + mod_info->ent_top);
    end = (void*)(prog_hdrs[0].p_vaddr + mod_info->ent_end);
    for (i = 0; (void*)&export[i] < end; i++)
    {
        if (export[i].attribute != ATTR_MOD_INFO)
        {
            continue;
        }
        for (j = 0; j < export[i].num_functions; j++)
        {
            if (export[i].nid_table[j] == ENTRY_NID)
            {
                *entry = export[i].entry_table[j];
                IF_DEBUG LOG ("Found application entry at 0x%08X", *entry);
                return 0;
            }
        }
    }
    LOG ("Cannot find application entry.");
    return -1;
}

/********************************************//**
 *  \brief Loads a system module by requested 
 *  library.
 *  
 *  All modules contain one or more library. 
 *  This will load the correct module given 
 *  the library name.
 *  \returns Zero on success, otherwise error
 ***********************************************/
int 
uvl_load_module_for_lib (char *lib_name) ///< Name of library for the module to load
{
    // TODO: Get filename for mod name and load module
    // First unload module if loaded 
    return 0;
}

/********************************************//**
 *  \brief Validates ELF header
 *  
 *  Makes sure the ELF is recognized by the 
 *  Vita's architecture.
 *  \returns Zero if valid, otherwise invalid
 ***********************************************/
int 
uvl_elf_check_header (Elf32_Ehdr_t *hdr) ///< ELF header to check
{
    IF_VERBOSE LOG ("Magic number: 0x%02X 0x%02X 0x%02X 0x%02X", hdr->e_ident[EI_MAG0], hdr->e_ident[EI_MAG1], hdr->e_ident[EI_MAG2], hdr->e_ident[EI_MAG3]);
    // magic number
    if (!(hdr->e_ident[EI_MAG0] == ELFMAG0 && hdr->e_ident[EI_MAG1] == ELFMAG1 && hdr->e_ident[EI_MAG2] == ELFMAG2 && hdr->e_ident[EI_MAG3] == ELFMAG3))
    {
        LOG ("Invalid ELF magic number.");
        return -1;
    }
    // class
    if (!(hdr->e_ident[EI_CLASS] == ELFCLASS32))
    {
        LOG ("Not a 32bit executable.");
        return -1;
    }
    // data
    if (!(hdr->e_ident[EI_DATA] == ELFDATA2LSB))
    {
        LOG ("Not a valid ARM executable.");
        return -1;
    }
    // version
    if (!(hdr->e_ident[EI_VERSION] == EV_CURRENT))
    {
        LOG ("Unsupported ELF version.");
        return -1;
    }
    // type
    if (!(hdr->e_type == ET_EXEC || hdr->e_type == ET_SCE_EXEC))
    {
        LOG ("Only ET_EXEC files can be loaded currently.");
        return -1;
    }
    // machine
    if (!(hdr->e_machine == EM_ARM))
    {
        LOG ("Not an ARM executable.");
        return -1;
    }
    // version
    if (!(hdr->e_version == EV_CURRENT))
    {
        LOG ("Unsupported ELF version.");
        return -1;
    }
    // contains headers
    if (!(hdr->e_shoff > 0 && hdr->e_phoff > 0))
    {
        LOG ("Missing table header(s).");
        return -1;
    }
    // contains strings
    if (!(hdr->e_shstrndx > 0))
    {
        LOG ("Missing strings table.");
        return -1;
    }
    return 0;
}

/********************************************//**
 *  \brief Finds SCE module info
 *  
 *  This function locates the strings table 
 *  and finds the section where the module 
 *  information resides. Then it reads the 
 *  module information. This function will 
 *  move the pointer in the file descriptor.
 *  \returns Zero on success, otherwise error
 ***********************************************/
int 
uvl_elf_get_module_info (void *data,            ///< ELF data start
                 Elf32_Ehdr_t *elf_hdr,         ///< ELF header
                module_info_t **mod_info)       ///< Where to read information to
{
    Elf32_Shdr_t *sec_hdr;
    // find strings table
    IF_DEBUG LOG ("Reading strings table header.");
    sec_hdr = (void*)((u32_t)data + elf_hdr->e_shoff + elf_hdr->e_shstrndx * elf_hdr->e_shentsize);

    IF_DEBUG LOG ("String table at %08X for %08X", sec_hdr->sh_offset, sec_hdr->sh_size);
    char *strings;
    int name_idx;
    strings = (void*)((u32_t)data + sec_hdr->sh_offset);
    name_idx = memstr (strings, sec_hdr->sh_size, UVL_SEC_MODINFO, strlen (UVL_SEC_MODINFO)) - strings;
    if (name_idx <= 0)
    {
        LOG ("Cannot find section %s in string table.", UVL_SEC_MODINFO);
        return -1;
    }
    IF_DEBUG LOG ("Index of %s: %u", UVL_SEC_MODINFO, name_idx);
    // find sceModuleInfo section
    int i;
    IF_DEBUG LOG ("Reading %u sections.", elf_hdr->e_shnum);
    for (i = 0; i < elf_hdr->e_shnum; i++)
    {
        sec_hdr = (void*)((u32_t)data + elf_hdr->e_shoff + i * sizeof (Elf32_Shdr_t));
        if (sec_hdr->sh_name == name_idx) // we want this section
        {
            IF_DEBUG LOG ("Found requested section %u.", i);
            IF_DEBUG LOG ("Reading section at offset 0x%08X. Size: %u", sec_hdr->sh_offset, sec_hdr->sh_size);
            *mod_info = (void*)((u32_t)data + sec_hdr->sh_offset);
            return 0;
        }
    }
    return -1;
}

/********************************************//**
 *  \brief Frees memory of where we want to load
 *  
 *  Finds the max and min addresses we want to
 *  load to using program headers and frees 
 *  any module taking up those spaces.
 *  \returns Zero on success, otherwise error
 ***********************************************/
int
uvl_elf_free_memory (Elf32_Phdr_t *prog_hdrs,   ///< Array of program headers
                              int count)        ///< Number of program headers
{
    void *min_addr = (void*)0xFFFFFFFF;
    void *max_addr = (void*)0x00000000;
    loaded_module_info_t m_mod_info;
    PsvUID mod_list[MAX_LOADED_MODS];
    u32_t num_loaded = MAX_LOADED_MODS;
    int i, j;
    u32_t length;
    int temp[2];

    IF_VERBOSE LOG ("Reading %u program headers.", count);
    for (i = 0; i < count; i++)
    {
        if (prog_hdrs[i].p_vaddr < min_addr)
        {
            min_addr = prog_hdrs[i].p_vaddr;
        }
        if ((u32_t)prog_hdrs[i].p_vaddr + prog_hdrs[i].p_memsz > (u32_t)max_addr)
        {
            max_addr = (void*)((u32_t)prog_hdrs[i].p_vaddr + prog_hdrs[i].p_memsz);
        }
    }
    IF_DEBUG LOG ("Lowest load address: 0x%08X, highest: 0x%08X", (u32_t)min_addr, (u32_t)max_addr);\

    IF_DEBUG LOG ("Getting list of loaded modules.");
    if (sceKernelGetModuleList (0xFF, mod_list, &num_loaded) < 0)
    {
        LOG ("Failed to get module list.");
        return -1;
    }
    IF_DEBUG LOG ("Found %u loaded modules.", num_loaded);
    for (i = 0; i < num_loaded; i++)
    {
        m_mod_info.size = sizeof (loaded_module_info_t); // should be 440
        IF_VERBOSE LOG ("Getting information for module #%u, UID: 0x%X.", i, mod_list[i]);
        if (sceKernelGetModuleInfo (mod_list[i], &m_mod_info) < 0)
        {
            LOG ("Error getting info for mod 0x%08X, continuing", mod_list[i]);
            continue;
        }
        for (j = 0; j < 3; j++)
        {
            //if (m_mod_info.segments[j].vaddr > min_addr || (u32_t)m_mod_info.segments[j].vaddr + m_mod_info.segments[j].memsz > (u32_t)min_addr)
            if (m_mod_info.segments[j].vaddr == (void*)0x81000000)
            {
                IF_DEBUG LOG ("Module %s segment %u (0x%08X, size %u) is in our address space. Attempting to unload.", m_mod_info.module_name, j, (u32_t)m_mod_info.segments[j].vaddr, m_mod_info.segments[j].memsz);
                if (sceKernelStopUnloadModule (mod_list[i], 0, 0, 0, &temp[0], &temp[1]) < 0)
                {
                    LOG ("Error unloading %s.", m_mod_info.module_name);
                    return -1;
                }
                break;
            }
        }
    }
    return 0;
}
