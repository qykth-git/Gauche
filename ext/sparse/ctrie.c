/*
 * ctrie.c - Compact Trie
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

#include "ctrie.h"

/*
 * Constructor
 */

CompactTrie *MakeCompactTrie(void)
{
    CompactTrie *t = SCM_NEW(CompactTrie);
    CompactTrieInit(t);
    return t;
}

void CompactTrieInit(CompactTrie *t)
{
    t->numEntries = 0;
    t->root = NULL;
}

/*
 * Nodes
 */

#define KEY2INDEX(key, level) (((key)>>((level)*TRIE_SHIFT)) & TRIE_MASK)

#define NODE_HAS_ARC(node, ind)      SCM_BITS_TEST(&node->emap, (ind))
#define NODE_ARC_SET(node, ind)      SCM_BITS_SET(&node->emap, (ind))
#define NODE_ARC_RESET(node, ind)    SCM_BITS_RESET(&node->emap, (ind))

#define NODE_ARC_IS_LEAF(node, ind)  SCM_BITS_TEST(&node->lmap, (ind))
#define NODE_LEAF_SET(node, ind)     SCM_BITS_SET(&node->lmap, (ind))
#define NODE_LEAF_RESET(node, ind)   SCM_BITS_RESET(&node->lmap, (ind))

#define NODE_INDEX2OFF(node, ind)    Scm__CountBitsBelow(node->emap, (ind))

#define NODE_ENTRY(node, off)        ((node)->entries[(off)])

static Node *make_node(int nentry)
{
    /* We round up nentry by every two; on 32bit machine Boehm GC allocates
       by 8byte boundary anyways.  It also reduces number of allocations
       when entries are added constantly. */
    int nalloc = (nentry+1)&(~1);
    /* SCM_NEW2 returns zero cleared chunk. */
    return SCM_NEW2(Node*, sizeof(Node) + sizeof(void*)*(nalloc-2));
}

static Node *node_insert(Node *orig, u_long ind, void *entry, int leafp)
{
    int size = Scm__CountBitsInWord(orig->emap);
    int insertpoint = Scm__CountBitsBelow(orig->emap, ind);
    int i;
    
    if (size&1) {
        /* we have one more room */
        NODE_ARC_SET(orig, ind);
        if (leafp) NODE_LEAF_SET(orig, ind);
        if (insertpoint < size) {
            for (i=size-1; i>=insertpoint; i--) {
                orig->entries[i+1] = orig->entries[i];
            }
        }
        orig->entries[insertpoint] = entry;
        return orig;
    } else {
        /* we need to extend the node */
        Node *newn = make_node(size+2);
        newn->emap = orig->emap;
        newn->lmap = orig->lmap;
        NODE_ARC_SET(newn, ind);
        if (leafp) NODE_LEAF_SET(newn, ind);
        for (i=0; i<insertpoint; i++) newn->entries[i] = orig->entries[i];
        newn->entries[insertpoint] = entry;
        for (; i<size; i++) newn->entries[i+1] = orig->entries[i];
        return newn;
    }
}

static Node *node_delete(Node *orig, u_long ind)
{
    int size = Scm__CountBitsInWord(orig->emap);
    int deletepoint = Scm__CountBitsBelow(orig->emap, ind);
    int i;

    /* TODO: shrink node */
    NODE_ARC_RESET(orig, ind);
    NODE_LEAF_RESET(orig, ind);
    for (i=deletepoint; i<size-1; i++) orig->entries[i] = orig->entries[i+1];
    return orig;
}

/*
 * Leaves
 */

#define LEAF_KEY(leaf) (((leaf)->key0&0xffff) + (((leaf)->key1&0xffff) << 16))

static Leaf *new_leaf(u_long key, Leaf *(*creator)(void*), void *data)
{
    Leaf *l = creator(data);
    l->key0 = key & 0xffff;
    l->key1 = (key>>16) & 0xffff;
    return l;
}

/*
 * Search
 */
