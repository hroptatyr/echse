/*** task.h -- gathering task properties
 *
 * Copyright (C) 2013-2015 Sebastian Freundt
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
#if !defined INCLUDED_task_h_
#define INCLUDED_task_h_

#include <unistd.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "strlst.h"
#include "evstrm.h"
#include "oid.h"

typedef const struct echs_task_s *echs_task_t;
typedef echs_oid_t echs_toid_t;

typedef struct {
	const char *u;
	const char *g;
	const char *wd;
	const char *sh;
} cred_t;

struct echs_task_s {
	echs_toid_t oid;

	/* stream of instants when to run this task */
	echs_evstrm_t strm;

	/* command, environment */
	const char *cmd;
	struct strlst_s *env;

	/* owner of the task, this is meant to be a uid with values <0
	 * indicating that this has not been set */
	int owner;
	/* credentials we want this job run as */
	cred_t run_as;

	/* the organiser and attendees of the whole shebang
	 * the organiser in echsd terms will be the user the script
	 * is run as, the attendees will be the people receiving
	 * the specified output by mail */
	const char *org;
	struct strlst_s *att;

	/* input and output files */
	const char *in;
	const char *out;
	const char *err;
	unsigned int mailout:1U;
	unsigned int moutset:1U;
	unsigned int mailerr:1U;
	unsigned int merrset:1U;

	/* maximum number of simultaneous runs, upped by 1, i.e.
	 * 0 means -1 means infinite, 1 means 0 means never run
	 * 2 means 1 means don't run concurrently, etc. */
	unsigned int max_simul:6U;
};


extern struct echs_task_s *echs_task_clone(echs_task_t);
extern void free_echs_task(echs_task_t);

/**
 * Forcefully change oid of T to OID. */
extern int echs_task_rset_toid(echs_task_t t, echs_toid_t oid);

/**
 * Forcefully change owner of T to UID.
 * Negative values of UID `unset' the owner field. */
extern int echs_task_rset_ownr(echs_task_t t, int uid);


/* convenience */
static inline __attribute__((const, pure)) bool
echs_task_eq_p(echs_task_t t1, echs_task_t t2)
{
	return t1 == t2 || (t1 && t2 && t1->oid == t2->oid);
}

static inline __attribute__((const, pure)) bool
echs_task_owned_by_p(echs_task_t t, int uid)
{
	return (t->owner & uid) == uid;
}

#endif	/* INCLUDED_task_h_ */
