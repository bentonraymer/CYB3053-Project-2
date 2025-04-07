#include "alloc.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>


#define ALIGNMENT 16 /**< The alignment of the memory blocks */

static free_block *HEAD = NULL; /**< Pointer to the first element of the free list */

/**
 * Split a free block into two blocks
 *
 * @param block The block to split
 * @param size The size of the first new split block
 * @return A pointer to the first block or NULL if the block cannot be split
 */
void *split(free_block *block, int size) {
    if((block->size < size + sizeof(free_block))) {
        return NULL;
    }

    void *split_pnt = (char *)block + size + sizeof(free_block);
    free_block *new_block = (free_block *) split_pnt;

    new_block->size = block->size - size - sizeof(free_block);
    new_block->next = block->next;

    block->size = size;

    return block;
}

/**
 * Find the previous neighbor of a block
 *
 * @param block The block to find the previous neighbor of
 * @return A pointer to the previous neighbor or NULL if there is none
 */
free_block *find_prev(free_block *block) {
    free_block *curr = HEAD;
    while(curr != NULL) {
        char *next = (char *)curr + curr->size + sizeof(free_block);
        if(next == (char *)block)
            return curr;
        curr = curr->next;
    }
    return NULL;
}

/**
 * Find the next neighbor of a block
 *
 * @param block The block to find the next neighbor of
 * @return A pointer to the next neighbor or NULL if there is none
 */
free_block *find_next(free_block *block) {
    char *block_end = (char*)block + block->size + sizeof(free_block);
    free_block *curr = HEAD;

    while(curr != NULL) {
        if((char *)curr == block_end)
            return curr;
        curr = curr->next;
    }
    return NULL;
}

/**
 * Remove a block from the free list
 *
 * @param block The block to remove
 */
void remove_free_block(free_block *block) {
    free_block *curr = HEAD;
    if(curr == block) {
        HEAD = block->next;
        return;
    }
    while(curr != NULL) {
        if(curr->next == block) {
            curr->next = block->next;
            return;
        }
        curr = curr->next;
    }
}

/**
 * Coalesce neighboring free blocks
 *
 * @param block The block to coalesce
 * @return A pointer to the first block of the coalesced blocks
 */
void *coalesce(free_block *block) {
    if (block == NULL) {
        return NULL;
    }

    free_block *prev = find_prev(block);
    free_block *next = find_next(block);

    // Coalesce with previous block if it is contiguous.
    if (prev != NULL) {
        char *end_of_prev = (char *)prev + prev->size + sizeof(free_block);
        if (end_of_prev == (char *)block) {
            prev->size += block->size + sizeof(free_block);

            // Ensure prev->next is updated to skip over 'block', only if 'block' is directly next to 'prev'.
            if (prev->next == block) {
                prev->next = block->next;
            }
            block = prev; // Update block to point to the new coalesced block.
        }
    }

    // Coalesce with next block if it is contiguous.
    if (next != NULL) {
        char *end_of_block = (char *)block + block->size + sizeof(free_block);
        if (end_of_block == (char *)next) {
            block->size += next->size + sizeof(free_block);

            // Ensure block->next is updated to skip over 'next'.
            block->next = next->next;
        }
    }

    return block;
}

/**
 * Call sbrk to get memory from the OS
 *
 * @param size The amount of memory to allocate
 * @return A pointer to the allocated memory
 */
void *do_alloc(size_t size) {

    // Ensure that the input size is greater than zero
    if (size <= 0) return NULL;

    // Ensure the size is a multiple of 16, which is the alignment value.
    size = (size + ALIGNMENT - 1) & ~(ALIGNMENT - 1);

    // Use sbrk to allocate more memory according to the size input in the function. sbrk returns the new break address (pointer), which means the address the heap ends at at the bottom and breaks up the end of the heap and start of unallocated memory
    void *ptr = sbrk(size);

    // Check to see if the allocation suceeded; if not, return null. This call could fail for reasons including that the requested memory exceeds the process's limits, or if there's insufficient memory available on the system.
    if (ptr == (void *)-1) {
        return NULL;
    }
    // If the allocation did succeed, the code will reach this point and will return the correct pointer
    return ptr;
}

