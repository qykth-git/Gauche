/*
 * port.c - port implementation
 *
 *  Copyright(C) 2000-2001 by Shiro Kawai (shiro@acm.org)
 *
 *  Permission to use, copy, modify, distribute this software and
 *  accompanying documentation for any purpose is hereby granted,
 *  provided that existing copyright notices are retained in all
 *  copies and that this notice is included verbatim in all
 *  distributions.
 *  This software is provided as is, without express or implied
 *  warranty.  In no circumstances the author(s) shall be liable
 *  for any damages arising out of the use of this software.
 *
 *  $Id: port.c,v 1.46 2002-02-07 10:33:51 shirok Exp $
 */

#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#define LIBGAUCHE_BODY
#include "gauche.h"

/*================================================================
 * Common
 */

static void port_print(ScmObj obj, ScmPort *port, ScmWriteContext *ctx);
static void port_finalize(GC_PTR obj, GC_PTR data);

SCM_DEFINE_BUILTIN_CLASS_SIMPLE(Scm_PortClass, port_print);

/* Cleaning up:
 *   The underlying file descriptor/stream may be closed when the port
 *   is explicitly closed by close-port, or implicitly destroyed by
 *   garbage collector.  To keep consistency, Scheme ports should never
 *   share the same file descriptor.  However, C code and Scheme port
 *   may share the same file descriptor for efficiency (e.g. stdios).
 *   In such cases, it is C code's responsibility to destroy the port.
 */
static int port_cleanup(ScmPort *port)
{
    switch (SCM_PORT_TYPE(port)) {
    case SCM_PORT_FILE:
        if (SCM_PORT_OWNER_P(port)) {
            return fclose(port->src.file.fp);
        }
        break;
    case SCM_PORT_PROC:
        if (port->src.proc.vtable->Close) {
            port->src.proc.vtable->Flush(SCM_PORT(port));
            return port->src.proc.vtable->Close(SCM_PORT(port));
        }
        break;
    }
    return 0;
}

/* called by GC */
static void port_finalize(GC_PTR obj, GC_PTR data)
{
    port_cleanup((ScmPort *)obj);
}

/*
 * Internal Constructor.
 *   If this port owns the underlying file descriptor/stream, 
 *   ownerp must be TRUE.
 */
static ScmPort *make_port(int dir, int type, int ownerp)
{
    ScmPort *port;
    GC_finalization_proc ofn; GC_PTR ocd;

    port = SCM_NEW(ScmPort);
    SCM_SET_CLASS(port, SCM_CLASS_PORT);
    port->direction = dir;
    port->type = type;
    port->scrcnt = 0;
    port->ungotten = SCM_CHAR_INVALID;
    port->ownerp = ownerp;
    port->icpolicy = SCM_PORT_IC_IGNORE; /* default */
    if (ownerp) {
        GC_REGISTER_FINALIZER(port,
                              port_finalize,
                              NULL,
                              &ofn, &ocd);
    }
    return port;
}

/*
 * Close
 */
ScmObj Scm_ClosePort(ScmPort *port)
{
    int result = port_cleanup(port);
    port->type = SCM_PORT_CLOSED;
    return result? SCM_FALSE : SCM_TRUE;
}

/*===============================================================
 * Getting information
 */
ScmObj Scm_PortName(ScmPort *port)
{
    ScmObj z = SCM_NIL;
    
    switch (SCM_PORT_TYPE(port)) {
    case SCM_PORT_FILE:
        if (SCM_STRINGP(port->src.file.name)) z = port->src.file.name;
        else z = SCM_MAKE_STR("(unknown file)");
        break;
    case SCM_PORT_ISTR:
        z = SCM_MAKE_STR("(input string)");
        break;
    case SCM_PORT_OSTR:
        z = SCM_MAKE_STR("(output string)");
        break;
    case SCM_PORT_CLOSED:
        z = SCM_MAKE_STR("(closed port)");
        break;
    case SCM_PORT_PROC:
        {
            ScmProcPortInfo *info = port->src.proc.vtable->Info(port);
            z = info ? info->name : SCM_MAKE_STR("(proc port)");
        }
        break;
    default:
        Scm_Panic("Scm_PortName: something screwed up");
        /*NOTREACHED*/
    }
    return z;
}

int Scm_PortLine(ScmPort *port)
{
    int l = 0;
    
    switch (SCM_PORT_TYPE(port)) {
    case SCM_PORT_FILE:
        l = port->src.file.line;
        break;
    case SCM_PORT_ISTR:;
    case SCM_PORT_OSTR:;
    case SCM_PORT_CLOSED:
        l = -1;
        break;
    case SCM_PORT_PROC:
        {
            ScmProcPortInfo *info = port->src.proc.vtable->Info(port);
            l = info ? info->line : -1;
        }
        break;
    default:
        Scm_Panic("Scm_PortLine: something screwed up");
        /*NOTREACHED*/
    }
    return l;
}

