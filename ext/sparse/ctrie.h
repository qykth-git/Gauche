/*
 * ctrie.h - Compact Trie
 *
 *   Copyright (c) 2009  Shiro Kawai  <shiro@acm.org>
 * 
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 * 
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *
 *   3. Neither the name of the authors nor the names of its contributors
 *      may be used to endorse or promote products derived from this
 *      software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 *   TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 *   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef GAUCHE_CTRIE_H
#define GAUCHE_CTRIE_H

#include <gauche.h>
#include <gauche/extend.h>
#include <gauche/bits_inline.h>

/* CompactTrie is a structure to store data indexed by full 32bit integer.
 * It can be used as a space-efficient vector and as a back-end of sparse
 * hash table.  Compact Trie itself is *not* an ScmObj.
 *
 * CompactTrie is not intended to be used outside of ext/sparse source tree,
 * hence we don't use 'Scm' prefix for data structures, for simplicity.
 */

/* CompactTrie consists of NODEs and LEAFs.  LEAF is just an intptr_t value.
 * NODE is a variable length structure, whose first two words are bitmaps.
 * (NB: we may change the layout here, since full-word bitmap tends to become
 * false pointer and may have negative impact to our conservative GC.)
 *
 * NODE represents 32-way branch.  The first bitmap, EMAP or entry map,
 * shows which logical index of this node is active.  The second bitmap,
 * LMAP, shows which entry of logical index of this node is LEAF.
 * LMAP is always a strict subset of EMAP.   If bit N of EMAP is 1 and
 * bit N of LMAP is 0, the N-th index points to the child node.
 *
 * Suppose EMAP is 0x00001809 and LMAP is 0x00001008.   It means this 
 * node has entries in index 0, 3, 11, 12.  Entry #0 and #11 points to
 * child NODEs, and entry #3 and #12 are LEAFs.
 *
 * The actual entries follow the bitmaps in compact format.  For the above
 * example, four intptr_t follows, corresponding to entry #0, #3, #11 and #12,
 * respectively.
 */

#define MAX_NODE_SIZE 32
#define TRIE_SHIFT    5
#define TRIE_MASK     (0x1f)

typedef struct NodeRec {
    u_long   emap;
    u_long   lmap;
    void    *entries[2];        /* variable length; 2 is the minimum entries */
} Node;

typedef struct LeafRec {
    u_long   key0;              /* lower half word of the key */
    u_long   key1;              /* upper half word of the key */
} Leaf;

typedef struct CompactTrieRec {
    u_int    numEntries;
    Node     *root;
} CompactTrie;

/* Create empty CompactTrie */
extern CompactTrie *MakeCompactTrie(void);
extern void CompactTrieInit(CompactTrie *);
extern void CompactTrieClear(CompactTrie *,
                             void (*clearer)(Leaf*, void*),
                             void *data);

/* Search CompactTrie with KEY. */
extern Leaf *CompactTrieGet(CompactTrie *ct, u_long key);
extern Leaf *CompactTrieAdd(CompactTrie *ct, u_long key,
                            Leaf *(*creator)(void*), void *data);
extern Leaf *CompactTrieDelete(CompactTrie *ct, u_long key);

/* Iterator */


/* For debug */
#if SCM_DEBUG_HELPER
extern void CompactTrieDump(ScmPort *out, CompactTrie *ct,
                            void (*dumper)(ScmPort *, Leaf*, int, void*),
                            void *data);
#endif /*SCM_DEBUG_HELPER*/

#endif /*GAUCHE_CTRIE_H*/