/**
 * Allocates memory for the end user
 *
 * @param size The amount of memory to allocate
 * @return A pointer to the requested block of memory
 */
void *tumalloc(size_t size) {
    
    // If the free list is empty call do_alloc to allocate new memory
    if (HEAD == NULL) {
        void *raw = do_alloc(size + sizeof(header));
        // This will be taken if there was an issue with sbrk or do_alloc
        if (raw == NULL) {
            return NULL;
        }

        // Create header & add information
        header *hdr = (header *)raw;
        hdr->size = size;
        hdr->magic = 0x01234567;
        return (void *)(hdr + 1);
    }

    // Free list is not empty, so look for a block to allocate from
    else {
        free_block *block = HEAD;
        // Loop through the list of free blocks
        while (block != NULL) {
            // If the block is big enough, split it and return the pointer
            if (size + sizeof(header) <= block->size) {
                header *hdr = (header *) split(block, size + sizeof(header));
                // If the block was split, remove it from the free list
                if (hdr == NULL) {
                    remove_free_block(block);
                    hdr = (header *) block;
                } else {
                    remove_free_block((free_block *) hdr);
                }
                // Create header & add information
                hdr->size = size;
                hdr->magic = 0x01234567;
                // Return the pointer to the memory after the header
                return (void *)(hdr + 1);
            }
            block = block->next;
        }
        // If no block was found, allocate a new one
        void *raw = do_alloc(size + sizeof(header));
        header *hdr = (header *)raw;
        hdr->size = size;
        hdr->magic = 0x01234567;
        return (void *)(hdr + 1);
    }
}



/**
 * Allocates and initializes a list of elements for the end user
 *
 * @param num How many elements to allocate
 * @param size The size of each element
 * @return A pointer to the requested block of initialized memory
 */
void *tucalloc(size_t num, size_t size) {
    // Calculate the total needed size
    size_t total_size = num * size;

    // If input is 0, return NULL
    if (total_size == 0) {
        return NULL;
    }

    // Allocate the total size
    void *ptr = tumalloc(total_size);

    // If allocation was not successful, return NULL
    if (ptr == NULL) {
        return NULL;
    }

    // Initialize the allocated memory to 0
    memset(ptr, 0, total_size);
    return ptr;
}

/**
 * Reallocates a chunk of memory with a bigger size
 *
 * @param ptr A pointer to an already allocated piece of memory
 * @param new_size The new requested size to allocate
 * @return A new pointer containing the contents of ptr, but with the new_size
 */
void *turealloc(void *ptr, size_t new_size) {
    if (ptr == NULL) {
        return tumalloc(new_size);
    }

    if (new_size == 0) {
        tufree(ptr);
        return NULL;
    }

    // Get the header of the block
    header *old_header = (header *)ptr - 1;

    // If the old block is big enough already, no need to allocate new block; return old pointer
    if (old_header->size >= new_size) {
        return ptr;
    }

    // Otherwise, allocate a new block
    void *new_ptr = tumalloc(new_size);
    
    // If the allocation failed
    if (!new_ptr) {
        return NULL;
    }

    // Copy the old data to the new block
    size_t copy_size = old_header->size < new_size ? old_header->size : new_size;
    memcpy(new_ptr, ptr, copy_size);

    // Return the new pointer
    return new_ptr;
}

/**
 * Removes used chunk of memory and returns it to the free list
 *
 * @param ptr Pointer to the allocated piece of memory
 */
void tufree(void *ptr) {

    // Convert the user pointer to the header pointer
    header *hdr = (header *)((char *)ptr - sizeof(header));

    // Check the magic number; take top path if it's correct
    if (hdr->magic == 0x01234567) {
        // Convert the user pointer to the header pointer
        free_block *block = (free_block *)hdr;
        // Set the size of the block to the size of the header
        block->size = hdr->size;
        // Insert the block at the beginning of the free list
        block->next = HEAD;
        HEAD = block;
        // Coalesce the block with any surrounding free blocks
        coalesce(block);
    // If the magic number is not correct, print that there's memory corruption
    } else {
        printf("MEMORY CORRUPTION DETECTED\n");
        abort();
    }
    }