int Scm_PortPosition(ScmPort *port)
{
    int pos = 0;
    
    switch (SCM_PORT_TYPE(port)) {
    case SCM_PORT_FILE:
        pos = port->src.file.column;
        break;
    case SCM_PORT_ISTR:;
        pos = port->src.istr.current - port->src.istr.start;
    case SCM_PORT_OSTR:
        pos = SCM_DSTRING_SIZE(&port->src.ostr);
        break;
    case SCM_PORT_CLOSED:;
        pos = -1;
        break;
    case SCM_PORT_PROC:
        {
            ScmProcPortInfo *info = port->src.proc.vtable->Info(port);
            pos = info ? info->position : -1;
        }
        break;
    default:
        Scm_Panic("Scm_PortLine: something screwed up");
        /*NOTREACHED*/
    }
    return pos;
}

static void port_print(ScmObj obj, ScmPort *port, ScmWriteContext *ctx)
{
    Scm_Printf(port, "#<%s%sport %A %p>",
               (SCM_PORT_DIR(obj)&SCM_PORT_INPUT)? "i" : "",
               (SCM_PORT_DIR(obj)&SCM_PORT_OUTPUT)? "o" : "",
               Scm_PortName(SCM_PORT(obj)),
               obj);
}

/* Returns port's associated file descriptor number, if any.
   Returns -1 otherwise. */
int Scm_PortFileNo(ScmPort *port)
{
    if (SCM_PORT_TYPE(port) == SCM_PORT_FILE) {
        return fileno(port->src.file.fp);
    } else if (SCM_PORT_TYPE(port) == SCM_PORT_PROC) {
        ScmProcPortInfo *info = port->src.proc.vtable->Info(port);
        if (info) {
            if (info->fd >= 0) return info->fd;
            if (info->fp) return fileno(info->fp);
        }
        return -1;
    } else {
        return -1;
    }
}

int Scm_CharReady(ScmPort *port)
{
    /* WRITEME */
    return TRUE;
}

/*===============================================================
 * File Port
 */

ScmObj Scm_MakeFilePort(FILE *fp, ScmObj name, const char *mode, int ownerp)
{
    int dir;
    ScmPort *p;
    if (*mode == 'r') dir = SCM_PORT_INPUT;
    else              dir = SCM_PORT_OUTPUT;
    p = make_port(dir, SCM_PORT_FILE, ownerp);
    p->src.file.fp = fp;
    p->src.file.line = 1;
    p->src.file.column = 1;
    p->src.file.name = name;
    return SCM_OBJ(p);
}

ScmObj Scm_OpenFilePort(const char *path, const char *mode)
{
    FILE *fp;

    if (*mode == 'c' || *mode == 'W' || *mode == 'A') {
        /* Special modes:
            c : create file exclusively.  fails if file exists.
            W : opens existing file.  fails if file doesn't exist.
            A : same as 'W', but file pointer seeks to the end.
         */
        int fd, rdwr = FALSE, bin = FALSE, flags = 0;
        char c, op = *mode;
        while ((c = *++mode) != 0) {
            if (c == 'b') bin = TRUE;
            if (c == '+') rdwr = TRUE;
        }
        if (op == 'c') flags = O_CREAT|O_EXCL;
        if (rdwr) flags |= O_RDWR;
        else      flags |= O_WRONLY;
        if (op != 'A') flags |= O_TRUNC;

        fd = Scm_SysCall(open(path, flags, 0666));
        if (fd < 0) {
            if (errno == EEXIST || errno == ENOENT) return SCM_FALSE;
            Scm_SysError("couldn't open %s", path);
        }
        mode = ((op == 'A')?
                (bin? (rdwr? "ab+" : "ab") : (rdwr? "a+" : "a")) :
                (bin? (rdwr? "wb+" : "wb") : (rdwr? "w+" : "w")));
        fp = fdopen(fd, mode);
    } else {
        fp = fopen(path, mode);
    }
    if (fp == NULL) {
        Scm_SigCheck(Scm_VM());
        return SCM_FALSE;
    }
    return Scm_MakeFilePort(fp, SCM_MAKE_STR_COPYING(path), mode, TRUE);
}

/*
 * Auxiliary function for macros
 * These functions should be called only from the associated macros.
 * Error check is omitted for better performance.
 */

/* Called from SCM_FILE_GETC, when it finds the char is multibyte. 
   Assuming ungotten and incomplete buffer is empty.
   If EOF is found in the middle of the character, keep the read bytes
   in the incomplete buffer and return EOF. */

