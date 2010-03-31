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

#include <stdarg.h>
#include <string.h>
#include <errno.h>
#if defined(SEAP_THREAD_SAFE)
# include <pthread.h>
#endif

#include "generic/common.h"
#include "public/sm_alloc.h"
#include "_seap-types.h"
#include "_sexp-types.h"
#include "_sexp-manip.h"
#include "_seap-scheme.h"
#include "generic/redblack.h"
#include "_seap-command.h"
#include "_seap-packet.h"

#if 0 /* XXX: dead code? */
static SEAP_cmd_t *SEAP_cmd_new (void)
{
        SEAP_cmd_t *cmd;

        cmd = sm_talloc (SEAP_cmd_t);
        memset (cmd, 0, sizeof (SEAP_cmd_t));
        cmd->args = NULL;
        
        return (cmd);
}

static void SEAP_cmd_free (SEAP_cmd_t *p)
{
        sm_free (p);
}
#endif /* 0 */

int SEAP_cmd_register (SEAP_CTX_t *ctx, SEAP_cmdcode_t code, uint32_t flags, SEAP_cmdfn_t func, ...) /* sd, arg */
{
        va_list ap;
        int     sd;
        void   *arg;
        
        SEAP_desc_t   *dsc;
        SEAP_cmdtbl_t *tbl;
        SEAP_cmdrec_t *rec;

        _A(ctx  != NULL);
        _A(func != NULL);

        sd  = -1;
        arg = NULL;

        va_start (ap, func);
                
        if (flags & SEAP_CMDREG_LOCAL) {
                sd  = va_arg (ap, int);
                dsc = SEAP_desc_get (&(ctx->sd_table), sd);
                
                if (dsc == NULL)
                        return (-1);
                
                tbl = dsc->cmd_c_table;
        } else {
                tbl = ctx->cmd_c_table;
        }
        
        _A(tbl != NULL);
        
        if (flags & SEAP_CMDREG_USEARG) {
                arg = va_arg (ap, void *);
                _A(arg != NULL);
        }

        rec = SEAP_cmdrec_new ();
        rec->code = code;
        rec->func = func;
        rec->arg  = arg;

        switch (SEAP_cmdtbl_add (tbl, rec)) {
        case 0:
                /* rec is freed by SEAP_cmdtbl_add */
                break;
        case SEAP_CMDTBL_ECOLL:
                _D("Can't register command: code=%u, tbl=%p: already registered.\n",
                   code, (void *)tbl);
                SEAP_cmdrec_free (rec);
                return (-1);
        case -1:
                _D("Can't register command: code=%u, func=%p, tbl=%p, arg=%p: errno=%u, %s.\n",
                   code, (void *)func, (void *)tbl, arg, errno, strerror (errno));
                SEAP_cmdrec_free (rec);
                return (-1);
        default:
                SEAP_cmdrec_free (rec);
                errno = EDOOFUS;
                return (-1);
        }
        
        return (0);
}

/* 
 *  SEAP_cmd_register (ctx, SEAP_CMDREG_USEARG, 1, cmdfn, sd, NULL);
 *  SEAP_cmd_register (ctx, SEAP_CMDREG_GLOBAL, 1, cmdfn);
 *  SEAP_cmd_register (ctx, 0, 1, cmdfn, sd);
 *  SEAP_cmd_register (ctx, SEAP_CMDREG_USEARG | SEAP_CMDREG_GLOBAL, 1, cmdfn, NULL);
 */ 

SEAP_cmdrec_t *SEAP_cmdrec_new (void)
{
        SEAP_cmdrec_t *r;

        r = sm_talloc (SEAP_cmdrec_t);
        r->code = 0;
        r->func = NULL;
        r->arg  = NULL;

        return (r);
}

void SEAP_cmdrec_free (SEAP_cmdrec_t *r)
{
        sm_free (r);
}

SEAP_cmdtbl_t *SEAP_cmdtbl_new (void)
{
        SEAP_cmdtbl_t *t;
        
        t = sm_talloc (SEAP_cmdtbl_t);

        t->flags = 0;
        t->table = NULL;
        t->maxcnt = 0;

#if defined(SEAP_THREAD_SAFE)
        if (pthread_rwlock_init (&t->lock, NULL) != 0) {
                _D("Can't initialize rwlock: %u, %s.\n",
                   errno, strerror (errno));
                sm_free (t);
                return (NULL);
        }
#endif
        return (t);
}

void SEAP_cmdtbl_free (SEAP_cmdtbl_t *t)
{
        _A(t != NULL);
        
        if (t->flags & SEAP_CMDTBL_LARGE)
                SEAP_cmdtbl_backendL_free (t);
        else
                SEAP_cmdtbl_backendS_free (t);
        
        return;
}

int SEAP_cmdtbl_setsize (SEAP_cmdtbl_t *t, size_t maxsz)
{
        _A(t != NULL);        
        t->maxcnt = maxsz;
        return (0);
}

int SEAP_cmdtbl_setfl (SEAP_cmdtbl_t *t, uint8_t f)
{
        _A(t != NULL);
        t->flags |= f;
        return (0);
}

