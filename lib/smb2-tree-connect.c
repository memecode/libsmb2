/* -*-  mode:c; tab-width:8; c-basic-offset:8; indent-tabs-mode:nil;  -*- */
/*
   Copyright (C) 2016 by Ronnie Sahlberg <ronniesahlberg@gmail.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License as published by
   the Free Software Foundation; either version 2.1 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.
*/
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef STDC_HEADERS
#include <stddef.h>
#endif

#include <errno.h>

#include "smb2.h"
#include "libsmb2.h"
#include "libsmb2-private.h"

static int
smb2_encode_tree_connect_request(struct smb2_context *smb2,
                                 struct smb2_pdu *pdu,
                                 struct smb2_tree_connect_request *req)
{
        int len;
        char *buf;
        struct smb2_iovec *iov;
        
        len = SMB2_TREE_CONNECT_REQUEST_SIZE & 0xfffffffe;
        buf = malloc(len);
        if (buf == NULL) {
                smb2_set_error(smb2, "Failed to allocate tree connect setup "
                               "buffer");
                return -1;
        }
        memset(buf, 0, len);
        
        iov = smb2_add_iovector(smb2, &pdu->out, buf, len, free);
        
        smb2_set_uint16(iov, 0, SMB2_TREE_CONNECT_REQUEST_SIZE);
        smb2_set_uint16(iov, 2, req->flags);
        /* path offset */
        smb2_set_uint16(iov, 4, SMB2_HEADER_SIZE + len);
        smb2_set_uint16(iov, 6, req->path_length);


        /* Path */
        iov = smb2_add_iovector(smb2, &pdu->out,
                                malloc(req->path_length),
                                req->path_length,
                                free);

        memcpy(iov->buf, req->path, req->path_length);

        if (smb2_pad_to_64bit(smb2, &pdu->out) != 0) {
                return -1;
        }

        return 0;
}

static int
smb2_decode_tree_connect_reply(struct smb2_context *smb2,
                               struct smb2_pdu *pdu,
                               struct smb2_tree_connect_reply *rep)
{
        uint16_t struct_size;

        smb2_get_uint16(&pdu->in.iov[0], 0, &struct_size);
        if (struct_size != SMB2_TREE_CONNECT_REPLY_SIZE) {
                smb2_set_error(smb2, "Unexpected size of Tree Connect reply. "
                               "Expected %d, got %d",
                               SMB2_TREE_CONNECT_REPLY_SIZE,
                               (int)pdu->in.iov[0].len);
                return -1;
        }

        smb2_get_uint8(&pdu->in.iov[0], 2, &rep->share_type);
        smb2_get_uint32(&pdu->in.iov[0], 4, &rep->share_flags);
        smb2_get_uint32(&pdu->in.iov[0], 4, &rep->capabilities);
        smb2_get_uint32(&pdu->in.iov[0], 4, &rep->maximal_access);

        return 0;
}

int smb2_cmd_tree_connect_async(struct smb2_context *smb2,
                                struct smb2_tree_connect_request *req,
                                smb2_command_cb cb, void *cb_data)
{
        struct smb2_pdu *pdu;

        pdu = smb2_allocate_pdu(smb2, SMB2_TREE_CONNECT, cb, cb_data);
        if (pdu == NULL) {
                return -1;
        }

        if (smb2_encode_tree_connect_request(smb2, pdu, req)) {
                smb2_free_pdu(smb2, pdu);
                return -1;
        }
        
        if (smb2_queue_pdu(smb2, pdu)) {
                smb2_free_pdu(smb2, pdu);
                return -1;
        }

        return 0;
}

int smb2_process_tree_connect_reply(struct smb2_context *smb2,
                                    struct smb2_pdu *pdu)
{
        struct smb2_tree_connect_reply reply;

        /* Update tree ID to use for future PDUs */
        smb2->tree_id = pdu->header.sync.tree_id;
        
        if (smb2_decode_tree_connect_reply(smb2, pdu, &reply) < 0) {
                pdu->cb(smb2, -EBADMSG, NULL, pdu->cb_data);
                return -1;
        }

        pdu->cb(smb2, pdu->header.status, &reply, pdu->cb_data);

        return 0;
}
