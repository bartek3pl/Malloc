/*
  Bartłomiej Hildebrandt
  302126

  Oświadczam, że jestem jedynym autorem kodu źródłowego. Rozwiązanie jest
  inspirowane rozwiązaniem przedstawionym w rozdziale §9.9 książki CSAPP.
*/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <unistd.h>

#include "mm.h"
#include "memlib.h"

/* If you want debugging output, use the following macro.  When you hand
 * in, remove the #define DEBUG line. */
#define DEBUG
#ifdef DEBUG
#define debug(...) printf(__VA_ARGS__)
#else
#define debug(...)
#endif

/* do not change the following! */
#ifdef DRIVER
/* create aliases for driver tests */
#define malloc mm_malloc
#define free mm_free
#define realloc mm_realloc
#define calloc mm_calloc
#endif /* def DRIVER */

#define DWORD_ALIGNMENT ALIGNMENT

#define WORD_SIZE 4         /* Word, header and footer size */
#define DWORD_SIZE 8        /* Double word size */
#define MIN_BLOCK_SIZE 16   /* Minimum overall size of block */
#define CHUNKSIZE (1 << 12) /* Extend heap by this amount */

#define MAX_BLOCK_SIZE ~0x7         /* 29 most significant bits */
#define MAX_ALLOCATION_BIT_SIZE 0x1 /* Just 1 bit for allocation bit */

#define MAX(x, y) ((x) > (y) ? (x) : (y))

/* Merge a block size and allocated bit into a word */
#define MERGE(size, allocation_bit) ((size) | (allocation_bit))

/* Read and write a word at address p */
#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))

/* Read the size and allocated fields from address p */
#define GET_SIZE(p) (GET(p) & MAX_BLOCK_SIZE)
#define GET_ALLOCATION_BIT(p) (GET(p) & MAX_ALLOCATION_BIT_SIZE)

/* Given block ptr bp, compute address of its header and footer */
#define BLOCK_HEADER(bp) ((char *)(bp)-WORD_SIZE)
#define BLOCK_FOOTER(bp)                                                       \
  ((char *)(bp) + GET_SIZE(BLOCK_HEADER(bp)) - DWORD_SIZE)

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLOCK(bp) ((char *)(bp) + GET_SIZE(((char *)(bp)-WORD_SIZE)))
#define PREV_BLOCK(bp) ((char *)(bp)-GET_SIZE(((char *)(bp)-DWORD_SIZE)))

// Block allocation bit
typedef enum {
  FREE = 0, /* Block is free */
  USED = 1, /* Block is allocated */
} bt_alloc_bit;

/* Prologue and epilogue blocks sizes */
#define PROLOGUE_BLOCK MERGE(DWORD_SIZE, USED)
#define EPILOGUE_BLOCK MERGE(0, USED)

typedef int32_t word_t; /* Heap is bascially an array of 4-byte words. */

static char *heap_start = 0; /* Address of the first block in heap */

// Memory block structure
typedef struct {
  word_t header;  /* Header with block size and allocation bit */
  word_t payload; /* Allocated memory + optional padding */
  word_t footer;  /* Footer with block size and allocation bit */
} block_t;

static inline size_t round_up(size_t size) {
  return DWORD_SIZE * ((DWORD_SIZE + size + (DWORD_SIZE - 1)) / DWORD_SIZE);
}

// Try to increase heap size and return start address of new memory
static void *morecore(size_t size) {
  void *ptr = mem_sbrk(size);
  if (ptr == (void *)-1)
    return NULL;
  return ptr;
}

