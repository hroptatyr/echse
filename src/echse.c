/*** echse.c -- testing echse concept
 *
 * Copyright (C) 2013-2014 Sebastian Freundt
 *
 * Author:  Sebastian Freundt <freundt@ga-group.nl>
 *
 * This file is part of echse.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the author nor the names of any contributors
 *    may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 ***/
#if defined HAVE_CONFIG_H
# include "config.h"
#endif	/* HAVE_CONFIG_H */
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include "echse.h"
#include "echse-genuid.h"
#include "evical.h"
#include "dt-strpf.h"
#include "fdprnt.h"
#include "nifty.h"

struct unroll_param_s {
	echs_instant_t from;
	echs_instant_t till;
	struct rrulsp_s filt;
};


static __attribute__((format(printf, 1, 2))) void
serror(const char *fmt, ...)
{
	va_list vap;
	va_start(vap, fmt);
	vfprintf(stderr, fmt, vap);
	va_end(vap);
	if (errno) {
		fputc(':', stderr);
		fputc(' ', stderr);
		fputs(strerror(errno), stderr);
	}
	fputc('\n', stderr);
	return;
}


/* global stream map */
static echs_evstrm_t *strms;
static size_t nstrms;
static size_t zstrms;

static int
add_strm(echs_evstrm_t s)
{
	if (UNLIKELY(nstrms >= zstrms)) {
		const size_t nuz = (zstrms * 2U) ?: 64U;
		strms = realloc(strms, (zstrms = nuz) * sizeof(*strms));
		if (UNLIKELY(strms == NULL)) {
			return -1;
		}
	}
	strms[nstrms++] = s;
	return 0;
}

static int
rem_strm(echs_evstrm_t s)
{
	for (size_t i = 0U; i < nstrms; i++) {
		if (strms[i] == s) {
			/* found him */
			strms[i] = NULL;
			if (i + 1U == nstrms) {
				nstrms--;
			}
			return 0;
		}
	}
	return -1;
}

static void
condense_strms(void)
{
	size_t i, j;

	for (i = 0U; i < nstrms && strms[i] != NULL; i++);
	for (j = i++; i < nstrms; i++) {
		if ((strms[j] = strms[i]) != NULL) {
			j++;
		}
	}
	/* at last, we've shortened the STRMS array down to J objects */
	nstrms = j;
	return;
}

static void
free_strms(void)
{
	if (LIKELY(strms != NULL)) {
		free(strms);
	}
	strms = NULL;
	nstrms = zstrms = 0UL;
	return;
}


/* mapping from oid to task and stream */
struct tmap_s {
	echs_toid_t oid;
	echs_task_t t;
};

static struct tmap_s *task_ht;
static size_t ztask_ht;

static size_t
put_task_slot(echs_toid_t oid)
{
/* find slot for OID for putting */
	for (size_t i = 16U/*retries*/, slot = oid & (ztask_ht - 1U); i; i--) {
		if (LIKELY(!task_ht[slot].oid)) {
			return slot;
		} else if (task_ht[slot].oid != oid) {
			/* collision, retry */
			;
		} else {
			/* free old task and stream */
			rem_strm(task_ht[slot].t->strm);
			free_echs_task(task_ht[slot].t);
		        return slot;
		}
	}
	return (size_t)-1ULL;
}

static size_t
get_task_slot(echs_toid_t oid)
{
/* find slot for OID for getting */
	if (UNLIKELY(task_ht == NULL)) {
		/* something must have really gone wrong */
		return (size_t)-1ULL;
	}
	for (size_t i = 16U/*retries*/, slot = oid & (ztask_ht - 1U); i; i--) {
		if (task_ht[slot].oid == oid) {
			return slot;
		}
	}
	return (size_t)-1ULL;
}

static int
resz_task_ht(void)
{
	const size_t olz = ztask_ht;
	const struct tmap_s *olt = task_ht;

again:
	/* buy the shiny new house */
	task_ht = calloc((ztask_ht *= 2U), sizeof(*task_ht));
	if (UNLIKELY(task_ht == NULL)) {
		return -1;
	}
	/* and now move */
	for (size_t i = 0U; i < olz; i++) {
		if (olt[i].oid) {
			size_t j = put_task_slot(olt[i].oid);

			if (UNLIKELY(j >= ztask_ht)) {
				free(task_ht);
				goto again;
			}
			task_ht[j] = olt[i];
		}
	}
	return 0;
}

