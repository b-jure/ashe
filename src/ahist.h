#ifndef AHIST_H
#define AHIST_H

#include "acommon.h"


#define resethistcurrent() 	(ashe.history.current = NULL)


 /* commands history */
struct a_histnode {
	struct a_histnode *prev;
	struct a_histnode *next;
	const char *contents;
	a_int32 len; /* len of 'contents' */
};


/* list of commands */
struct a_histlist {
	a_memmax nnodes; /* total number of nodes in this list */
	struct a_histnode *head;
	struct a_histnode *tail;
	struct a_histnode *current;
};


struct a_histnode *ashe_newhisthead(struct a_histlist *hl, const char *contents);
struct a_histnode *ashe_newhisttail(struct a_histlist *hl, const char *contents);
const char *ashe_histprev(struct a_histlist *hl);
const char *ashe_histnext(struct a_histlist *hl);
void ashe_inithist(struct a_histlist *hl, const char *filepath, int canfail);
void ashe_freehistlist(struct a_histlist *hl, const char *filepath, a_ubyte canfail);
void ashe_freehistnodes(struct a_histlist *hl);

#endif
