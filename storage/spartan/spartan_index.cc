/*
  Spartan_index.cpp

  This class reads and writes an index file for use with the Spartan data 
  class. The file format is a simple binary storage of the 
  Spartan_index::SDE_INDEX structure. The size of the key can be set via 
  the constructor.
*/
#include "storage/spartan/spartan_index.h"
#include "my_dir.h"
#include "my_dbug.h"
#include "my_alloc.h"
#include "mysql/service_mysql_alloc.h"
#include "mysql/components/services/bits/psi_bits.h"
#include <string.h>


#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <assert.h>

#define IS_LEAF(x) (((uintptr_t)x & 1))
#define SET_LEAF(x) ((void*)((uintptr_t)x | 1))
#define LEAF_RAW(x) ((art_leaf*)((void*)((uintptr_t)x & ~1)))


#ifdef __i386__
    #include <emmintrin.h>
#else
#ifdef __amd64__
    #include <emmintrin.h>
#endif
#endif


Spartan_index::Spartan_index(art_tree *t,int keylen)
{
    t->root = NULL;
    crashed = false;
    t->size = 0;
    max_key_len = keylen;
    index_file = -1;
    block_size = max_key_len + sizeof(long long) + sizeof(int);
}


/* destructor */
Spartan_index::~Spartan_index(void)
{
}

/* create the index file */
int Spartan_index::create_index(char *path, int keylen)
{
  DBUG_ENTER("Spartan_index::create_index");
  open_index(path);
  max_key_len = keylen;
  /* 
    Block size is the key length plus the size of the index
    length variable.
  */
  block_size = max_key_len + sizeof(long long);
  DBUG_RETURN(0);
}

/* open index specified as path (pat+filename) */
int Spartan_index::open_index(char *path)
{
  DBUG_ENTER("Spartan_index::open_index");
  /*
    Open the file with read/write mode,
    create the file if not found, 
    treat file as binary, and use default flags.
  */
  index_file = my_open(path, O_RDWR | O_CREAT | 0, MYF(0));
  if(index_file == -1)
    DBUG_RETURN(errno);
  DBUG_RETURN(0);
}

/**
 * Allocates a node of the given type,
 * initializes to zero and sets the type.
 */
art_node* Spartan_index::alloc_node(uint8_t type) {
    art_node* n;
    switch (type) {
        case NODE4:
            n = (art_node*)calloc(1, sizeof(art_node4));
            break;
        case NODE16:
            n = (art_node*)calloc(1, sizeof(art_node16));
            break;
        case NODE48:
            n = (art_node*)calloc(1, sizeof(art_node48));
            break;
        case NODE256:
            n = (art_node*)calloc(1, sizeof(art_node256));
            break;
        default:
            abort();
    }
    n->type = type;
    return n;
}


void Spartan_index::destroy_node(art_node *n) {
    // Break if null
    if (!n) return;

    // Special case leafs
    if (IS_LEAF(n)) {
        free(LEAF_RAW(n));
        return;
    }

    // Handle each node type
    int i, idx;
    union {
        art_node4 *p1;
        art_node16 *p2;
        art_node48 *p3;
        art_node256 *p4;
    } p;
    switch (n->type) {
        case NODE4:
            p.p1 = (art_node4*)n;
            for (i=0;i<n->num_children;i++) {
                destroy_node(p.p1->children[i]);
            }
            break;

        case NODE16:
            p.p2 = (art_node16*)n;
            for (i=0;i<n->num_children;i++) {
                destroy_node(p.p2->children[i]);
            }
            break;

        case NODE48:
            p.p3 = (art_node48*)n;
            for (i=0;i<256;i++) {
                idx = ((art_node48*)n)->keys[i]; 
                if (!idx) continue; 
                destroy_node(p.p3->children[idx-1]);
            }
            break;

        case NODE256:
            p.p4 = (art_node256*)n;
            for (i=0;i<256;i++) {
                if (p.p4->children[i])
                    destroy_node(p.p4->children[i]);
            }
            break;

        default:
            abort();
    }

    // Free ourself on the way up
    free(n);
}

/**
 * Returns the size of the ART tree.
 */