static echs_task_t
get_task(echs_toid_t oid)
{
/* find the task with oid OID. */
	size_t slot;

	if (UNLIKELY(!ztask_ht)) {
		return NULL;
	} else if (UNLIKELY((slot = get_task_slot(oid)) >= ztask_ht)) {
		/* not resizing now */
		return NULL;
	} else if (!task_ht[slot].oid) {
		/* not found */
		return NULL;
	}
	return task_ht[slot].t;
}

static int
put_task(echs_toid_t oid, echs_task_t t)
{
	if (UNLIKELY(!ztask_ht)) {
		/* instantiate hash table */
		task_ht = calloc(ztask_ht = 4U, sizeof(*task_ht));
	}
again:
	with (size_t slot = put_task_slot(oid)) {
		if (UNLIKELY(slot >= ztask_ht)) {
			/* resize */
			if (UNLIKELY(resz_task_ht() < 0)) {
				return -1;
			}
			goto again;
		}
		task_ht[slot] = (struct tmap_s){oid, t};
	}
	/* also file a stream to our strms registry */
	add_strm(t->strm);
	return 0;
}

static void
free_task_ht(void)
{
	if (LIKELY(task_ht != NULL)) {
		for (struct tmap_s *p = task_ht, *const ep = task_ht + ztask_ht;
		     p < ep; p++) {
			if (p->oid) {
				/* the streams have been freed
				 * by the muxer already (we used evmux() i.e.
				 * without cloning the streams)
				 * therefore we must massage the tasks a little
				 * before calling the task dtor */
				struct echs_task_s *tmpt = deconst(p->t);
				tmpt->strm = NULL;
				free_echs_task(tmpt);
			}
		}
		free(task_ht);
		task_ht = NULL;
	}
	return;
}


static void
chck_filt(struct rrulsp_s *restrict f)
{
	if (bi31_has_bits_p(f->dom) ||
	    bi447_has_bits_p(&f->dow) ||
	    bui31_has_bits_p(f->mon) ||
	    bi63_has_bits_p(f->wk) ||
	    bi383_has_bits_p(&f->doy)) {
		/* indicate active filter */
		f->freq = FREQ_YEARLY;
	}
	return;
}

static void
unroll_ical(echs_evstrm_t smux, const struct unroll_param_s *p)
{
	echs_event_t e;

	/* just get it out now */
	echs_prnt_ical_init();
	while (!echs_event_0_p(e = echs_evstrm_next(smux))) {
		if (echs_instant_lt_p(e.from, p->from)) {
			continue;
		} else if (echs_instant_lt_p(p->till, e.from)) {
			break;
		}
		/* otherwise print */
		with (echs_task_t t = get_task(e.oid)) {
			echs_prnt_ical_event(t, e);
		}
	}
	echs_prnt_ical_fini();
	return;
}

static int
unroll_prnt(int ofd, echs_event_t e, const char *fmt)
{
	(void)fdprintf;

	fdbang(ofd);
	for (const char *fp = fmt; *fp; fp++) {
		if (UNLIKELY(*fp == '\\')) {
			static const char esc[] =
				"\a\bcd\e\fghijklm\nopq\rs\tu\v";

			if (*++fp && *fp >= 'a' && *fp <= 'v') {
				fdputc(esc[*fp - 'a']);
			} else if (!*fp) {
				/* huh? trailing lone backslash */
				break;
			} else {
				fdputc(*fp);
			}
		} else if (UNLIKELY(*fp == '%')) {
			echs_instant_t i;
			obint_t x;

			switch (*++fp) {
				echs_task_t t;

			case 'b':
				i = e.from;
				goto cpy_inst;
			case 'e':
				i = e.till;
				goto cpy_inst;
			case 's':
				if (UNLIKELY((t = get_task(e.oid)) == NULL)) {
					;
				} else if (UNLIKELY(t->cmd == NULL)) {
					;
				} else {
					const size_t cmz = strlen(t->cmd);
					fdwrite(t->cmd, cmz);
				}
				continue;
			case 'S':
				if (e.sts) {
					echs_state_t st = 0U;
					for (;
					     e.sts && !(e.sts & 0b1U);
					     e.sts >>= 1U, st++);
					const char *sn = state_name(st);
					size_t sz = strlen(sn);

					/* first one without leading comma */
					fdwrite(sn, sz);
					/* and the rest of the states */
					while (st++, e.sts >>= 1U) {
						for (;
						     e.sts && !(e.sts & 0b1U);
						     e.sts >>= 1U, st++);
						sn = state_name(st);
						sz = strlen(sn);
						fdputc(',');
						fdwrite(sn, sz);
					}
				}
				continue;
			case 'u':
				if ((x = e.oid)) {
					goto cpy_obint;
				}
				continue;
			case '%':
				fdputc('%');
			default:
				continue;
			}

		cpy_inst:
			{
				char b[32U];
				size_t z;
				z = dt_strf(b, sizeof(b), i);
				fdwrite(b, z);
			}
			continue;
		cpy_obint:
			{
				const char *nm = obint_name(x);
				const size_t nz = strlen(nm);

				fdwrite(nm, nz);
			}
			continue;
		} else {
			fdputc(*fp);
		}
	}
	return 0;
}

