#include <pmm.h>
#include <list.h>
#include <string.h>
#include <stdio.h>
#include <buddy_pmm.h>
static free_area_t free_area;
#define free_list (free_area.free_list)
#define nr_free (free_area.nr_free)
#define MAX_INIT_PAGES (1 << 14)  // 2^14 = 16384 页

static void
buddy_system_init(void) {
    list_init(&free_list);
    nr_free = 0;
}

static void
buddy_system_init_memmap(struct Page *base, size_t n) {
    assert(n > 0);
    n = MAX_INIT_PAGES; // 限制最大分配的页数为 16384 页

    // 初始化每个页, 将其标记为未使用, 并将页面引用计数清零
    struct Page *p = base;
    for (; p != base + n; p ++) {
        assert(PageReserved(p));
        p->flags = p->property = 0;
        set_page_ref(p, 0);
    }
    base->property = n;// 将起始页的 property 设置为 n，表示这是一个大小为 n 页的空闲块
    SetPageProperty(base);// 标记 base 这个块为空闲块 
    nr_free += n;// 更新空闲页计数器
    if (list_empty(&free_list)) {
        // 将这个空闲块插入到 free_list 中
        list_add(&free_list, &(base->page_link));
    } else {
        list_entry_t* le = &free_list;
        while ((le = list_next(le)) != &free_list) {
            struct Page* page = le2page(le, page_link);
            if (base < page) {
                list_add_before(le, &(base->page_link));
                break;
            } else if (list_next(le) == &free_list) {
                list_add(le, &(base->page_link));
            }
        }
    }
}

static size_t roundup2(size_t n) {
    size_t size = 1;
    while (size < n) {
        size <<= 1;
    }
    return size;
}

static struct Page *
buddy_system_alloc_pages(size_t n) {
    assert(n > 0);
    n = roundup2(n);// 向上取整到最近的 2 的幂
    if (n > nr_free) {
        return NULL;  
    }               // 检查是否有足够空闲页
    struct Page *page = NULL;
    list_entry_t *le = &free_list;

    // 遍历空闲链表，查找第一个满足大小的块
    while ((le = list_next(le)) != &free_list) {
        struct Page *p = le2page(le, page_link);
        if (p->property >= n) {
            cprintf("找到第一个合适的空闲块 p\n");
            cprintf("准备用于分配的空闲块 p 的地址为: 0x%016lx.\n", p);
            cprintf("准备分配的空闲块 p 的页数为: %d\n", p->property);
            page = p;// 找到第一个块合适的空闲块
            break;
        }
    }

    // 分裂空闲块
    if (page != NULL) {

        list_entry_t* prev = list_prev(&(page->page_link));// 找到 page 前面的块
        list_del(&(page->page_link));// 从空闲块链表中移除空闲块 page

        // 持续分裂，直到大小匹配
        while (page->property > n) {
            struct Page *buddy = page + (page->property >> 1);  // 计算伙伴块
            buddy->property = page->property >> 1;              // 设置伙伴块大小
            SetPageProperty(buddy);                             // 标记为空闲块
            list_add(prev, &(buddy->page_link));                // 插入链表
            page->property >>= 1;                               // 当前块大小减半
        }
        nr_free -= n;   // 更新空闲页计数
        ClearPageProperty(page);    // 标记为已分配
    }
    return page;
}

