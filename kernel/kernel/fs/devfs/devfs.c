/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <algo/dynamic_array.h>
#include <drivers/driver_manager.h>
#include <fs/devfs/devfs.h>
#include <fs/vfs.h>
#include <libkern/bits/errno.h>
#include <libkern/libkern.h>
#include <libkern/lock.h>
#include <libkern/log.h>
#include <mem/kmalloc.h>
#include <tasking/proc.h>

#define DEVFS_ZONE_SIZE 32 * KB

static dynamic_array_t entry_zones; /* Just store pointers to free zones one time. */
static void* next_space_to_put_entry;
static uint32_t free_space_in_last_entry_zone;

static dynamic_array_t name_zones; /* Just store pointers to free zones one time. */
static void* next_space_to_put_name;
static uint32_t free_space_in_last_name_zone;

static dynamic_array_t ops_zones; /* Just store pointers to free zones one time. */
static void* next_space_to_put_ops;
static uint32_t free_space_in_last_ops_zone;

static devfs_inode_t* devfs_root;

static uint32_t next_inode_index = 2;

static lock_t _devfs_lock;

/**
 * Zones Management
 */

static int _devfs_alloc_entry_zone()
{
    void* new_zone = kmalloc(DEVFS_ZONE_SIZE);
    if (!new_zone) {
        return ENOMEM;
    }

    dynamic_array_push(&entry_zones, &new_zone);
    next_space_to_put_entry = new_zone;
    free_space_in_last_entry_zone = DEVFS_ZONE_SIZE;
    return 0;
}

static int _devfs_alloc_name_zone()
{
    void* new_zone = kmalloc(DEVFS_ZONE_SIZE);
    if (!new_zone) {
        return -ENOMEM;
    }

    dynamic_array_push(&name_zones, &new_zone);
    next_space_to_put_name = new_zone;
    free_space_in_last_name_zone = DEVFS_ZONE_SIZE;
    return 0;
}

static int _devfs_alloc_ops_zone()
{
    void* new_zone = kmalloc(DEVFS_ZONE_SIZE);
    if (!new_zone) {
        return -ENOMEM;
    }

    dynamic_array_push(&ops_zones, &new_zone);
    next_space_to_put_ops = new_zone;
    free_space_in_last_ops_zone = DEVFS_ZONE_SIZE;
    return 0;
}

static devfs_inode_t* _devfs_get_entry(uint32_t indx)
{
    uint32_t entries_per_zone = DEVFS_ZONE_SIZE / DEVFS_INODE_LEN;
    uint32_t zones_count = entry_zones.size;
    if (indx >= zones_count * entries_per_zone) {
        return 0;
    }

    uint32_t requested_zone = indx / entries_per_zone;
    uint32_t index_within_zone = indx % entries_per_zone;
    uint32_t* tmp_ptr = dynamic_array_get(&entry_zones, requested_zone);
    devfs_inode_t* res = (devfs_inode_t*)*tmp_ptr;
    if (!res) {
        return 0;
    }
    return &res[index_within_zone];
}

/**
 * Memory Allocation
 */

static devfs_inode_t* _devfs_new_entry()
{
    if (free_space_in_last_entry_zone < sizeof(devfs_inode_t)) {
        _devfs_alloc_entry_zone();
    }

    devfs_inode_t* res = (devfs_inode_t*)next_space_to_put_entry;
    next_space_to_put_entry += sizeof(devfs_inode_t);
    free_space_in_last_entry_zone -= sizeof(devfs_inode_t);
    memset(res, 0, DEVFS_INODE_LEN);
    res->index = next_inode_index++;
    return res;
}

static char* _devfs_new_name(int len)
{
    len = (len + 1 + 0x3) & (uint32_t)(~0b11);
    if (free_space_in_last_name_zone < len) {
        _devfs_alloc_name_zone();
    }

    char* res = (char*)next_space_to_put_name;
    next_space_to_put_name += len;
    free_space_in_last_name_zone -= len;
    memset(res, 0, len);
    return res;
}