static void
unroll_frmt(echs_evstrm_t smux, const struct unroll_param_s *p, const char *fmt)
{
	echs_event_t e;

	/* just get it out now */
	fdbang(STDOUT_FILENO);
	while (!echs_event_0_p(e = echs_evstrm_next(smux))) {
		if (echs_instant_lt_p(p->till, e.from)) {
			break;
		} else if (echs_instant_lt_p(e.from, p->from)) {
			continue;
		} else if (p->filt.freq &&
			   !echs_instant_matches_p(&p->filt, e.from)) {
			continue;
		}
		/* otherwise print */
		unroll_prnt(STDOUT_FILENO, e, fmt);
		/* finalise buf */
		fdputc('\n');
		fdflush();
	}
	fdflush();
	return;
}

static int
_inject_fd(int fd)
{
	char buf[65536U];
	ical_parser_t pp = NULL;
	ssize_t nrd;

more:
	switch ((nrd = read(fd, buf, sizeof(buf)))) {
		echs_instruc_t ins;

	default:
		if (echs_evical_push(&pp, buf, nrd) < 0) {
			/* pushing more brings nothing */
			break;
		}
		/*@fallthrough@*/
	case 0:
		do {
			ins = echs_evical_pull(&pp);

			/* only allow PUBLISH requests for now */
			if (UNLIKELY(ins.v != INSVERB_CREA)) {
				break;
			} else if (UNLIKELY(ins.t == NULL)) {
				continue;
			} else if (UNLIKELY(!ins.t->oid)) {
				free_echs_task(ins.t);
				continue;
			}
			/* and otherwise inject him */
			put_task(ins.t->oid, ins.t);
		} while (1);
		if (LIKELY(nrd > 0)) {
			goto more;
		}
		/*@fallthrough@*/
	case -1:
		/* last ever pull this morning */
		ins = echs_evical_last_pull(&pp);

		/* still only allow PUBLISH requests for now */
		if (UNLIKELY(ins.v != INSVERB_CREA)) {
			break;
		} else if (UNLIKELY(ins.t != NULL)) {
			/* that can't be right, we should have got
			 * the last task in the loop above, this means
			 * this is a half-finished thing and we don't
			 * want no half-finished things */
			free_echs_task(ins.t);
		}
		break;
	}
	return 0;
}

static int
_merge_fd(int fd)
{
	char buf[65536U];
	ical_parser_t pp = NULL;
	ssize_t nrd;

more:
	switch ((nrd = read(fd, buf, sizeof(buf)))) {
		echs_instruc_t ins;

	default:
		if (echs_evical_push(&pp, buf, nrd) < 0) {
			/* pushing more brings nothing */
			break;
		}
		/*@fallthrough@*/
	case 0:
		do {
			ins = echs_evical_pull(&pp);

			/* only allow PUBLISH requests for now */
			if (UNLIKELY(ins.v != INSVERB_CREA)) {
				break;
			} else if (UNLIKELY(ins.t == NULL)) {
				continue;
			} else if (UNLIKELY(!ins.t->oid)) {
				free_echs_task(ins.t);
				continue;
			}
			/* and otherwise inject him */
			echs_task_icalify(STDOUT_FILENO, ins.t);
			free_echs_task(ins.t);
		} while (1);
		if (LIKELY(nrd > 0)) {
			goto more;
		}
		/*@fallthrough@*/
	case -1:
		/* last ever pull this morning */
		ins = echs_evical_last_pull(&pp);

		/* still only allow PUBLISH requests for now */
		if (UNLIKELY(ins.v != INSVERB_CREA)) {
			break;
		} else if (UNLIKELY(ins.t != NULL)) {
			/* that can't be right, we should have got
			 * the last task in the loop above, this means
			 * this is a half-finished thing and we don't
			 * want no half-finished things */
			free_echs_task(ins.t);
		}
		break;
	}
	return 0;
}