int SEAP_cmdtbl_unsetfl (SEAP_cmdtbl_t *t, uint8_t f)
{
        _A(t != NULL);
        t->flags &= ~(f);
        return (0);
}

int SEAP_cmdtbl_add (SEAP_cmdtbl_t *t, SEAP_cmdrec_t *r)
{
        _A(t != NULL);
        _A(t != NULL);
        return (SEAP_CMDTBL_LARGE & t->flags ?
                SEAP_cmdtbl_backendL_add (t, r) :
                SEAP_cmdtbl_backendS_add (t, r));
}

int SEAP_cmdtbl_ins (SEAP_cmdtbl_t *t, SEAP_cmdrec_t *r)
{
        _A(t != NULL);
        _A(t != NULL);
        return (SEAP_CMDTBL_LARGE & t->flags ?
                SEAP_cmdtbl_backendL_ins (t, r) :
                SEAP_cmdtbl_backendS_ins (t, r));
}

int SEAP_cmdtbl_del (SEAP_cmdtbl_t *t, SEAP_cmdrec_t *r)
{
        _A(t != NULL);
        _A(t != NULL);
        return (SEAP_CMDTBL_LARGE & t->flags ?
                SEAP_cmdtbl_backendL_del (t, r) :
                SEAP_cmdtbl_backendS_del (t, r));
}

SEAP_cmdrec_t *SEAP_cmdtbl_get (SEAP_cmdtbl_t *t, SEAP_cmdcode_t c)
{
        _A(t != NULL);
        return (SEAP_CMDTBL_LARGE & t->flags ?
                SEAP_cmdtbl_backendL_get (t, c) :
                SEAP_cmdtbl_backendS_get (t, c));
}

int SEAP_cmdtbl_cmp (SEAP_cmdrec_t *a, SEAP_cmdrec_t *b)
{
        _A(a != NULL);
        _A(b != NULL);
        return (int)(a->code - b->code);
}

int SEAP_cmd_unregister (SEAP_CTX_t *ctx, SEAP_cmdcode_t code)
{
        return (-1);
}

static SEXP_t *__SEAP_cmd_sync_handler (SEXP_t *res, void *arg)
{
        struct SEAP_synchelper *h = (struct SEAP_synchelper *)arg;
        
        _D("res=%p\n", res);

        h->args = res;
        (void) pthread_cond_signal (&h->cond);
        
        return (NULL);
}

SEXP_t *SEAP_cmd2sexp (SEAP_cmd_t *cmd)
{
        return (NULL);
}