// #ifndef BROKEN_GCC_C99_INLINE
// extern inline unsigned long int Spartan_index::art_size(art_tree *t);
// #endif

art_node** Spartan_index::find_child(art_node *n, uchar c) {
    int i, mask, bitfield;
    union {
        art_node4 *p1;
        art_node16 *p2;
        art_node48 *p3;
        art_node256 *p4;
    } p;
    switch (n->type) {
        case NODE4:
            p.p1 = (art_node4*)n;
            for (i=0 ; i < n->num_children; i++) {
		/* this cast works around a bug in gcc 5.1 when unrolling loops
		 * https://gcc.gnu.org/bugzilla/show_bug.cgi?id=59124
		 */
                if (((uchar*)p.p1->keys)[i] == c)
                    return &p.p1->children[i];
            }
            break;

        {
        case NODE16:
            p.p2 = (art_node16*)n;

            // support non-86 architectures
            #ifdef __i386__
                // Compare the key to all 16 stored keys
                __m128i cmp;
                cmp = _mm_cmpeq_epi8(_mm_set1_epi8(c),
                        _mm_loadu_si128((__m128i*)p.p2->keys));
                
                // Use a mask to ignore children that don't exist
                mask = (1 << n->num_children) - 1;
                bitfield = _mm_movemask_epi8(cmp) & mask;
            #else
            #ifdef __amd64__
                // Compare the key to all 16 stored keys
                __m128i cmp;
                cmp = _mm_cmpeq_epi8(_mm_set1_epi8(c),
                        _mm_loadu_si128((__m128i*)p.p2->keys));

                // Use a mask to ignore children that don't exist
                mask = (1 << n->num_children) - 1;
                bitfield = _mm_movemask_epi8(cmp) & mask;
            #else
                // Compare the key to all 16 stored keys
                bitfield = 0;
                for (i = 0; i < 16; ++i) {
                    if (p.p2->keys[i] == c)
                        bitfield |= (1 << i);
                }

                // Use a mask to ignore children that don't exist
                mask = (1 << n->num_children) - 1;
                bitfield &= mask;
            #endif
            #endif

            /*
             * If we have a match (any bit set) then we can
             * return the pointer match using ctz to get
             * the index.
             */
            if (bitfield)
                return &p.p2->children[__builtin_ctz(bitfield)];
            break;
        }

        case NODE48:
            p.p3 = (art_node48*)n;
            i = p.p3->keys[c];
            if (i)
                return &p.p3->children[i-1];
            break;

        case NODE256:
            p.p4 = (art_node256*)n;
            if (p.p4->children[c])
                return &p.p4->children[c];
            break;

        default:
            abort();
    }
    return NULL;
}

// Simple inlined if
inline int Spartan_index::min(int a, int b) {
    return (a < b) ? a : b;
}

/**
 * Returns the number of prefix characters shared between
 * the key and node.
 */
int Spartan_index::check_prefix(const art_node *n, const uchar *key, int key_len, int depth) {
    int max_cmp = Spartan_index::min(Spartan_index::min(n->partial_len, MAX_PREFIX_LEN), key_len - depth);
    int idx;
    for (idx=0; idx < max_cmp; idx++) {
        if (n->partial[idx] != key[depth+idx])
            return idx;
    }
    return idx;
}

/**
 * Checks if a leaf matches
 * @return 0 on success.
 */
int Spartan_index::leaf_matches(const art_leaf *n, const uchar *key, int key_len, int depth) {
    (void)depth;
    // Fail if the key lengths are different
    if (n->key_len != (int)key_len) return 1;

    // Compare the keys starting at the depth
    return memcmp(n->key, key, key_len);
}





// Find the minimum leaf under a node
art_leaf* Spartan_index::minimum(const art_node *n) {
    // Handle base cases
    if (!n) return NULL;
    if (IS_LEAF(n)) return LEAF_RAW(n);

    int idx;
    switch (n->type) {
        case NODE4:
            return minimum(((const art_node4*)n)->children[0]);
        case NODE16:
            return minimum(((const art_node16*)n)->children[0]);
        case NODE48:
            idx=0;
            while (!((const art_node48*)n)->keys[idx]) idx++;
            idx = ((const art_node48*)n)->keys[idx] - 1;
            return minimum(((const art_node48*)n)->children[idx]);
        case NODE256:
            idx=0;
            while (!((const art_node256*)n)->children[idx]) idx++;
            return minimum(((const art_node256*)n)->children[idx]);
        default:
            abort();
    }
}

