#ifndef FRAMEWORK_LIST_H
#define FRAMEWORK_LIST_H

#include <stdbool.h>
#include <stddef.h>


//根据成员地址取得所属结构体地址
#define FRAMEWORK_CONTAINER_OF(ptr, type, member) \
		(type *)((char *)(ptr) - offsetof(type, member))
/* Common framework list declarations for CM7 and CM4. */
typedef struct FrameworkListNode
{
    struct FrameworkListNode *next;
    struct FrameworkListNode *prev;
} FrameworkListNode_t;


//初始化节点，使前后指针均指向自身
static inline void Framework_ListInit(
    FrameworkListNode_t *node)
{
    node->next = node;
    node->prev = node;
}

//检查以head为头节点的链表是否为空
static inline bool Framework_ListIsEmpty(
    const FrameworkListNode_t *head)
{
	return (head->next == head);
}

//将未入链节点插入链表尾部
static inline void Framework_ListAddTail(
    FrameworkListNode_t *head,
    FrameworkListNode_t *node
)
{
	node->next = head;
	node->prev = head->prev;
	
	head->prev->next = node;
	head->prev = node;
}

//从链表移除节点，并将节点恢复为自环
static inline void Framework_ListRemove(
    FrameworkListNode_t *node)
{
	node->prev->next = node->next;
	node->next->prev = node->prev;

    Framework_ListInit(node);
}
#endif /* FRAMEWORK_LIST_H */