static Leaf *get_rec(Node *n, u_long key, int level)
{
    u_long ind = KEY2INDEX(key, level);
    if (!NODE_HAS_ARC(n, ind)) return NULL;
    if (NODE_ARC_IS_LEAF(n, ind)) {
        Leaf *l = (Leaf*)NODE_ENTRY(n, NODE_INDEX2OFF(n, ind));
        if (LEAF_KEY(l) == key) return l;
        else return NULL;
    } else {
        return get_rec((Node*)NODE_ENTRY(n, NODE_INDEX2OFF(n, ind)),
                       key, level+1);
    }
}

Leaf *CompactTrieGet(CompactTrie *ct, u_long key)
{
    key &= 0xffffffff;
    if (ct->root == NULL) return NULL;
    else return get_rec(ct->root, key, 0);
}

/*
 * Search, and if not found, create
 */
static Node *add_rec(CompactTrie *ct, Node *n, u_long key, int level,
                     Leaf **result, Leaf *(*creator)(void*), void *data)

{
    Leaf *l;
    u_long ind = KEY2INDEX(key, level);
    
    if (!NODE_HAS_ARC(n, ind)) {
        *result = l = new_leaf(key, creator, data);
        ct->numEntries++;
        return node_insert(n, ind, (void*)l, TRUE);
    }
    else if (!NODE_ARC_IS_LEAF(n, ind)) {
        u_long off = NODE_INDEX2OFF(n, ind);
        Node *orig = (Node*)NODE_ENTRY(n, off);
        Node *m = add_rec(ct, orig, key, level+1, result, creator, data);
        if (m != orig) NODE_ENTRY(n, off) = m;
        return n;
    }
    else {
        u_long off = NODE_INDEX2OFF(n, ind);
        Leaf *l0 = (Leaf*)NODE_ENTRY(n, off);
        u_long k0 = LEAF_KEY(l0), i0;

        if (key == k0) { *result = l0; return n; }
        i0 = KEY2INDEX(LEAF_KEY(l0), level+1);
        Node *m = make_node(2);
        NODE_ARC_SET(m, i0);
        NODE_LEAF_SET(m, i0);
        NODE_ENTRY(m, 0) = l0;
        NODE_ENTRY(n, off) = add_rec(ct, m, key, level+1, result, creator, data);
        NODE_LEAF_RESET(n, ind);
        return n;
    }
}

Leaf *CompactTrieAdd(CompactTrie *ct, u_long key,
                     Leaf *(*creator)(void*), void *data)
{
    key &= 0xffffffff;
    if (ct->root == NULL) {
        Leaf *l = new_leaf(key, creator, data);
        ct->root = make_node(2);
        ct->numEntries = 1;
        NODE_ARC_SET(ct->root, key&TRIE_MASK);
        NODE_LEAF_SET(ct->root, key&TRIE_MASK);
        NODE_ENTRY(ct->root, 0) = l;
        return l;
    } else {
        Leaf *e = NULL;
        Node *p = add_rec(ct, ct->root, key, 0, &e, creator, data);
        if (p != ct->root) ct->root = p;
        return e;
    }
}

/*
 * Delete
 */
Node *del_rec(CompactTrie *ct, Node *n, u_long key, int level, Leaf **result)
{
    u_long ind = KEY2INDEX(key, level);

    if (!NODE_HAS_ARC(n, ind)) return n;
    else if (!NODE_ARC_IS_LEAF(n, ind)) {
        u_long off = NODE_INDEX2OFF(n, ind);
        Node *orig = (Node*)NODE_ENTRY(n, off);
        Node *m = del_rec(ct, orig, key, level+1, result);
        if (m != orig) NODE_ENTRY(n, off) = m;
        return n;
    }
    else {
        u_long off = NODE_INDEX2OFF(n, ind);
        Leaf *l0 = (Leaf*)NODE_ENTRY(n, off);
        u_long k0 = LEAF_KEY(l0);
        if (key != k0) return n;
        
        *result = l0;
        return node_delete(n, ind);
    }
}