// Find the maximum leaf under a node
art_leaf* Spartan_index::maximum(const art_node *n) {
    // Handle base cases
    if (!n) return NULL;
    if (IS_LEAF(n)) return LEAF_RAW(n);

    int idx;
    switch (n->type) {
        case NODE4:
            return maximum(((const art_node4*)n)->children[n->num_children-1]);
        case NODE16:
            return maximum(((const art_node16*)n)->children[n->num_children-1]);
        case NODE48:
            idx=255;
            while (!((const art_node48*)n)->keys[idx]) idx--;
            idx = ((const art_node48*)n)->keys[idx] - 1;
            return maximum(((const art_node48*)n)->children[idx]);
        case NODE256:
            idx=255;
            while (!((const art_node256*)n)->children[idx]) idx--;
            return maximum(((const art_node256*)n)->children[idx]);
        default:
            abort();
    }
}




/**
 * Returns the minimum valued leaf
 */
art_leaf* Spartan_index::art_minimum(const art_tree *t) {
    return minimum((art_node*)t->root);
}

/**
 * Returns the maximum valued leaf
 */
art_leaf* Spartan_index::art_maximum(const art_tree *t) {
    return maximum((art_node*)t->root);
}

art_leaf* Spartan_index::make_leaf(const uchar *key, int key_len, long long pos) {
    art_leaf *l = (art_leaf*)calloc(1, sizeof(art_leaf)+key_len);
    l->pos = pos;
    l->key_len = key_len;
    // key = (uchar *)my_malloc(PSI_NOT_INSTRUMENTED,key_len, MYF(MY_ZEROFILL | MY_WME));
    memcpy(l->key, key, key_len);
    return l;
}


int Spartan_index::longest_common_prefix(art_leaf *l1, art_leaf *l2, int depth) {
    int max_cmp = Spartan_index::min(l1->key_len, l2->key_len) - depth;
    int idx;
    for (idx=0; idx < max_cmp; idx++) {
        if (l1->key[depth+idx] != l2->key[depth+idx])
            return idx;
    }
    return idx;
}




void Spartan_index::copy_header(art_node *dest, art_node *src) {
    dest->num_children = src->num_children;
    dest->partial_len = src->partial_len;
    memcpy(dest->partial, src->partial, Spartan_index::min(MAX_PREFIX_LEN, src->partial_len));
}

void Spartan_index::add_child256(art_node256 *n, art_node **ref, unsigned char c, void *child) {
    (void)ref;
    n->n.num_children++;
    n->children[c] = (art_node*)child;
}

void Spartan_index::add_child48(art_node48 *n, art_node **ref, unsigned char c, void *child) {
    if (n->n.num_children < 48) {
        int pos = 0;
        while (n->children[pos]) pos++;
        n->children[pos] = (art_node*)child;
        n->keys[c] = pos + 1;
        n->n.num_children++;
    } else {
        art_node256 *new_node = (art_node256*)alloc_node(NODE256);
        for (int i=0;i<256;i++) {
            if (n->keys[i]) {
                new_node->children[i] = n->children[n->keys[i] - 1];
            }
        }
        copy_header((art_node*)new_node, (art_node*)n);
        *ref = (art_node*)new_node;
        free(n);
        add_child256(new_node, ref, c, child);
    }
}

