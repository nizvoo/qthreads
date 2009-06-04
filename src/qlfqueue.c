// This lock-free algorithm borrowed from
// http://www.research.ibm.com/people/m/michael/podc-1996.pdf

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include <stdlib.h>		       /* for malloc() */
#include <qthread/qthread.h>
#include <qthread/qlfqueue.h>

#include <qthread/qpool.h>
#include "qthread_asserts.h"

/* queue declarations */
typedef struct _qlfqueue_node
{
    void *value;
    volatile struct _qlfqueue_node * volatile next;
} qlfqueue_node_t;

struct qlfqueue_s		/* typedef'd to qlfqueue_t */
{
    volatile qlfqueue_node_t * volatile head;
    volatile qlfqueue_node_t * volatile tail;
};

static qpool *qlfqueue_node_pool = NULL;

/* to avoid ABA reinsertion trouble, each pointer in the queue needs to have a
 * monotonically increasing counter associated with it. The counter doesn't
 * need to be huge, just big enough to avoid trouble. We'll
 * just claim 4, to be conservative. Thus, a qlfqueue_node_t must be aligned to
 * 16 bytes. */
#if defined(QTHREAD_USE_VALGRIND) && NO_ABA_PROTECTION
# define QPTR(x) (x)
# define QCTR(x) 0
# define QCOMPOSE(x,y) (x)
#else
# define QCTR_MASK (15)
# define QPTR(x) ((volatile qlfqueue_node_t*volatile)(((volatile uintptr_t)(x))&~(uintptr_t)QCTR_MASK))
# define QCTR(x) (((volatile uintptr_t)(x))&QCTR_MASK)
# define QCOMPOSE(x,y) (void*)(((volatile uintptr_t)QPTR(x))|((QCTR(y)+1)&QCTR_MASK))
#endif

/* to avoid compiler bugs regarding volatile... */
static Q_NOINLINE volatile qlfqueue_node_t * volatile * vol_id_qlfqn(volatile qlfqueue_node_t*volatile*ptr)
{/*{{{*/
    return ptr;
}/*}}}*/
#define _(x) *vol_id_qlfqn(&(x))

qlfqueue_t *qlfqueue_create(void)
{				       /*{{{ */
    qlfqueue_t *q;

    if (qlfqueue_node_pool == NULL) {
	qlfqueue_node_pool =
	    qpool_create_aligned(sizeof(qlfqueue_node_t), 16);
    }
    assert(qlfqueue_node_pool != NULL);

    q = malloc(sizeof(struct qlfqueue_s));
    if (q != NULL) {
	_(q->head) = (volatile qlfqueue_node_t *) qpool_alloc(NULL, qlfqueue_node_pool);
	assert(_(q->head) != NULL);
	if (QPTR(_(q->head)) == NULL) {   // if we're not using asserts, fail nicely
	    free(q);
	    q = NULL;
	}
	_(q->tail) = _(q->head);
	_(QPTR(_(q->tail))->next) = NULL;
    }
    return q;
}				       /*}}} */

int qlfqueue_destroy(qthread_t *me, qlfqueue_t * q)
{				       /*{{{ */
    qargnonull(q);
    while (QPTR(_(q->head)) != QPTR(_(q->tail))) {
	qlfqueue_dequeue(me, q);
    }
    qpool_free(me, qlfqueue_node_pool, (void*)(QPTR(_(q->head))));
    free(q);
    return QTHREAD_SUCCESS;
}				       /*}}} */

int qlfqueue_enqueue(qthread_t *me, qlfqueue_t * q, void *elem)
{				       /*{{{ */
    volatile qlfqueue_node_t * tail;
    volatile qlfqueue_node_t * next;
    qlfqueue_node_t * node;

    qargnonull(elem);
    qargnonull(q);
    qargnonull(me);

    node = (qlfqueue_node_t *) qpool_alloc(me, qlfqueue_node_pool);
    // these asserts should be redundant
    assert(node != NULL);
    assert(QCTR(node) == 0);	// node MUST be aligned

    memset((void*)node, 0, sizeof(qlfqueue_node_t));
    node->value = elem;

    while (1) {
	tail = _(q->tail);
	next = _(QPTR(tail)->next);
	if (tail == _(q->tail)) {	       // are tail and next consistent?
	    if (QPTR(next) == NULL) {  // was tail pointing to the last node?
		if (qthread_cas_ptr
		    (&(QPTR(tail)->next), next,
		     QCOMPOSE(node, next)) == next)
		    break;	       // success!
	    } else {		       // tail not pointing to last node
		(void)qthread_cas_ptr(&(q->tail), tail,
			     QCOMPOSE(next, tail));
	    }
	}
    }
    (void)qthread_cas_ptr(&(q->tail), tail, QCOMPOSE(node, tail));
    return QTHREAD_SUCCESS;
}				       /*}}} */

void *qlfqueue_dequeue(qthread_t *me, qlfqueue_t * q)
{				       /*{{{ */
    void *p = NULL;
    volatile qlfqueue_node_t * head;
    volatile qlfqueue_node_t * tail;
    volatile qlfqueue_node_t * next;

    assert(q != NULL);
    if (q == NULL) {
	return NULL;
    }
    while (1) {
	head = _(q->head);
	tail = _(q->tail);
	next = _(QPTR(head)->next);
	if (head == _(q->head)) {	       // are head, tail, and next consistent?
	    if (QPTR(head) == QPTR(tail)) {	// is queue empty or tail falling behind?
		if (QPTR(next) == NULL) {	// is queue empty?
		    return NULL;
		}
		(void)qthread_cas_ptr(&(q->tail), tail, QCOMPOSE(next, tail));	// advance tail ptr
	    } else if (QPTR(next) != NULL) {		       // no need to deal with tail
		// read value before CAS, otherwise another dequeue might free the next node
		p = QPTR(next)->value;
		if (qthread_cas_ptr (&(q->head), head, QCOMPOSE(next, head)) ==
			head) {
		    break;	       // success!
		}
	    }
	}
    }
    qpool_free(me, qlfqueue_node_pool, (void*)(QPTR(head)));
    return p;
}				       /*}}} */

int qlfqueue_empty(qlfqueue_t * q)
{				       /*{{{ */
    volatile qlfqueue_node_t * head;
    volatile qlfqueue_node_t * tail;
    volatile qlfqueue_node_t * next;

    assert(q != NULL);
    if (q == NULL) {
	return 1;
    }

    while (1) {
	head = _(q->head);
	tail = _(q->tail);
	next = _(QPTR(head)->next);
	if (head == _(q->head)) {	       // are head, tail, and next consistent?
	    if (QPTR(head) == QPTR(tail)) {	// is queue empty or tail falling behind?
		if (QPTR(next) == NULL) {	// queue is empty!
		    return 1;
		} else {	       // tail falling behind (queue NOT empty)
		    return 0;
		}
	    } else {		       // queue is NOT empty and tail is NOT falling behind
		return 0;
	    }
	}
    }
}				       /*}}} */