static file_ops_t* _devfs_new_ops()
{
    if (free_space_in_last_ops_zone < sizeof(file_ops_t)) {
        _devfs_alloc_ops_zone();
    }

    file_ops_t* res = (file_ops_t*)next_space_to_put_ops;
    next_space_to_put_ops += sizeof(file_ops_t);
    free_space_in_last_ops_zone -= sizeof(file_ops_t);
    return res;
}

static inline devfs_inode_t* _devfs_get_devfs_inode(uint32_t inode_indx)
{
    /*  Since in vfs, we use a rule that 2nd inode is a root inode, so, let's
        follow this rule here. But in our storage elements are indexed from 0,
        so we need put up with this. */
    return _devfs_get_entry(inode_indx - 2);
}

/**
 * Inode Tools
 */

static int _devfs_add_to_list(devfs_inode_t* parent, devfs_inode_t* new_entry)
{
    if (!parent || !new_entry) {
        return -EINVAL;
    }

    if (!parent->last) {
        parent->first = new_entry;
        parent->last = new_entry;
        new_entry->prev = 0;
        new_entry->next = 0;
    } else {
        parent->last->next = new_entry;
        new_entry->prev = parent->last;
        parent->last = new_entry;
        new_entry->next = 0;
    }

    new_entry->parent = parent;

    return 0;
}

static devfs_inode_t* _devfs_alloc_entry(devfs_inode_t* parent)
{
    devfs_inode_t* new_entry = _devfs_new_entry();
    if (!new_entry) {
        return 0;
    }
    if (_devfs_add_to_list(parent, new_entry) < 0) {
        /* Bringing back allocation */
        next_space_to_put_entry -= sizeof(devfs_inode_t);
        free_space_in_last_entry_zone += sizeof(devfs_inode_t);
        return 0;
    }
    return new_entry;
}

static int _devfs_set_name(devfs_inode_t* entry, const char* name, uint32_t len)
{
    if (!entry || !name) {
        return -EINVAL;
    }
    if (len > 255) {
        return -2;
    }

    char* name_space = _devfs_new_name(len);
    if (!name_space) {
        return -ENOMEM;
    }

    memcpy((void*)name_space, (void*)name, len);
    entry->name = name_space;
    return 0;
}

static int _devfs_set_handlers(devfs_inode_t* entry, const file_ops_t* ops)
{
    if (!entry) {
        return -EINVAL;
    }

    file_ops_t* ops_space = _devfs_new_ops();
    if (!ops_space) {
        return -ENOMEM;
    }

    memcpy(ops_space, ops, sizeof(file_ops_t));
    entry->handlers = ops_space;
    return 0;
}

/**
 * FS Tools
 */

static int _devfs_setup_root()
{
    devfs_root = _devfs_new_entry();
    devfs_root->index = 2;
    devfs_root->mode = S_IFDIR;
    return 0;
}

/**
 * VFS Api
 */

fsdata_t devfs_data(dentry_t* dentry)
{
    /* Set everything to 0, since devfs isn't supposed to be used by several devices.
       All of mount of devfs will show the same info. */
    fsdata_t fsdata;
    fsdata.sb = 0;
    fsdata.gt = 0;
    return fsdata;
}

int devfs_prepare_fs(vfs_device_t* vdev)
{
    dynamic_array_init(&name_zones, sizeof(void*));
    dynamic_array_init(&entry_zones, sizeof(void*));
    dynamic_array_init(&ops_zones, sizeof(void*));
    _devfs_setup_root();
    return 0;
}

int devfs_read_inode(dentry_t* dentry)
{
    lock_acquire(&_devfs_lock);
    /*  We currently have a uniqueue structure of inode for devfs. So we need to be
        confident using inode in vfs, since inode and devfs_inode are not similar. */
    devfs_inode_t* devfs_inode = _devfs_get_devfs_inode(dentry->inode_indx);
    if (!devfs_inode) {
        lock_release(&_devfs_lock);
        return -EFAULT;
    }
    memcpy((void*)dentry->inode, (void*)devfs_inode, DEVFS_INODE_LEN);
    lock_release(&_devfs_lock);
    return 0;
}