void Spartan_index::add_child16(art_node16 *n, art_node **ref, unsigned char c, void *child) {
    if (n->n.num_children < 16) {
        unsigned mask = (1 << n->n.num_children) - 1;
        
        // support non-x86 architectures
        #ifdef __i386__
            __m128i cmp;

            // Compare the key to all 16 stored keys
            cmp = _mm_cmplt_epi8(_mm_set1_epi8(c),
                    _mm_loadu_si128((__m128i*)n->keys));

            // Use a mask to ignore children that don't exist
            unsigned bitfield = _mm_movemask_epi8(cmp) & mask;
        #else
        #ifdef __amd64__
            __m128i cmp;

            // Compare the key to all 16 stored keys
            cmp = _mm_cmplt_epi8(_mm_set1_epi8(c),
                    _mm_loadu_si128((__m128i*)n->keys));

            // Use a mask to ignore children that don't exist
            unsigned bitfield = _mm_movemask_epi8(cmp) & mask;
        #else
            // Compare the key to all 16 stored keys
            unsigned bitfield = 0;
            for (short i = 0; i < 16; ++i) {
                if (c < n->keys[i])
                    bitfield |= (1 << i);
            }

            // Use a mask to ignore children that don't exist
            bitfield &= mask;    
        #endif
        #endif

        // Check if less than any
        unsigned idx;
        if (bitfield) {
            idx = __builtin_ctz(bitfield);
            memmove(n->keys+idx+1,n->keys+idx,n->n.num_children-idx);
            memmove(n->children+idx+1,n->children+idx,
                    (n->n.num_children-idx)*sizeof(void*));
        } else
            idx = n->n.num_children;

        // Set the child
        n->keys[idx] = c;
        n->children[idx] = (art_node*)child;
        n->n.num_children++;

    } else {
        art_node48 *new_node = (art_node48*)alloc_node(NODE48);

        // Copy the child pointers and populate the key map
        memcpy(new_node->children, n->children,
                sizeof(void*)*n->n.num_children);
        for (int i=0;i<n->n.num_children;i++) {
            new_node->keys[n->keys[i]] = i + 1;
        }
        copy_header((art_node*)new_node, (art_node*)n);
        *ref = (art_node*)new_node;
        free(n);
        add_child48(new_node, ref, c, child);
    }
}

void Spartan_index::add_child4(art_node4 *n, art_node **ref, unsigned char c, void *child) {
    if (n->n.num_children < 4) {
        int idx;
        for (idx=0; idx < n->n.num_children; idx++) {
            if (c < n->keys[idx]) break;
        }

        // Shift to make room
        memmove(n->keys+idx+1, n->keys+idx, n->n.num_children - idx);
        memmove(n->children+idx+1, n->children+idx,
                (n->n.num_children - idx)*sizeof(void*));

        // Insert element
        n->keys[idx] = c;
        n->children[idx] = (art_node*)child;
        n->n.num_children++;

    } else {
        art_node16 *new_node = (art_node16*)alloc_node(NODE16);

        // Copy the child pointers and the key map
        memcpy(new_node->children, n->children,
                sizeof(void*)*n->n.num_children);
        memcpy(new_node->keys, n->keys,
                sizeof(unsigned char)*n->n.num_children);
        copy_header((art_node*)new_node, (art_node*)n);
        *ref = (art_node*)new_node;
        free(n);
        add_child16(new_node, ref, c, child);
    }
}

void Spartan_index::add_child(art_node *n, art_node **ref, unsigned char c, void *child) {
    switch (n->type) {
        case NODE4:
            return add_child4((art_node4*)n, ref, c, child);
        case NODE16:
            return add_child16((art_node16*)n, ref, c, child);
        case NODE48:
            return add_child48((art_node48*)n, ref, c, child);
        case NODE256:
            return add_child256((art_node256*)n, ref, c, child);
        default:
            abort();
    }
}

/**
 * Calculates the index at which the prefixes mismatch
 */
int Spartan_index::prefix_mismatch(const art_node *n, const uchar *key, int key_len, int depth) {
    int max_cmp = Spartan_index::min(Spartan_index::min(MAX_PREFIX_LEN, n->partial_len), key_len - depth);
    int idx;
    for (idx=0; idx < max_cmp; idx++) {
        if (n->partial[idx] != key[depth+idx])
            return idx;
    }

    // If the prefix is short we can avoid finding a leaf
    if (n->partial_len > MAX_PREFIX_LEN) {
        // Prefix is longer than what we've checked, find a leaf
        art_leaf *l = minimum(n);
        max_cmp = Spartan_index::min(l->key_len, key_len)- depth;
        for (; idx < max_cmp; idx++) {
            if (l->key[idx+depth] != key[depth+idx])
                return idx;
        }
    }
    return idx;
}