int Scm__PortFileGetc(int prefetch, ScmPort *port)
{
    char *p;
    int next, nfollows, i, ch;

    port->scrcnt = 0;
    port->scratch[0] = prefetch;
    nfollows = SCM_CHAR_NFOLLOWS(prefetch);
    for (i=1; i<= nfollows; i++) {
        next = getc(port->src.file.fp);
        if (next == EOF) {
            Scm_SigCheck(Scm_VM());
            return EOF;
        }
        if (next == '\n') port->src.file.line++;
        port->scratch[i] = next;
        port->scrcnt++;
    }
    p = port->scratch;
    SCM_CHAR_GET(p, ch);
    port->scrcnt = 0;
    return ch;
}

/* Called from SCM_GETB, when there's an ungotten char or buffered
   incomplete char. */
int Scm__PortGetbInternal(ScmPort *port)
{
    int ch = 0;
    
    if (SCM_PORT_UNGOTTEN(port)) {
        char *p = port->scratch;
        ch = port->ungotten;
        port->scrcnt = SCM_CHAR_NBYTES(ch);
        SCM_CHAR_PUT(p, ch);
        port->ungotten = SCM_CHAR_INVALID;
    }
    if (!port->scrcnt) {
        /* This shouldn't happen, but just in case ... */
        SCM_GETB(ch, port);
    } else {
        int i;
        ch = port->scratch[0];
        for (i=1; i<port->scrcnt; i++) {
            port->scratch[i-1] = port->scratch[i];
        }
        port->scrcnt--;
    }
    return ch;
}

/* Called from SCM_GETC, when there's a buffered incomplete char. */
int Scm__PortGetcInternal(ScmPort *port)
{
    int ch = 0, nfollows = 0;
    char *p;
    
    if (!port->scrcnt) {
        /* this shouldn't happen, but just in case ... */
        SCM_GETC(ch, port);
    } else {
        /* fill the buffer */
        nfollows = SCM_CHAR_NFOLLOWS(port->scratch[0]);
        for (; port->scrcnt <= nfollows; port->scrcnt++) {
            int b = 0;
            switch (SCM_PORT_TYPE(port)) {
              case SCM_PORT_FILE: SCM__FILE_GETB(b, port); break;
              case SCM_PORT_ISTR: SCM__ISTR_GETB(b, port); break;
              case SCM_PORT_PROC: SCM__PROC_GETB(b, port); break;
              default: Scm_Panic("getc: something screwed up");
                  /*NOTREACHED*/
            }
            if (b == EOF) return EOF;
            port->scratch[port->scrcnt] = b;
        }
        p = port->scratch;
        SCM_CHAR_GET(p, ch);
    }
    return ch;
}

/*===============================================================
 * String port
 */

ScmObj Scm_MakeInputStringPort(ScmString *str)
{
    ScmPort *z = make_port(SCM_PORT_INPUT, SCM_PORT_ISTR, FALSE);
    z->src.istr.start = SCM_STRING_START(str);
    z->src.istr.rest  = SCM_STRING_SIZE(str);
    z->src.istr.current = z->src.istr.start;
    return SCM_OBJ(z);
}

ScmObj Scm_MakeOutputStringPort(void)
{
    ScmPort *z = make_port(SCM_PORT_OUTPUT, SCM_PORT_OSTR, FALSE);
    Scm_DStringInit(&z->src.ostr);
    return SCM_OBJ(z);
}

ScmObj Scm_GetOutputString(ScmPort *port)
{
    if (SCM_PORT_TYPE(port) != SCM_PORT_OSTR)
        Scm_Error("output string port required, but got %S", port);
    return Scm_DStringGet(&SCM_PORT(port)->src.ostr);
}

/*===============================================================
 * Generic procedures
 */

void Scm_Putb(ScmByte b, ScmPort *port)
{
    SCM_PUTB(b, port);
}

void Scm_Putc(ScmChar c, ScmPort *port)
{
    SCM_PUTC(c, port);
}

void Scm_Puts(ScmString *s, ScmPort *port)
{
    SCM_PUTS(s, port);
}

void Scm_Putz(const char *s, int len, ScmPort *port)
{
    SCM_PUTZ(s, len, port);
}

void Scm_Putnl(ScmPort *port)
{
    SCM_PUTNL(port);
}

void Scm_Flush(ScmPort *port)
{
    SCM_FLUSH(port);
}

void Scm_Ungetc(ScmChar ch, ScmPort *port)
{
    SCM_UNGETC(ch, port);
}

int Scm_Getb(ScmPort *port)
{
    int b = 0;
    SCM_GETB(b, port);
    return b;
}

int Scm_Getc(ScmPort *port)
{
    int c = 0;
    SCM_GETC(c, port);
    return c;
}

/*
 * Getz - block read.
 */
