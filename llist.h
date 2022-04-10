#ifndef LLIST_H
#define LLIST_H

#include <linux/stddef.h>
#include <linux/types.h>

#include <stdatomic.h>
#include <stddef.h>

#define ACCESS_ONCE(x) (*(volatile __typeof(x) *)&(x))

#define READ_ONCE(x) \
({ __typeof(x) ___x = ACCESS_ONCE(x); ___x; })

#define bool int

#define container_of(ptr, type, member) ({                      \
        const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
        (type *)( (char *)__mptr - offsetof(type,member) );})

struct llist_head {
    struct llist_node *first;
};

struct llist_node {
    struct llist_node *next;
};

static inline void init_llist_head(struct llist_head *list)
{
    list->first = NULL;
}

#define llist_entry(ptr, type, member)      \
    container_of(ptr, type, member)

static inline bool llist_empty(const struct llist_head *head)
{
    return READ_ONCE(head->first) == NULL;
}

bool llist_add_batch(struct llist_node *new_first,
                struct llist_node *new_last,
                struct llist_head *head);

static inline bool llist_add(struct llist_node *new, struct llist_head *head)
{
    return llist_add_batch(new, new, head);
}

struct llist_node *llist_del_first(struct llist_head *head);

#endif /* LLIST_H */
