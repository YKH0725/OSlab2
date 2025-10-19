# budddy_system设计文档


## 1.原理概述
本设计实现了一个基于伙伴系统（Buddy System）的内存管理模块，用于操作系统的物理内存管理。伙伴系统通过将内存划分为2的幂次方大小的块，实现了高效的内存分配和释放，同时有效减少了内存碎片。

## 2.数据结构
### 空闲区域
```c
free_area_t free_area;  // 全局空闲区域管理结构
#define free_list (free_area.free_list)  // 空闲块链表
#define nr_free (free_area.nr_free)      // 空闲页总数
```
- free_area：管理空闲内存块的结构体

- free_list：空闲块链表头

- nr_free：当前空闲页总数

## 3.核心功能实现

### 3.1初始化函数

#### 3.1.1 buddy_system_init()
```c
static void buddy_system_init(void) {
    list_init(&free_list);  // 初始化空闲链表
    nr_free = 0;            // 空闲页数初始化为 0
}
```
- 功能：初始化伙伴系统
- 操作：
-- 初始化空闲链表
-- 将空闲页计数器清零

#### 3.1.2 buddy_system_init_memmap(struct Page *base, size_t n)
```c
static void buddy_system_init_memmap(struct Page *base, size_t n) {
    assert(n > 0);
    n = MAX_INIT_PAGES;  // 限制最大分配页数
    
    // 初始化每个页面的元信息
    struct Page *p = base;
    for (; p != base + n; p++) {
        assert(PageReserved(p));  // 确保页面是保留的
        p->flags = p->property = 0;
        set_page_ref(p, 0);       // 引用计数清零
    }
    
    base->property = n;           // 标记为大小为 n 的空闲块
    SetPageProperty(base);        // 设置 PG_property 标志
    nr_free += n;                 // 更新空闲页计数
    
    // 将空闲块按地址升序插入链表
    if (list_empty(&free_list)) {
        list_add(&free_list, &(base->page_link));
    } else {
        // 遍历链表，找到合适的位置插入
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
```
- 功能：初始化内存映射
- 参数：
-- base：起始页指针
-- n：页数
- 操作：
-- 将所有页标记为未使用
-- 设置起始页的property为总页数
-- 将空闲块加入链表
-- 更新空闲页计数器

### 3.2内存分配与释放

#### 3.2.1 buddy_system_alloc_pages(size_t n)
```c
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
```
- 功能：分配n页内存
- 流程：
-- 1.将请求大小向上取整为2的幂次方
-- 2.查找足够大的空闲块
-- 3.若找到，则分裂块直到大小匹配
-- 4.更新空闲页计数器
-- 5.返回分配的内存页指针

#### 3.2.2 buddy_system_free_pages(struct Page *base, size_t n)
```c
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
        
    } while (merged && base->property < MAX_INIT_PAGES);
    
    cprintf("最终合并结果: 块地址=0x%016lx, 页数=%d\n\n", base, base->property);
}
```
- 功能：释放n页内存
- 流程：
-- 1.将释放大小向上取整为2的幂次方
-- 2.标记释放的页为未使用
-- 3.将释放的块加入空闲链表
-- 4.尝试与相邻块合并
-- 5.更新空闲页计数器

### 3.3辅助函数

#### roundup2(size_t n)
```c
static size_t roundup2(size_t n) {
    size_t size = 1;
    while (size < n) {
        size <<= 1;
    }
    return size;
}
```
- 功能：将大小向上取整为2的幂次方
- 示例：7→8，14→16，21→32

## 4.测试
设置了5个测试样例，来完成对空闲块的分配，合并是否有误以及空闲块边界的检查。测试函数代码如下。
```c
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
    struct Page *p1 = alloc_pages(5);  // 请求 15 页
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
    struct Page *p2 = alloc_pages(8);   // 请求 28 页
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
    struct Page *p3 = alloc_pages(16385);  // 请求 6 页
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
```
首先初始化空闲块数量为1，页数目为16384（2^14），然后开始进行测试。