int Scm_Getz(char *buf, int buflen, ScmPort *port)
{
    int nread = 0;
    
    if (!SCM_IPORTP(port))
        Scm_Error("input port required, but got %S", SCM_OBJ(port));
    switch (SCM_PORT_TYPE(port)) {
    case SCM_PORT_FILE:
        nread = fread(buf, 1, buflen, port->src.file.fp);
        if (nread < 0 && errno == EINTR) Scm_SigCheck(Scm_VM());
        break;
    case SCM_PORT_ISTR:
        if (buflen <= port->src.istr.rest) {
            memcpy(buf, port->src.istr.current, buflen);
            port->src.istr.rest -= buflen;
            port->src.istr.current += buflen;
            nread = buflen;
        } else {
            nread = port->src.istr.rest;
            memcpy(buf, port->src.istr.current, nread);
            port->src.istr.rest = 0;
        }
        break;
    case SCM_PORT_PROC:
        nread = port->src.proc.vtable->Getz(buf, buflen, port);
        break;
    case SCM_PORT_CLOSED:
        Scm_Error("attempted to read from closed port: %S", port);
        break;
    default:
        Scm_Panic("Scm_Getz: something very wrong internally");
    }
    return nread;
}

/*
 * ReadLine
 */

static inline ScmObj readline_int(ScmPort *port)
{
    ScmDString ds;
    int ch = 0;
    SCM_GETC(ch, port);
    if (ch == SCM_CHAR_INVALID) return SCM_EOF;
    Scm_DStringInit(&ds);
    for (;;) {
        if (ch == '\r') {
            int ch2;
            SCM_GETC(ch2, port);
            if (ch2 == '\n' || ch == SCM_CHAR_INVALID) {
                return Scm_DStringGet(&ds);
            } else {
                SCM_UNGETC(ch2, port);
                return Scm_DStringGet(&ds);
            }
        } else if (ch == '\n' || ch == SCM_CHAR_INVALID) {
            return Scm_DStringGet(&ds); 
        }
        SCM_DSTRING_PUTC(&ds, ch);
        SCM_GETC(ch, port);
    }
}

ScmObj Scm_ReadLine(ScmPort *port)
{
    if (SCM_PORT_DIR(port) != SCM_PORT_INPUT)
        Scm_Error("input port required: %S\n", port);
    if (SCM_PORT_TYPE(port) == SCM_PORT_PROC) {
        /* procedure port may have optimized method */
        return port->src.proc.vtable->Getline(port);
    } else {
        return readline_int(port);
    }
}

/*===============================================================
 * Procedural port
 */

/* default dummy procedures */
static int null_getb(ScmPort *dummy)
    /*ARGSUSED*/
{
    return SCM_CHAR_INVALID;
}

static int null_getc(ScmPort *dummy)
    /*ARGSUSED*/
{
    return SCM_CHAR_INVALID;
}

static int null_getz(char *buf, int buflen, ScmPort *dummy)
    /*ARGSUSED*/
{
    return 0;
}

static ScmObj null_getline(ScmPort *port)
{
    return readline_int(port);
}

static int null_ready(ScmPort *dummy)
    /*ARGSUSED*/
{
    return TRUE;
}

static int null_putb(ScmByte b, ScmPort *dummy)
    /*ARGSUSED*/
{
    return 0;
}

static int null_putc(ScmChar c, ScmPort *dummy)
    /*ARGSUSED*/
{
    return 0;
}

static int null_putz(const char *str, int len, ScmPort *dummy)
    /*ARGSUSED*/
{
    return 0;
}

static int null_puts(ScmString *s, ScmPort *dummy)
    /*ARGSUSED*/
{
    return 0;
}

static int null_flush(ScmPort *dummy)
    /*ARGSUSED*/
{
    return 0;
}

static int null_close(ScmPort *dummy)
    /*ARGSUSED*/
{
    return 0;
}

static ScmProcPortInfo *null_info(ScmPort *dummy)
    /*ARGSUSED*/
{
    return NULL;
}

ScmObj Scm_MakeVirtualPort(int direction, ScmPortVTable *vtable, void *data,
                           int ownerp)
{
    ScmPortVTable *vt = SCM_NEW_ATOMIC(ScmPortVTable);
    ScmPort *port = make_port(direction, SCM_PORT_PROC, ownerp);
    
    /* Copy vtable, and ensure all entries contain some ptr */
    *vt = *vtable;
    if (!vt->Getb) vt->Getb = null_getb;
    if (!vt->Getc) vt->Getc = null_getc;
    if (!vt->Getz) vt->Getz = null_getz;
    if (!vt->Getline) vt->Getline = null_getline;
    if (!vt->Ready) vt->Ready = null_ready;
    if (!vt->Putb) vt->Putb = null_putb;
    if (!vt->Putc) vt->Putc = null_putc;
    if (!vt->Putz) vt->Putz = null_putz;
    if (!vt->Puts) vt->Puts = null_puts;
    if (!vt->Flush) vt->Flush = null_flush;
    if (!vt->Close) vt->Close = null_close;
    if (!vt->Info) vt->Info = null_info;

    port->src.proc.vtable = vt;
    port->src.proc.clientData = data;
    return SCM_OBJ(port);
}

