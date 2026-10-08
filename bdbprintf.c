/*
 * Buffered Debug Printf for Amiga C89
 * Adapted from aukboopsi for Petmate.
 */
#ifdef USE_DEBUG_BDBPRINT
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <dos/dosextens.h>

#define BDB_BUFFER_SIZE 4096

static char bdb_buffer[BDB_BUFFER_SIZE];
static volatile int bdb_position = 0;

extern struct Task *myTask;

int bdbprintf(const char *format, ...)
{
    va_list args;
    int remaining;
    int written;

    /* Called from several processes (main, network, thumb...): the
     * read-position/append/advance must not interleave with another
     * bdbprintf() or with flushbdbprint()'s reset, or lines get lost or
     * overwritten. vsnprintf() never Wait()s, so Forbid() is safe here. */
    Forbid();
    remaining = BDB_BUFFER_SIZE - bdb_position - 1;
    if (remaining <= 0) { Permit(); return 0; }

    va_start(args, format);
    written = vsnprintf(bdb_buffer + bdb_position, remaining + 1, format, args);
    va_end(args);

    if (written > remaining) written = remaining;
    if (written > 0) bdb_position += written;
    Permit();

    if (myTask) Signal(myTask, SIGBREAKF_CTRL_F);
    return written;
}

void flushbdbprint(void)
{
    /* Main process only. Take the text out under Forbid() and print the
     * copy afterwards: Printf() can block on the console, and another
     * process's bdbprintf() appending meanwhile used to be wiped by the
     * reset that followed it. */
    static char out[BDB_BUFFER_SIZE];
    int len;

    Forbid();
    len = bdb_position;
    if (len > 0) {
        CopyMem(bdb_buffer, out, len);
        bdb_position = 0;
        bdb_buffer[0] = '\0';
    }
    Permit();

    if (len > 0) {
        out[len] = '\0';
        /* out holds arbitrary logged text (URLs, etc.) -- never pass it as
         * Printf()'s own format string, or a stray '%' in logged data
         * makes Printf() read nonexistent varargs off the stack (format-
         * string bug, easy Address Error trigger on m68k). */
        Printf("%s", out);
        fflush(stdout);
    }
}

void clearbdbprint(void)
{
    Forbid();
    bdb_position = 0;
    bdb_buffer[0] = '\0';
    Permit();
}

int bdbavailable(void)
{
    return BDB_BUFFER_SIZE - bdb_position - 1;
}

/* Instance tracking */
static volatile int instances_created = 0;
static volatile int instances_disposed = 0;

int bdbprintf_new(const char *className, void *instance)
{
    instances_created++;
    return bdbprintf("[NEW] %s instance:%lx (total:%ld)\n",
                     className, (unsigned long)instance, (long)instances_created);
}

int bdbprintf_dispose(const char *className, void *instance)
{
    instances_disposed++;
    return bdbprintf("[DISPOSE] %s instance:%lx (total:%ld)\n",
                     className, (unsigned long)instance, (long)instances_disposed);
}

void bdbprintf_report_leaks(void)
{
    long leaked = instances_created - instances_disposed;
    if (leaked != 0) {
        bdbprintf("\n*** BOOPSI LEAK: created=%ld disposed=%ld leaked=%ld ***\n",
                  (long)instances_created, (long)instances_disposed, leaked);
        flushbdbprint();
    } else if (instances_created > 0) {
        bdbprintf("\nBOOPSI: %ld instances, no leaks.\n", (long)instances_created);
        flushbdbprint();
    }
}

/* Class tracking */
static volatile int classes_made = 0;
static volatile int classes_freed = 0;

int bdbprintf_makeclass(const char *className, void *classPtr)
{
    classes_made++;
    (void)className; (void)classPtr;
    return 0;
}

int bdbprintf_freeclass(const char *className, void *classPtr)
{
    classes_freed++;
    (void)className; (void)classPtr;
    return 0;
}

void bdbprintf_report_classes(void)
{
    long leaked = classes_made - classes_freed;
    if (leaked != 0) {
        bdbprintf("\n*** CLASS LEAK: made=%ld freed=%ld leaked=%ld ***\n",
                  (long)classes_made, (long)classes_freed, leaked);
        flushbdbprint();
    } else if (classes_made > 0) {
        bdbprintf("\nClasses: %ld made/freed, no leaks.\n", (long)classes_made);
        flushbdbprint();
    }
}
#endif
