#include "heap.h"

int size_compare(void *, void *);
int void_compare(void *, void *);

int pool_map_leak(struct pool_map *, struct pool *);

int size_compare(void * x, void * y) {
    size_t l = *((size_t *) x);
    size_t r = *((size_t *) y);
    return l < r ? -1 : l > r ? 1 : 0;
}

int void_compare(void * x, void * y) {
    size_t l = (size_t) x;
    size_t r = (size_t) y;
    return l < r ? -1 : l > r ? 1 : 0;
}

int pool_map_leak(struct pool_map * pool_map, struct pool * pool) {
    int status = 0;

    struct map map;
    struct range range;
    struct pool_buffer * buffer;
    struct pool_node * node;
    struct map_kv kv;

    if(map_create(&map, void_compare, &pool_map->map_pool)) {
        status = panic("failed to create map object");
    } else {
        if(range_create(&range, &pool_map->range_pool)) {
            status = panic("failed to create range object");
        } else {
            buffer = pool->buffer;
            while(buffer && !status) {
                if(range_add(&range, (long) buffer->buffer, (long) buffer->buffer + (pool->size * pool->count) - 1))
                    status = panic("failed to add range object");
                buffer = buffer->next;
            }

            node = pool->root;
            while(node && !status) {
                if(map_insert(&map, node, node))
                    status = panic("failed to insert map object");
                node = node->next;
            }

            kv = map_start(&map);
            while(kv.key && !status) {
                if(range_remove(&range, (long) kv.key, (long) kv.key + pool->size - 1))
                    status = panic("failed to remove range object");
                kv = map_next(&map);
            }

            if(status) {
                /* skip on error */
            } else if(range.root) {
                status = panic("memory leak detected in pool(%zu,%zu)", pool->size, pool->count);
            }

            range_destroy(&range);
        }
        map_destroy(&map);
    }

    return status;
}

int pool_map_create(struct pool_map * pool_map, size_t size) {
    int status = 0;

    if(!size) {
        status = panic("invalid size");
    } else {
        pool_map->size = size;
        if(pool_create(&pool_map->object_pool, sizeof(struct pool), pool_map->size / sizeof(struct pool))) {
            status = panic("failed to create pool object");
        } else {
            if(pool_create(&pool_map->map_pool, sizeof(struct map_node), pool_map->size / sizeof(struct map_node))) {
                status = panic("failed to create pool object");
            } else {
                if(pool_create(&pool_map->range_pool, sizeof(struct range_node), pool_map->size / sizeof(struct range_node))) {
                    status = panic("failed to create pool object");
                } else {
                    if(map_create(&pool_map->map, size_compare, &pool_map->map_pool))
                        status = panic("failed to create map object");
                    if(status)
                        pool_destroy(&pool_map->range_pool);
                }
                if(status)
                    pool_destroy(&pool_map->map_pool);
            }
            if(status)
                pool_destroy(&pool_map->object_pool);
        }
    }

    return status;
}

void pool_map_destroy(struct pool_map * pool_map) {
    struct map_kv kv;

    kv = map_start(&pool_map->map);
    while(kv.value) {
        pool_map_leak(pool_map, kv.value);
        pool_destroy(kv.value);
        pool_put(&pool_map->object_pool, kv.value);
        kv = map_next(&pool_map->map);
    }

    map_destroy(&pool_map->map);
    pool_destroy(&pool_map->range_pool);
    pool_destroy(&pool_map->map_pool);
    pool_destroy(&pool_map->object_pool);
}

struct pool * pool_map_get(struct pool_map * pool_map, size_t size) {
    int status = 0;
    struct pool * pool;

    pool = map_search(&pool_map->map, &size);
    if(!pool) {
        pool = pool_get(&pool_map->object_pool);
        if(!pool) {
            status = panic("out of memory");
        } else {
            if(pool_create(pool, size, pool_map->size / size)) {
                status = panic("failed to create pool object");
            } else {
                if(map_insert(&pool_map->map, &pool->size, pool))
                    status = panic("failed to insert map object");
                if(status)
                    pool_destroy(pool);
            }
            if(status)
                pool_put(&pool_map->object_pool, pool);
        }
    }

    return status ? NULL : pool;
}

int heap_create(struct heap * heap, size_t size) {
    int status = 0;

    if(pool_map_create(&heap->pool_map, size)) {
        status = panic("failed to create pool map object");
    } else {
        heap->stack_pool = heap_pool(heap, size);
        if(!heap->stack_pool) {
            status = panic("failed to pool heap object");
        } else {
            heap->map_pool = heap_pool(heap, sizeof(struct map_node));
            if(!heap->map_pool) {
                status = panic("failed to pool heap object");
            } else {
                heap->range_pool = heap_pool(heap, sizeof(struct range_node));
                if(!heap->range_pool) {
                    status = panic("failed to pool heap object");
                } else {
                    heap->logic_pool = heap_pool(heap, sizeof(struct logic_node));
                    if(!heap->logic_pool)
                        status = panic("failed to pool heap object");
                }
            }
        }
        if(status)
            pool_map_destroy(&heap->pool_map);
    }

    return status;
}

void heap_destroy(struct heap * heap) {
    pool_map_destroy(&heap->pool_map);
}

struct pool * heap_pool(struct heap * heap, size_t size) {
    return pool_map_get(&heap->pool_map, size);
}
