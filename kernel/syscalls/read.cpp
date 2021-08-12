/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
*/

// includes
#include <kernel/Debug.h>
#include <kernel/filesystem/FileDescription.h>
#include <kernel/Process.h>

namespace Kernel {

using BlockFlags = Thread::FileBlocker::BlockFlags;

KResultOr<FlatPtr> Process::sys$readv(int fd, Userspace<const struct iovec*> iov, int iov_count)
{
    VERIFY_PROCESS_BIG_LOCK_ACQUIRED(this)
    REQUIRE_PROMISE(stdio);
    if (iov_count < 0)
        return EINVAL;

    if (iov_count > (int)MiB)
        return EFAULT;

    u64 total_length = 0;
    Vector<iovec, 32> vecs;
    if (!vecs.try_resize(iov_count))
        return ENOMEM;
    if (!copy_n_from_user(vecs.data(), iov, iov_count))
        return EFAULT;
    for (auto& vec : vecs) {
        total_length += vec.iov_len;
        if (total_length > NumericLimits<i32>::max())
            return EINVAL;
    }

    auto description = fds().file_description(fd);
    if (!description)
        return EBADF;

    if (!description->is_readable())
        return EBADF;

    if (description->is_directory())
        return EISDIR;

    int nread = 0;
    for (auto& vec : vecs) {
        if (description->is_blocking()) {
            if (!description->can_read()) {
                auto unblock_flags = BlockFlags::None;
                if (Thread::current()->block<Thread::ReadBlocker>({}, *description, unblock_flags).was_interrupted())
                    return EINTR;
                if (!has_flag(unblock_flags, BlockFlags::Read))
                    return EAGAIN;

            }
        }
        auto buffer = UserOrKernelBuffer::for_user_buffer((u8*)vec.iov_base, vec.iov_len);
        if (!buffer.has_value())
            return EFAULT;
        auto result = description->read(buffer.value(), vec.iov_len);
        if (result.is_error())
            return result.error();
        nread += result.value();
    }

    return nread;
}

KResultOr<FlatPtr> Process::sys$read(int fd, Userspace<u8*> buffer, size_t size)
{
    VERIFY_PROCESS_BIG_LOCK_ACQUIRED(this)
    REQUIRE_PROMISE(stdio);
    if (size == 0)
        return 0;
    if (size > NumericLimits<ssize_t>::max())
        return EINVAL;
    dbgln_if(IO_DEBUG, "sys$read({}, {}, {})", fd, buffer.ptr(), size);
    auto description = fds().file_description(fd);
    if (!description)
        return EBADF;
    if (!description->is_readable())
        return EBADF;
    if (description->is_directory())
        return EISDIR;
    if (description->is_blocking()) {
        if (!description->can_read()) {
            auto unblock_flags = BlockFlags::None;
            if (Thread::current()->block<Thread::ReadBlocker>({}, *description, unblock_flags).was_interrupted())
                return EINTR;
            if (!has_flag(unblock_flags, BlockFlags::Read))
                return EAGAIN;

        }
    }
    auto user_buffer = UserOrKernelBuffer::for_user_buffer(buffer, size);
    if (!user_buffer.has_value())
        return EFAULT;
    auto result = description->read(user_buffer.value(), size);
    if (result.is_error())
        return result.error();
    return result.value();
}

}
