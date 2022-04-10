#include "llist.h"

bool llist_add_batch(struct llist_node *new_first, struct llist_node *new_last,
             struct llist_head *head)
{
    struct llist_node *first;

    do {
        new_last->next = first = READ_ONCE(head->first);
    } while (!atomic_compare_exchange_weak(&head->first, first, new_first));

    return !first;
}

struct llist_node *llist_del_first(struct llist_head *head)
{
    struct llist_node *entry, *old_entry, *next;

    do {
        entry = atomic_load(&head->first);
        if (entry == NULL)
            return NULL;
        old_entry = entry;
        next = READ_ONCE(entry->next);
    } while(!atomic_compare_exchange_weak(&head->first, old_entry, next));

    return entry;
}
