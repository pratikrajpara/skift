#include <skift/__plugs__.h>

#include <skift/iostream.h>
#include <skift/atomic.h>
#include <skift/assert.h>

#include "kernel/serial.h"
#include "kernel/sheduler.h"
#include "kernel/process.h"
#include "kernel/memory.h"

/* --- Framework initialization --------------------------------------------- */ 

iostream_t *in_stream ;
iostream_t *out_stream;
iostream_t *err_stream;
iostream_t *log_stream;

iostream_t internal_log_stream = {0};

int log_stream_write(iostream_t* stream, const void* buffer, uint size)
{
    UNUSED(stream);
    return serial_write(buffer, size);
}

void __plug_init(void)
{
    internal_log_stream.write = log_stream_write;

    in_stream  = NULL;
    out_stream = &internal_log_stream;
    err_stream = &internal_log_stream;
    log_stream = &internal_log_stream;
}

void __plug_assert_failed(const char *expr, const char *file, const char *function, int line)
{
    sk_log(LOG_FATAL, "Kernel assert failed: %s in %s:%s() ln%d!", (char *)expr, (char *)file, (char *)function, line);
    PANIC("Kernel assert failed (see logs).");
}

/* --- Systeme API ---------------------------------------------------------- */

// We are the system so we does't need that ;)
void __plug_system_get_info(system_info_t* info)
{
    UNUSED(info);
    assert(false);
}

void __plug_system_get_status(system_status_t * status)
{
    UNUSED(status);
    assert(false);
}

/* --- Memory allocator plugs ----------------------------------------------- */

int __plug_memalloc_lock()
{
    sk_atomic_begin();
    return 0;
}

int __plug_memalloc_unlock()
{
    sk_atomic_end();
    return 0;
}

void *__plug_memalloc_alloc(uint size)
{
    void *p = (void *)memory_alloc(memory_kpdir(), size, 0);
    sk_log(LOG_DEBUG, "Allocated %d pages for the kernel at %08x.", size, p);
    return p;
}

int __plug_memalloc_free(void *memory, uint size)
{
    memory_free(memory_kpdir(), (uint)memory, size, 0);
    sk_log(LOG_DEBUG, "Free'ed %d pages for the kernel at %08x.", size, memory);
    return 0;
}

/* --- Logger plugs --------------------------------------------------------- */

int __plug_logger_lock()
{
    sk_atomic_begin();
    return 0;
}

int __plug_logger_unlock()
{
    sk_atomic_end();
    return 0;
}

/* --- Iostream plugs ------------------------------------------------------- */

int __plug_iostream_open(const char *file_path, iostream_flag_t flags)
{
    return process_open_file(sheduler_running_process(), file_path, flags);
}

int __plug_iostream_close(int fd)
{
    return process_close_file(sheduler_running_process(), fd);
}

int __plug_iostream_read(int fd, void *buffer, uint size)
{
    return process_read_file(sheduler_running_process(), fd, buffer, size);
}

int __plug_iostream_write(int fd, const void *buffer, uint size)
{
    return process_write_file(sheduler_running_process(), fd, buffer, size);
}

int __plug_iostream_ioctl(int fd, int request, void *args)
{
    return process_ioctl_file(sheduler_running_process(), fd, request, args);
}

int __plug_iostream_seek(int fd, int offset, iostream_whence_t whence)
{
    return process_seek_file(sheduler_running_process(), fd, offset, whence);
}

int __plug_iostream_tell(int fd, iostream_whence_t whence)
{
    return process_tell_file(sheduler_running_process(), fd, whence);
}

int __plug_iostream_fstat(int fd, iostream_stat_t *stat)
{
    return process_fstat_file(sheduler_running_process(), fd, stat);
}