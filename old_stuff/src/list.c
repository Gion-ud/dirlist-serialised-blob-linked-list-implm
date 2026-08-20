#include <stdlib.h>
#include <string.h>
#include <assert.h>


typedef struct _c_list_node {
    void                   *data;
    struct _c_list_node    *prev_np;
    struct _c_list_node    *next_np;
} list_node_t;

typedef list_node_t list_t;

void list_init(list_t *list_p) {
    if (!list_p) return;
    list_p->prev_np = list_p;
    list_p->next_np = list_p;
}

void list_push_front(list_t *list_p, list_node_t *node_p) {
    if (!list_p || !node_p) return;
    assert(list_p->prev_np && list_p->next_np);
    assert(list_p->prev_np->next_np == list_p);
    assert(list_p->next_np->prev_np == list_p);

    node_p->prev_np = list_p;
    node_p->next_np = list_p->next_np;
    list_p->next_np->prev_np    = node_p;
    list_p->next_np             = node_p;
}

void list_push_back(list_t *list_p, list_node_t *node_p) {
    if (!list_p || !node_p) return;
    assert(list_p->prev_np && list_p->next_np);
    assert(list_p->prev_np->next_np == list_p);
    assert(list_p->next_np->prev_np == list_p);

    node_p->next_np = list_p;
    node_p->prev_np = list_p->prev_np;
    list_p->prev_np->next_np    = node_p;
    list_p->prev_np             = node_p;
}

void list_unlink(list_node_t *node_p) {
    if (!node_p) return;
    assert(node_p->prev_np && node_p->next_np);
    assert(node_p->prev_np->next_np == node_p);
    assert(node_p->next_np->prev_np == node_p);
    node_p->next_np->prev_np = node_p->prev_np;
    node_p->prev_np->next_np = node_p->next_np;
}


