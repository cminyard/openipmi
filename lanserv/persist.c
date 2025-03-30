/*
 * persist.c
 *
 * MontaVista IPMI LAN server persistence tool
 *
 * Author: MontaVista Software, LLC.
 *         Corey Minyard <minyard@mvista.com>
 *         source@mvista.com
 *
 * Copyright 2012 MontaVista Software LLC.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * Lesser General Public License (GPL) Version 2 or the modified BSD
 * license below.  The following disclamer applies to both licenses:
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 *  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 *  OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 *  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 *  TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 *  USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * GNU Lesser General Public Licence
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2 of
 *  the License, or (at your option) any later version.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this program; if not, write to the Free
 *  Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Modified BSD Licence
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *   3. The name of the author may not be used to endorse or promote
 *      products derived from this software without specific prior
 *      written permission.
 */

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdarg.h>
#include <OpenIPMI/persist.h>

enum pitem_type {
    PITEM_DATA = 'd',
    PITEM_INT = 'i',
    PITEM_STR = 's'
};

struct pitem {
    char *iname;
    enum pitem_type type;
    void *data;
    long dval;
    struct pitem *next;
};

struct persist_s {
    struct sys_data_s *sys;
    char *name;

    struct pitem *items;
};

int persist_enable = 1;

static char *app = NULL;
static const char *basedir;

int
persist_init(struct sys_data_s *sys,
	     const char *papp, const char *instance, const char *ibasedir)
{
    unsigned int len;
    char *dname;
    struct stat st;
    char *n;
    int rv = 0;

    if (!persist_enable)
	return 0;

    if (app)
	return EBUSY;
    
    basedir = ibasedir;

    len = strlen(papp) + strlen(instance) + 2;
    app = sys->alloc(sys, len);
    if (!app)
	return ENOMEM;
    strcpy(app, papp);
    strcat(app, "/");
    strcat(app, instance);

    len = strlen(basedir) + strlen(app) + 3;
    dname = sys->alloc(sys, len);
    if (!dname) {
	sys->free(sys, app);
	return ENOMEM;
    }
    strcpy(dname, basedir);
    strcat(dname, "/");
    strcat(dname, app);
    strcat(dname, "/");
    if (dname[0] == '/')
	n = strchr(dname + 1, '/');
    else
	n = strchr(dname, '/');
    while (n) {
	*n = '\0';
	if (stat(dname, &st) != 0) {
	    if (mkdir(dname, 0755) != 0) {
		rv = errno;
		goto out;
	    }
	} else if (!S_ISDIR(st.st_mode)) {
	    rv = ENOTDIR;
	    goto out;
	}
	*n++ = '/';
	n = strchr(n, '/');
    }
 out:
    sys->free(sys, dname);
    return rv;
}

static char *
do_va_nameit(struct sys_data_s *sys, const char *name, va_list ap)
{
    unsigned int len;
    va_list aq;
    char dummy;
    char *rv;

    va_copy(aq, ap);
    len = vsnprintf(&dummy, 1, name, aq);
    va_end(aq);
    rv = sys->alloc(sys, len + 1);
    if (!rv)
	return NULL;
    vsprintf(rv, name, ap);
    return rv;
}

persist_t *
alloc_vpersist(struct sys_data_s *sys, const char *iname, va_list ap)
{
    persist_t *p = sys->alloc(sys, sizeof(*p));

    if (!p)
	return NULL;
    p->sys = sys;
    p->name = do_va_nameit(sys, iname, ap);
    if (!p->name) {
	sys->free(sys, p);
	return NULL;
    }
    p->items = NULL;
    return p;
}

persist_t *
alloc_persist(struct sys_data_s *sys, const char *name, ...)
{
    persist_t *p;
    va_list ap;

    va_start(ap, name);
    p = alloc_vpersist(sys, name, ap);
    va_end(ap);
    return p;
}

static char *
get_fname(persist_t *p, char *sfx)
{
    int len = (strlen(basedir) + strlen(app) + strlen(p->name)
	       + strlen(sfx) + 3);
    char *fname = p->sys->alloc(p->sys, len);

    if (!fname)
	return NULL;
    strcpy(fname, basedir);
    strcat(fname, "/");
    strcat(fname, app);
    strcat(fname, "/");
    strcat(fname, p->name);
    strcat(fname, sfx);

    return fname;
}

static unsigned char
fromhex(char c)
{
    if (c >= '0' && c <= '9')
	return c - '0';
    if (c >= 'A' && c <= 'Z')
	return (c - 'A' + 10) & 0xf;
    return c - 'a' + 10;
}