long long Spartan_index::recursive_insert(art_node *n, art_node **ref, const uchar *key, int key_len, long long pos, int depth, int *old, int replace) {
    // If we are at a NULL node, inject a leaf
    if (!n) {
        *ref = (art_node*)SET_LEAF(make_leaf(key, key_len, pos));
        return 0;
    }

    // If we are at a leaf, we need to replace it with a node
    if (IS_LEAF(n)) {
        art_leaf *l = LEAF_RAW(n);

        // Check if we are updating an existing value
        if (!leaf_matches(l, key, key_len, depth)) {
            *old = 1;
            long long old_val = l->pos;
            if(replace) l->pos = pos;
            return old_val;
        }

        // New value, we must split the leaf into a node4
        art_node4 *new_node = (art_node4*)alloc_node(NODE4);

        // Create a new leaf
        art_leaf *l2 = make_leaf(key, key_len, pos);

        // Determine longest prefix
        int longest_prefix = longest_common_prefix(l, l2, depth);
        new_node->n.partial_len = longest_prefix;
        memcpy(new_node->n.partial, key+depth, Spartan_index::min(MAX_PREFIX_LEN, longest_prefix));
        // Add the leafs to the new node4
        *ref = (art_node*)new_node;
        add_child4(new_node, ref, l->key[depth+longest_prefix], SET_LEAF(l));
        add_child4(new_node, ref, l2->key[depth+longest_prefix], SET_LEAF(l2));
        return 0;
    }

    // Check if given node has a prefix
    if (n->partial_len) {
        // Determine if the prefixes differ, since we need to split
        int prefix_diff = prefix_mismatch(n, key, key_len, depth);
        if ((uint32_t)prefix_diff >= n->partial_len) {
            depth += n->partial_len;
            goto RECURSE_SEARCH;
        }

        // Create a new node
        art_node4 *new_node = (art_node4*)alloc_node(NODE4);
        *ref = (art_node*)new_node;
        new_node->n.partial_len = prefix_diff;
        memcpy(new_node->n.partial, n->partial, Spartan_index::min(MAX_PREFIX_LEN, prefix_diff));

        // Adjust the prefix of the old node
        if (n->partial_len <= MAX_PREFIX_LEN) {
            add_child4(new_node, ref, n->partial[prefix_diff], n);
            n->partial_len -= (prefix_diff+1);
            memmove(n->partial, n->partial+prefix_diff+1,
                    Spartan_index::min(MAX_PREFIX_LEN, n->partial_len));
        } else {
            n->partial_len -= (prefix_diff+1);
            art_leaf *l = minimum(n);
            add_child4(new_node, ref, l->key[depth+prefix_diff], n);
            memcpy(n->partial, l->key+depth+prefix_diff+1,
                    Spartan_index::min(MAX_PREFIX_LEN, n->partial_len));
        }

        // Insert the new leaf
        art_leaf *l = make_leaf(key, key_len, pos);
        add_child4(new_node, ref, key[depth+prefix_diff], SET_LEAF(l));
        return 0;
    }

RECURSE_SEARCH:;

    // Find a child to recurse to
    art_node **child = find_child(n, key[depth]);
    if (child) {
        return recursive_insert(*child, child, key, key_len, pos, depth+1, old, replace);
    }

    // No child, node goes within us
    art_leaf *l = make_leaf(key, key_len, pos);
    add_child(n, ref, key[depth], SET_LEAF(l));
    return 0;
}




/* insert a key into the index in memory */
long long Spartan_index::insert_key(art_tree *t, const uchar *key, long long pos , int length)
{
  int old_val = 0;
  int key_len = length;
  long long old = recursive_insert(t->root, &t->root, key, key_len, pos, 0, &old_val, 0);
  if (!old_val) t->size++;
  return old;
}



void Spartan_index::remove_child256(art_node256 *n, art_node **ref, unsigned char c) {
    n->children[c] = NULL;
    n->n.num_children--;

    // Resize to a node48 on underflow, not immediately to prevent
    // trashing if we sit on the 48/49 boundary
    if (n->n.num_children == 37) {
        art_node48 *new_node = (art_node48*)alloc_node(NODE48);
        *ref = (art_node*)new_node;
        copy_header((art_node*)new_node, (art_node*)n);

        int pos = 0;
        for (int i=0;i<256;i++) {
            if (n->children[i]) {
                new_node->children[pos] = n->children[i];
                new_node->keys[i] = pos + 1;
                pos++;
            }
        }
        free(n);
    }
}

