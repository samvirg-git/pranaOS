/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <fs/vfs.h>
#include <libkern/bits/errno.h>
#include <libkern/libkern.h>
#include <libkern/log.h>
#include <mem/kmalloc.h>
#include <mem/vmm/zoner.h>
#include <tasking/elf.h>
#include <tasking/tasking.h>

// #define ELF_DEBUG

#ifdef __i386__
#define MACHINE_ARCH EM_386
#elif __arm__
#define MACHINE_ARCH EM_ARM
#endif

#define PAGES_PER_COPING_BUFFER 8
#define COPING_BUFFER_LEN (PAGES_PER_COPING_BUFFER * VMM_PAGE_SIZE)
#define USER_STACK_SIZE VMM_PAGE_SIZE

static int _elf_load_do_copy_to_ram(proc_t* p, file_descriptor_t* fd, elf_program_header_32_t* ph)
{
    pdirectory_t* prev_pdir = vmm_get_active_pdir();
    vmm_switch_pdir(p->pdir);

    uint32_t zones_count = proc->zones.size;

    for (uint32_t i = 0; i < zones_count; i++) {
        proc_zone_t* zone = (proc_zone_t*)dynamic_array_get(&proc->zones, i);
        if (zone->type == ZONE_TYPE_BSS) {
            memset((void*)zone->start, 0, zone->len);
        }
    }

    zone_t coping_zone = zoner_new_zone(COPING_BUFFER_LEN);
    uint32_t mem_remaining = ph->p_memsz;
    uint32_t file_remaining = ph->p_filesz;
    uint32_t mem_offset = ph->p_vaddr;
    uint32_t file_offset = ph->p_offset;

    while (mem_remaining) {
        memset(coping_zone.ptr, 0, COPING_BUFFER_LEN);
        if (file_remaining) {
            uint32_t file_read_len = min(file_remaining, COPING_BUFFER_LEN);
            fd->ops->read(fd->dentry, coping_zone.ptr, file_offset, file_read_len);
            file_offset += file_read_len;
            file_remaining -= file_read_len;
        }

        void* write_ptr = coping_zone.ptr;
        for (int i = 0; i < PAGES_PER_COPING_BUFFER && mem_remaining; i++) {
            uint32_t mem_write_len = min(mem_remaining, VMM_PAGE_SIZE);
            if (proc_find_zone(p, mem_offset)) {
                vmm_copy_to_user((void*)mem_offset, write_ptr, mem_write_len);
            }
            mem_offset += mem_write_len;
            mem_remaining -= mem_write_len;
            write_ptr += VMM_PAGE_SIZE;
        }
    }

    zoner_free_zone(coping_zone);
    return vmm_switch_pdir(prev_pdir);
}

static int _elf_load_interpret_program_header_entry(proc_t* p, file_descriptor_t* fd)
{
    elf_program_header_32_t ph;
    int err = vfs_read(fd, &ph, sizeof(ph));
    if (err != sizeof(ph)) {
        return err;
    }

#ifdef ELF_DEBUG
    log("Header type %x %x - %x", ph.p_type, ph.p_vaddr, ph.p_memsz);
#endif
    switch (ph.p_type) {
    case PT_LOAD:
        _elf_load_do_copy_to_ram(p, fd, &ph);
        break;
    default:
        break;
    }

    return 0;
}

static int _elf_load_interpret_section_header_entry(proc_t* p, file_descriptor_t* fd)
{
    elf_section_header_32_t sh;
    int err = vfs_read(fd, &sh, sizeof(sh));
    if (err != sizeof(sh)) {
        return err;
    }

#ifdef ELF_DEBUG
    log("Section type %x %x - %x", sh.sh_type, sh.sh_addr, sh.sh_size);
#endif
    if (sh.sh_flags & SHF_ALLOC) {
        uint32_t zone_flags = ZONE_READABLE;
        uint32_t zone_type = ZONE_TYPE_NULL;
        if (sh.sh_flags & SHF_WRITE) {
            zone_flags |= ZONE_WRITABLE;
        }
        if (sh.sh_flags & SHF_EXECINSTR) {
            zone_flags |= ZONE_EXECUTABLE;
        }
        // FIXME: Mapping of zone types is bad.
        switch (sh.sh_type) {
        case SHT_PROGBITS:
            zone_type = ZONE_TYPE_CODE;
            break;
        case SHT_NOBITS:
            zone_type = ZONE_TYPE_BSS;
            break;
        case SHT_INIT_ARRAY:
            zone_type = ZONE_TYPE_DATA;
            break;
        case SHT_FINI_ARRAY:
            zone_type = ZONE_TYPE_DATA;
            break;
        case SHT_PREINIT_ARRAY:
            zone_type = ZONE_TYPE_DATA;
            break;
        default:
            break;
        }

        if (zone_type) {
            proc_zone_t* zone = proc_extend_zone(p, sh.sh_addr, sh.sh_size);
            if (zone) {
                zone->type = zone_type;
                zone->flags |= zone_flags;
            }
        }
    }
    return 0;
}

