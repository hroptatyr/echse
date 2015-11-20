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
#include "nummapstr.h"

typedef const struct echs_task_s *echs_task_t;
typedef echs_oid_t echs_toid_t;

typedef struct {
	nummapstr_t u;
	nummapstr_t g;
	const char *wd;
	const char *sh;
} cred_t;

/**
 * Task structure, this is one of the two work horse structures in echse.
 *
 * This structure can capture and represent
 * - a VTODO/VEVENT recurrence (orders)
 * - a VTODO execution
 * - a VTODO/VJOURNAL report
 *
 * Recurring VTODOs or VEVENTs are determined by having a DTSTART
 * in the ical representation whereas executional VTODOs mustn't
 * have that but can of course have a DUE date or a DURATION.
 *
 * Recurring VTODOs can be determined by non-NULL STRM,
 * whereas non-recurring VTODOs have the VTOD_TYP slot set to non-0.
 *
 * Conversely, we serialise recurring VTODOs as VEVENTs and executional
 * VTODOs as VTODO. */
struct echs_task_s {
	echs_toid_t oid;

	/* stream of instants when to run this task */
	echs_evstrm_t strm;

	/* command, environment */
	const char *cmd;
	struct strlst_s *env;

	/* owner of the task */
	nummapstr_t owner;
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
	unsigned int mailrun:1U;
	unsigned int mrunset:1U;
	/* pad to next byte */
	unsigned int:2U;

	/* maximum number of simultaneous runs, upped by 1, i.e.
	 * 0 means -1 means infinite, 1 means 0 means never run
	 * 2 means 1 means don't run concurrently, etc. */
	unsigned int max_simul:6U;
	/* more padding */
	enum {
		VTOD_TYP_UNK,
		VTOD_TYP_TIMEOUT,
		VTOD_TYP_DUE,
		VTOD_TYP_COMPL,
	} vtod_typ:2U;

	/* just an ordinary umask value as supported by umask(1) */
	unsigned int umsk:10U;
	/* padding */
	unsigned int:6U;

	/* due date or timeout value if this is a (non-recurring) VTODO
	 * (i.e. the STRM slot will be NULL)
	 * which one it is is determined by VTOD_TYP above */
	union {
		echs_idiff_t timeout;
		echs_instant_t due;
		echs_instant_t compl;
	};
};


extern struct echs_task_s *echs_task_clone(echs_task_t);
extern void free_echs_task(echs_task_t);

/**
 * Forcefully change oid of T to OID. */
extern int echs_task_rset_toid(echs_task_t t, echs_toid_t oid);

/**
 * Forcefully change owner of T to UID.
 * Negative values of UID `unset' the owner field. */
extern int echs_task_rset_ownr(echs_task_t t, unsigned int uid);


/* convenience */
static inline __attribute__((const, pure)) bool
echs_task_eq_p(echs_task_t t1, echs_task_t t2)
{
	return t1 == t2 || (t1 && t2 && t1->oid == t2->oid);
}

#endif	/* INCLUDED_task_h_ */
