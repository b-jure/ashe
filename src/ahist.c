#include "ahist.h"
#include "autils.h"
#include "aalloc.h"



ASHE_PRIVATE struct a_histnode *newnode(const char *contents)
{
	struct a_histnode *hnode;

	hnode = ashe_malloc(sizeof(*hnode));
	hnode->contents = contents;
	hnode->len = strlen(contents);
	return hnode;
}


ASHE_PRIVATE void freenode(struct a_histnode *node)
{
	ashe_free((void*)node->contents);
	ashe_free(node);
}


ASHE_PRIVATE inline void checknodelimit(struct a_histlist *hl)
{
	struct a_histnode *hnode;

	if (a_unlikely(hl->nnodes >= ASHE_HISTLIMIT)) {
		ashe_assert(hl->nnodes > 0 && hl->tail != NULL);
		hnode = hl->tail;
		hl->tail = hl->tail->next;
		hl->tail->prev = NULL;
		freenode(hnode);
		hl->nnodes--;
	}
}


ASHE_PUBLIC struct a_histnode *ashe_newhisthead(struct a_histlist *hl, const char *contents)
{
	struct a_histnode *hnode;

	checknodelimit(hl);
	hnode = newnode(contents);
	hnode->prev = hl->head;
	hnode->next = NULL;
	if (!hl->head)
		hl->tail = hnode;
	else
		hnode->prev->next = hnode;
	hl->head = hnode;
	hl->nnodes++;
	return hnode;
}


ASHE_PUBLIC struct a_histnode *ashe_newhisttail(struct a_histlist *hl, const char *contents)
{
	struct a_histnode *hnode;

	checknodelimit(hl);
	hnode = newnode(contents);
	hnode->next = hl->tail;
	hnode->prev = NULL;
	if (!hl->tail)
		hl->head = hnode;
	else
		hnode->next->prev = hnode;
	hl->tail = hnode;
	hl->nnodes++;
	return hnode;
}


ASHE_PUBLIC const char *ashe_histprev(struct a_histlist *hl)
{
	if (!hl->current) {
		hl->current = hl->head;
		if (hl->current)
			return hl->current->contents;
	} else if (hl->current->prev) {
		hl->current = hl->current->prev;
		return hl->current->contents;
	}
	return NULL;
}


ASHE_PUBLIC const char *ashe_histnext(struct a_histlist *hl)
{
	if (hl->current && hl->current->next) {
		hl->current = hl->current->next;
		return hl->current->contents;
	}
	return NULL;
}



/* -------------------------------------------------------------------------
 * histbuff
 * ------------------------------------------------------------------------- */

/*
 * Size of 'buffer' in 'histbuff'.
 * The size is equal to 'MAXCMDSIZE' which is the
 * maximum size of command + one byte for newline
 * character (that is how each line is saved inside
 * of history file).
 */
#define HISTBUFFSIZE 	(MAXCMDSIZE + 1)

struct histbuff {
	FILE *fp;
	a_ssize nread; /* unread bytes */
	a_int32 len; /* len of 'buffer' */
	char buffer[HISTBUFFSIZE];
};


ASHE_PRIVATE void inithistbuff(struct histbuff *buff)
{
	buff->fp = NULL;
	buff->len = 0;
	buff->nread = 0;
}


ASHE_PRIVATE void fillhistbuff(struct histbuff *buff)
{
	a_ssize rsize;
	a_int32 saverrno;

	ashe_assert(buff->fp != NULL);
	ashe_assert(buff->nread == 0);

	rsize = HISTBUFFSIZE - buff->len;
	buff->nread = fread(&buff->buffer[buff->len], 1, rsize, buff->fp);
	if (a_unlikely(buff->nread != rsize && ferror(buff->fp))) {
		saverrno = errno; /* in case 'fclose' fails */
		fclose(buff->fp);
		errno = saverrno;
		ashe_panic_libcall("fread");
	}
}