依次分配页数为6的p0，页数为5的p1，页数为8的p2，然后打乱顺序，以p0,p2,p1的顺序进行释放，这样可以准确地检测合并空闲块的逻辑是否正确。然后分配页数为16384的p3，因为超出最大空闲块的页数，因此会分配失败，最后清空空闲块后再分配一个p4，检查空闲块的逻辑是否正确。完成的测试结果如下。
```c
测试
空闲块数目为: 1
空闲页数目为: 16384
--------------------------------------------
p0请求6页
找到第一个满足要求的空闲块 p
准备用于分配的空闲块 p 的地址为: 0xffffffffc020f318.
准备分配的空闲块 p 的页数为: 16384
开始分裂空闲块……

空闲块1的虚拟地址为:0xffffffffc020f458.
空闲页数目为: 8

空闲块2的虚拟地址为:0xffffffffc020f598.
空闲页数目为: 16

空闲块3的虚拟地址为:0xffffffffc020f818.
空闲页数目为: 32

空闲块4的虚拟地址为:0xffffffffc020fd18.
空闲页数目为: 64

空闲块5的虚拟地址为:0xffffffffc0210718.
空闲页数目为: 128

空闲块6的虚拟地址为:0xffffffffc0211b18.
空闲页数目为: 256

空闲块7的虚拟地址为:0xffffffffc0214318.
空闲页数目为: 512

空闲块8的虚拟地址为:0xffffffffc0219318.
空闲页数目为: 1024

空闲块9的虚拟地址为:0xffffffffc0223318.
空闲页数目为: 2048

空闲块10的虚拟地址为:0xffffffffc0237318.
空闲页数目为: 4096

空闲块11的虚拟地址为:0xffffffffc025f318.
空闲页数目为: 8192

--------------------------------------------
p1请求5页
找到第一个满足要求的空闲块 p
准备用于分配的空闲块 p 的地址为: 0xffffffffc020f458.
准备分配的空闲块 p 的页数为: 8
开始分裂空闲块……

空闲块1的虚拟地址为:0xffffffffc020f598.
空闲页数目为: 16

空闲块2的虚拟地址为:0xffffffffc020f818.
空闲页数目为: 32

空闲块3的虚拟地址为:0xffffffffc020fd18.
空闲页数目为: 64

空闲块4的虚拟地址为:0xffffffffc0210718.
空闲页数目为: 128

空闲块5的虚拟地址为:0xffffffffc0211b18.
空闲页数目为: 256

空闲块6的虚拟地址为:0xffffffffc0214318.
空闲页数目为: 512

空闲块7的虚拟地址为:0xffffffffc0219318.
空闲页数目为: 1024

空闲块8的虚拟地址为:0xffffffffc0223318.
空闲页数目为: 2048

空闲块9的虚拟地址为:0xffffffffc0237318.
空闲页数目为: 4096

空闲块10的虚拟地址为:0xffffffffc025f318.
空闲页数目为: 8192

--------------------------------------------
p2请求8页
找到第一个满足要求的空闲块 p
准备用于分配的空闲块 p 的地址为: 0xffffffffc020f598.
准备分配的空闲块 p 的页数为: 16
开始分裂空闲块……

空闲块1的虚拟地址为:0xffffffffc020f6d8.
空闲页数目为: 8

空闲块2的虚拟地址为:0xffffffffc020f818.
空闲页数目为: 32

空闲块3的虚拟地址为:0xffffffffc020fd18.
空闲页数目为: 64

空闲块4的虚拟地址为:0xffffffffc0210718.
空闲页数目为: 128

空闲块5的虚拟地址为:0xffffffffc0211b18.
空闲页数目为: 256

空闲块6的虚拟地址为:0xffffffffc0214318.
空闲页数目为: 512

空闲块7的虚拟地址为:0xffffffffc0219318.
空闲页数目为: 1024

空闲块8的虚拟地址为:0xffffffffc0223318.
空闲页数目为: 2048

空闲块9的虚拟地址为:0xffffffffc0237318.
空闲页数目为: 4096

空闲块10的虚拟地址为:0xffffffffc025f318.
空闲页数目为: 8192

--------------------------------------------
释放p0
开始尝试合并，当前块的地址为: 0xffffffffc020f318, 页数为: 8
没有前一个块可以检查
检查后一个块: 地址=0xffffffffc020f6d8, 页数=8
✗ 不能与后一个块合并: 地址不连续 
本轮没有找到可以合并的伙伴块
--------------------------------------------
最终合并结果: 块地址=0xffffffffc020f318, 页数=8

空闲块1的虚拟地址为:0xffffffffc020f318.
空闲页数目为: 8

空闲块2的虚拟地址为:0xffffffffc020f6d8.
空闲页数目为: 8

空闲块3的虚拟地址为:0xffffffffc020f818.
空闲页数目为: 32

空闲块4的虚拟地址为:0xffffffffc020fd18.
空闲页数目为: 64

空闲块5的虚拟地址为:0xffffffffc0210718.
空闲页数目为: 128

空闲块6的虚拟地址为:0xffffffffc0211b18.
空闲页数目为: 256

空闲块7的虚拟地址为:0xffffffffc0214318.
空闲页数目为: 512

空闲块8的虚拟地址为:0xffffffffc0219318.
空闲页数目为: 1024

空闲块9的虚拟地址为:0xffffffffc0223318.
空闲页数目为: 2048

空闲块10的虚拟地址为:0xffffffffc0237318.
空闲页数目为: 4096

空闲块11的虚拟地址为:0xffffffffc025f318.
空闲页数目为: 8192

释放p0后，空闲块数目为: 11
释放p0后，空闲页数目为: 16368

--------------------------------------------
释放p2
开始尝试合并，当前块的地址为: 0xffffffffc020f598, 页数为: 8
检查前一个块: 地址=0xffffffffc020f318, 页数=8
✗ 不能与前一个块合并: 地址不连续 
检查后一个块: 地址=0xffffffffc020f6d8, 页数=8
✓ 可以与后一个块合并
  当前块地址: 0xffffffffc020f598, 页数: 8
  后一个块地址: 0xffffffffc020f6d8, 页数: 8
✓ 合并成功！新块地址: 0xffffffffc020f598, 页数: 16
合并成功，继续尝试合并……
--------------------------------------------
检查前一个块: 地址=0xffffffffc020f318, 页数=8
✗ 不能与前一个块合并: 地址不连续 大小不同 
检查后一个块: 地址=0xffffffffc020f818, 页数=32
✗ 不能与后一个块合并: 大小不同 
本轮没有找到可以合并的伙伴块
--------------------------------------------
最终合并结果: 块地址=0xffffffffc020f598, 页数=16

空闲块1的虚拟地址为:0xffffffffc020f318.
空闲页数目为: 8

空闲块2的虚拟地址为:0xffffffffc020f598.
空闲页数目为: 16

空闲块3的虚拟地址为:0xffffffffc020f818.
空闲页数目为: 32

空闲块4的虚拟地址为:0xffffffffc020fd18.
空闲页数目为: 64

空闲块5的虚拟地址为:0xffffffffc0210718.
空闲页数目为: 128

空闲块6的虚拟地址为:0xffffffffc0211b18.
空闲页数目为: 256

空闲块7的虚拟地址为:0xffffffffc0214318.
空闲页数目为: 512

空闲块8的虚拟地址为:0xffffffffc0219318.
空闲页数目为: 1024

空闲块9的虚拟地址为:0xffffffffc0223318.
空闲页数目为: 2048

空闲块10的虚拟地址为:0xffffffffc0237318.
空闲页数目为: 4096

空闲块11的虚拟地址为:0xffffffffc025f318.
空闲页数目为: 8192

释放p2后，空闲块数目为: 11
释放p2后，空闲页数目为: 16376

释放p1
开始尝试合并，当前块的地址为: 0xffffffffc020f458, 页数为: 8
检查前一个块: 地址=0xffffffffc020f318, 页数=8
✓ 可以与前一个块合并
  前一个块地址: 0xffffffffc020f318, 页数: 8
  当前块地址: 0xffffffffc020f458, 页数: 8
✓ 合并成功！新块地址: 0xffffffffc020f318, 页数: 16
没有前一个块可以检查
检查后一个块: 地址=0xffffffffc020f598, 页数=16
✓ 可以与后一个块合并
  当前块地址: 0xffffffffc020f318, 页数: 16
  后一个块地址: 0xffffffffc020f598, 页数: 16
✓ 合并成功！新块地址: 0xffffffffc020f318, 页数: 32
合并成功，继续尝试合并……
--------------------------------------------
没有前一个块可以检查
检查后一个块: 地址=0xffffffffc020f818, 页数=32
✓ 可以与后一个块合并
  当前块地址: 0xffffffffc020f318, 页数: 32
  后一个块地址: 0xffffffffc020f818, 页数: 32
✓ 合并成功！新块地址: 0xffffffffc020f318, 页数: 64
合并成功，继续尝试合并……
--------------------------------------------
没有前一个块可以检查
检查后一个块: 地址=0xffffffffc020fd18, 页数=64
✓ 可以与后一个块合并
  当前块地址: 0xffffffffc020f318, 页数: 64
  后一个块地址: 0xffffffffc020fd18, 页数: 64
✓ 合并成功！新块地址: 0xffffffffc020f318, 页数: 128
合并成功，继续尝试合并……
--------------------------------------------
没有前一个块可以检查
检查后一个块: 地址=0xffffffffc0210718, 页数=128
✓ 可以与后一个块合并
  当前块地址: 0xffffffffc020f318, 页数: 128
  后一个块地址: 0xffffffffc0210718, 页数: 128
✓ 合并成功！新块地址: 0xffffffffc020f318, 页数: 256
合并成功，继续尝试合并……
--------------------------------------------
没有前一个块可以检查
检查后一个块: 地址=0xffffffffc0211b18, 页数=256
✓ 可以与后一个块合并
  当前块地址: 0xffffffffc020f318, 页数: 256
  后一个块地址: 0xffffffffc0211b18, 页数: 256
✓ 合并成功！新块地址: 0xffffffffc020f318, 页数: 512
合并成功，继续尝试合并……
--------------------------------------------
没有前一个块可以检查
检查后一个块: 地址=0xffffffffc0214318, 页数=512
✓ 可以与后一个块合并
  当前块地址: 0xffffffffc020f318, 页数: 512
  后一个块地址: 0xffffffffc0214318, 页数: 512
✓ 合并成功！新块地址: 0xffffffffc020f318, 页数: 1024
合并成功，继续尝试合并……
--------------------------------------------
没有前一个块可以检查
检查后一个块: 地址=0xffffffffc0219318, 页数=1024
✓ 可以与后一个块合并
  当前块地址: 0xffffffffc020f318, 页数: 1024
  后一个块地址: 0xffffffffc0219318, 页数: 1024
✓ 合并成功！新块地址: 0xffffffffc020f318, 页数: 2048
合并成功，继续尝试合并……
--------------------------------------------
没有前一个块可以检查
检查后一个块: 地址=0xffffffffc0223318, 页数=2048
✓ 可以与后一个块合并
  当前块地址: 0xffffffffc020f318, 页数: 2048
  后一个块地址: 0xffffffffc0223318, 页数: 2048
✓ 合并成功！新块地址: 0xffffffffc020f318, 页数: 4096
合并成功，继续尝试合并……
--------------------------------------------
没有前一个块可以检查
检查后一个块: 地址=0xffffffffc0237318, 页数=4096
✓ 可以与后一个块合并
  当前块地址: 0xffffffffc020f318, 页数: 4096
  后一个块地址: 0xffffffffc0237318, 页数: 4096
✓ 合并成功！新块地址: 0xffffffffc020f318, 页数: 8192
合并成功，继续尝试合并……
--------------------------------------------
没有前一个块可以检查
检查后一个块: 地址=0xffffffffc025f318, 页数=8192
✓ 可以与后一个块合并
  当前块地址: 0xffffffffc020f318, 页数: 8192
  后一个块地址: 0xffffffffc025f318, 页数: 8192
✓ 合并成功！新块地址: 0xffffffffc020f318, 页数: 16384
合并成功，继续尝试合并……
--------------------------------------------
最终合并结果: 块地址=0xffffffffc020f318, 页数=16384

空闲块1的虚拟地址为:0xffffffffc020f318.
空闲页数目为: 16384

释放p1后，空闲块数目为: 1
释放p1后，空闲页数目为: 16384

--------------------------------------------
p3请求16385页
空闲块1的虚拟地址为:0xffffffffc020f318.
空闲页数目为: 16384

清空空闲页！
p4请求25页
分配失败，空闲页数目为: 0
测试1结束
check_alloc_page() succeeded!
```
这样，我们就完成了对buddy system的测试。结果正确，实验成功。














