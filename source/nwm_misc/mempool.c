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

#include <stddef.h>
#include <string.h>

#include "mempool.h"
#include "global.h"

struct block {
    void *next;
};

int mp_init(size_t bs, size_t bc, void *m, mp_pool_t *mp)
{
    if(bs < sizeof(size_t)) {
        return -1;
    }
    if(bs % sizeof(size_t)) {
        return -1;
    }
    mp->bs = bs;
    mp->ul_bc = bc;
    mp->b = NULL;
    mp->ul_b = m;

    mp->bc = bc;
    mp->m = m;

    return 0;
}

void *mp_malloc(mp_pool_t *mp)
{
    /*
     * 1. First we try to allocate an unlinked block
     * 2. In case there are no more unlinked blocks left we try to return the head from the list of free blocks
     * 3. Otherwise we will have to abort since there are no free blocks left
     */
    char *m_end = (char *)mp->m + mp->bs * mp->bc;

    if(mp->ul_bc > 0) {
        mp->ul_bc--;
        void *b = mp->ul_b;

        if (b < mp->m || b >= (void *)m_end) {
            showDbg("Out of range pointer for pool initial malloc %08"PRIx32" (begin %08"PRIx32" end %08"PRIx32")", (u32)b, (u32)mp->m, (u32)m_end);
            return NULL;
        }
        if (((char *)b - (char *)mp->m) % mp->bs) {
            showDbg("Mis-aligned pointer for pool initial malloc %08"PRIx32" (begin %08"PRIx32" bs %08"PRIx32")", (u32)b, (u32)mp->m, (u32)mp->bs);
            return NULL;
        }

        mp->ul_b = (void *) (((unsigned char *) mp->ul_b) + mp->bs);
        memset(b, 0xc3, mp->bs);
        return b;
    } else if(mp->b) {
        void *b = mp->b;

        if (b < mp->m || b >= (void *)m_end) {
            showDbg("Out of range pointer for pool malloc %08"PRIx32" (begin %08"PRIx32" end %08"PRIx32")", (u32)b, (u32)mp->m, (u32)m_end);
            return NULL;
        }
        if (((char *)b - (char *)mp->m) % mp->bs) {
            showDbg("Mis-aligned pointer for pool malloc %08"PRIx32" (begin %08"PRIx32" bs %08"PRIx32")", (u32)b, (u32)mp->m, (u32)mp->bs);
            return NULL;
        }

        mp->b = ((struct block *) mp->b)->next;
        memset(b, 0xc3, mp->bs);
        return b;
    }

    return NULL;
}

int mp_free(mp_pool_t *mp, void *b)
{
    /*
     * We add b as the head of the list of free blocks
     */

    char *m_end = (char *)mp->m + mp->bs * mp->bc;
    if (b < mp->m || b >= (void *)m_end) {
        showDbg("Out of range pointer for pool free %08"PRIx32" (begin %08"PRIx32" end %08"PRIx32")", (u32)b, (u32)mp->m, (u32)m_end);
        return -1;
    }
    if (((char *)b - (char *)mp->m) % mp->bs) {
        showDbg("Mis-aligned pointer for pool free %08"PRIx32" (begin %08"PRIx32" bs %08"PRIx32")", (u32)b, (u32)mp->m, (u32)mp->bs);
        return -2;
    }

    ((struct block *) b)->next = mp->b;
    mp->b = b;

    return 0;
}
