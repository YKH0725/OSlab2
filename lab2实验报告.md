# lab2

## 练习一 理解first-fit 连续物理内存分配算法  //2213633翟玉坤

## 练习一要求 first-fit 连续物理内存分配算法作为物理内存分配一个很基础的方法，需要同学们理解它的实现过程。请大家仔细阅读实验手册的教程并结合kern/mm/default_pmm.c中的相关代码，认真分析default_init，default_init_memmap，default_alloc_pages， default_free_pages等相关函数，并描述程序在进行物理内存分配的过程以及各个函数的作用。 请在实验报告中简要说明你的设计实现过程。请回答如下问题：你的first fit算法是否有进一步的改进空间？

### first-fit 及代码分析

first-fit是一种用于在操作系统中进行内存管理的算法，其主要通过在内存链表中进行遍历以寻找可用的空闲内存，并将其分配。
它通过对操作系统中存在的一个由内存块组成的链表，它们会从低到高连接于一起，本算法通过对这个链表从头开始进行对空闲可分配内存寻找，当出现了一个符合要求的内存块时，算法会将其进行分配操作，若未能找到符合要求的，则返回null或报错。

### 源代码函数解析 

1. static void
default_init(void) { //主要进行一些必要的初始化操作，例如对于内存链表的初始化
    list_init(&free_list);  //初始化内存链表，将其初始化为一个空的双向链表
    nr_free = 0;  //初始化没有可用的空闲内存页
}

2. static void
default_init_memmap(struct Page *base, size_t n) //此函数主要用于对物理内存页进行转换操作，将其初始化为可进行后续分配的操作的空闲内存块

3. static struct Page *
default_alloc_pages(size_t n) // 此部分第一部分定义了page结构体，page结构体用于表示原始的物理内存页，并且对于系统对物理内存的管理至关重要，可以实现对于物理内存使用状态的跟踪，虚拟内存和物理内存的联系等。
                              // 此部分第二部分函数时first-fist算法的核心部分，用于可用内存块的搜索操作及其后续的分配实现。
list_entry_t *le = &free_list;
    while ((le = list_next(le)) != &free_list) {
        struct Page *p = le2page(le, page_link);
        if (p->property >= n) {
            page = p;
            break;
        }
    }                         // 此部分用于对是否存在可以满足需要的空闲内存块的搜索操作

    if (page != NULL) {
        list_entry_t* prev = list_prev(&(page->page_link));
        list_del(&(page->page_link));
        if (page->property > n) {
            struct Page *p = page + n;
            p->property = page->property - n;
            SetPageProperty(p);
            list_add(prev, &(p->page_link));
        }
        nr_free -= n;
        ClearPageProperty(page);
    }                      // 此部分旨在在找到有满足需求的内存块后对其进行进一步的分配操作。

4. default_free_pages(struct Page *base, size_t n) //此部分是first-fit算法的内存释放模块，将已经被算法分配的内存重新转为空闲内存并将其重新合并回内存链表。

### 问题回答：first-fit算法改进

- 可以在遍历起始点设置进行改进，在原算法中，每次遍历都要从链表最头部开始，但可以通过让程序记录每次内存分配完成的位置，在下一次遍历时会从这个记录点开始，这可以减少对于内存链表头部的竞争压力。

## 练习2：实现 Best-Fit 连续物理内存分配算法（需要编程）

在完成练习一后，参考kern/mm/default_pmm.c对First Fit算法的实现，编程实现Best Fit页面分配算法，算法的时空复杂度不做要求，能通过测试即可。
请在实验报告中简要说明你的设计实现过程，阐述代码是如何对物理内存进行分配和释放，并回答如下问题：
- 你的 Best-Fit 算法是否有进一步的改进空间？

### 算法原理

Best-Fit的基本思想是找一块能满足进程需求、但又尽可能小的空闲分区进行分配。一个典型的设计过程如下：
1. 遍历所有空闲内存块（空闲分区）；
2. 找出所有能够容纳该进程的空闲块（即空闲块大小 ≥ 进程所需内存大小）；
3. 从这些空闲块中选出“最小的那个”，即最接近进程需求大小的分区；
4. 把该分区分配给进程，如果这块内存大于进程需求，还会把剩余部分留作新的空闲分区。

