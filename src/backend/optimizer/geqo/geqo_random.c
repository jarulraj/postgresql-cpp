/*------------------------------------------------------------------------
 *
 * geqo_random.c
 *	   random number generator
 *
 * Portions Copyright (c) 1996-2015, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/backend/optimizer/geqo/geqo_random.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "optimizer/geqo_random.h"


void
geqo_set_seed(PlannerInfo *root, double seed)
{
	GeqoPrivateData *private__ = (GeqoPrivateData *) root->join_search_private;

	/*
	 * XXX. This seeding algorithm could certainly be improved - but it is not
	 * critical to do so.
	 */
	memset(private__->random_state, 0, sizeof(private__->random_state));
	memcpy(private__->random_state,
		   &seed,
		   Min(sizeof(private__->random_state), sizeof(seed)));
}

double
geqo_rand(PlannerInfo *root)
{
	GeqoPrivateData *private__ = (GeqoPrivateData *) root->join_search_private;

	return pg_erand48(private__->random_state);
}
