/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef _KERNEL_IO_SOCKETS_SOCKET_H
#define _KERNEL_IO_SOCKETS_SOCKET_H

#include <fs/vfs.h>
#include <libkern/syscall_structs.h>
#include <libkern/types.h>

#define MAX_SOCKET_COUNT 16

int socket_create(int domain, int type, int protocol, file_descriptor_t* fd, file_ops_t* ops);
socket_t* socket_duplicate(socket_t* sock);
int socket_put(socket_t* sock);

#endif /* _KERNEL_IO_SOCKETS_SOCKET_H */