// Four cases of blocks coalescing
static void *coalesce(void *bp) {
  size_t prev_block_allocation_bit =
    GET_ALLOCATION_BIT(BLOCK_FOOTER(PREV_BLOCK(bp)));
  size_t next_block_allocation_bit =
    GET_ALLOCATION_BIT(BLOCK_HEADER(NEXT_BLOCK(bp)));
  size_t block_size = GET_SIZE(BLOCK_HEADER(bp));

  /* Current block just needs to be freed */
  if (prev_block_allocation_bit == USED && next_block_allocation_bit == USED) {
    return bp;
  }

  else if (prev_block_allocation_bit == USED &&
           next_block_allocation_bit == FREE) {
    size_t next_block_size = GET_SIZE(BLOCK_HEADER(NEXT_BLOCK(bp)));
    /* Update header */
    PUT(BLOCK_HEADER(bp), MERGE(block_size + next_block_size, FREE));
    /* Update footer */
    PUT(BLOCK_FOOTER(bp), MERGE(block_size + next_block_size, FREE));
  }

  else if (prev_block_allocation_bit == FREE &&
           next_block_allocation_bit == USED) {
    size_t prev_block_size = GET_SIZE(BLOCK_HEADER(PREV_BLOCK(bp)));
    /* Update header */
    PUT(BLOCK_HEADER(PREV_BLOCK(bp)),
        MERGE(block_size + prev_block_size, FREE));
    /* Update footer */
    PUT(BLOCK_FOOTER(bp), MERGE(block_size + prev_block_size, FREE));
    /* Update pointer to current block */
    bp = PREV_BLOCK(bp);
  }

  else if (prev_block_allocation_bit == FREE &&
           next_block_allocation_bit == FREE) {
    size_t prev_block_size = GET_SIZE(BLOCK_HEADER(PREV_BLOCK(bp)));
    size_t next_block_size = GET_SIZE(BLOCK_HEADER(NEXT_BLOCK(bp)));
    /* Update header */
    PUT(BLOCK_HEADER(PREV_BLOCK(bp)),
        MERGE(block_size + prev_block_size + next_block_size, FREE));
    /* Update footer */
    PUT(BLOCK_FOOTER(NEXT_BLOCK(bp)),
        MERGE(block_size + prev_block_size + next_block_size, FREE));
    /* Update pointer to current block */
    bp = PREV_BLOCK(bp);
  }

  return bp;
}

// Extend heap by chosen size
static void *extend_heap(size_t size) {
  size_t words = size / WORD_SIZE; /* Number of words in size */

  /* New size needs to be double-word aligned, so if the words number is even
  then we already have proper alignment, else we need to align one more block */

  size_t adjusted_new_size = 0;
  if (words % 2) {
    adjusted_new_size = size;
  } else {
    adjusted_new_size = size + WORD_SIZE;
  }

  char *bp = morecore(adjusted_new_size);
  if (bp == NULL) {
    return NULL;
  }

  /* Initialize new epilogue block */
  PUT(BLOCK_HEADER(NEXT_BLOCK(bp)), EPILOGUE_BLOCK);

  /* Initialize free blocks in new memory */
  PUT(BLOCK_HEADER(bp), MERGE(adjusted_new_size, FREE)); /* Header */
  PUT(BLOCK_FOOTER(bp), MERGE(adjusted_new_size, FREE)); /* Footer */

  /* We want to implement immediate coalescing, so if the last block in previous
   * memory was free we need to coalesce new block in new memory with previous
   * one */
  return coalesce(bp);
}

/*
 * mm_init - Called when a new trace starts.
 */
int mm_init(void) {
  /* Pad heap start so first payload is at DWORD_ALIGNMENT. */

  if ((heap_start = morecore(MIN_BLOCK_SIZE)) == NULL) {
    return -1;
  }

  /* Prologue block and epilogue block are blocks which are never freed and
   * they exists only to eliminate edge conditions during coalescing  */

  PUT(heap_start, 0);                              /* Alignment padding */
  PUT(heap_start + WORD_SIZE, PROLOGUE_BLOCK);     /* Prologue block header */
  PUT(heap_start + 2 * WORD_SIZE, PROLOGUE_BLOCK); /* Prologue block footer */
  PUT(heap_start + 3 * WORD_SIZE, EPILOGUE_BLOCK); /* Epilogue block header */

  /* Indicates start of heap - after Prologue block address */
  heap_start += 2 * WORD_SIZE;

  /* Extend heap by selected amount - CHUNKSIZE to have some free memory for
   * blocks allocation */
  extend_heap(CHUNKSIZE);

  return 0;
}

