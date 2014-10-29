#define	JEMALLOC_HUGE_C_
#include "jemalloc/internal/jemalloc_internal.h"

/******************************************************************************/
/* Data. */

/* Protects chunk-related data structures. */
static malloc_mutex_t	huge_mtx;

/******************************************************************************/

/* Tree of chunks that are stand-alone huge allocations. */
static extent_tree_t	huge;

void *
huge_malloc(arena_t *arena, size_t size, bool zero)
{

	return (huge_palloc(arena, size, chunksize, zero));
}

void *
huge_palloc(arena_t *arena, size_t size, size_t alignment, bool zero)
{
	void *ret;
	size_t csize;
	extent_node_t *node;
	bool is_zeroed;

	/* Allocate one or more contiguous chunks for this request. */

	csize = CHUNK_CEILING(size);
	if (csize == 0) {
		/* size is large enough to cause size_t wrap-around. */
		return (NULL);
	}

	/* Allocate an extent node with which to track the chunk. */
	node = base_node_alloc();
	if (node == NULL)
		return (NULL);

	/*
	 * Copy zero into is_zeroed and pass the copy to chunk_alloc(), so that
	 * it is possible to make correct junk/zero fill decisions below.
	 */
	is_zeroed = zero;
	arena = choose_arena(arena);
	ret = arena_chunk_alloc_huge(arena, csize, alignment, &is_zeroed);
	if (ret == NULL) {
		base_node_dalloc(node);
		return (NULL);
	}

	/* Insert node into huge. */
	node->addr = ret;
	node->size = csize;
	node->arena = arena;

	malloc_mutex_lock(&huge_mtx);
	extent_tree_ad_insert(&huge, node);
	malloc_mutex_unlock(&huge_mtx);

	if (config_fill && zero == false) {
		if (unlikely(opt_junk))
			memset(ret, 0xa5, csize);
		else if (unlikely(opt_zero) && is_zeroed == false)
			memset(ret, 0, csize);
	}

	return (ret);
}

bool
huge_ralloc_no_move(void *ptr, size_t oldsize, size_t size, size_t extra)
{

	/*
	 * Avoid moving the allocation if the size class can be left the same.
	 */
	if (oldsize > arena_maxclass
	    && CHUNK_CEILING(oldsize) >= CHUNK_CEILING(size)
	    && CHUNK_CEILING(oldsize) <= CHUNK_CEILING(size+extra)) {
		assert(CHUNK_CEILING(oldsize) == oldsize);
		return (false);
	}

	/* Reallocation would require a move. */
	return (true);
}

void *
huge_ralloc(arena_t *arena, void *ptr, size_t oldsize, size_t size,
    size_t extra, size_t alignment, bool zero, bool try_tcache_dalloc)
{
	void *ret;
	size_t copysize;

	/* Try to avoid moving the allocation. */
	if (huge_ralloc_no_move(ptr, oldsize, size, extra) == false)
		return (ptr);

	/*
	 * size and oldsize are different enough that we need to use a
	 * different size class.  In that case, fall back to allocating new
	 * space and copying.
	 */
	if (alignment > chunksize)
		ret = huge_palloc(arena, size + extra, alignment, zero);
	else
		ret = huge_malloc(arena, size + extra, zero);

	if (ret == NULL) {
		if (extra == 0)
			return (NULL);
		/* Try again, this time without extra. */
		if (alignment > chunksize)
			ret = huge_palloc(arena, size, alignment, zero);
		else
			ret = huge_malloc(arena, size, zero);

		if (ret == NULL)
			return (NULL);
	}

	/*
	 * Copy at most size bytes (not size+extra), since the caller has no
	 * expectation that the extra bytes will be reliably preserved.
	 */
	copysize = (size < oldsize) ? size : oldsize;
	memcpy(ret, ptr, copysize);
	iqalloc(ptr, try_tcache_dalloc);
	return (ret);
}

#ifdef JEMALLOC_JET
#undef huge_dalloc_junk
#define	huge_dalloc_junk JEMALLOC_N(huge_dalloc_junk_impl)
#endif
static void
huge_dalloc_junk(void *ptr, size_t usize)
{

	if (config_fill && have_dss && unlikely(opt_junk)) {
		/*
		 * Only bother junk filling if the chunk isn't about to be
		 * unmapped.
		 */
		if (config_munmap == false || (have_dss && chunk_in_dss(ptr)))
			memset(ptr, 0x5a, usize);
	}
}
#ifdef JEMALLOC_JET
#undef huge_dalloc_junk
#define	huge_dalloc_junk JEMALLOC_N(huge_dalloc_junk)
huge_dalloc_junk_t *huge_dalloc_junk = JEMALLOC_N(huge_dalloc_junk_impl);
#endif

void
huge_dalloc(void *ptr)
{
	extent_node_t *node, key;

	malloc_mutex_lock(&huge_mtx);

	/* Extract from tree of huge allocations. */
	key.addr = ptr;
	node = extent_tree_ad_search(&huge, &key);
	assert(node != NULL);
	assert(node->addr == ptr);
	extent_tree_ad_remove(&huge, node);

	malloc_mutex_unlock(&huge_mtx);

	huge_dalloc_junk(node->addr, node->size);
	arena_chunk_dalloc_huge(node->arena, node->addr, node->size);
	base_node_dalloc(node);
}

size_t
huge_salloc(const void *ptr)
{
	size_t ret;
	extent_node_t *node, key;

	malloc_mutex_lock(&huge_mtx);

	/* Extract from tree of huge allocations. */
	key.addr = __DECONST(void *, ptr);
	node = extent_tree_ad_search(&huge, &key);
	assert(node != NULL);

	ret = node->size;

	malloc_mutex_unlock(&huge_mtx);

	return (ret);
}

prof_tctx_t *
huge_prof_tctx_get(const void *ptr)
{
	prof_tctx_t *ret;
	extent_node_t *node, key;

	malloc_mutex_lock(&huge_mtx);

	/* Extract from tree of huge allocations. */
	key.addr = __DECONST(void *, ptr);
	node = extent_tree_ad_search(&huge, &key);
	assert(node != NULL);

	ret = node->prof_tctx;

	malloc_mutex_unlock(&huge_mtx);

	return (ret);
}

void
huge_prof_tctx_set(const void *ptr, prof_tctx_t *tctx)
{
	extent_node_t *node, key;

	malloc_mutex_lock(&huge_mtx);

	/* Extract from tree of huge allocations. */
	key.addr = __DECONST(void *, ptr);
	node = extent_tree_ad_search(&huge, &key);
	assert(node != NULL);

	node->prof_tctx = tctx;

	malloc_mutex_unlock(&huge_mtx);
}

bool
huge_boot(void)
{

	/* Initialize chunks data. */
	if (malloc_mutex_init(&huge_mtx))
		return (true);
	extent_tree_ad_new(&huge);

	return (false);
}

void
huge_prefork(void)
{

	malloc_mutex_prefork(&huge_mtx);
}

void
huge_postfork_parent(void)
{

	malloc_mutex_postfork_parent(&huge_mtx);
}

void
huge_postfork_child(void)
{

	malloc_mutex_postfork_child(&huge_mtx);
}