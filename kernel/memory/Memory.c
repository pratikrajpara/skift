
#include <libsystem/Atomic.h>
#include <libsystem/CString.h>
#include <libsystem/Logger.h>
#include <libsystem/io/Stream.h>

#include "kernel/memory/Memory.h"
#include "kernel/memory/Physical.h"
#include "kernel/memory/Virtual.h"

static bool _memory_initialized = false;

extern int __start;
extern int __end;

static MemoryRange kernel_memory_range(void)
{
    return memory_range_around_non_aligned_address((uintptr_t)&__start, (size_t)&__end - (size_t)&__start);
}

void memory_initialize(Multiboot *multiboot)
{
    logger_info("Initializing memory management...");

    for (size_t i = 0; i < 1024 * 1024 / 8; i++)
    {
        MEMORY[i] = 0xff;
    }

    // Setup the kernel pagedirectory.
    for (uint i = 0; i < 256; i++)
    {
        PageDirectoryEntry *entry = &kpdir.entries[i];
        entry->User = 0;
        entry->Write = 1;
        entry->Present = 1;
        entry->PageFrameNumber = (uint)&kptable[i] / PAGE_SIZE;
    }

    for (size_t i = 0; i < multiboot->memory_map_size; i++)
    {
        MemoryMapEntry *entry = &multiboot->memory_map[i];

        if (entry->type == MEMORY_MAP_ENTRY_AVAILABLE)
        {
            physical_set_free(entry->range.base, entry->range.size / PAGE_SIZE);
        }
    }

    USED_MEMORY = 0;
    TOTAL_MEMORY = multiboot->memory_usable;

    logger_info("Mapping kernel...");
    memory_map_eternal(&kpdir, kernel_memory_range());

    logger_info("Mapping modules...");
    for (size_t i = 0; i < multiboot->modules_size; i++)
    {
        memory_map_eternal(&kpdir, multiboot->modules[i].range);
    }

    virtual_unmap(memory_kpdir(), 0, 1); // Unmap the 0 page
    physical_set_used(0, 1);

    memory_pdir_switch(&kpdir);
    paging_enable();

    logger_info("%uKio of memory detected", TOTAL_MEMORY / 1024);
    logger_info("%uKio of memory is used by the kernel", USED_MEMORY / 1024);

    logger_info("Paging enabled!");

    _memory_initialized = true;
}

void memory_dump(void)
{
    printf("\n\tMemory status:");
    printf("\n\t - Used  physical Memory: %12dkib", USED_MEMORY / 1024);
    printf("\n\t - Total physical Memory: %12dkib", TOTAL_MEMORY / 1024);
}

size_t memory_get_used(void)
{
    size_t result;

    atomic_begin();

    result = USED_MEMORY;

    atomic_end();

    return result;
}

size_t memory_get_total(void)
{
    size_t result;

    atomic_begin();

    result = TOTAL_MEMORY;

    atomic_end();

    return result;
}

PageDirectory *memory_kpdir(void)
{
    return &kpdir;
}

uintptr_t memory_alloc(PageDirectory *page_directory, size_t size, MemoryFlags flags)
{
    if (!size)
        return 0;

    size_t page_count = PAGE_ALIGN_UP(size) / PAGE_SIZE;

    atomic_begin();

    uintptr_t physical_address = physical_alloc(page_count);

    if (!physical_address)
    {
        atomic_end();

        logger_error("Failled to allocate memory: not enough physical memory!");

        return 0;
    }

    uintptr_t virtual_address = virtual_alloc(
        page_directory,
        physical_address,
        page_count,
        flags & MEMORY_USER);

    if (!virtual_address)
    {
        physical_free(physical_address, page_count);
        atomic_end();

        logger_error("Failled to allocate memory: not enough virtual memory!");

        return 0;
    }

    atomic_end();

    if (flags & MEMORY_CLEAR)
        memset((void *)virtual_address, 0, page_count * PAGE_SIZE);

    return virtual_address;
}

uintptr_t memory_alloc_identity_page(PageDirectory *pdir)
{
    atomic_begin();

    for (size_t i = 1; i < 256 * 1024; i++)
    {
        int address = i * PAGE_SIZE;

        if (!page_present(pdir, address) &&
            !physical_is_used(address, 1))
        {
            physical_set_used(address, 1);
            virtual_map(pdir, address, address, 1, false);

            atomic_end();

            memset((void *)address, 0, PAGE_SIZE);

            return address;
        }
    }

    atomic_end();

    logger_warn("Failled to allocate identity mapped page!");
    return 0;
}

void memory_free(PageDirectory *pdir, uint addr, uint count, int user)
{
    __unused(user);

    atomic_begin();

    if (virtual_present(pdir, addr, count))
    {
        physical_free(virtual2physical(pdir, addr), count);
        virtual_unmap(pdir, addr, count);
    }

    atomic_end();
}