static int
_valid_fd(int fd)
{
	char buf[65536U];
	ical_parser_t pp = NULL;
	ssize_t nrd;

more:
	switch ((nrd = read(fd, buf, sizeof(buf)))) {
		echs_instruc_t ins;

	default:
		if (echs_evical_push(&pp, buf, nrd) < 0) {
			/* pushing more brings nothing */
			break;
		}
		/*@fallthrough@*/
	case 0:
		do {
			ins = echs_evical_pull(&pp);

			/* only allow PUBLISH requests for now */
			if (UNLIKELY(ins.v != INSVERB_CREA)) {
				break;
			} else if (UNLIKELY(ins.t == NULL)) {
				continue;
			} else if (UNLIKELY(!ins.t->oid)) {
				free_echs_task(ins.t);
				continue;
			}
			/* and otherwise inject him */
			with (echs_range_t r = echs_evstrm_valid(ins.t->strm)) {
				char beg[32U], end[32U];

				dt_strf(beg, sizeof(beg), r.beg);
				dt_strf(end, sizeof(end), r.end);
				printf("task 0x%lx %s-%s\n",
				       ins.t->oid, beg, end);
			}
			free_echs_task(ins.t);
		} while (1);
		if (LIKELY(nrd > 0)) {
			goto more;
		}
		/*@fallthrough@*/
	case -1:
		/* last ever pull this morning */
		ins = echs_evical_last_pull(&pp);

		/* still only allow PUBLISH requests for now */
		if (UNLIKELY(ins.v != INSVERB_CREA)) {
			break;
		} else if (UNLIKELY(ins.t != NULL)) {
			/* that can't be right, we should have got
			 * the last task in the loop above, this means
			 * this is a half-finished thing and we don't
			 * want no half-finished things */
			free_echs_task(ins.t);
		}
		break;
	}
	return 0;
}


#if defined STANDALONE
#include "echse.yucc"

static int
cmd_unroll(const struct yuck_cmd_unroll_s argi[static 1U])
{
	static const char dflt_fmt[] = "%b\t%s";
	/* params that filter the output */
	struct unroll_param_s p = {.filt = {.freq = FREQ_NONE}};
	echs_evstrm_t smux = NULL;

	if (argi->from_arg) {
		p.from = dt_strp(argi->from_arg, NULL, 0U);
	} else {
#if defined HAVE_ANON_STRUCTS_INIT
		p.from = (echs_instant_t){.y = 2000, .m = 1, .d = 1};
#else  /* !HAVE_ANON_STRUCTS_INIT */
		p.from = echs_nul_instant();
		p.from.y = 2000;
		p.from.m = 1;
		p.from.d = 1;
#endif	/* HAVE_ANON_STRUCTS_INIT */
	}

	if (argi->till_arg) {
		p.till = dt_strp(argi->till_arg, NULL, 0U);
	} else {
#if defined HAVE_ANON_STRUCTS_INIT
		p.till = (echs_instant_t){.y = 2037, .m = 12, .d = 31};
#else  /* !HAVE_ANON_STRUCTS_INIT */
		p.till = echs_nul_instant();
		p.till.y = 2037;
		p.till.m = 12;
		p.till.d = 31;
#endif	/* HAVE_ANON_STRUCTS_INIT */
	}

	if (argi->filter_arg) {
		const char *sel = argi->filter_arg;
		const size_t len = strlen(sel);

		/* just use the rrule parts, there won't be a frequency set */
		p.filt = echs_read_rrul(sel, len);
		/* quickly assess the rrule, set a frequency if there's
		 * stuff to be filtered */
		chck_filt(&p.filt);
	}

	for (size_t i = 0UL; i < argi->nargs; i++) {
		const char *fn = argi->args[i];
		int fd;

		if (UNLIKELY((fd = open(fn, O_RDONLY)) < 0)) {
			serror("\
echse: Error: cannot open file `%s'", fn);
			continue;
		}
		/* otherwise inject */
		_inject_fd(fd);
		close(fd);
	}
	if (argi->nargs == 0UL) {
		/* read from stdin */
		_inject_fd(STDIN_FILENO);
	}
	/* there might be riff raff (NULLs) in the stream array */
	condense_strms();
	if (UNLIKELY((smux = echs_evstrm_vmux(strms, nstrms)) == NULL)) {
		/* return early */
		return 1;
	}
	/* noone needs the streams in an array anymore */
	free_strms();

	if (argi->format_arg != NULL && !strcmp(argi->format_arg, "ical")) {
		/* special output format */
		unroll_ical(smux, &p);
	} else {
		const char *fmt = argi->format_arg ?: dflt_fmt;
		unroll_frmt(smux, &p, fmt);
	}

	free_echs_evstrm(smux);
	free_task_ht();
	return 0;
}

