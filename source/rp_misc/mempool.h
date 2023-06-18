/*
    MIT License

    Copyright (c) 2021 Adrian Reuter

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.
 */

#ifndef MEMPOOL_H
#define MEMPOOL_H

#include <stddef.h> /* size_t */

/*
 * mp_data contains the data of the memory pool
 */
struct mp_data {
    size_t bs;      // the size of a block from the memory pool
    size_t ul_bc;   // the count of unlinked blocks in the memory pool
    void *b;        // a pointer to the head of the linked list of unused blocks
    void *ul_b;     // a pointer to the first unlinked block 
};

typedef struct mp_data mp_pool_t;

/*
 * Initialize the memory pool.
 * If bs is smaller than sizeof(size_t) it will set mp_error to MP_INVALID_BLOCKSIZE
 */
int mp_init(size_t bs, size_t bc, void *m, mp_pool_t *mp);

/*
 * Retrieve a free block from the memory pool.
 * If unsuccessfull it will set mp_error to MP_OUT_OF_MEMORY
 */
void *mp_malloc(mp_pool_t *mp);

/*
 * Adds a block to the list of free blocks
 */
void mp_free(mp_pool_t *mp, void *b);

#endif /* MEMPOOL_H */