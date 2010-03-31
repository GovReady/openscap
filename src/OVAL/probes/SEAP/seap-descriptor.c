/*
 * Copyright 2008 Red Hat Inc., Durham, North Carolina.
 * All Rights Reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Authors:
 *      "Daniel Kopecek" <dkopecek@redhat.com>
 */

#include <pthread.h>
#include "public/sm_alloc.h"
#include "generic/bitmap.h"
#include "_sexp-parser.h"
#include "_seap-scheme.h"
#include "seap-descriptor.h"
#include "_sexp-atomic.h"

int SEAP_desc_add (SEAP_desctable_t *sd_table, SEXP_pstate_t *pstate,
                         SEAP_scheme_t scheme, void *scheme_data)
{
        bitmap_bitn_t sd;
        pthread_mutexattr_t mutex_attr;
        
        sd = bitmap_setfree (&(sd_table->bitmap));
        
        if (sd >= 0) {
                if (sd >= sd_table->sdsize) {
                        /* sd araay is to small -> realloc */
                        sd_table->sdsize = sd + SDTABLE_REALLOC_ADD;
                        sd_table->sd = sm_realloc (sd_table->sd, sizeof (SEAP_desc_t) * sd_table->sdsize);
                }

                sd_table->sd[sd].next_id = 0;
                sd_table->sd[sd].sexpbuf = NULL;
                /* sd_table->sd[sd].sexpcnt = 0; */
                sd_table->sd[sd].pstate  = pstate;
                sd_table->sd[sd].scheme  = scheme;
                sd_table->sd[sd].scheme_data = scheme_data;
                sd_table->sd[sd].ostate  = NULL;
                sd_table->sd[sd].next_cid = 0;
                sd_table->sd[sd].cmd_c_table = SEAP_cmdtbl_new ();
                sd_table->sd[sd].cmd_w_table = SEAP_cmdtbl_new ();
                sd_table->sd[sd].pck_queue   = pqueue_new (1024); /* FIXME */
                
                pthread_mutexattr_init (&mutex_attr);
                pthread_mutexattr_settype (&mutex_attr, PTHREAD_MUTEX_RECURSIVE);
                
                pthread_mutex_init (&(sd_table->sd[sd].r_lock), &mutex_attr);
                pthread_mutex_init (&(sd_table->sd[sd].w_lock), &mutex_attr);
                
                pthread_mutexattr_destroy (&mutex_attr);
                
                return ((int)sd);
        }

        return (-1);
}

int SEAP_desc_del (SEAP_desctable_t *sd_table, int sd)
{
        
        return (0);
}

SEAP_desc_t *SEAP_desc_get (SEAP_desctable_t *sd_table, int sd)
{
        if (sd < 0 || sd > sd_table->sdsize) {
                errno = EBADF;
                return (NULL);
        }

        return (&(sd_table->sd[sd]));
}

SEAP_msgid_t SEAP_desc_genmsgid (SEAP_desctable_t *sd_table, int sd)
{
        SEAP_desc_t *dsc;
        SEAP_msgid_t  id;
        dsc = SEAP_desc_get (sd_table, sd);
        
        if (dsc == NULL) {
                errno = EINVAL;
                return (-1);
        }

#if SEAP_MSGID_BITS == 64
        id = SEXP_atomic_inc_u64 (&(dsc->next_id));
#else
        id = SEXP_atomic_inc_u32 (&(dsc->next_id));
#endif
        return (id);
}

SEAP_cmdid_t SEAP_desc_gencmdid (SEAP_desctable_t *sd_table, int sd)
{
        SEAP_desc_t *dsc;
        SEAP_cmdid_t  id;

        dsc = SEAP_desc_get (sd_table, sd);
        
        if (dsc == NULL) {
                errno = EINVAL;
                return (-1);
        }

        id = SEXP_atomic_inc_u16 (&(dsc->next_cid));

        return (id);
}