### 算法设计

Best-Fit分配算法和First-Fit算法相比，唯一的区别在于分配内存时选择空闲块的方法不同。我们需要依次实现
- best_fit_init_memmap
- best_fit_alloc_pages
- best_fit_free_pages

首先是best_fit_init_memmap的实现，它主要负责初始化一段连续的物理页（Page）管理信息，供后续分配使用。它实现从 base 开始的连续 n 页做初始化，然后把它们的标志位清空、引用计数设为 0，把这段内存标记为“空闲页块”，并把它插入到空闲链表 free_list 中，成为后续分配的候选区域。其中插入空闲链表的原则是按照按物理地址升序的方式进行。

```cpp
static void
best_fit_init_memmap(struct Page *base, size_t n) {
    assert(n > 0);
    struct Page *p = base;
    for (; p != base + n; p ++) {
        assert(PageReserved(p));
        // 清空当前页框的标志和属性信息，并将页框的引用计数设置为0
        p->flags = 0;
        p->property = 0;
        set_page_ref(p,0);
    }
    base->property = n;
    SetPageProperty(base);
    nr_free += n;
    if (list_empty(&free_list)) {
        list_add(&free_list, &(base->page_link));
    } else {
        list_entry_t* le = &free_list;
        while ((le = list_next(le)) != &free_list) {
            struct Page* page = le2page(le, page_link);
    // 当base < page时，找到第一个大于base的页，将base插入到它前面，并退出循环
    // 当list_next(le) == &free_list时，若已经到达链表结尾，将base插入到链表尾部
            if(base<page){
                list_add_before(le,&(base->page_link));
                break;
            }else if(list_next(le) == &free_list){
                list_add(le,&(base->page_link));
                break;
            }
        }
    }
}
```

下面是best_fit_alloc_pages函数的实现，主要包括两个部分，即空闲块的分配和找到对应块后剩余部分的处理。首先，遍历寻找最小满足块。实现的基本思想是通过le遍历free_list中的每个空闲块，通过 p->property 读取每个空闲块的大小，维护 min_size（初值为 nr_free+1）。如果发现 p->property >= n 且比当前 min_size 更小，就更新 min_size 并把 page 指向该块。遍历结束后 page 指向“最接近请求大小的块”（best fit）。然后，将该空闲块从对应的链表中取出，若这块内存大于实际需求，还会将剩余部分留作新的空闲分区放回链表。

```cpp
static struct Page *
best_fit_alloc_pages(size_t n) { 
    assert(n > 0);
    if (n > nr_free) {
        return NULL;
    }
    struct Page *page = NULL;
    list_entry_t *le = &free_list;
    size_t min_size = nr_free + 1;

    // 遍历空闲链表，查找满足需求的空闲页框
    while ((le = list_next(le)) != &free_list) {
        struct Page *p = le2page(le, page_link);
        if (p->property >= n && p->property < min_size ) {
            min_size = p->property;
            page = p;    
        }
    }

    if (page != NULL) {
        list_entry_t* prev = list_prev(&(page->page_link));
        list_del(&(page->page_link));
        if (page->property > n) {
            struct Page *p = page + n;
            p->property = page->property - n;
            SetPageProperty(p);
            list_add(prev, &(p->page_link));
        }
        nr_free -= n;
        ClearPageProperty(page);
    }
    return page;
}
```

下面是内存的释放。先对待释放的每个页清除标志并把引用计数置0，接着把释放区的首页设为空闲块（base->property = n）并标记、累加 nr_free，然后按物理地址有序将该块插入 free_list，最后尝试与前一个和后一个相邻空闲块合并——若前块尾紧邻当前块则把当前块并入前块并删除当前节点、将 base 指向合并后的块；再检查并合并后块，合并时更新 property、清除被并入块的 PG_property 并从链表删除，保证链表按地址顺序且 nr_free 与各块的property 保持一致。