ASHE_PRIVATE const char *gethistline(struct histbuff *buff)
{
	const char *buffp;
	const char *base;
	a_int32 len;

	ashe_assert(buff->len > 0 || buff->nread > 0);

	base = buff->buffer + buff->len;
	buffp = base;
	len = 0;

	while ((buffp = strnchr(buffp, HISTBUFFSIZE - buff->len, '\n'))) {
		buff->len = buffp - buff->buffer;
		len = buffp - base;
		if (!ashe_isescaped(base, len) && !ashe_indq(base, len))
			break;
	}

	if (buffp == NULL) { /* need to read more ? */
		ashe_assert(base != buff->buffer);
		buff->nread = 0;
		len = HISTBUFFSIZE - (base - buff->buffer);
		memmove(buff->buffer, base, len);
		buff->len = len;
		fillhistbuff(buff);
		return gethistline(buff);
	}

	buff->nread -= len;
	return ashe_dupstrn(base, len - 1);
}



/* -------------------------------------------------------------------------
 * Read history file
 * ------------------------------------------------------------------------- */

ASHE_PRIVATE void getrealfilepath(a_arr_char *buffer, const char *filepath)
{
	a_arr_char_push_str(buffer, filepath, strlen(filepath));
	a_arr_char_push(buffer, '\0');
	ashe_expandvars(buffer);
}


ASHE_PRIVATE a_int32 readhistoryfile(struct a_histlist *hl, const char *filepath)
{
	struct histbuff buff;
	a_arr_char filebuff;
	a_int32 status;


	status = 0;
	inithistbuff(&buff);
	a_arr_char_init(&filebuff);

	if (!filepath)
		filepath = ASHE_HISTFILEPATH;

	getrealfilepath(&filebuff, filepath);
	filepath = a_arr_ptr(filebuff);

	if (a_unlikely((buff.fp = fopen(filepath, "r")) == NULL))
		a_defer(-1);

	fillhistbuff(&buff); /* prime buffer */
	while (!feof(buff.fp))
		ashe_newhisttail(hl, gethistline(&buff));

defer:
	if (buff.fp) fclose(buff.fp);
	a_arr_char_free(&filebuff, NULL);
	return status;
}


ASHE_PUBLIC void ashe_inithist(struct a_histlist *hl, const char *filepath, int canfail)
{
	memset(hl, 0, sizeof(*hl));
	if (readhistoryfile(hl, filepath) < 0) {
		if (canfail) {
			ashe_freehistnodes(hl);
			memset(hl, 0, sizeof(*hl));
		} else {
			ashe_panic_libcall("fopen");
		}
	}
}



/* -------------------------------------------------------------------------
 * Save to history file
 * ------------------------------------------------------------------------- */

ASHE_PRIVATE a_int32 savehistoryfile(struct a_histlist *hl, const char *filepath)
{
	FILE *fp;
	a_arr_char buffer;
	struct a_histnode *node;
	size_t len;
	a_int32 status;


	status = 0;
	a_arr_char_init(&buffer);

	/* early return if no history */
	if (!hl->head) return status;

	if (!filepath) filepath = ASHE_HISTFILEPATH;

	getrealfilepath(&buffer, filepath);
	filepath = a_arr_ptr(buffer);

	if (a_unlikely((fp = fopen(filepath, "w")) == NULL))
		a_defer(-1);

	a_arr_len(buffer) = 0;

	for (node = hl->tail; node; node = node->next) {
		len = node->len;
		a_arr_char_push_str(&buffer, node->contents, len++);
		a_arr_char_push_strlit(&buffer, "\n\0");
		if (a_unlikely(fwrite(a_arr_ptr(buffer), 1, len, fp) != len))
			a_defer(-1);
	}

defer:
	if (fp) fclose(fp);
	a_arr_char_free(&buffer, NULL);
	return status;
}



/* -------------------------------------------------------------------------
 * Cleanup
 * ------------------------------------------------------------------------- */

ASHE_PUBLIC void ashe_freehistnodes(struct a_histlist *hl)
{
	struct a_histnode *curr;
	struct a_histnode *prev;

	curr = hl->head;
	while (curr) {
		prev = curr->prev;
		freenode(curr);
		curr = prev;
	}
}


ASHE_PUBLIC void ashe_freehistlist(struct a_histlist *hl, const char *filepath, a_ubyte canfail)
{
	if (savehistoryfile(hl, filepath) < 0 && !canfail)
		ashe_panic("failed writing history file");
	ashe_freehistnodes(hl);
}