/*===============================================================
 * Port over file descriptor
 */

/* A port can be constructed over given file descriptor.  It may be
   either buffered or unbuffered. */
/* TODO: need to fix how to handle the error condition */

struct fdport {
    ScmProcPortInfo info;       /* includes file descriptor */
    char eofread;               /* TRUE if EOF is read. */
    char err;                   /* TRUE if error has occurred. */
};

#define DECL_FDPORT(pdata, port) \
    struct fdport *pdata = (struct fdport *)port->src.proc.clientData
#define CHECK_EOF(pdata, eofcode) \
    if (pdata->eofread) return EOF
#define CHECK_RESULT(result, pdata, eofcode)    \
    do {                                        \
        if (result < 0)  {                      \
            Scm_SigCheck(Scm_VM());             \
            pdata->err = errno;                 \
            return eofcode;                     \
        }                                       \
        if (result == 0) {                      \
            pdata->eofread = TRUE;              \
            return eofcode;                     \
        }                                       \
    } while (0)
#define CHECK_ERROR(result, pdata)              \
    do {                                        \
        if (result < 0)  {                      \
            Scm_SigCheck(Scm_VM());             \
            pdata->err = errno;                 \
            return -1;                          \
        }                                       \
    } while (0)

/* Unbuffered I/O */
static int fdport_getb_unbuffered(ScmPort *port)
{
    char c;
    int nread;
    DECL_FDPORT(pdata, port);
    CHECK_EOF(pdata, EOF);
    nread = read(pdata->info.fd, &c, 1);
    CHECK_RESULT(nread, pdata, EOF);
    if (c == '\n') { pdata->info.line++; pdata->info.position = 1; }
    else pdata->info.position++;
    return c;
}

static int fdport_getc_unbuffered(ScmPort *port)
{
    char chbuf[SCM_CHAR_MAX_BYTES], c;
    int chcnt, chmax, ch, nread;
    DECL_FDPORT(pdata, port);
    CHECK_EOF(pdata, SCM_CHAR_INVALID);

    nread = read(pdata->info.fd, &c, 1);
    CHECK_RESULT(nread, pdata, SCM_CHAR_INVALID);
    if ((chmax = SCM_CHAR_NFOLLOWS(c)) == 0) {
        return c;
    }
    chmax++;
    chbuf[0] = c;
    for (chcnt = 1; chcnt < chmax; chcnt++) {
        nread = read(pdata->info.fd, &chbuf[chcnt], 1);
        CHECK_RESULT(nread, pdata, SCM_CHAR_INVALID);
    }
    SCM_CHAR_GET(chbuf, ch);
    if (ch == '\n') { pdata->info.line++; pdata->info.position = 1; }
    else pdata->info.position++;
    return ch;
}

static int fdport_getz_unbuffered(char *buf, int buflen, ScmPort *port)
{
    int nread;
    DECL_FDPORT(pdata, port);
    CHECK_EOF(pdata, 0);

    nread = read(pdata->info.fd, buf, buflen);
    CHECK_RESULT(nread, pdata, 0);
    return nread;
}

static int fdport_ready_unbuffered(ScmPort *port)
{
    /* TODO: write me */
    return TRUE;
}

static int fdport_putb_unbuffered(ScmByte b, ScmPort *port)
{
    DECL_FDPORT(pdata, port);
    int nwrote;
    do {
        nwrote = write(pdata->info.fd, &b, 1);
        CHECK_ERROR(nwrote, pdata);
    } while (nwrote == 0);
    return 1;
}

static int fdport_putc_unbuffered(ScmChar ch, ScmPort *port)
{
    DECL_FDPORT(pdata, port);
    int nwrote, nbytes, count;
    char chbuf[SCM_CHAR_MAX_BYTES], *bufp;
    count = nbytes = SCM_CHAR_NBYTES(ch);
    SCM_CHAR_PUT(chbuf, ch);
    bufp = chbuf;
    do {
        nwrote = write(pdata->info.fd, bufp, count);
        CHECK_ERROR(nwrote, pdata);
        count -= nwrote;
        bufp += nwrote;
    } while (count > 0);
    return nbytes;
}

static int fdport_putz_unbuffered(const char *buf, int len, ScmPort *port)
{
    DECL_FDPORT(pdata, port);
    int nwrote, size = (len < 0? strlen(buf) : len), count = size;
    do {
        nwrote = write(pdata->info.fd, buf, count);
        CHECK_ERROR(nwrote, pdata);
        count -= nwrote;
        buf += nwrote;
    } while (count > 0);
    return size;
}

