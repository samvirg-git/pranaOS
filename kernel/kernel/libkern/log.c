/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <drivers/generic/uart.h>
#include <libkern/libkern.h>
#include <libkern/lock.h>
#include <libkern/log.h>
#include <libkern/stdarg.h>

// Turn off lock debug output for log.
#ifdef DEBUG_LOCK
#undef lock_acquire
#undef lock_release
#endif

typedef int (*_putch_callback)(char ch, char* buf_base, size_t* written, void* callback_params);

static lock_t _log_lock;
static const char* HEX_alphabet = "0123456789ABCDEF";
static const char* hex_alphabet = "0123456789abcdef";

static int _printf_hex32_impl(unsigned int value, const char* alph, char* base_buf, size_t* written, _putch_callback callback, void* callback_params)
{
    int nxt = 0;
    char tmp_buf[16];

    while (value > 0) {
        tmp_buf[nxt++] = alph[(value % 16)];
        value /= 16;
    }

    callback('0', base_buf, written, callback_params);
    callback('x', base_buf, written, callback_params);
    if (nxt == 0) {
        callback('0', base_buf, written, callback_params);
    }

    while (nxt) {
        callback(tmp_buf[--nxt], base_buf, written, callback_params);
    }
    return 0;
}

static int _printf_hex64_impl(unsigned long value, const char* alph, char* base_buf, size_t* written, _putch_callback callback, void* callback_params)
{
    int nxt = 0;
    char tmp_buf[32];

    while (value > 0) {
        tmp_buf[nxt++] = alph[(value % 16)];
        value /= 16;
    }

    callback('0', base_buf, written, callback_params);
    callback('x', base_buf, written, callback_params);
    if (nxt == 0) {
        callback('0', base_buf, written, callback_params);
    }

    while (nxt) {
        callback(tmp_buf[--nxt], base_buf, written, callback_params);
    }
    return 0;
}

static int _printf_hex32(unsigned int value, char* base_buf, size_t* written, _putch_callback callback, void* callback_params)
{
    return _printf_hex32_impl(value, hex_alphabet, base_buf, written, callback, callback_params);
}

static int _printf_HEX32(unsigned int value, char* base_buf, size_t* written, _putch_callback callback, void* callback_params)
{
    return _printf_hex32_impl(value, HEX_alphabet, base_buf, written, callback, callback_params);
}

static int _printf_hex64(unsigned long value, char* base_buf, size_t* written, _putch_callback callback, void* callback_params)
{
    return _printf_hex64_impl(value, hex_alphabet, base_buf, written, callback, callback_params);
}

static int _printf_HEX64(unsigned long value, char* base_buf, size_t* written, _putch_callback callback, void* callback_params)
{
    return _printf_hex64_impl(value, HEX_alphabet, base_buf, written, callback, callback_params);
}

static int _printf_u32(unsigned int value, char* base_buf, size_t* written, _putch_callback callback, void* callback_params)
{
    int nxt = 0;
    char tmp_buf[16];

    while (value > 0) {
        tmp_buf[nxt++] = (value % 10) + '0';
        value /= 10;
    }

    if (nxt == 0) {
        callback('0', base_buf, written, callback_params);
    }

    while (nxt) {
        callback(tmp_buf[--nxt], base_buf, written, callback_params);
    }
    return 0;
}

static int _printf_u64(unsigned long value, char* base_buf, size_t* written, _putch_callback callback, void* callback_params)
{
    int nxt = 0;
    char tmp_buf[32];

    while (value > 0) {
        tmp_buf[nxt++] = (value % 10) + '0';
        value /= 10;
    }

    if (nxt == 0) {
        callback('0', base_buf, written, callback_params);
    }

    while (nxt) {
        callback(tmp_buf[--nxt], base_buf, written, callback_params);
    }
    return 0;
}

static int _printf_i32(int value, char* buf, size_t* written, _putch_callback callback, void* callback_params)
{
    if (value < 0) {
        callback('-', buf, written, callback_params);
        value = -value;
    }
    return _printf_u32(value, buf, written, callback, callback_params);
}

static int _printf_i64(long value, char* buf, size_t* written, _putch_callback callback, void* callback_params)
{
    if (value < 0) {
        callback('-', buf, written, callback_params);
        value = -value;
    }
    return _printf_u64(value, buf, written, callback, callback_params);
}

static int _printf_string(const char* value, char* buf, size_t* written, _putch_callback callback, void* callback_params)
{
    size_t len = strlen(value);
    for (size_t i = 0; i < len; i++) {
        callback(value[i], buf, written, callback_params);
    }
    return 0;
}