static int
cmd_genuid(const struct yuck_cmd_genuid_s argi[static 1U])
{
	const bool forcep = argi->force_flag;
	const char *fmt = argi->format_arg ?: "echse/%f/%x@example.com";
	int rc = 0;

	echse_init_genuid();
	for (size_t i = 0UL; i < argi->nargs; i++) {
		const char *fn = argi->args[i];

		if (echse_genuid1(fmt, fn, forcep) < 0) {
			rc++;
		}
	}
	echse_fini_genuid();
	return rc;
}

static int
cmd_merge(const struct yuck_cmd_merge_s argi[static 1U])
{
	echs_prnt_ical_init();
	for (size_t i = 0UL; i < argi->nargs; i++) {
		const char *fn = argi->args[i];
		int fd;

		if (UNLIKELY((fd = open(fn, O_RDONLY)) < 0)) {
			serror("\
echse: Error: cannot open file `%s'", fn);
			continue;
		}
		/* otherwise inject */
		_merge_fd(fd);
		close(fd);
	}
	if (argi->nargs == 0UL) {
		/* read from stdin */
		_merge_fd(STDIN_FILENO);
	}
	echs_prnt_ical_fini();
	return 0;
}

static int
cmd_valid(const struct yuck_cmd_valid_s argi[static 1U])
{
	for (size_t i = 0UL; i < argi->nargs; i++) {
		const char *fn = argi->args[i];
		int fd;

		if (UNLIKELY((fd = open(fn, O_RDONLY)) < 0)) {
			serror("\
echse: Error: cannot open file `%s'", fn);
			continue;
		}
		/* otherwise inject */
		_valid_fd(fd);
		close(fd);
	}
	if (argi->nargs == 0UL) {
		/* read from stdin */
		_valid_fd(STDIN_FILENO);
	}
	return 0;
}


int
main(int argc, char *argv[])
{
	/* command line options */
	yuck_t argi[1U];
	int rc = 0;

	if (yuck_parse(argi, argc, argv)) {
		rc = 1;
		goto out;
	}

	switch (argi->cmd) {
	default:
	case ECHSE_CMD_NONE:
		fputs("\
echse: no valid command given\n\
Try --help for a list of commands.\n", stderr);
		rc = 1;
		break;
	case ECHSE_CMD_UNROLL:
		rc = cmd_unroll((struct yuck_cmd_unroll_s*)argi);
		break;
	case ECHSE_CMD_GENUID:
		rc = cmd_genuid((struct yuck_cmd_genuid_s*)argi);
		break;
	case ECHSE_CMD_MERGE:
		rc = cmd_merge((struct yuck_cmd_merge_s*)argi);
		break;
	case ECHSE_CMD_VALID:
		rc = cmd_valid((struct yuck_cmd_valid_s*)argi);
		break;
	}
	/* some global resources */
	clear_interns();
	clear_bufpool();
out:
	yuck_free(argi);
	return rc;
}
#endif	/* STANDALONE */

/* echse.c ends here */