void Spartan_index::remove_child48(art_node48 *n, art_node **ref, unsigned char c) {
    int pos = n->keys[c];
    n->keys[c] = 0;
    n->children[pos-1] = NULL;
    n->n.num_children--;

    if (n->n.num_children == 12) {
        art_node16 *new_node = (art_node16*)alloc_node(NODE16);
        *ref = (art_node*)new_node;
        copy_header((art_node*)new_node, (art_node*)n);

        int child = 0;
        for (int i=0;i<256;i++) {
            pos = n->keys[i];
            if (pos) {
                new_node->keys[child] = i;
                new_node->children[child] = n->children[pos - 1];
                child++;
            }
        }
        free(n);
    }
}

void Spartan_index::remove_child16(art_node16 *n, art_node **ref, art_node **l) {
    int pos = l - n->children;
    memmove(n->keys+pos, n->keys+pos+1, n->n.num_children - 1 - pos);
    memmove(n->children+pos, n->children+pos+1, (n->n.num_children - 1 - pos)*sizeof(void*));
    n->n.num_children--;

    if (n->n.num_children == 3) {
        art_node4 *new_node = (art_node4*)alloc_node(NODE4);
        *ref = (art_node*)new_node;
        copy_header((art_node*)new_node, (art_node*)n);
        memcpy(new_node->keys, n->keys, 4);
        memcpy(new_node->children, n->children, 4*sizeof(void*));
        free(n);
    }
}

void Spartan_index::remove_child4(art_node4 *n, art_node **ref, art_node **l) {
    int pos = l - n->children;
    memmove(n->keys+pos, n->keys+pos+1, n->n.num_children - 1 - pos);
    memmove(n->children+pos, n->children+pos+1, (n->n.num_children - 1 - pos)*sizeof(void*));
    n->n.num_children--;

    // Remove nodes with only a single child
    if (n->n.num_children == 1) {
        art_node *child = n->children[0];
        if (!IS_LEAF(child)) {
            // Concatenate the prefixes
            int prefix = n->n.partial_len;
            if (prefix < MAX_PREFIX_LEN) {
                n->n.partial[prefix] = n->keys[0];
                prefix++;
            }
            if (prefix < MAX_PREFIX_LEN) {
                int sub_prefix = Spartan_index::min(child->partial_len, MAX_PREFIX_LEN - prefix);
                memcpy(n->n.partial+prefix, child->partial, sub_prefix);
                prefix += sub_prefix;
            }

            // Store the prefix in the child
            memcpy(child->partial, n->n.partial, Spartan_index::min(prefix, MAX_PREFIX_LEN));
            child->partial_len += n->n.partial_len + 1;
        }
        *ref = child;
        free(n);
    }
}

void Spartan_index::remove_child(art_node *n, art_node **ref, unsigned char c, art_node **l) {
    switch (n->type) {
        case NODE4:
            return remove_child4((art_node4*)n, ref, l);
        case NODE16:
            return remove_child16((art_node16*)n, ref, l);
        case NODE48:
            return remove_child48((art_node48*)n, ref, c);
        case NODE256:
            return remove_child256((art_node256*)n, ref, c);
        default:
            abort();
    }
}


art_leaf* Spartan_index::recursive_delete(art_node *n, art_node **ref, const uchar *key, int key_len, int depth) {
    // Search terminated
    if (!n) return NULL;

    // Handle hitting a leaf node
    if (IS_LEAF(n)) {
        art_leaf *l = LEAF_RAW(n);
        if (!leaf_matches(l, key, key_len, depth)) {
            *ref = NULL;
            return l;
        }
        return NULL;
    }

    // Bail if the prefix does not match
    if (n->partial_len) {
        int prefix_len = check_prefix(n, key, key_len, depth);
        if (prefix_len != Spartan_index::min(MAX_PREFIX_LEN, n->partial_len)) {
            return NULL;
        }
        depth = depth + n->partial_len;
    }

    // Find child node
    art_node **child = find_child(n, key[depth]);
    if (!child) return NULL;

    // If the child is leaf, delete from this node
    if (IS_LEAF(*child)) {
        art_leaf *l = LEAF_RAW(*child);
        if (!leaf_matches(l, key, key_len, depth)) {
            remove_child(n, ref, key[depth], child);
            return l;
        }
        return NULL;

    // Recurse
    } else {
        return recursive_delete(*child, child, key, key_len, depth+1);
    }
}

