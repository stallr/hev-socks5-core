/*
 ============================================================================
 Name        : hev-socks5-logger.c
 Author      : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2021 hev
 Description : Socks5 Logger
 ============================================================================
 */

#include <time.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#endif
#include <unistd.h>
#include <sys/uio.h>
#include <sys/stat.h>

#include "hev-socks5-logger.h"
#include "hev-socks5-logger-priv.h"

static int fd = -1;
static HevSocks5LoggerLevel req_level;
static atomic_flag logger_lock = ATOMIC_FLAG_INIT;

static void
hev_socks5_logger_lock (void)
{
    while (atomic_flag_test_and_set_explicit (&logger_lock,
                                              memory_order_acquire))
        ;
}

static void
hev_socks5_logger_unlock (void)
{
    atomic_flag_clear_explicit (&logger_lock, memory_order_release);
}

static int
hev_socks5_logger_localtime (const time_t *now, struct tm *out)
{
#ifdef _WIN32
    return localtime_s (out, now);
#else
    return localtime_r (now, out) ? 0 : -1;
#endif
}

static int
hev_socks5_logger_open_null (void)
{
#ifdef _WIN32
    return open ("NUL", O_WRONLY | O_APPEND);
#else
    return open ("/dev/null", O_WRONLY | O_APPEND);
#endif
}

int
hev_socks5_logger_init (HevSocks5LoggerLevel level, const char *path)
{
    int res = 0;

    hev_socks5_logger_lock ();
    req_level = level;

    if (fd >= 0) {
        close (fd);
        fd = -1;
    }

    if ((!path) || (!path[0]))
        path = "stderr";

    if ((0 == strcmp (path, "none")) || (0 == strcmp (path, "off")) ||
        (0 == strcmp (path, "null")))
        fd = -1;
    else if (0 == strcmp (path, "stdout"))
        fd = dup (1);
    else if (0 == strcmp (path, "stderr"))
        fd = dup (2);
    else
        fd = open (path, O_WRONLY | O_APPEND | O_CREAT, 0640);

    if ((0 == strcmp (path, "none")) || (0 == strcmp (path, "off")) ||
        (0 == strcmp (path, "null"))) {
        hev_socks5_logger_unlock ();
        return 0;
    }

    if (fd < 0)
        fd = hev_socks5_logger_open_null ();

    if (fd < 0)
        res = -1;

    hev_socks5_logger_unlock ();
    return res;
}

void
hev_socks5_logger_fini (void)
{
    hev_socks5_logger_lock ();
    if (fd >= 0) {
        close (fd);
        fd = -1;
    }
    hev_socks5_logger_unlock ();
}

int
hev_socks5_logger_enabled (HevSocks5LoggerLevel level)
{
    int enabled;

    hev_socks5_logger_lock ();
    enabled = level >= req_level && fd >= 0;
    hev_socks5_logger_unlock ();

    return enabled ? 1 : 0;
}

void
hev_socks5_logger_log (HevSocks5LoggerLevel level, const char *fmt, ...)
{
    struct iovec iov[4];
    const char *ts_fmt;
    char msg[1024];
    struct tm ti;
    char ts[32];
    time_t now;
    va_list ap;
    int len;
    int msg_len;

    hev_socks5_logger_lock ();
    if (level < req_level || fd < 0)
        goto unlock;

    time (&now);
    if (hev_socks5_logger_localtime (&now, &ti) != 0)
        goto unlock;

    ts_fmt = "[%04u-%02u-%02u %02u:%02u:%02u] ";
    len = snprintf (ts, sizeof (ts), ts_fmt, 1900 + ti.tm_year, 1 + ti.tm_mon,
                    ti.tm_mday, ti.tm_hour, ti.tm_min, ti.tm_sec);
    if (len < 0)
        goto unlock;
    if ((size_t)len >= sizeof (ts))
        len = (int)sizeof (ts) - 1;

    iov[0].iov_base = ts;
    iov[0].iov_len = (size_t)len;

    switch (level) {
    case HEV_SOCKS5_LOGGER_DEBUG:
        iov[1].iov_base = "[D] ";
        break;
    case HEV_SOCKS5_LOGGER_INFO:
        iov[1].iov_base = "[I] ";
        break;
    case HEV_SOCKS5_LOGGER_WARN:
        iov[1].iov_base = "[W] ";
        break;
    case HEV_SOCKS5_LOGGER_ERROR:
        iov[1].iov_base = "[E] ";
        break;
    case HEV_SOCKS5_LOGGER_UNSET:
        iov[1].iov_base = "[?] ";
        break;
    }
    iov[1].iov_len = 4;

    va_start (ap, fmt);
    msg_len = vsnprintf (msg, sizeof (msg), fmt, ap);
    va_end (ap);
    if (msg_len < 0)
        goto unlock;
    if ((size_t)msg_len >= sizeof (msg))
        msg_len = (int)sizeof (msg) - 1;
    iov[2].iov_base = msg;
    iov[2].iov_len = (size_t)msg_len;

    iov[3].iov_base = "\n";
    iov[3].iov_len = 1;

    if (writev (fd, iov, 4)) {
        /* ignore return value */
    }

unlock:
    hev_socks5_logger_unlock ();
}
