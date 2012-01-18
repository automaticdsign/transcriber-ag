/*========================================================================*
 * TITLE:       Dynamic loading 
 * PORT:        Ross Linder
 *
 * Interface for stupid non ELF OS's
 *========================================================================*/

#ifdef __hpux
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "ag_dlfcn.h"

/*
 * This is a minimal implementation of the ELF dlopen, dlclose, dlsym
 * and dlerror routines based on HP's shl_load, shl_unload and
 * shl_findsym. */

/*
 * Reference Counting.
 *
 * Empirically it looks like the HP routines do not maintain a
 * reference count, so I maintain one here.
 */

typedef struct lib_entry
{
    shl_t handle;
    int count;
    struct lib_entry *next;
}
*LibEntry;

#define lib_entry_handle(e) ((e)->handle)
#define lib_entry_count(e) ((e)->count)
#define lib_entry_next(e) ((e)->next)
#define set_lib_entry_handle(e,v) ((e)->handle = (v))
#define set_lib_entry_count(e,v) ((e)->count = (v))
#define set_lib_entry_next(e,v) ((e)->next = (v))
#define increment_lib_entry_count(e) ((e)->count++)
#define decrement_lib_entry_count(e) ((e)->count--)

static LibEntry Entries = NULL;

static LibEntry find_lib_entry(shl_t handle)
{
    LibEntry entry;

    for (entry = Entries; entry != NULL; entry = lib_entry_next(entry))
        if (lib_entry_handle(entry) == handle)
            return entry;
    return NULL;
}

static LibEntry new_lib_entry(shl_t handle)
{
    LibEntry entry;

    if ((entry = (LibEntry) malloc(sizeof(struct lib_entry))) != NULL)
    {
        set_lib_entry_handle(entry, handle);
        set_lib_entry_count(entry, 1);
        set_lib_entry_next(entry, Entries);
        Entries = entry;
    }
    return entry;
}

static void free_lib_entry(LibEntry entry)
{
    if (entry == Entries)
        Entries = lib_entry_next(entry);
    else
    {
        LibEntry last, next;
        for (last = Entries, next = lib_entry_next(last);
                    next != NULL;
                    last = next, next = lib_entry_next(last))
        {
            if (entry == next)
            {
                set_lib_entry_next(last, lib_entry_next(entry));
                break;
            }
        }
    }
    free(entry);
}


/*
 * Error Handling.
 */

#define ERRBUFSIZE 1000

static char errbuf[ERRBUFSIZE];
static int dlerrno = 0;

char *dlerror(void)
{
    return dlerrno ? errbuf : NULL;
}


/*
 * Opening and Closing Liraries.
 */

void *dlopen(const char *fname, int mode)
{
    shl_t handle;
    LibEntry entry = NULL;

    dlerrno = 0;
    if (fname == NULL)
        handle = PROG_HANDLE;
    else
    {
        handle = shl_load(fname, mode, 0L);
        if (handle != NULL)
        {
            if ((entry = find_lib_entry(handle)) == NULL)
            {
                if ((entry = new_lib_entry(handle)) == NULL)
                {
                    shl_unload(handle);
                    handle = NULL;
                }
            }
            else
                increment_lib_entry_count(entry);
        }
        if (handle == NULL)
        {
            char *errstr;
            dlerrno = errno;
            errstr = strerror(errno);
            if (errno == NULL) errstr = "???";
            sprintf(errbuf, "can't open %s: %s", fname, errstr);
        }
    }
#ifdef DEBUG
    printf("opening library %s, handle = %x, count = %d\n",
           fname, handle, entry ? lib_entry_count(entry) : -1);
    if (dlerrno) printf("%s\n", dlerror());
#endif
    return (void *) handle;
}

int dlclose(void *handle)
{
    LibEntry entry;
#ifdef DEBUG
    entry = find_lib_entry(handle);
    printf("closing library handle = %x, count = %d\n",
           handle, entry ? lib_entry_count(entry) : -1);
#endif

    dlerrno = 0;
    if ((shl_t) handle == PROG_HANDLE)
        return 0; /* ignore attempts to close main program */
    else
    {

        if ((entry = find_lib_entry((shl_t) handle)) != NULL)
        {
            decrement_lib_entry_count(entry);
            if (lib_entry_count(entry) > 0)
                return 0;
            else
            {
                /* unload once reference count reaches zero */
                free_lib_entry(entry);
                if (shl_unload((shl_t) handle) == 0)
                    return 0;
            }
        }
        /* if you get to here, an error has occurred */
        dlerrno = 1;
        sprintf(errbuf, "attempt to close library failed");
#ifdef DEBUG
        printf("%s\n", dlerror());
#endif
        return -1;
    }
}


/*
 * Symbol Lookup.
 */

void *dlsym(void *handle, const char *name)
{
    void *f;
    shl_t myhandle;

    dlerrno = 0;
    myhandle = (handle == NULL) ? PROG_HANDLE : (shl_t) handle;

    if (shl_findsym(&myhandle, name, TYPE_PROCEDURE, &f) != 0)
    {
        dlerrno = 1;
        sprintf(errbuf, "symbol %s not found", name);
        f = NULL;
    }

    return(f);
}
#endif

#ifdef _WIN32

#include <windows.h>
#include <stdio.h>
#include "ag_dlfcn.h"

static char errbuf[512];

void *dlopen(const char *name, int mode)
{
    HINSTANCE hdll;

    errbuf[0] = 0;

    hdll = LoadLibrary(name);
#ifdef _WIN32
    if (! hdll)
    {
        sprintf(errbuf, "error code %d loading library %s", GetLastError(), name);
        return NULL;
    }
#else
    if ((UINT) hdll < 32)
    {
        sprintf(errbuf, "error code %d loading library %s", (UINT) hdll, name);
        return NULL;
    }
#endif
    return (void *) hdll;
}

void *dlsym(void *lib, const char *name)
{
    HMODULE hdll = (HMODULE) lib;
    void *symAddr;
    errbuf[0] = 0;

    symAddr = (void *) GetProcAddress(hdll, name);
    if (symAddr == NULL)
        sprintf(errbuf, "can't find symbol %s", name);
    return symAddr;
}

int dlclose(void *lib)
{
    HMODULE hdll = (HMODULE) lib;

    errbuf[0] = 0;
#ifdef _WIN32
    if (FreeLibrary(hdll))
        return 0;
    else
    {
        sprintf(errbuf, "error code %d closing library", GetLastError());
        return -1;
    }
#else
    FreeLibrary(hdll);
    return 0;
#endif
}

char *dlerror()
{
    return errbuf[0] ? errbuf : NULL;
}

int lim_connect ()
{
    return 0;
}
#endif

