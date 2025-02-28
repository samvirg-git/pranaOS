/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef _KERNEL_PLATFORM_AARCH32_VMM_PDE_H
#define _KERNEL_PLATFORM_AARCH32_VMM_PDE_H

#include <libkern/c_attrs.h>
#include <libkern/types.h>

struct PACKED table_desc {
    union {
        struct {
            int valid : 1; /* Valid mapping */
            int zero1 : 1;
            int zero2 : 1;
            int ns : 1;
            int zero3 : 1;
            int domain : 4;
            int imp : 1;
            int baddr : 22;
        };
        uint32_t data;
    };
};
typedef struct table_desc table_desc_t;

#define pde_t table_desc_t
#define TABLE_DESC_FRAME_OFFSET 10

enum TABLE_DESC_PAGE_FLAGS {
    TABLE_DESC_PRESENT = 0x1,
    TABLE_DESC_WRITABLE = 0x2,
    TABLE_DESC_USER = 0x4,
    TABLE_DESC_PWT = 0x8,
    TABLE_DESC_PCD = 0x10,
    TABLE_DESC_ACCESSED = 0x20,
    TABLE_DESC_DIRTY = 0x40,
    TABLE_DESC_4MB = 0x80,
    TABLE_DESC_CPU_GLOBAL = 0x100,
    TABLE_DESC_LV4_GLOBAL = 0x200,
    TABLE_DESC_COPY_ON_WRITE = 0x400,
    TABLE_DESC_ZEROING_ON_DEMAND = 0x800
};

void table_desc_init(table_desc_t* pde);
void table_desc_set_allocated_state(table_desc_t* pde);
bool table_desc_is_in_allocated_state(table_desc_t* pde);
void table_desc_clear(table_desc_t* pde);
void table_desc_set_attrs(table_desc_t* pde, uint32_t attrs);
void table_desc_del_attrs(table_desc_t* pde, uint32_t attrs);
bool table_desc_has_attrs(table_desc_t pde, uint32_t attrs);

void table_desc_set_frame(table_desc_t* pde, uint32_t frame);
void table_desc_del_frame(table_desc_t* pde);

bool table_desc_is_present(table_desc_t pde);
bool table_desc_is_writable(table_desc_t pde);
bool table_desc_is_4mb(table_desc_t pde);
bool table_desc_is_copy_on_write(table_desc_t pde);
uint32_t table_desc_get_frame(table_desc_t pde);

#endif //_KERNEL_PLATFORM_AARCH32_VMM_PDE_H