static int fdport_puts_unbuffered(ScmString *str, ScmPort *port)
{
    DECL_FDPORT(pdata, port);
    int nwrote, count = SCM_STRING_SIZE(str);
    const char *buf = SCM_STRING_START(str);
    do {
        nwrote = write(pdata->info.fd, buf, count);
        CHECK_ERROR(nwrote, pdata);
        count -= nwrote;
        buf += nwrote;
    } while (count > 0);
    return SCM_STRING_SIZE(str);
}

static int fdport_close_unbuffered(ScmPort *port)
{
    DECL_FDPORT(pdata, port);
    if (SCM_PORT_OWNER_P(port) && pdata->info.fd >= 0)
        return Scm_SysCall(close(pdata->info.fd));
    else
        return -1;
}

static ScmProcPortInfo *fdport_info(ScmPort *port)
{
    DECL_FDPORT(pdata, port);
    return &pdata->info;
}

/* Create a port on specified file descriptor.
      NAME  - used for the name of the port.
      DIRECTION - either SCM_PORT_INPUT or SCM_PORT_OUTPUT
      FD - the opened file descriptor.
      BUFFERED - if TRUE, the port will be buffered (using fdopen).
      OWNERP - if TRUE, fd will be closed when this port is closed.
 */
ScmObj Scm_MakePortWithFd(ScmObj name, int direction,
                          int fd, int buffered, int ownerp)
{
    if (buffered) {
        FILE *fp;
        char *mode = NULL;
        if (direction == SCM_PORT_INPUT) mode = "r";
        else if (direction == SCM_PORT_OUTPUT) mode = "w";
        else Scm_Error("invalid port direction: %d", direction);
        fp = fdopen(fd, mode);
        if (fp == NULL) Scm_SysError("fdopen failed on %d", fd);
        return Scm_MakeFilePort(fp, name, mode, ownerp);
    } else {
        ScmPortVTable vt;
        struct fdport *pdata;
    
        pdata = SCM_NEW(struct fdport);
        pdata->info.name = name;
        pdata->info.line = 1;
        pdata->info.position = 1;
        pdata->info.fd = fd;
        pdata->info.fp = NULL;
        pdata->eofread = pdata->err = FALSE;
        vt.Getb = fdport_getb_unbuffered;
        vt.Getc = fdport_getc_unbuffered;
        vt.Getz = fdport_getz_unbuffered;
        vt.Getline = NULL;      /* use default */
        vt.Ready = fdport_ready_unbuffered;
        vt.Putb = fdport_putb_unbuffered;
        vt.Putc = fdport_putc_unbuffered;
        vt.Putz = fdport_putz_unbuffered;
        vt.Puts = fdport_puts_unbuffered;
        vt.Flush = NULL;        /* use default */
        vt.Close = fdport_close_unbuffered;
        vt.Info = fdport_info;
        return Scm_MakeVirtualPort(direction, &vt, pdata, ownerp);
    }
}

/*===============================================================
 * Buffered port
 */

struct bufport {
    int (*filler)(char*, int, void*);
    void *clientData;
    char *buffer;
    int bufsiz;                 /* size of the buffer */
    int chars;                  /* # of chars currently in the buffer.
                                   -1 if reached to EOF. */
    int current;                /* current character pointer */
};

#define PORT_BUFPORT(port)  \
   ((struct bufport*)SCM_PORT(port)->src.proc.clientData)

static void bufport_fill(struct bufport *bp)
{
    bp->chars = bp->filler(bp->buffer, bp->bufsiz, bp->clientData);
    if (bp->chars == 0) bp->chars = -1;
    bp->current = 0;
}

static int bufport_getb(ScmPort *port)
{
    struct bufport *bp = PORT_BUFPORT(port);
    if (bp->chars < 0) return EOF;
    if (bp->current >= bp->chars) bufport_fill(bp);
    if (bp->chars < 0) return EOF;
    return (unsigned char)bp->buffer[bp->current++];
}