int devfs_write_inode(dentry_t* dentry)
{
    lock_acquire(&_devfs_lock);
    devfs_inode_t* devfs_inode = _devfs_get_devfs_inode(dentry->inode_indx);
    if (!devfs_inode) {
        lock_release(&_devfs_lock);
        return -EFAULT;
    }
    memcpy((void*)devfs_inode, (void*)dentry->inode, DEVFS_INODE_LEN);
    lock_release(&_devfs_lock);
    return 0;
}

int devfs_free_inode(dentry_t* dentry)
{
    return 0;
}

int devfs_getdents(dentry_t* dir, uint8_t* buf, uint32_t* offset, uint32_t len)
{
    devfs_inode_t* devfs_inode = (devfs_inode_t*)dir->inode;

    /* Currently we leave, dir when offset is the max number. */
    if (*offset == 0xffffffff) {
        return 0;
    }

    int already_read = 0;
    dirent_t tmp;
    tmp.name = 0;

    /* Return . */
    if (*offset == 0) {
        ssize_t read = vfs_helper_write_dirent((dirent_t*)(buf + already_read), len, devfs_inode->index, ".");
        if (read <= 0) {
            if (!already_read) {
                return -EINVAL;
            }
            return already_read;
        }
        already_read += read;
        len -= read;
        *offset = 1;
    }

    /* Return .. */
    if (*offset == 1) {
        uint32_t inode_index = devfs_inode->index;
        if (devfs_inode->parent) {
            inode_index = devfs_inode->parent->index;
        }

        ssize_t read = vfs_helper_write_dirent((dirent_t*)(buf + already_read), len, inode_index, "..");
        if (read <= 0) {
            if (!already_read) {
                return -EINVAL;
            }
            return already_read;
        }
        already_read += read;
        len -= read;
        *offset = 2;
    }

    /* Scanining dir from the start */
    lock_acquire(&_devfs_lock);
    if (*offset == 2) {
        if (!devfs_inode->first) {
            lock_release(&_devfs_lock);
            return 0;
        }
        *offset = (uint32_t)devfs_inode->first;
    }

    while (*offset != 0xffffffff) {
        devfs_inode_t* child_devfs_inode = (devfs_inode_t*)*offset;
        ssize_t read = vfs_helper_write_dirent((dirent_t*)(buf + already_read), len, child_devfs_inode->index, child_devfs_inode->name);
        if (read <= 0) {
            lock_release(&_devfs_lock);
            if (!already_read) {
                return -EINVAL;
            }
            return already_read;
        }
        already_read += read;
        len -= read;
        if (child_devfs_inode->next) {
            *offset = (uint32_t)child_devfs_inode->next;
        } else {
            *offset = 0xffffffff;
        }
    }

    lock_release(&_devfs_lock);
    return already_read;
}

int devfs_lookup(dentry_t* dir, const char* name, uint32_t len, dentry_t** result)
{
    devfs_inode_t* devfs_inode = (devfs_inode_t*)dir->inode;

    if (len == 1) {
        if (name[0] == '.') {
            *result = dentry_get(dir->dev_indx, devfs_inode->index);
            return 0;
        }
    }

    if (len == 2) {
        if (name[0] == '.' && name[1] == '.') {
            *result = dentry_get(dir->dev_indx, devfs_inode->parent->index);
            return 0;
        }
    }

    devfs_inode_t* child = devfs_inode->first;
    while (child) {
        uint32_t child_name_len = strlen(child->name);
        if (len == child_name_len) {
            if (strncmp(name, child->name, len) == 0) {
                *result = dentry_get(dir->dev_indx, child->index);
                return 0;
            }
        }
        child = child->next;
    }

    return -ENOENT;
}

int devfs_mkdir_dummy(dentry_t* dir, const char* name, uint32_t len, mode_t mode)
{
    return -1;
}

int devfs_rmdir_dummy(dentry_t* dir)
{
    return -1;
}

