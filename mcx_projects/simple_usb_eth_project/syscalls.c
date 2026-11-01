
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <stdint.h>

/*
 * Bare-metal Newlib syscall implementations.
 *
 * stdout/stderr are handled separately by _write()
 * in retarget_stdio.c.
 */

int _close(int fd)
{
    (void)fd;
    errno = EBADF;
    return -1;
}

int _fstat(int fd, struct stat *st)
{
    if (fd >= 0 && fd <= 2)
    {
        st->st_mode = S_IFCHR;
        return 0;
    }

    errno = EBADF;
    return -1;
}

int _isatty(int fd)
{
    if (fd >= 0 && fd <= 2)
        return 1;

    errno = EBADF;
    return 0;
}

int _lseek(int fd, int offset, int whence)
{
    (void)fd;
    (void)offset;
    (void)whence;

    errno = ESPIPE;
    return -1;
}

int _read(int fd, char *buf, int len)
{
    (void)buf;
    (void)len;

    if (fd != 0)
    {
        errno = EBADF;
        return -1;
    }

    /* stdin is not implemented. */
    errno = EAGAIN;
    return -1;
}

int _getpid(void)
{
    return 1;
}

int _kill(int pid, int sig)
{
    (void)pid;
    (void)sig;

    errno = EINVAL;
    return -1;
}