static void *
read_data(struct sys_data_s *sys, char *l, long *rsize, int isstr)
{
    int size = 0;
    char *c;
    unsigned char *r, *p;

    for (c = l; *c && *c != '\n'; c++) {
	if (*c == '\\') {
	    c++;
	    if (!isxdigit(*c))
		return NULL;
	    c++;
	    if (!isxdigit(*c))
		return NULL;
	}
	size++;
    }
    r = sys->alloc(sys, size + isstr);
    if (!r)
	return NULL;
    *rsize = size;

    for (c = l, p = r; *c && *c != '\n'; c++, p++) {
	if (*c == '\\') {
	    *p = (fromhex(*(c + 1)) << 4) | fromhex(*(c + 2));
	    c += 2;
	} else {
	    *p = *c;
	}
    }
    if (isstr)
	*p = '\0';
    return r;
}

static void
write_data(void *idata, unsigned int len, FILE *f)
{
    unsigned char *d = idata;
    unsigned int i;

    for (i = 0; i < len; i++, d++) {
	if (isprint(*d) && (*d != '\\'))
	    fputc(*d, f);
	else
	    fprintf(f, "\\%2.2x", *d);
    }
}

persist_t *
read_persist(struct sys_data_s *sys, const char *name, ...)
{
    char *fname;
    va_list ap;
    persist_t *p = NULL;
    FILE *f;
    char *line;
    char *end;
    size_t n;

    if (!persist_enable)
	return NULL;

    va_start(ap, name);
    p = alloc_vpersist(sys, name, ap);
    if (!p)
	goto out_err;
    fname = get_fname(p, "");
    if (!fname)
	goto out_err;
    f = fopen(fname, "r");
    sys->free(sys, fname);
    if (!f)
	goto out_err;

    for (line = NULL; getline(&line, &n, f) != -1;
	 sys->free(sys, line), line = NULL) {
	char *name = line;
	char *type = strchr(name, ':');
	char *val;
	struct pitem *pi;

	if (!type)
	    continue;
	*type++ = '\0';
	if (strlen(name) == 0 || !*type || *(type + 1) != ':')
	    continue;
	*(type + 1) = '\0';
	val = type + 2;

	pi = sys->alloc(sys, sizeof(*pi));
	if (!pi) {
	    sys->free(sys, line);
	    goto out_err;
	}

	pi->iname = sys_strdup(sys, name);
	if (!pi->iname) {
	    sys->free(sys, pi);
	    sys->free(sys, line);
	    goto out_err;
	}
	pi->type = type[0];

	switch (type[0]) {
	case PITEM_DATA:
	    pi->data = read_data(sys, val, &pi->dval, 0);
	    if (!pi->data)
		goto bad_data;
	    break;
	case PITEM_INT:
	    pi->data = NULL;
	    pi->dval = strtol(val, &end, 0);
	    if (*end != '\n' && *end != '\0')
		goto bad_data;
	    break;
	case PITEM_STR:
	    pi->data = read_data(sys, val, &pi->dval, 1);
	    if (!pi->data)
		goto bad_data;
	    break;
	bad_data:
	default:
	    sys->free(sys, pi->iname);
	    sys->free(sys, pi);
	    continue;
	}

	pi->next = p->items;
	p->items = pi;
    }

    va_end(ap);
    return p;
 out_err:
    if (p)
	free_persist(p);
    va_end(ap);
    return NULL;
}

int
write_persist_file(persist_t *p, FILE *f)
{
    struct pitem *pi;

    for (pi = p->items; pi; pi = pi->next) {
	fprintf(f, "%s:%c:", pi->iname, pi->type);
	switch (pi->type) {
	case PITEM_DATA:
	case PITEM_STR:
	    write_data(pi->data, pi->dval, f);
	    break;
	case PITEM_INT:
	    fprintf(f, "%ld", pi->dval);
	}
	fputc('\n', f);
    }
    return 0;
}

int
write_persist(persist_t *p)
{
    char *fname, *fname2;
    int rv = 0;
    FILE *f;

    if (!persist_enable)
	return 0;

    fname = get_fname(p, ".tmp");
    if (!fname) {
	return ENOMEM;
    }

    fname2 = get_fname(p, "");
    if (!fname2) {
	p->sys->free(p->sys, fname);
	return ENOMEM;
    }

    f = fopen(fname, "w");
    if (!f) {
	p->sys->free(p->sys, fname);
	p->sys->free(p->sys, fname2);
	return ENOMEM;
    }

    write_persist_file(p, f);
    fclose(f);

    if (rename(fname, fname2) != 0)
	rv = errno;

    p->sys->free(p->sys, fname);
    p->sys->free(p->sys, fname2);

    return rv;
}