int devfs_open(dentry_t* dentry, file_descriptor_t* fd, uint32_t flags)
{
    devfs_inode_t* devfs_inode = (devfs_inode_t*)dentry->inode;
    if (devfs_inode->handlers->open) {
        return devfs_inode->handlers->open(dentry, fd, flags);
    }
    /*  The device doesn't have custom open, so returns ENOEXEC in this case 
        according to vfs. */
    return -ENOEXEC;
}

int devfs_can_read(dentry_t* dentry, uint32_t start)
{
    devfs_inode_t* devfs_inode = (devfs_inode_t*)dentry->inode;
    if (devfs_inode->handlers->can_read) {
        return devfs_inode->handlers->can_read(dentry, start);
    }
    return true;
}

int devfs_can_write(dentry_t* dentry, uint32_t start)
{
    devfs_inode_t* devfs_inode = (devfs_inode_t*)dentry->inode;
    if (devfs_inode->handlers->can_write) {
        return devfs_inode->handlers->can_write(dentry, start);
    }
    return true;
}

int devfs_read(dentry_t* dentry, uint8_t* buf, uint32_t start, uint32_t len)
{
    devfs_inode_t* devfs_inode = (devfs_inode_t*)dentry->inode;
    if (devfs_inode->handlers->read) {
        return devfs_inode->handlers->read(dentry, buf, start, len);
    }
    return -EFAULT;
}

int devfs_write(dentry_t* dentry, uint8_t* buf, uint32_t start, uint32_t len)
{
    devfs_inode_t* devfs_inode = (devfs_inode_t*)dentry->inode;
    if (devfs_inode->handlers->write) {
        return devfs_inode->handlers->write(dentry, buf, start, len);
    }
    return -EFAULT;
}

int devfs_fstat(dentry_t* dentry, fstat_t* stat)
{
    devfs_inode_t* devfs_inode = (devfs_inode_t*)dentry->inode;
    stat->dev = devfs_inode->dev_id;
    stat->ino = devfs_inode->index;
    stat->mode = devfs_inode->mode;
    // Calling a custom fstat
    if (devfs_inode->handlers->fstat) {
        return devfs_inode->handlers->fstat(dentry, stat);
    }
    return 0;
}

int devfs_ioctl(dentry_t* dentry, uint32_t cmd, uint32_t arg)
{
    devfs_inode_t* devfs_inode = (devfs_inode_t*)dentry->inode;
    if (devfs_inode->handlers->ioctl) {
        return devfs_inode->handlers->ioctl(dentry, cmd, arg);
    }
    return -EFAULT;
}

proc_zone_t* devfs_mmap(dentry_t* dentry, mmap_params_t* params)
{
    devfs_inode_t* devfs_inode = (devfs_inode_t*)dentry->inode;
    if (devfs_inode->handlers->mmap) {
        return devfs_inode->handlers->mmap(dentry, params);
    }
    /* If we don't have a custom impl, let's used a std one */
    return (proc_zone_t*)VFS_USE_STD_MMAP;
}

/**
 * Driver install functions.
 */