SEXP_t *SEAP_cmd_exec (SEAP_CTX_t    *ctx,
                       int            sd,
                       uint32_t       flags,
                       SEAP_cmdcode_t code,
                       SEXP_t        *args,
                       SEAP_cmdtype_t type,
                       SEAP_cmdfn_t   func,
                       void          *funcarg)
{
        SEAP_desc_t   *dsc;
        SEAP_cmdrec_t *rec;
        SEAP_cmdtbl_t *tbl[2];
        SEXP_t        *res;
        int8_t i;
        
        _A(ctx != NULL);

#if !defined(NDEBUG) || defined(VALIDATE_SEXP)
        if (args != NULL) {
                SEXP_VALIDATE(args);
        }
#endif

        _D("code=%u, args=%p\n", code, args);

        dsc = SEAP_desc_get (&(ctx->sd_table), sd);
        
        if (dsc == NULL)
                return (NULL);
        
        if (flags & (SEAP_EXEC_LOCAL | SEAP_EXEC_WQUEUE)) {
                _D("EXEC_LOCAL\n");
                
                /* get table pointers */
                if (flags & SEAP_EXEC_WQUEUE) {
                        i = 0;
                        tbl[0] = dsc->cmd_w_table;
                } else if (flags & SEAP_EXEC_LONLY) {
                        i = 0;
                        tbl[0] = dsc->cmd_c_table;
                } else {
                        i = 1;
                        
                        if (flags & SEAP_EXEC_GFIRST) {
                                tbl[1] = ctx->cmd_c_table;
                                tbl[0] = dsc->cmd_c_table;
                        } else {
                                tbl[1] = dsc->cmd_c_table;
                                tbl[0] = ctx->cmd_c_table;
                        }
                }
                
                /* lookup command */
                rec = NULL;
                
                for (; i >= 0; --i)
                        if ((rec = SEAP_cmdtbl_get (tbl[i], code)) != NULL)
                                break;
                
                _D("rec=%p, w=%u\n", rec, (flags & SEAP_EXEC_WQUEUE) ? 1 : 0);
                
                if (rec == NULL) {
                        
                        return (NULL);
                }
                /* execute command */
                res = rec->func (args, rec->arg);
                
                _D("res=%p\n", res);

                /* filter result */
                if (func != NULL)
                        res = func (res, funcarg);
                
                _D("func@%p(res)=%p\n", func, res);
                
                return (res);
        } else {
                SEAP_cmd_t    *cmdptr;
                SEAP_packet_t *packet;

                _D("EXEC_REMOTE\n");
                
                packet = SEAP_packet_new ();
                cmdptr = SEAP_packet_settype (packet, SEAP_PACKET_CMD);
                
                cmdptr->id    = SEAP_desc_gencmdid (&(ctx->sd_table), sd);
                cmdptr->rid   = 0;
                cmdptr->class = SEAP_CMDCLASS_USR;
                cmdptr->code  = code;
                cmdptr->args  = args;
                cmdptr->flags = 0;
                
                switch (type) {
                case SEAP_CMDTYPE_SYNC: {
                        struct SEAP_synchelper h;
                        
                        cmdptr->flags |= SEAP_CMDFLAG_SYNC;
                
                        if (pthread_cond_init (&h.cond, NULL) != 0 ||
                            pthread_mutex_init (&h.mtx, NULL) != 0)
                                abort ();

                        h.args = NULL;
                        
                        if (pthread_mutex_lock (&(h.mtx)) != 0)
                                abort ();
                        
                        rec = SEAP_cmdrec_new ();
                        rec->code = cmdptr->id;
                        rec->func = &__SEAP_cmd_sync_handler;
                        rec->arg  = &h;

                        switch (SEAP_cmdtbl_add (dsc->cmd_w_table, rec)) {
                        case 0:
                                break;
                        case SEAP_CMDTBL_ECOLL:
                                _D("Can't register async command handler: id=%u, tbl=%p, sd=%u: already registered.\n",
                                   rec->code, (void *)dsc->cmd_w_table, sd);
                                SEAP_cmdrec_free (rec);
                                return (NULL);
                        case -1:
                                _D("Can't register async command handler: id=%u, tbl=%p, sd=%u: errno=%u, %s.\n",
                                   rec->code, (void *)dsc->cmd_w_table, sd, errno, strerror (errno));
                                SEAP_cmdrec_free (rec);
                                return (NULL);
                        default:
                                SEAP_cmdrec_free (rec);
                                errno = EDOOFUS;
                                return (NULL);
                        }
                        
                        if (SEAP_packet_send (ctx, sd, packet) != 0) {
                                protect_errno {
                                        _D("FAIL: errno=%u, %s.\n", errno, strerror (errno));
                                        /* TODO: unregister handler */
                                        SEAP_packet_free (packet);
                                }
                                return (NULL);
                        }
                        
                        if (pthread_cond_wait (&h.cond, &h.mtx) != 0) {
                                abort ();
                        } else {

                                _D("cond return: h.args=%p\n", h.args);
                                
                                if (h.args == NULL)
                                        res = NULL;
                                else if (func != NULL)
                                        res = func (h.args, funcarg);
                                else
                                        res = h.args;
                                
                                pthread_mutex_unlock (&(h.mtx));
                                pthread_cond_destroy (&(h.cond));
                                pthread_mutex_destroy (&(h.mtx));
                        }
                        
                        SEAP_packet_free (packet);
                        
                        return (res);
                }
                case SEAP_CMDTYPE_ASYNC:
                        cmdptr->flags |= SEAP_CMDFLAG_ASYNC;
                        
                        /* Register handler */
                        rec = SEAP_cmdrec_new ();
                        rec->code = cmdptr->id;
                        rec->func = func;
                        rec->arg  = funcarg;
                        
                        switch (SEAP_cmdtbl_add (dsc->cmd_w_table, rec)) {
                        case 0:
                                break;
                        case SEAP_CMDTBL_ECOLL:
                                _D("Can't register async command handler: id=%u, tbl=%p, sd=%u: already registered.\n",
                                   rec->code, (void *)dsc->cmd_w_table, sd);
                                SEAP_cmdrec_free (rec);
                                return (NULL);
                        case -1:
                                _D("Can't register async command handler: id=%u, tbl=%p, sd=%u: errno=%u, %s.\n",
                                   rec->code, (void *)dsc->cmd_w_table, sd, errno, strerror (errno));
                                SEAP_cmdrec_free (rec);
                                return (NULL);
                        default:
                                SEAP_cmdrec_free (rec);
                                errno = EDOOFUS;
                                return (NULL);
                        }
                        
                        if (SEAP_packet_send (ctx, sd, packet) != 0) {
                                protect_errno {
                                        _D("FAIL: errno=%u, %s.\n", errno, strerror (errno));
                                        /* TODO: unregister handler */
                                        SEAP_packet_free (packet);
                                }
                                return (NULL);
                        }
                        
                        SEAP_packet_free (packet);

                        return (args);
                default:
                        errno = EINVAL;
                        return (NULL);
                }
        }
        
        /* NOTREACHED */
        errno = EDOOFUS;
        return (NULL);
}

SEAP_cmdjob_t *SEAP_cmdjob_new (void)
{
        SEAP_cmdjob_t *j;

        j = sm_talloc (SEAP_cmdjob_t);
        j->ctx = NULL;
        j->sd  = -1;
        
        return (j);
}

void SEAP_cmdjob_free (SEAP_cmdjob_t *j)
{
        sm_free (j);
}