int memory_map(PageDirectory *pdir, uint addr, uint count, int user)
{
    atomic_begin();

    for (uint i = 0; i < count; i++)
    {
        uint vaddr = addr + i * PAGE_SIZE;

        if (!virtual_present(pdir, vaddr, 1))
        {
            uint paddr = physical_alloc(1);
            virtual_map(pdir, vaddr, paddr, 1, user);
        }
    }

    atomic_end();

    return 0;
}

int memory_map_eternal(PageDirectory *pdir, MemoryRange range)
{
    size_t page_count = PAGE_ALIGN_UP(range.size) / PAGE_SIZE;

    atomic_begin();
    physical_set_used(range.base, page_count);
    virtual_map(pdir, range.base, range.base, page_count, 0);
    atomic_end();

    return 0;
}

int memory_unmap(PageDirectory *pdir, uint addr, uint count)
{
    atomic_begin();

    for (uint i = 0; i < count; i++)
    {
        uint vaddr = addr + i * PAGE_SIZE;

        if (virtual_present(pdir, vaddr, 1))
        {
            physical_free(virtual2physical(pdir, vaddr), 1);
            virtual_unmap(pdir, vaddr, 1);
        }
    }

    atomic_end();

    return 0;
}

PageDirectory *memory_pdir_create()
{
    atomic_begin();

    PageDirectory *pdir = (PageDirectory *)memory_alloc(&kpdir, sizeof(PageDirectory), MEMORY_CLEAR);

    if (pdir == NULL)
    {
        logger_error("Page directory allocation failled!");
        return NULL;
    }

    memset(pdir, 0, sizeof(PageDirectory));

    // Copy first gigs of virtual memory (kernel space);
    for (uint i = 0; i < 256; i++)
    {
        PageDirectoryEntry *e = &pdir->entries[i];
        e->User = 0;
        e->Write = 1;
        e->Present = 1;
        e->PageFrameNumber = (uint)&kptable[i] / PAGE_SIZE;
    }

    atomic_end();

    return pdir;
}

void memory_pdir_destroy(PageDirectory *pdir)
{
    atomic_begin();

    for (size_t i = 256; i < 1024; i++)
    {
        PageDirectoryEntry *e = &pdir->entries[i];

        if (e->Present)
        {
            PageTable *pt = (PageTable *)(e->PageFrameNumber * PAGE_SIZE);

            for (size_t i = 0; i < 1024; i++)
            {
                PageTableEntry *p = &pt->entries[i];

                if (p->Present)
                {
                    physical_free(p->PageFrameNumber * PAGE_SIZE, 1);
                }
            }

            memory_free(&kpdir, (uint)pt, 1, 0);
        }
    }
    memory_free(&kpdir, (uint)pdir, 1, 0);

    atomic_end();
}

#define MEMORY_DUMP_REGION_START(__pdir, __addr)               \
    {                                                          \
        memory_used = true;                                    \
        memory_empty = false;                                  \
        current_physical = virtual2physical(__pdir, __addr);   \
        printf("\n\t %8x [%08x:", (__addr), current_physical); \
    }

#define MEMORY_DUMP_REGION_END(__pdir, __addr)                            \
    {                                                                     \
        memory_used = false;                                              \
        printf("%08x] %08x", virtual2physical(__pdir, __addr), (__addr)); \
    }

void memory_pdir_dump(PageDirectory *pdir, bool user)
{
    if (!_memory_initialized)
        return;

    bool memory_used = false;
    bool memory_empty = true;
    uint current_physical = 0;

    for (int i = user ? 256 : 0; i < 1024; i++)
    {
        PageDirectoryEntry *pde = &pdir->entries[i];
        if (pde->Present)
        {
            PageTable *ptable = (PageTable *)(pde->PageFrameNumber * PAGE_SIZE);

            for (int j = 0; j < 1024; j++)
            {
                PageTableEntry *p = &ptable->entries[j];

                if (p->Present && !memory_used)
                {
                    MEMORY_DUMP_REGION_START(pdir, (i * 1024 + j) * PAGE_SIZE);
                }
                else if (!p->Present && memory_used)
                {
                    MEMORY_DUMP_REGION_END(pdir, (i * 1024 + j - 1) * PAGE_SIZE);
                }
                else if (p->Present)
                {
                    uint new_physical = virtual2physical(pdir, (i * 1024 + j) * PAGE_SIZE);

                    if (!(current_physical + PAGE_SIZE == new_physical))
                    {
                        printf("%08x | ", current_physical);
                        printf("%08x:", new_physical);
                    }

                    current_physical = new_physical;
                }
            }
        }
        else if (memory_used)
        {
            MEMORY_DUMP_REGION_END(pdir, (i * 1024 - 1) * PAGE_SIZE);
        }
    }

    if (memory_empty)
    {
        printf("[empty]");
    }
}

void memory_pdir_switch(PageDirectory *pdir)
{
    paging_load_directory(virtual2physical(&kpdir, (uintptr_t)pdir));
}