static ssize_t _printf_internal(char* buf, const char* format, _putch_callback callback, void* callback_params, va_list arg)
{
    const char* p = format;
    size_t written = 0;
    while (*p) {
        int l_arg = 0;
        int h_arg = 0;
        if (*p == '%' && *(p + 1)) {
            // Reading arguments
        parse_args:
            p++;
            switch (*p) {
            case 'l':
                l_arg++;
                if (*(p + 1)) {
                    goto parse_args;
                }
                break;
            case 'h':
                h_arg++;
                if (*(p + 1)) {
                    goto parse_args;
                }
                break;
            default:
                break;
            }

            // Reading conversion specifiers
            switch (*p) {
            case 'i':
            case 'd':
                if (l_arg) {
                    long value = va_arg(arg, long);
                    _printf_i64(value, buf, &written, callback, callback_params);
                } else {
                    int value = va_arg(arg, int);
                    _printf_i32(value, buf, &written, callback, callback_params);
                }
                break;
            case 'u':
                if (l_arg) {
                    uint64_t value = va_arg(arg, uint64_t);
                    _printf_u64(value, buf, &written, callback, callback_params);
                } else {
                    uint32_t value = va_arg(arg, uint32_t);
                    _printf_u32(value, buf, &written, callback, callback_params);
                }
                break;
            case 'x':
                if (l_arg) {
                    uint64_t value = va_arg(arg, uint64_t);
                    _printf_hex64(value, buf, &written, callback, callback_params);
                } else {
                    uint32_t value = va_arg(arg, uint32_t);
                    _printf_hex32(value, buf, &written, callback, callback_params);
                }
                break;
            case 'X':
                if (l_arg) {
                    uint64_t value = va_arg(arg, uint64_t);
                    _printf_HEX64(value, buf, &written, callback, callback_params);
                } else {
                    uint32_t value = va_arg(arg, uint32_t);
                    _printf_HEX32(value, buf, &written, callback, callback_params);
                }
                break;
            case 'c': {
                char value = (char)va_arg(arg, int);
                callback(value, buf, &written, callback_params);
            } break;
            case 's': {
                const char* value = va_arg(arg, const char*);
                _printf_string(value, buf, &written, callback, callback_params);
            } break;
            default:
                break;
            }
        } else {
            callback(*p, buf, &written, callback_params);
        }
        p++;
    }
    return written;
}

static int putch_callback_sized_buf(char ch, char* buf_base, size_t* written, void* callback_params)
{
    if (!callback_params) {
        return -1;
    }

    if (!written) {
        return -1;
    }

    size_t n = *(size_t*)callback_params;
    size_t vw = *written;
    if (vw >= n) {
        return -1;
    }
    buf_base[vw++] = ch;
    *written = vw;
    return 0;
}

int vsnprintf(char* s, size_t n, const char* format, va_list arg)
{
    if (!s) {
        return 0;
    }

    if (!n) {
        return 0;
    }

    ssize_t wr = _printf_internal(s, format, putch_callback_sized_buf, &n, arg);
    if (wr == n) {
        s[n - 1] = '\0';
    } else {
        s[wr] = '\0';
    }
    return (int)wr;
}

int snprintf(char* s, size_t n, const char* format, ...)
{
    va_list arg;
    va_start(arg, format);
    int res = vsnprintf(s, n, format, arg);
    va_end(arg);
    return res;
}

static int putch_callback_buf(char ch, char* buf_base, size_t* written, void* callback_params)
{
    if (!written) {
        return -1;
    }

    size_t vw = *written;
    buf_base[vw++] = ch;
    *written = vw;
    return 0;
}

int vsprintf(char* s, const char* format, va_list arg)
{
    if (!s) {
        return 0;
    }
    ssize_t wr = _printf_internal(s, format, putch_callback_buf, NULL, arg);
    s[wr] = '\0';
    return (int)wr;
}

int sprintf(char* s, const char* format, ...)
{
    va_list arg;
    va_start(arg, format);
    int res = vsprintf(s, format, arg);
    va_end(arg);
    return res;
}

static int putch_callback_stream(char c, char* buf_base, size_t* written, void* callback_params)
{
    return uart_write(COM1, c);
}

static int vlog_unfmt(const char* format, va_list arg)
{
    return _printf_internal(NULL, format, putch_callback_stream, NULL, arg);
}

static int vlog_fmt(const char* init_msg, const char* format, va_list arg)
{
    vlog_unfmt(init_msg, arg);
    vlog_unfmt(format, arg);
    if (format[-1] != '\n') {
        vlog_unfmt("\n", arg);
    }
    return 0;
}

int log(const char* format, ...)
{
    lock_acquire(&_log_lock);
    va_list arg;
    va_start(arg, format);
    int ret = vlog_fmt("\033[1;37m[LOG]\033[0m  ", format, arg);
    va_end(arg);
    lock_release(&_log_lock);
    return ret;
}

int log_warn(const char* format, ...)
{
    lock_acquire(&_log_lock);
    va_list arg;
    va_start(arg, format);
    int ret = vlog_fmt("\033[1;33m[WARN]\033[0m ", format, arg);
    va_end(arg);
    lock_release(&_log_lock);
    return ret;
}

int log_error(const char* format, ...)
{
    lock_acquire(&_log_lock);
    va_list arg;
    va_start(arg, format);
    int ret = vlog_fmt("\033[1;31m[ERR]\033[0m  ", format, arg);
    va_end(arg);
    lock_release(&_log_lock);
    return ret;
}

int log_not_formatted(const char* format, ...)
{
    lock_acquire(&_log_lock);
    va_list arg;
    va_start(arg, format);
    int ret = vlog_unfmt(format, arg);
    va_end(arg);
    lock_release(&_log_lock);
    return ret;
}

void logger_setup()
{
    lock_init(&_log_lock);
    uart_setup(COM1);
}