static int _elf_load_alloc_stack(proc_t* p)
{
    proc_zone_t* stack_zone = proc_new_random_zone_backward(p, USER_STACK_SIZE);
    stack_zone->type = ZONE_TYPE_STACK;
    stack_zone->flags |= ZONE_READABLE | ZONE_WRITABLE;
    set_base_pointer(p->main_thread->tf, stack_zone->start + USER_STACK_SIZE);
    set_stack_pointer(p->main_thread->tf, stack_zone->start + USER_STACK_SIZE);
    return 0;
}

static inline int _elf_do_load(proc_t* p, file_descriptor_t* fd, elf_header_32_t* header)
{
    fd->offset = header->e_shoff;
    int sh_num = header->e_shnum;
    for (int i = 0; i < sh_num; i++) {
        _elf_load_interpret_section_header_entry(p, fd);
    }

    fd->offset = header->e_phoff;
    int ph_num = header->e_phnum;
    for (int i = 0; i < ph_num; i++) {
        _elf_load_interpret_program_header_entry(p, fd);
    }

    proc_zone_t* stack_zone = proc_new_random_zone(p, VMM_PAGE_SIZE); // Forbid 0 allocations to make it work well
    _elf_load_alloc_stack(p);
    set_instruction_pointer(p->main_thread->tf, header->e_entry);
    return 0;
}

int elf_check_header(elf_header_32_t* header)
{
    static const char elf_signature[] = { 0x7F, 0x45, 0x4c, 0x46 };
    if (memcmp(header->e_ident, elf_signature, sizeof(elf_signature)) != 0) {
        return -ENOEXEC;
    }

    // FIXME: Currently we support only 32bit execs
    if (header->e_ident[EI_CLASS] != ELF_CLASS_32) {
        return -EBADARCH;
    }

    if (header->e_type != ET_EXEC) {
        return -ENOEXEC;
    }

    if (header->e_machine != MACHINE_ARCH) {
        return -EBADARCH;
    }

    return 0;
}

int elf_load(proc_t* p, file_descriptor_t* fd)
{
    elf_header_32_t header;
    int err = vfs_read(fd, &header, sizeof(header));
    if (err != sizeof(header)) {
        return err;
    }

    err = elf_check_header(&header);
    if (err) {
        return err;
    }

    return _elf_do_load(p, fd, &header);
}

int elf_find_symtab_unchecked(void* mapped_data, void** symtab, size_t* symtab_entries, char** strtab)
{
    *symtab = NULL;
    *symtab_entries = 0;
    *strtab = NULL;
    elf_header_32_t* header = (elf_header_32_t*)mapped_data;

    int sh_num = header->e_shnum;
    elf_section_header_32_t* headers_array = (elf_section_header_32_t*)(mapped_data + header->e_shoff);
    for (int i = 0; i < sh_num; i++) {
        if (headers_array[i].sh_type == SHT_SYMTAB) {
            *symtab = (void*)(mapped_data + headers_array[i].sh_offset);
            *symtab_entries = headers_array[i].sh_size / sizeof(elf_sym_32_t);
        }

        if (headers_array[i].sh_type == SHT_STRTAB && header->e_shstrndx != i) {
            *strtab = (void*)(mapped_data + headers_array[i].sh_offset);
        }
    }

    return 0;
}

ssize_t elf_find_function_in_symtab(void* symtab_p, size_t syms_n, uint32_t ip)
{
    elf_sym_32_t* symtab = (elf_sym_32_t*)(symtab_p);
    uint32_t prev = 0;
    ssize_t ans = -1;

    for (size_t i = 0; i < syms_n; i++) {
        if (prev <= symtab[i].st_value && symtab[i].st_value <= ip) {
            prev = symtab[i].st_value;
            ans = symtab[i].st_name;
        }
    }
    return ans;
}