static void
buddy_system_free_pages(struct Page *base, size_t n) {
    assert(n > 0);
    n = roundup2(n);// 向上取整到 2 的幂
    
    // 初始化页面元信息
    struct Page *p = base;
    for (; p != base + n; p ++) {
        assert(!PageReserved(p) && !PageProperty(p));
        p->flags = 0;
        set_page_ref(p, 0);// 页面引用计数清零 
    }

    base->property = n;     // 将起始页的 property 设置为 n，表示这是一个大小为 n 页的空闲块
    SetPageProperty(base);  // 标记 base 这个块为空闲块 
    nr_free += n;           // 更新空闲页计数器
    
    // 将块按地址升序插入链表
    if (list_empty(&free_list)) {
        list_add(&free_list, &(base->page_link));
    } else {
        list_entry_t* le = &free_list;
        while ((le = list_next(le)) != &free_list) {
            struct Page* page = le2page(le, page_link);
            if (base < page) {
                list_add_before(le, &(base->page_link));
                break;
            } else if (list_next(le) == &free_list) {
                list_add(le, &(base->page_link));
            }
        }
    }
    
    cprintf("开始尝试合并伙伴块，当前块的地址为: 0x%016lx, 页数为: %d\n", base, base->property);
    
    // 合并逻辑
    int merged;
    do {
        merged = 0;
        
        // 检查前一个块
        list_entry_t* prev_le = list_prev(&(base->page_link));
        if (prev_le != &free_list) {
            struct Page *prev = le2page(prev_le, page_link);
            cprintf("检查前一个块: 地址=0x%016lx, 页数=%d\n", prev, prev->property);
            
            if (prev + prev->property == base && prev->property == base->property) {
                cprintf("✓ 可以与前一个块合并\n");
                cprintf("  前一个块地址: 0x%016lx, 页数: %d\n", prev, prev->property);
                cprintf("  当前块地址: 0x%016lx, 页数: %d\n", base, base->property);
                
                // 合并前一个块
                prev->property <<= 1;          // 前一块大小翻倍
                ClearPageProperty(base);      // 清除当前块的标志
                list_del(&(base->page_link));  // 从链表中移除当前块
                base = prev;                  // 合并后，base 指向前一块
                merged = 1;                   // 标记已合并
                
                cprintf("✓ 合并成功！新块地址: 0x%016lx, 页数: %d\n", base, base->property);
                continue; // 继续尝试合并
            } else {
                cprintf("✗ 不能与前一个块合并: ");
                if (prev + prev->property != base) {
                    cprintf("地址不连续 ");
                }
                if (prev->property != base->property) {
                    cprintf("大小不同 ");
                }
                cprintf("\n");
            }
        } else {
            cprintf("没有前一个块可以检查\n");
        }
        
        // 检查后一个块
        list_entry_t* next_le = list_next(&(base->page_link));
        if (next_le != &free_list) {
            struct Page *next = le2page(next_le, page_link);
            cprintf("检查后一个块: 地址=0x%016lx, 页数=%d\n", next, next->property);
            
            if (base + base->property == next && base->property == next->property) {
                cprintf("✓ 可以与后一个块合并\n");
                cprintf("  当前块地址: 0x%016lx, 页数: %d\n", base, base->property);
                cprintf("  后一个块地址: 0x%016lx, 页数: %d\n", next, next->property);
                
                // 合并后一个块
                base->property <<= 1;
                ClearPageProperty(next);
                list_del(&(next->page_link));
                merged = 1;
                
                cprintf("✓ 合并成功！新块地址: 0x%016lx, 页数: %d\n", base, base->property);
                // 继续循环尝试进一步合并
            } else {
                cprintf("✗ 不能与后一个块合并: ");
                if (base + base->property != next) {
                    cprintf("地址不连续 ");
                }
                if (base->property != next->property) {
                    cprintf("大小不同 ");
                }
                cprintf("\n");
            }
        } else {
            cprintf("没有后一个块可以检查\n");
        }
        
        if(!merged){
            cprintf("本轮没有找到可以合并的伙伴块\n");
        } else {
            cprintf("合并成功，继续尝试合并……\n");
        }
        cprintf("--------------------------------------------\n");
        
    } while (merged && base->property < MAX_INIT_PAGES);
    
    cprintf("最终合并结果: 块地址=0x%016lx, 页数=%d\n\n", base, base->property);
}

static size_t
buddy_system_nr_free_pages(void) {
    return nr_free;
}