static int bufport_getc(ScmPort *port)
{
    struct bufport *bp = PORT_BUFPORT(port);
    ScmChar ch;
    int nbytes;

    for (;;) {
        if (bp->chars < 0) return EOF;
        if (bp->current >= bp->chars) {
            bufport_fill(bp);
            continue;
        }
        nbytes = SCM_CHAR_NFOLLOWS(bp->buffer[bp->current]) + 1;
        if (bp->current + nbytes > bp->chars) {
            /* We don't have enough bytes to consist a character. Move
               incomplete character to the scratch, and repeat filling
               until we have enough bytes or encounters EOF. */
            port->scrcnt = 0;   /* should already be 0, but for safety. */
            for (;;) {
                int i, nc = bp->chars - bp->current;
                for (i=0; i<nc; i++) {
                    port->scratch[port->scrcnt++] = bp->buffer[bp->current++];
                }
                bufport_fill(bp);
                if (bp->chars < 0) return EOF;/* TODO: deal with scratch buf */
                if (bp->chars + port->scrcnt >= nbytes) break;
            }
            while (port->scrcnt < nbytes) {
                port->scratch[port->scrcnt++] = bp->buffer[bp->current++];
            }
            SCM_CHAR_GET(port->scratch, ch);
            port->scrcnt = 0;
            return ch;
        } else {
            SCM_CHAR_GET(bp->buffer+bp->current, ch);
            bp->current += nbytes;
            return ch;
        }
    }
}

static int bufport_getz(char *buf, int buflen, ScmPort *port)
{
    struct bufport *bp = PORT_BUFPORT(port);
    int nread = 0;
    
    if (bp->chars < 0) return 0;
    while (nread + bp->chars - bp->current < buflen) {
        int chunklen = bp->chars - bp->current;
        SCM_ASSERT(chunklen >= 0);
        memcpy(buf + nread, bp->buffer + bp->current, chunklen);
        nread += chunklen;
        bufport_fill(bp);
        if (bp->chars < 0) return nread;
    }
    memcpy(buf + nread, bp->buffer + bp->current, buflen - nread);
    bp->current += buflen - nread;
    SCM_ASSERT(bp->current <= bp->chars);
    return buflen;
}

static void bufport_flush_internal(struct bufport *bp)
{
    int nwritten = bp->filler(bp->buffer, bp->current, bp->clientData);
    if (nwritten >= 0 && nwritten < bp->current) {
        memmove(bp->buffer, bp->buffer+nwritten, bp->current-nwritten);
        bp->current -= nwritten;
    } else {
        bp->current = 0;
    }
}

static int bufport_putb(ScmByte b, ScmPort *port)
{
    struct bufport *bp = PORT_BUFPORT(port);
    if (bp->current >= bp->bufsiz) {
        bufport_flush_internal(bp);
    }
    bp->buffer[bp->current++] = b;
    return 0;
}

static int bufport_putc(ScmChar c, ScmPort *port)
{
    struct bufport *bp = PORT_BUFPORT(port);
    int nbytes = SCM_CHAR_NBYTES(c);
    if (bp->current + nbytes >= bp->bufsiz) {
        SCM_CHAR_PUT(port->scratch, c);
        port->scrcnt = 0;
        while (bp->current < bp->bufsiz) {
            bp->buffer[bp->current++] = port->scratch[port->scrcnt++];
        }
        bufport_flush_internal(bp);
        while (port->scrcnt < nbytes) {
            bp->buffer[bp->current++] = port->scratch[port->scrcnt++];
        }
        port->scrcnt = 0;
    } else {
        SCM_CHAR_PUT(bp->buffer+bp->current, c);
        bp->current += nbytes;
    }
    return 0;
}

static int bufport_putz(const char *buf, int siz, ScmPort *port)
{
    struct bufport *bp = PORT_BUFPORT(port);
    int nwrote = 0;
    
    if (siz < 0) siz = strlen(buf);
    while (bp->current + siz > bp->bufsiz) {
        /* fill the buffer and flush */
        int first = bp->bufsiz - bp->current;
        memcpy(bp->buffer + bp->current, buf + nwrote, first);
        bp->current = bp->bufsiz;
        bufport_flush_internal(bp);
        siz -= first;
        nwrote += first;
    }
    memcpy(bp->buffer + bp->current, buf + nwrote, siz);
    bp->current += siz;
    return 0;
}

static int bufport_puts(ScmString *s, ScmPort *port)
{
    return bufport_putz(SCM_STRING_START(s), SCM_STRING_SIZE(s), port);
}

static int bufport_flush(ScmPort *port)
{
    bufport_flush_internal(PORT_BUFPORT(port));
    return 0;
}

static int bufport_close(ScmPort *port)
{
    struct bufport *bp = PORT_BUFPORT(port);
    bp->filler(NULL, 0, bp->clientData);
    return 0;
}

