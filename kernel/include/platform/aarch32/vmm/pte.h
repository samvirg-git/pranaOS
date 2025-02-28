/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef _KERNEL_PLATFORM_AARCH32_VMM_PTE_H
#define _KERNEL_PLATFORM_AARCH32_VMM_PTE_H

#include <libkern/c_attrs.h>
#include <libkern/types.h>

struct PACKED page_desc {
    union {
        struct {
            unsigned int xn : 1; // Execute never. Stops execution of page.
            unsigned int one : 1; // Always one for tables
            unsigned int b : 1; // cacheable
            unsigned int c : 1; // Cacheable
            unsigned int ap1 : 2;
            unsigned int tex : 3;
            unsigned int ap2 : 1;
            unsigned int s : 1;
            unsigned int ng : 1;
            unsigned int baddr : 20;
        };
        uint32_t data;
    };
};
typedef struct page_desc page_desc_t;

#define pte_t page_desc_t
#define PAGE_DESC_FRAME_OFFSET 12

enum PAGE_DESC_PAGE_FLAGS {
    PAGE_DESC_PRESENT = 0x1,
    PAGE_DESC_WRITABLE = 0x2,
    PAGE_DESC_USER = 0x4,
    PAGE_DESC_WRITETHOUGH = 0x8,
    PAGE_DESC_NOT_CACHEABLE = 0x10,
    PAGE_DESC_ACCESSED = 0x20,
    PAGE_DESC_DIRTY = 0x40,
    PAGE_DESC_PAT = 0x80,
    PAGE_DESC_CPU_GLOBAL = 0x100,
    PAGE_DESC_LV4_GLOBAL = 0x200,
    PAGE_DESC_COPY_ON_WRITE = 0x400,
    PAGE_DESC_ZEROING_ON_DEMAND = 0x800
};

void page_desc_init(page_desc_t* pte);
void page_desc_set_attrs(page_desc_t* pte, uint32_t attrs);
void page_desc_del_attrs(page_desc_t* pte, uint32_t attrs);
bool page_desc_has_attrs(page_desc_t pte, uint32_t attr);

void page_desc_set_frame(page_desc_t* pte, uint32_t frame);
void page_desc_del_frame(page_desc_t* pte);

bool page_desc_is_present(page_desc_t pte);
bool page_desc_is_writable(page_desc_t pte);
bool page_desc_is_user(page_desc_t pte);
bool page_desc_is_not_cacheable(page_desc_t pte);
bool page_desc_is_cow(page_desc_t pte);

uint32_t page_desc_get_frame(page_desc_t pte);
uint32_t page_desc_get_settings(page_desc_t pte);
uint32_t page_desc_get_settings_ignore_cow(page_desc_t pte);

#endif //_KERNEL_PLATFORM_AARCH32_VMM_PTE_H