Leaf *CompactTrieDelete(CompactTrie *ct, u_long key)
{
    Node *n;
    Leaf *e = NULL;
    if (ct->root == NULL) return NULL;
    n = del_rec(ct, ct->root, key, 0, &e);
    if (n != ct->root) ct->root = n;
    return e;
}

/* 'init' can smash all the contents, but if you want to be more GC-friendly,
   this one clears up all the freed chunks. */
static void clear_rec(CompactTrie *ct, Node *n, 
                      void (*clearer)(Leaf*, void*),
                      void *data)
{
    int i, off;
    int size = Scm__CountBitsInWord(n->emap);
    char is_leaf[MAX_NODE_SIZE];

    for (i=0, off=0; i<MAX_NODE_SIZE; i++) {
        if (NODE_HAS_ARC(n, i)) {
            if (NODE_ARC_IS_LEAF(n, i)) is_leaf[off++] = TRUE;
            else is_leaf[off++] = FALSE;
        }
    }
    for (i=0; i<size; i++) {
        if (is_leaf[i]) clearer((Leaf*)NODE_ENTRY(n, i), data);
        else clear_rec(ct, (Node*)NODE_ENTRY(n, i), clearer, data);
        NODE_ENTRY(n, i) = NULL;
    }
    n->emap = n->lmap = 0;
}

void CompactTrieClear(CompactTrie *ct,
                      void (*clearer)(Leaf*, void*),
                      void *data)
{
    Node *n = ct->root;
    ct->numEntries = 0;
    ct->root = NULL;

    clear_rec(ct, n, clearer, data);
}

/*
 * Debug dump
 */
#if SCM_DEBUG_HELPER
static char digit32(u_int n)
{
    return (n < 10)? (char)(n+'0') : (char)(n-10+'a');
}

#define BUF_SIZE 8

static char *key_dump(u_long key, char *buf) /* buf must be of length 8 */
{
    int i;
    buf[BUF_SIZE-1] = '\0';
    for (i=0; i<BUF_SIZE-1; i++) {
        buf[BUF_SIZE-i-2] = digit32(key&TRIE_MASK);
        key >>= TRIE_SHIFT;
    }
    return buf;
}

static void leaf_dump(ScmPort *out, Leaf *self, int indent,
                      void (*dumper)(ScmPort*, Leaf*, int, void*), void *data)
{
    char keybuf[BUF_SIZE];
    Scm_Printf(out, "LEAF(%s,%x) ", key_dump(LEAF_KEY(self), keybuf),
               LEAF_KEY(self));
    if (dumper) dumper(out, self, indent, data);
    Scm_Printf(out, "\n");
}

static void node_dump(ScmPort *out, Node *n, int level, 
                      void (*dumper)(ScmPort*, Leaf*, int, void*), void *data)
{
    int i;
    
    Scm_Printf(out, "NODE(%p)\n", n);
    for (i=0; i<MAX_NODE_SIZE; i++) {
        if (!NODE_HAS_ARC(n, i)) continue;
        Scm_Printf(out, " %*s%c:", level*2, "", digit32(i));
        if (NODE_ARC_IS_LEAF(n, i)) {
            leaf_dump(out, (Leaf*)NODE_ENTRY(n, NODE_INDEX2OFF(n, i)),
                      level*2+1, dumper, data);
        } else {
            node_dump(out, (Node*)NODE_ENTRY(n, NODE_INDEX2OFF(n, i)),
                      level+1, dumper, data);
        }
    }
}

void CompactTrieDump(ScmPort *out, CompactTrie *ct,
                     void (*dumper)(ScmPort*, Leaf*, int, void*), void *data)
{
    Scm_Printf(out, "CompactTrie(%p, nentries=%d):\n", ct, ct->numEntries);
    if (ct->root == NULL) {
        Scm_Putz("(empty)\n", -1, out);
    } else {
        node_dump(out, ct->root, 0, dumper, data);
    }
}

#endif /*SCM_DEBUG_HELPER*/
