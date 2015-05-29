/*
 * The Qubes OS Project, http://www.qubes-os.org
 *
 * Copyright (C) 2010  Rafal Wojtczuk  <rafal@invisiblethingslab.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */
#define _CRT_SECURE_NO_WARNINGS
#define LIBVCHAN_EXPORTS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libvchan.h"
#include "libvchan_private.h"

libvchan_t *libvchan_server_init(int domain, int port, size_t read_min, size_t write_min) {
    char xs_path[255];
    libvchan_t *ctrl;

    ctrl = malloc(sizeof(*ctrl));
    if (!ctrl)
        return NULL;

    snprintf(xs_path, sizeof(xs_path), "data/vchan/%d/%d", domain, port);
    ctrl->xenvchan = libxenvchan_server_init(NULL, domain, xs_path, read_min, write_min);
    if (!ctrl->xenvchan) {
        free(ctrl);
        return NULL;
    }

    if (ERROR_SUCCESS != XenifaceOpen(&ctrl->xc_handle)) {
        libxenvchan_close(ctrl->xenvchan);
        free(ctrl);
        return NULL;
    }

    ctrl->xs_path = _strdup(xs_path);
    ctrl->xenvchan->blocking = 1;
    ctrl->remote_domain = domain;
    return ctrl;
}

libvchan_t *libvchan_client_init(int domain, int port) {
    char xs_path[255];
    char xs_path_watch[255];
    libvchan_t *ctrl = NULL;
    HANDLE xc_handle = NULL;
    char own_domid[16];
    DWORD status;
    HANDLE path_watch_event = NULL;
    PVOID path_watch_handle = NULL;

    if (ERROR_SUCCESS != XenifaceOpen(&xc_handle)) {
        goto fail;
    }

    /* wait for server to appear */
    status = StoreRead(xc_handle, "domid", sizeof(own_domid), own_domid);
    if (status != ERROR_SUCCESS) {
        fprintf(stderr, "Cannot get own domid\n");
        goto fail;
    }
    
    if (atoi(own_domid) == domain) {
        fprintf(stderr, "Loopback vchan connection not supported\n");
        goto fail;
    }

    path_watch_event = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!path_watch_event)
        goto fail;

    snprintf(xs_path, sizeof(xs_path), "/local/domain/%d/data/vchan/%s/%d", domain, own_domid, port);
    /* watch on this key as we might not have access to the whole directory */
    snprintf(xs_path_watch, sizeof(xs_path_watch), "%s/event-channel", xs_path);

    status = StoreAddWatch(xc_handle, xs_path_watch, path_watch_event, &path_watch_handle);
    if (status != ERROR_SUCCESS) {
        fprintf(stderr, "Cannot setup watch on %s\n", xs_path_watch);
        goto fail;
    }

    status = WaitForSingleObject(path_watch_event, INFINITE);
    if (status != WAIT_OBJECT_0) {
        fprintf(stderr, "Wait for xenstore failed: 0x%x\n", status);
        goto fail;
    }

    StoreRemoveWatch(xc_handle, path_watch_handle);
    CloseHandle(path_watch_event);

    ctrl = malloc(sizeof(*ctrl));
    if (!ctrl)
        goto fail;

    ctrl->xs_path = NULL;
    ctrl->xenvchan = libxenvchan_client_init(NULL, domain, xs_path);
    if (!ctrl->xenvchan) {
        goto fail;
    }
    
    ctrl->xenvchan->blocking = 1;
    /* notify server */
    EvtchnNotify(xc_handle, ctrl->xenvchan->event_port);
    ctrl->remote_domain = domain;
    ctrl->xc_handle = xc_handle;
    return ctrl;

fail:
    if (path_watch_handle)
        StoreRemoveWatch(xc_handle, path_watch_handle);
    if (path_watch_event)
        CloseHandle(path_watch_event);
    if (xc_handle)
        XenifaceClose(xc_handle);
    if (ctrl)
        free(ctrl);
    return NULL;
}