driver_desc_t _devfs_driver_info()
{
    driver_desc_t fs_desc = { 0 };
    fs_desc.type = DRIVER_FILE_SYSTEM;
    fs_desc.auto_start = false;
    fs_desc.is_device_driver = false;
    fs_desc.is_device_needed = false;
    fs_desc.is_driver_needed = false;
    fs_desc.functions[DRIVER_NOTIFICATION] = NULL;
    fs_desc.functions[DRIVER_FILE_SYSTEM_RECOGNIZE] = NULL;
    fs_desc.functions[DRIVER_FILE_SYSTEM_PREPARE_FS] = devfs_prepare_fs;
    fs_desc.functions[DRIVER_FILE_SYSTEM_CAN_READ] = devfs_can_read;
    fs_desc.functions[DRIVER_FILE_SYSTEM_CAN_WRITE] = devfs_can_write;
    fs_desc.functions[DRIVER_FILE_SYSTEM_OPEN] = devfs_open;
    fs_desc.functions[DRIVER_FILE_SYSTEM_READ] = devfs_read;
    fs_desc.functions[DRIVER_FILE_SYSTEM_WRITE] = devfs_write;
    fs_desc.functions[DRIVER_FILE_SYSTEM_TRUNCATE] = NULL;
    fs_desc.functions[DRIVER_FILE_SYSTEM_MKDIR] = devfs_mkdir_dummy;
    fs_desc.functions[DRIVER_FILE_SYSTEM_RMDIR] = devfs_rmdir_dummy;
    fs_desc.functions[DRIVER_FILE_SYSTEM_EJECT_DEVICE] = NULL;

    fs_desc.functions[DRIVER_FILE_SYSTEM_READ_INODE] = devfs_read_inode;
    fs_desc.functions[DRIVER_FILE_SYSTEM_WRITE_INODE] = devfs_write_inode;
    fs_desc.functions[DRIVER_FILE_SYSTEM_FREE_INODE] = devfs_free_inode;
    fs_desc.functions[DRIVER_FILE_SYSTEM_GET_FSDATA] = devfs_data;
    fs_desc.functions[DRIVER_FILE_SYSTEM_LOOKUP] = devfs_lookup;
    fs_desc.functions[DRIVER_FILE_SYSTEM_GETDENTS] = devfs_getdents;
    fs_desc.functions[DRIVER_FILE_SYSTEM_CREATE] = NULL;
    fs_desc.functions[DRIVER_FILE_SYSTEM_UNLINK] = NULL;
    fs_desc.functions[DRIVER_FILE_SYSTEM_FSTAT] = devfs_fstat;
    fs_desc.functions[DRIVER_FILE_SYSTEM_IOCTL] = devfs_ioctl;
    fs_desc.functions[DRIVER_FILE_SYSTEM_MMAP] = devfs_mmap;

    return fs_desc;
}

void devfs_install()
{
    driver_install(_devfs_driver_info(), "devfs");
}

/**
 * Register a device.
 */

devfs_inode_t* devfs_mkdir(dentry_t* dir, const char* name, uint32_t len)
{
    lock_acquire(&_devfs_lock);
    devfs_inode_t* devfs_inode = (devfs_inode_t*)dir->inode;
    devfs_inode_t* new_entry = _devfs_alloc_entry(devfs_inode);
    if (!new_entry) {
        lock_release(&_devfs_lock);
        return 0;
    }

    dentry_set_flag(dir, DENTRY_DIRTY);

    new_entry->mode = S_IFDIR;
    _devfs_set_name(new_entry, name, len);

    lock_release(&_devfs_lock);
    return new_entry;
}

devfs_inode_t* devfs_register(dentry_t* dir, uint32_t devid, const char* name, uint32_t len, mode_t mode, const file_ops_t* handlers)
{
    lock_acquire(&_devfs_lock);
    devfs_inode_t* devfs_inode = (devfs_inode_t*)dir->inode;
    devfs_inode_t* new_entry = _devfs_alloc_entry(devfs_inode);
    if (!new_entry) {
        lock_release(&_devfs_lock);
        return NULL;
    }

    new_entry->dev_id = devid;
    new_entry->mode = mode;
    _devfs_set_name(new_entry, name, len);
    _devfs_set_handlers(new_entry, handlers);
    dentry_set_flag(dir, DENTRY_DIRTY);

    lock_release(&_devfs_lock);
    return new_entry;
}

int devfs_mount()
{
    lock_init(&_devfs_lock);
    dentry_t* mp;
    if (vfs_resolve_path("/dev", &mp) < 0) {
        return -ENOENT;
    }
    int driver_id = vfs_get_fs_id("devfs");
    if (driver_id < 0) {
        log("Devfs: no driver is installed, exiting");
        return -ENOENT;
    }
    log("devfs: %x", driver_id);
    int err = vfs_mount(mp, new_virtual_device(DEVICE_STORAGE), driver_id);
    dentry_put(mp);
    if (!err) {
        dm_send_notification(DM_NOTIFICATION_DEVFS_READY, 0);
    }
    return err;
}