long long  Spartan_index::delete_key(art_tree *t, const uchar *key, int key_len){
  art_leaf *l = recursive_delete(t->root, &t->root, key, key_len, 0);
  if (l) {
    t->size--;
    long long old = l->pos;
    free(l);
    return old;
  }
  return 0;
}

uchar* Spartan_index::get_first_key(const art_tree *t) {
    art_leaf *l = art_minimum(t);
    if (l) {
        return l->key;
    }
    return NULL;
}

uchar* Spartan_index::get_last_key(const art_tree *t) {
    art_leaf *l = art_maximum(t);
    if (l) {
        return l->key;
    }
    return NULL;
}



long long Spartan_index::get_first_pos(const art_tree *t) {
    art_leaf *l = art_minimum(t);
    if (l) {
        return l->pos;
    }
    return NULL;
}

long long Spartan_index::get_last_pos(const art_tree *t) {
    art_leaf *l = art_maximum(t);
    if (l) {
        return l->pos;
    }
    return NULL;
}

art_leaf *Spartan_index::seek_index(const art_tree *t, const uchar *key, int key_len){
  art_node **child;
  art_node *n = t->root;
  int prefix_len, depth = 0;
  while (n) {
      // Might be a leaf
      if (IS_LEAF(n)) {
          n = (art_node*)LEAF_RAW(n);
          // Check if the expanded path matches
          if (!leaf_matches((art_leaf*)n, key, key_len, depth)) {
              return ((art_leaf*)n);
          }
          return NULL;
      }

      // Bail if the prefix does not match
      if (n->partial_len) {
          prefix_len = check_prefix(n, key, key_len, depth);
          if (prefix_len != min(MAX_PREFIX_LEN, n->partial_len))
              return NULL;
          depth = depth + n->partial_len;
      }

      // Recursively search
      child = find_child(n, key[depth]);
      n = (child) ? *child : NULL;
      depth++;
  }
  return NULL;
}

long long Spartan_index::get_index_pos(const art_tree *t, const uchar *key, int key_len){
  art_node **child;
  art_node *n = t->root;
  int prefix_len, depth = 0;
  while (n) {
      // Might be a leaf
      if (IS_LEAF(n)) {
          n = (art_node*)LEAF_RAW(n);
          // Check if the expanded path matches
          if (!leaf_matches((art_leaf*)n, key, key_len, depth)) {
              return ((art_leaf*)n)->pos;
          }
          return 0;
      }

      // Bail if the prefix does not match
      if (n->partial_len) {
          prefix_len = check_prefix(n, key, key_len, depth);
          if (prefix_len != min(MAX_PREFIX_LEN, n->partial_len))
              return 0;
          depth = depth + n->partial_len;
      }

      // Recursively search
      child = find_child(n, key[depth]);
      n = (child) ? *child : NULL;
      depth++;
  }
  return 0;
}

long long Spartan_index::update_key(art_tree *t, uchar *key, int key_len, long long pos) {
    // 首先，找到给定键的叶节点
    art_leaf *l = seek_index(t, key, key_len);

    // 如果叶节点存在，更新其位置并返回旧位置
    if (l) {
        long long old = l->pos;
        l->pos = pos;
        // key = (uchar *)my_malloc(PSI_NOT_INSTRUMENTED,key_len, MYF(MY_ZEROFILL | MY_WME));
        // memcpy(l->key, key, key_len);
        // l->key_len = key_len;
        return old;
    }

    // 如果叶节点不存在，利用现有的insert_key函数进行插入操作
    else {
        return insert_key(t, key, pos, key_len);
    }
}

int Spartan_index::destroy_index(art_tree *t) {
  DBUG_ENTER("Spartan_index::destroy_index");
  destroy_node(t->root);
  DBUG_RETURN(0);
}