// Place new allocated block in found free place
static void place(void *bp, size_t adjusted_size) {
  if ((GET_SIZE(BLOCK_HEADER(bp)) - adjusted_size) >= (2 * DWORD_SIZE)) {
    /* Set header */
    PUT(BLOCK_HEADER(bp), MERGE(adjusted_size, USED));
    /* Set footer */
    PUT(BLOCK_FOOTER(bp), MERGE(adjusted_size, USED));

    size_t next_block_new_size = GET_SIZE(BLOCK_HEADER(bp)) - adjusted_size;
    assert(next_block_new_size >= 0);

    /* Update header of next block */
    PUT(BLOCK_HEADER(NEXT_BLOCK(bp)), MERGE(next_block_new_size, FREE));
    /* Update footer of next block */
    PUT(BLOCK_FOOTER(NEXT_BLOCK(bp)), MERGE(next_block_new_size, FREE));
  } else {
    PUT(BLOCK_HEADER(bp), MERGE(GET_SIZE(BLOCK_HEADER(bp)), USED));
    PUT(BLOCK_FOOTER(bp), MERGE(GET_SIZE(BLOCK_HEADER(bp)), USED));
  }
}

// Find free block by first-fit strategy
static void *find_fit(size_t adjusted_size) {
  void *current_heap_address = heap_start;
  size_t current_block_size = GET_SIZE(BLOCK_HEADER(current_heap_address));

  while (GET_SIZE(BLOCK_HEADER(current_heap_address)) > 0) {
    /* If block is free and have proper size */
    if (GET_ALLOCATION_BIT(BLOCK_HEADER(current_heap_address)) == FREE &&
        current_block_size >= adjusted_size) {
      return current_heap_address;
    }
    /* Else continue searching (go to next block address) */
    else {
      current_heap_address = NEXT_BLOCK(current_heap_address);
      current_block_size = GET_SIZE(BLOCK_HEADER(current_heap_address));
    }
  }

  /* No fit */
  return NULL;
}

/*
 * malloc - Allocate a block by incrementing the brk pointer.
 */
void *malloc(size_t size) {
  if (size == 0) {
    return NULL;
  }

  if (heap_start == 0) {
    mm_init();
  }

  size_t adjusted_size;

  if (size <= DWORD_SIZE) {
    adjusted_size = MIN_BLOCK_SIZE;
  } else {
    adjusted_size = round_up(size);
  }

  /* Search the free list for a fit */
  char *bp;

  if ((bp = find_fit(adjusted_size)) != NULL) {
    place(bp, adjusted_size);
    return bp;
  }

  /* No fit found. Extend heap and place the block */
  size_t amount_to_extend_heap = MAX(adjusted_size, CHUNKSIZE);

  if ((bp = extend_heap(amount_to_extend_heap)) != NULL) {
    place(bp, adjusted_size);
    return bp;
  }

  return NULL;
}

/*
 * free
 */
void free(void *bp) {
  size_t size_to_free = GET_SIZE(BLOCK_HEADER(bp));

  PUT(BLOCK_HEADER(bp), MERGE(size_to_free, FREE));
  PUT(BLOCK_FOOTER(bp), MERGE(size_to_free, FREE));

  coalesce(bp);
}

/*
 * realloc - Change the size of the block by mallocing a new block,
 *      copying its data, and freeing the old block.
 **/
void *realloc(void *old_ptr, size_t size) {
  /* If size == 0 then this is just free, and we return NULL. */
  if (size == 0) {
    free(old_ptr);
    return NULL;
  }

  /* If old_ptr is NULL, then this is just malloc. */
  if (!old_ptr)
    return malloc(size);

  void *new_ptr = malloc(size);

  /* If malloc() fails, the original block is left untouched. */
  if (!new_ptr)
    return NULL;

  /* Copy the old data. */
  size_t old_size = GET_SIZE(BLOCK_HEADER(old_ptr));

  if (size < old_size)
    old_size = size;

  memcpy(new_ptr, old_ptr, old_size);

  /* Free the old block. */
  free(old_ptr);

  return new_ptr;
}

/*
 * calloc - Allocate the block and set it to zero.
 */
void *calloc(size_t nmemb, size_t size) {
  size_t bytes = nmemb * size;
  void *new_ptr = malloc(bytes);

  /* If malloc() fails, skip zeroing out the memory. */
  if (new_ptr)
    memset(new_ptr, 0, bytes);

  return new_ptr;
}

/*
 * mm_checkheap
 */
void mm_checkheap(int verbose) {
}