ScmObj Scm_MakeBufferedPort(int direction, /* SCM_PORT_{INPUT|OUTPUT} */
                            int bufsiz,    /* size of the buffer. */
                            int chars,     /* # of chars already exist
                                              in the buffer. */
                            char *buffer,  /* the buffer.  when NULL,
                                              this function allocates it. */
                            int (*filler)(char *buf, int siz, void *data),
                                           /* a procedure to do block I/O.
                                              For input port, this function
                                              should fill the buffer from
                                              the begining, up to at most
                                              SIZ bytes.  Returns # of 
                                              actual bytes read.  Returns -1
                                              if reached to EOF.  For output
                                              port, this function should flush
                                              SIZ characters in the buffer.
                                            */
                            void *data)
{
    ScmPortVTable vt;
    struct bufport *packet;

    if (bufsiz <= 0) Scm_Error("buffer size expected positive: %d", bufsiz);
    if (buffer == NULL) buffer = SCM_NEW_ATOMIC2(char*, bufsiz);
    packet = SCM_NEW(struct bufport);
    packet->filler = filler;
    packet->clientData = data;
    packet->bufsiz = bufsiz;
    packet->buffer = buffer;
    packet->chars = chars;
    packet->current = 0;

    vt.Getb = (direction == SCM_PORT_INPUT)? bufport_getb : NULL;
    vt.Getc = (direction == SCM_PORT_INPUT)? bufport_getc : NULL;
    vt.Getz = (direction == SCM_PORT_INPUT)? bufport_getz : NULL;
    vt.Getline = NULL;
    vt.Ready = NULL;
    vt.Putb = (direction == SCM_PORT_OUTPUT)? bufport_putb : NULL;
    vt.Putc = (direction == SCM_PORT_OUTPUT)? bufport_putc : NULL;
    vt.Putz = (direction == SCM_PORT_OUTPUT)? bufport_putz : NULL;
    vt.Puts = (direction == SCM_PORT_OUTPUT)? bufport_puts : NULL;
    vt.Flush = (direction == SCM_PORT_OUTPUT)? bufport_flush : NULL;
    vt.Close = (direction == SCM_PORT_OUTPUT)? bufport_close : NULL;
    vt.Info = NULL;            /* TODO */
    return Scm_MakeVirtualPort(direction, &vt, packet, FALSE);
}

/*===============================================================
 * with-port
 */
struct with_port_packet {
    ScmPort *origport[3];
    int mask;
    int closep;
};

static ScmObj port_restorer(ScmObj *args, int nargs, void *data)
{
    struct with_port_packet *p = (struct with_port_packet*)data;
    int pcnt = 0;
    ScmPort *curport;

    if (p->mask & SCM_PORT_CURIN) {
        curport = SCM_CURIN;
        SCM_CURIN = p->origport[pcnt++];
        if (p->closep) Scm_ClosePort(curport);
    }
    if (p->mask & SCM_PORT_CUROUT) {
        curport = SCM_CUROUT;
        SCM_CUROUT = p->origport[pcnt++];
        if (p->closep) Scm_ClosePort(curport);
    }
    if (p->mask & SCM_PORT_CURERR) {
        curport = SCM_CURERR;
        SCM_CURERR = p->origport[pcnt++];
        if (p->closep) Scm_ClosePort(curport);
    }
    return SCM_UNDEFINED;
}

ScmObj Scm_WithPort(ScmPort *port[], ScmProcedure *thunk, int mask, int closep)
{
    ScmObj finalizer;
    struct with_port_packet *packet;
    int pcnt = 0;
    
    if (SCM_PROCEDURE_REQUIRED(thunk) != 0) {
        Scm_Error("thunk required: %S", thunk);
    }
    packet = SCM_NEW(struct with_port_packet);
    if (mask & SCM_PORT_CURIN) {
        packet->origport[pcnt] = SCM_CURIN;
        SCM_CURIN = port[pcnt++];
    }
    if (mask & SCM_PORT_CUROUT) {
        packet->origport[pcnt] = SCM_CUROUT;
        SCM_CUROUT = port[pcnt++];
    }
    if (mask & SCM_PORT_CURERR) {
        packet->origport[pcnt] = SCM_CURERR;
        SCM_CURERR = port[pcnt++];
    }
    packet->mask = mask;
    packet->closep = closep;
    finalizer = Scm_MakeSubr(port_restorer, (void*)packet,
                             0, 0, SCM_FALSE);
    return Scm_VMDynamicWind(Scm_NullProc(), SCM_OBJ(thunk), finalizer);
}

/*===============================================================
 * Standard ports
 */

static ScmObj scm_stdin;
static ScmObj scm_stdout;
static ScmObj scm_stderr;

ScmObj Scm_Stdin(void)
{
    return scm_stdin;
}

ScmObj Scm_Stdout(void)
{
    return scm_stdout;
}

ScmObj Scm_Stderr(void)
{
    return scm_stderr;
}

void Scm__InitPort(void)
{
    scm_stdin  = Scm_MakeFilePort(stdin, SCM_MAKE_STR("(stdin)"), "r", FALSE);
    scm_stdout = Scm_MakeFilePort(stdout,SCM_MAKE_STR("(stdout)"), "w",FALSE);
    scm_stderr = Scm_MakeFilePort(stderr,SCM_MAKE_STR("(stderr)"), "w",FALSE);
}
