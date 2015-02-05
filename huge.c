#include <assert.h>

#include "chunk.h"
#include "huge.h"
#include "memory.h"
#include "mutex.h"

static extent_tree huge;
static mutex huge_mutex = MUTEX_INITIALIZER;

void huge_init(void) {
    extent_tree_ad_new(&huge);
}

void *huge_alloc(size_t size, size_t alignment) {
    size_t real_size = CHUNK_CEILING(size);
    struct extent_node *node = node_alloc();
    if (!node) {
        return NULL;
    }
    node->size = real_size;
    node->addr = chunk_alloc(NULL, real_size, alignment);
    if (!node->addr) {
        node_free(node);
        return NULL;
    }

    mutex_lock(&huge_mutex);
    extent_tree_ad_insert(&huge, node);
    mutex_unlock(&huge_mutex);

    return node->addr;
}

static void huge_no_move_shrink(void *ptr, size_t old_size, size_t new_size) {
    struct extent_node key;
    key.addr = ptr;

    mutex_lock(&huge_mutex);
    struct extent_node *node = extent_tree_ad_search(&huge, &key);
    assert(node);
    node->size = new_size;
    mutex_unlock(&huge_mutex);

    void *excess_addr = (char *)node->addr + new_size;
    size_t excess_size = old_size - new_size;

    memory_decommit(excess_addr, excess_size);
    chunk_free(excess_addr, excess_size);
}

static bool huge_no_move_expand(void *ptr, size_t old_size, size_t new_size) {
    struct extent_node key;
    key.addr = ptr;

    mutex_lock(&huge_mutex);
    struct extent_node *node = extent_tree_ad_search(&huge, &key);
    assert(node);
    mutex_unlock(&huge_mutex);

    void *expand_addr = (char *)ptr + old_size;
    size_t expand_size = new_size - old_size;

    void *trail = chunk_alloc(expand_addr, expand_size, CHUNK_SIZE);
    if (!trail) {
        return true;
    }
    assert(trail == expand_addr);

    mutex_lock(&huge_mutex);
    node->size = new_size;
    mutex_unlock(&huge_mutex);

    return false;
}

static void *huge_remap_expand(void *old_addr, size_t old_size, size_t new_size) {
    void *new_addr = chunk_alloc(NULL, new_size, CHUNK_SIZE);
    if (!new_addr) {
        return NULL;
    }

    if (memory_remap_fixed(old_addr, old_size, new_addr, new_size)) {
        return NULL;
    }

    // Attempt to fill the virtual memory hole created by mremap. The kernel should provide a flag
    // for preserving the old mapping to avoid the possibility of failing to map the right address.
    //
    // https://lkml.org/lkml/2014/10/2/624
    void *extra = memory_reserve(old_addr, old_size);
    if (extra) {
        if (ALIGNMENT_ADDR2OFFSET(extra, CHUNK_SIZE)) {
            memory_unmap(extra, old_size);
        } else {
            chunk_free(extra, old_size);
        }
    }

    struct extent_node key;
    key.addr = old_addr;

    mutex_lock(&huge_mutex);
    struct extent_node *node = extent_tree_ad_search(&huge, &key);
    assert(node);
    extent_tree_ad_remove(&huge, node);
    node->addr = new_addr;
    node->size = new_size;
    extent_tree_ad_insert(&huge, node);
    mutex_unlock(&huge_mutex);

    return new_addr;
}

void *huge_realloc(void *ptr, size_t old_size, size_t new_real_size) {
    if (new_real_size > old_size) {
        if (!huge_no_move_expand(ptr, old_size, new_real_size)) {
            return ptr;
        }
        return huge_remap_expand(ptr, old_size, new_real_size);
    } else if (new_real_size < old_size) {
        huge_no_move_shrink(ptr, old_size, new_real_size);
    }
    return ptr;
}

void huge_free(void *ptr) {
    struct extent_node *node, key;
    key.addr = ptr;

    mutex_lock(&huge_mutex);
    node = extent_tree_ad_search(&huge, &key);
    assert(node);
    extent_tree_ad_remove(&huge, node);
    mutex_unlock(&huge_mutex);

    memory_decommit(ptr, node->size);
    chunk_free(ptr, node->size);
    node_free(node);
}

size_t huge_alloc_size(void *ptr) {
    struct extent_node key;
    key.addr = ptr;

    mutex_lock(&huge_mutex);
    struct extent_node *node = extent_tree_ad_search(&huge, &key);
    assert(node);
    size_t size = node->size;
    mutex_unlock(&huge_mutex);

    return size;
}