int
iterate_persist(persist_t *p,
		void *cb_data,
		int (*data_func)(const char *name,
				 void *data, unsigned int len,
				 void *cb_data),
		int (*int_func)(const char *name,
				long val, void *cb_data))
{
    struct pitem *pi;
    int rv = 0;

    for (pi = p->items; pi; pi = pi->next) {
	rv = ITER_PERSIST_CONTINUE;
	switch (pi->type) {
	case PITEM_DATA:
	case PITEM_STR:
	    if (data_func)
		rv = data_func(pi->iname, pi->data, pi->dval, cb_data);
	    break;

	case PITEM_INT:
	    if (int_func)
		rv = int_func(pi->iname, pi->dval, cb_data);
	    break;
	}

	if (rv != ITER_PERSIST_CONTINUE)
	    return rv;
    }

    return rv;
}

void
free_persist(persist_t *p)
{
    struct pitem *pi;

    while (p->items) {
	pi = p->items;
	p->items = pi->next;

	if (pi->data)
	    p->sys->free(p->sys, pi->data);
	p->sys->free(p->sys, pi->iname);
	p->sys->free(p->sys, pi);
    }
    p->sys->free(p->sys, p->name);
    p->sys->free(p->sys, p);
}

static int
alloc_pi(persist_t *p, enum pitem_type type, const void *data, long len,
	 const char *iname, va_list ap)
{
    struct pitem *pi;

    pi = p->sys->alloc(p->sys, sizeof(*pi));
    if (!pi)
	return ENOMEM;

    pi->type = type;
    pi->iname = do_va_nameit(p->sys, iname, ap);
    if (!pi->iname) {
	p->sys->free(p->sys, pi);
	return ENOMEM;
    }

    if (data) {
	pi->data = p->sys->alloc(p->sys, len);
	if (!pi->data) {
	    p->sys->free(p->sys, pi->iname);
	    p->sys->free(p->sys, pi);
	    return ENOMEM;
	}
	memcpy(pi->data, data, len);
    } else {
	pi->data = NULL;
    }

    pi->dval = len;
    pi->next = p->items;
    p->items = pi;

    return 0;
}

static struct pitem *
find_pi(persist_t *p, const char *iname, va_list ap)
{
    struct pitem *pi = p->items;
    char *name = do_va_nameit(p->sys, iname, ap);

    if (!name)
	return NULL;

    while (pi) {
	if (strcmp(pi->iname, name) == 0)
	    break;
	pi = pi->next;
    }
    p->sys->free(p->sys, name);
    return pi;
}

int
add_persist_data(persist_t *p, void *data, unsigned int len,
		 const char *name, ...)
{
    va_list ap;
    int rv;

    va_start(ap, name);
    rv = alloc_pi(p, PITEM_DATA, data, len, name, ap);
    va_end(ap);
    return rv;
}

int
read_persist_data(persist_t *p, void **data, unsigned int *len,
		  const char *name, ...)
{
    va_list ap;
    struct pitem *pi;

    va_start(ap, name);
    pi = find_pi(p, name, ap);
    va_end(ap);

    if (!pi)
	return ENOENT;

    if (pi->type != PITEM_DATA)
	return EINVAL;

    *data = p->sys->alloc(p->sys, pi->dval);
    if (!*data)
	return ENOMEM;
    memcpy(*data, pi->data, pi->dval);
    *len = pi->dval;
    return 0;
}

int
add_persist_int(persist_t *p, long val, const char *name, ...)
{
    va_list ap;
    int rv;

    va_start(ap, name);
    rv = alloc_pi(p, PITEM_INT, NULL, val, name, ap);
    va_end(ap);
    return rv;
}

int
read_persist_int(persist_t *p, long *val, const char *name, ...)
{
    va_list ap;
    struct pitem *pi;

    va_start(ap, name);
    pi = find_pi(p, name, ap);
    va_end(ap);

    if (!pi)
	return ENOENT;

    if (pi->type != PITEM_INT)
	return EINVAL;

    *val = pi->dval;
    return 0;
}

int
add_persist_str(persist_t *p, const char *val, const char *name, ...)
{
    va_list ap;
    int rv;

    va_start(ap, name);
    rv = alloc_pi(p, PITEM_STR, val, strlen(val), name, ap);
    va_end(ap);
    return rv;
}

int
read_persist_str(persist_t *p, char **val, const char *name, ...)
{
    va_list ap;
    struct pitem *pi;

    va_start(ap, name);
    pi = find_pi(p, name, ap);
    va_end(ap);

    if (!pi)
	return ENOENT;

    if (pi->type != PITEM_STR)
	return EINVAL;

    *val = sys_strdup(p->sys, pi->data);
    if (!*val)
	return ENOMEM;
    return 0;
}

void
free_persist_data(persist_t *p, void *data)
{
    p->sys->free(p->sys, data);
}