```cpp
static void
best_fit_free_pages(struct Page *base, size_t n) {
    assert(n > 0);
    struct Page *p = base;
    for (; p != base + n; p ++) {
        assert(!PageReserved(p) && !PageProperty(p));
        p->flags = 0;
        set_page_ref(p, 0);
    }
    base->property = n;// 设置当前页块的属性为释放的页块数
    SetPageProperty(base);//将当前页块标记为已分配状态
    nr_free += n;//增加nr_free的值
    
    ......

    list_entry_t* le = list_prev(&(base->page_link));
    if (le != &free_list) {
        p = le2page(le, page_link);
        //判断连续性
        if (p + p->property == base) {
            p->property += base->property;
            ClearPageProperty(base);
            list_del(&(base->page_link));
            base = p;
        }
    }
    ......
}
```

### 测试结果
使用make qemu进行编译，然后使用make grade进行测试，测试结果如下：

```cpp
pandarbox@LAPTOP-7TBS3708:/mnt/d/大三上/操作系统/实验/labcode/lab2$ make grade
>>>>>>>>>> here_make>>>>>>>>>>>
gmake[1]: Entering directory '/mnt/d/大三上/操作系统/实验/labcode/lab2' + cc kern/init/entry.S + cc kern/init/init.c + cc kern/libs/stdio.c + cc kern/debug/panic.c + cc kern/driver/console.c + cc kern/driver/dtb.c + cc kern/mm/best_fit_pmm.c + cc kern/mm/default_pmm.c + cc kern/mm/pmm.c + cc libs/printfmt.c + cc libs/readline.c + cc libs/sbi.c + cc libs/string.c + ld bin/kernel riscv64-unknown-elf-objcopy bin/kernel --strip-all -O binary bin/ucore.img gmake[1]: Leaving directory '/mnt/d/大三上/操作系统/实验/labcode/lab2'
>>>>>>>>>> here_make>>>>>>>>>>>
<<<<<<<<<<<<<<< here_run_qemu <<<<<<<<<<<<<<<<<<
try to run qemu
qemu pid=23709
<<<<<<<<<<<<<<< here_run_check <<<<<<<<<<<<<<<<<<
  -check physical_memory_map_information:    OK
  -check_best_fit:                           OK
Total Score: 25/25

```

结果表明我们实现的best-fit算法正确。

### 进一步改进的空间

1. 在原始实现中，系统每次分配内存都要线性遍历整个空闲链表来寻找“最小的可用块”，时间复杂度是 O(k)（k 为空闲块数量）。当系统运行一段时间、空闲块很多时，这种线性查找会成为明显的性能瓶颈。通过建立“按块大小排序”的辅助索引结构（如红黑树），让查找最小可用块的时间复杂度降低到 O(log k) 或接近 O(1)。这样就能快速找到“最接近需求的空闲块”，而不必每次都遍历整个链表。

2. 为减少内碎片，可以引入最小分割阈值：只有当分裂后剩余部分大于等于阈值时才把空闲块拆分，否则把整块分配，避免产生很多小碎片；对小请求使用固定大小类或者快速桶分配，将小块统一归类以减少碎片和查找开销。代价是可能牺牲部分最佳匹配率并需维护额外的数据结构，但通常能显著降低碎片率并提高分配效率。

## challenge:buddy_system伙伴系统

见设计文档。

## 重要的知识点

### 1. 物理内存管理（Physical Memory Management）
实验中的 struct Page对应物理页帧的元数据管理（如引用计数、标志位、空闲块大小），标志位用于区分保留页和空闲页，原理中类似概念是页表的保护位（如 PTE_R、PTE_W）。

### ​​2. 地址转换（Address Translation）
2.1​ ​entry.S中的页表设置​，实验通过汇编代码初始化三级页表（Sv39），原理中描述多级页表的作用（如减少页表大小，提高效率）。实验需处理物理地址计算（如虚实偏移量），原理不涉及具体架构。

2.2 satp寄存器配置​。实验通过 satp启用分页，原理中讨论MMU如何利用页表转换地址。

2.3 sfence.vma 。实验通过该指令刷新TLB，原理中讨论TLB刷新时机。

### 3.设备树（Device Tree, DTB）
​​dtb_init函数。实验解析DTB获取硬件信息（如内存范围），原理中讨论硬件探测的通用方法。