static void
buddy_system_check(void) {
    cprintf("测试\n");
    int count = 0, total = 0;
    list_entry_t *le = &free_list;
    // 计算当前空闲块数目和空闲页数目
    while ((le = list_next(le)) != &free_list) {
        struct Page *p = le2page(le, page_link);
        assert(PageProperty(p));
        count ++, total += p->property;
    }
    assert(total == nr_free_pages());
    cprintf("空闲块数目为: %d\n", count);
    cprintf("空闲页数目为: %d\n", nr_free);
    cprintf("--------------------------------------------\n");
    
    cprintf("p0请求6页\n");
    struct Page *p0 = alloc_pages(6);  // 请求 6 页
    int i=1;
    while ((le = list_next(le)) != &free_list) {
        struct Page *p = le2page(le, page_link);
        assert(PageProperty(p));
        cprintf("空闲块%d的虚拟地址为:0x%016lx.\n", i, p);
        cprintf("空闲页数目为: %d\n\n", p->property);
        i+=1;
    }
    cprintf("--------------------------------------------\n");
    
    cprintf("p1请求5页\n");
    struct Page *p1 = alloc_pages(5);  // 请求 5 页
    i=1;
    while ((le = list_next(le)) != &free_list) {
        struct Page *p = le2page(le, page_link);
        assert(PageProperty(p));
        cprintf("空闲块%d的虚拟地址为:0x%016lx.\n", i, p);
        cprintf("空闲页数目为: %d\n\n", p->property);
        i+=1;
    }
    cprintf("--------------------------------------------\n");
    
    cprintf("p2请求8页\n");
    struct Page *p2 = alloc_pages(8);   // 请求 8 页
    i=1;
    while ((le = list_next(le)) != &free_list) {
        struct Page *p = le2page(le, page_link);
        assert(PageProperty(p));
        cprintf("空闲块%d的虚拟地址为:0x%016lx.\n", i, p);
        cprintf("空闲页数目为: %d\n\n", p->property);
        i+=1;
    }
    cprintf("--------------------------------------------\n");
    // 确保分配的页是不同的
    assert(p0 != p1 && p0 != p2 && p1 != p2);
    // 确保分配页的引用计数为 0
    assert(page_ref(p0) == 0 && page_ref(p1) == 0 && page_ref(p2) == 0);
    // 确保分配的页地址在物理内存范围内
    assert(page2pa(p0) < npage * PGSIZE);
    assert(page2pa(p1) < npage * PGSIZE);
    assert(page2pa(p2) < npage * PGSIZE);
    // 释放 p0
    cprintf("释放p0\n");
    free_pages(p0, 6);
    le = &free_list;
    count = 0, total = 0;
    i=1;
    while ((le = list_next(le)) != &free_list) {
        struct Page *p = le2page(le, page_link);
        assert(PageProperty(p));
        count ++, total += p->property;
        cprintf("空闲块%d的虚拟地址为:0x%016lx.\n", i, p);
        cprintf("空闲页数目为: %d\n\n", p->property);
        i+=1;
    }
    cprintf("释放p0后，空闲块数目为: %d\n", count);
    cprintf("释放p0后，空闲页数目为: %d\n\n", total);
    cprintf("--------------------------------------------\n");
   
    // 释放 p2
    cprintf("释放p2\n");
    free_pages(p2, 8);
    le = &free_list;
    count = 0, total = 0;
    i=1;
    while ((le = list_next(le)) != &free_list) {
        struct Page *p = le2page(le, page_link);
        assert(PageProperty(p));
        count ++, total += p->property;
        cprintf("空闲块%d的虚拟地址为:0x%016lx.\n", i, p);
        cprintf("空闲页数目为: %d\n\n", p->property);
        i+=1;
    }
    cprintf("释放p2后，空闲块数目为: %d\n", count);
    cprintf("释放p2后，空闲页数目为: %d\n\n", total);
    
    // 释放 p1
    cprintf("释放p1\n");
    free_pages(p1, 5);
    le = &free_list;
    count = 0, total = 0;
    i=1;
    while ((le = list_next(le)) != &free_list) {
        struct Page *p = le2page(le, page_link);
        assert(PageProperty(p));
        count ++, total += p->property;
        cprintf("空闲块%d的虚拟地址为:0x%016lx.\n", i, p);
        cprintf("空闲页数目为: %d\n\n", p->property);
        i+=1;
    }
    cprintf("释放p1后，空闲块数目为: %d\n", count);
    cprintf("释放p1后，空闲页数目为: %d\n\n", total);
    cprintf("--------------------------------------------\n");
    
    cprintf("p3请求16385页\n");
    struct Page *p3 = alloc_pages(16385);  // 请求 16385 页
    i=1;
    while ((le = list_next(le)) != &free_list) {
        struct Page *p = le2page(le, page_link);
        assert(PageProperty(p));
        cprintf("空闲块%d的虚拟地址为:0x%016lx.\n", i, p);
        cprintf("空闲页数目为: %d\n\n", p->property);
        i+=1;
    }
    
    // 清空空闲页计数，再尝试分配内存块
    unsigned int nr_free_store = nr_free;// 暂存当前的空闲页数目
    cprintf("清空空闲页！\n");
    nr_free = 0;
    
    // p4 请求 25 页
    cprintf("p4请求25页\n");
    struct Page *p4 = alloc_pages(25);
    assert(p4 == NULL);
    cprintf("分配失败，空闲页数目为: %d\n", nr_free);
    nr_free = nr_free_store;// 恢复空闲页数目
    cprintf("测试结束\n");
}

//这个结构体在
const struct pmm_manager buddy_pmm_manager = {
    .name = "buddy_pmm_manager",
    .init = buddy_system_init,
    .init_memmap = buddy_system_init_memmap,
    .alloc_pages = buddy_system_alloc_pages,
    .free_pages = buddy_system_free_pages,
    .nr_free_pages = buddy_system_nr_free_pages,
    .check = buddy_system_check,
};