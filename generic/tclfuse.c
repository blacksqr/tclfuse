/*
    FUSE: Filesystem in Userspace
    Copyright (C) 2001-2006  Miklos Szeredi <miklos@szeredi.hu>

    This program can be distributed under the terms of the GNU GPL.
    See the file COPYING.

gcc -g -dynamiclib -Wall `pkg-config fuse --cflags --libs` tclfuse.c -o tclfuse.so -ltcl8.5
*/

#define FUSE_USE_VERSION 26

#include <tcl.h>
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <syslog.h>
#include <stdarg.h>


typedef struct tclfuse {
    Tcl_Interp *interp;
    Tcl_HashTable files;
    char *base;
    int fargc;
    struct fuse *fuse;
    char *fargv[];
} tclfuse;

union u_c_fh {
    Tcl_Channel c;
    uint64_t fh;
};
    
static Tcl_ObjCmdProc fuse_MainObjCmd;


static Tcl_Obj * realPath(const char *path)
{
    Tcl_Obj *objPtr;
    struct fuse_context *c = fuse_get_context();
    tclfuse *tfPtr = (tclfuse *) c->private_data;
    
    objPtr = Tcl_NewStringObj(tfPtr->base, -1);
    Tcl_IncrRefCount(objPtr);
    Tcl_AppendStringsToObj(objPtr, path, NULL);
    Tcl_FSConvertToPathType(NULL, objPtr);
    
    return objPtr;
}

static int hello_access(const char *path, int mask)
{
    Tcl_Obj *pathPtr;
    int res = -1;

    pathPtr = realPath(path);
    res = Tcl_FSAccess(pathPtr, mask);
    Tcl_DecrRefCount(pathPtr);

    return res;
}

static int hello_getattr(const char *path, struct stat *stbuf)
{
    Tcl_Obj *pathPtr;           /* Path of file to stat */
    int res = 0;
    
    pathPtr = realPath(path);
    memset(stbuf, 0, sizeof(struct stat));
    res = Tcl_FSStat(pathPtr, (Tcl_StatBuf *) stbuf);
    Tcl_DecrRefCount(pathPtr);

    return res;
}

static int hello_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                         off_t offset, struct fuse_file_info *fi)
{
    int i, res, nelems;
    Tcl_Obj *resPtr, *pathPtr, *objPtr;
    struct fuse_context *c = fuse_get_context();
    tclfuse *tfPtr = (tclfuse *) c->private_data;

    fprintf(stderr, "hello_READDIR: %s\n", path);
    pathPtr = realPath(path);
    resPtr = Tcl_NewObj();
    Tcl_IncrRefCount(resPtr);
    res = Tcl_FSMatchInDirectory(NULL, resPtr, pathPtr, "*", NULL);
    Tcl_ListObjLength(NULL, resPtr, &nelems);

    for (i = 0; i < nelems; i++) {
        Tcl_StatBuf st;
        char *s;
        
        Tcl_ListObjIndex(NULL, resPtr, i, &objPtr);
        res = Tcl_FSStat(objPtr, &st);
        s = Tcl_GetString(objPtr) + strlen(tfPtr->base) + strlen(path);
        if (*s == '/') {
            s++;
        }
        if (filler(buf, s, (struct stat *) &st, 0)) {
            break;
        }
    }

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    return 0;
}

static int hello_open(const char *path, struct fuse_file_info *fi)
{
    Tcl_Obj *pathPtr, *listPtr;
    Tcl_Channel chan;
    int new, fd, err;
    struct fuse_context *c = fuse_get_context();
    tclfuse *tfPtr = (tclfuse *) c->private_data;
    Tcl_HashEntry *hPtr;

    fprintf(stderr, "hello_OPEN: %s flags %d\n", path, fi->flags);
    listPtr = Tcl_NewListObj(0, NULL);

    pathPtr = realPath(path);
    fi->keep_cache = 1;
    if (fi->flags == 0) {
        fi->flags |= O_RDONLY;
        Tcl_ListObjAppendElement(NULL, listPtr, Tcl_NewStringObj("RDONLY", -1));
        fprintf(stderr, "hello_OPEN: %s flags %d\n", path, fi->flags);
    }
    
    if (fi->flags & O_RDONLY) {
    fprintf(stderr, "=== RDONLY\n");
        Tcl_ListObjAppendElement(NULL, listPtr, Tcl_NewStringObj("RDONLY", -1));
    } else {
        fprintf(stderr, "=== NOT RDONLY\n");
    }
    if (fi->flags & O_WRONLY) {
        Tcl_ListObjAppendElement(NULL, listPtr, Tcl_NewStringObj("WRONLY", -1));
    }
    if (fi->flags & O_RDWR) {
        Tcl_ListObjAppendElement(NULL, listPtr, Tcl_NewStringObj("RDWR", -1));
    }
    if (fi->flags & O_NONBLOCK) {
        Tcl_ListObjAppendElement(NULL, listPtr, Tcl_NewStringObj("NONBLOCK", -1));
    }
    if (fi->flags & O_APPEND) {
        Tcl_ListObjAppendElement(NULL, listPtr, Tcl_NewStringObj("APPEND", -1));
    }
    if (fi->flags & O_CREAT) {
        Tcl_ListObjAppendElement(NULL, listPtr, Tcl_NewStringObj("CREAT", -1));
    }
    if (fi->flags & O_TRUNC) {
        Tcl_ListObjAppendElement(NULL, listPtr, Tcl_NewStringObj("TRUNC", -1));
    }
    if (fi->flags & O_EXCL) {
        Tcl_ListObjAppendElement(NULL, listPtr, Tcl_NewStringObj("EXCL", -1));
    }
    
    fprintf(stderr, "path: %s mode: %s\n", Tcl_GetString(pathPtr), Tcl_GetString(listPtr));

    chan = Tcl_FSOpenFileChannel(NULL, pathPtr, Tcl_GetString(listPtr), 0644);
    Tcl_SetChannelOption(NULL, chan, "-encoding", "binary");
    Tcl_SetChannelOption(NULL, chan, "-translation", "binary");
    Tcl_DecrRefCount(pathPtr);

    if (chan == NULL) {
        err = Tcl_GetErrno();
        fprintf(stderr, "ERROR(%d): %s\n", err, Tcl_ErrnoMsg(err));
        return err;
    }
    Tcl_GetChannelHandle(chan, TCL_READABLE, (ClientData *) &fd);
    hPtr = Tcl_CreateHashEntry(&tfPtr->files, (ClientData) fd, &new);
    Tcl_SetHashValue(hPtr, (ClientData) chan);
    fi->fh =  (uintptr_t)fd;

    return 0;
}

static int hello_release(const char *path, struct fuse_file_info *fi)
{
    struct fuse_context *c = fuse_get_context();
    tclfuse *tfPtr = (tclfuse *) c->private_data;
    Tcl_Channel chan;
    Tcl_HashEntry *hPtr;

    hPtr = Tcl_FindHashEntry(&tfPtr->files, (ClientData) fi->fh);
    if (hPtr == NULL) {
        fprintf(stderr, "no such file\n");
        return -1;
    }
    chan = Tcl_GetHashValue(hPtr);
    Tcl_Close(NULL, chan);
    Tcl_DeleteHashEntry(hPtr);
    
    return 1;
}

static int hello_read(const char *path, char *buf, size_t size, off_t offset,
                      struct fuse_file_info *fi)
{
    Tcl_Channel chan;
    Tcl_HashEntry *hPtr;
    size_t len;
    struct fuse_context *c = fuse_get_context();
    tclfuse *tfPtr = (tclfuse *) c->private_data;

    fprintf(stderr, "hello_READ: %s %d\n", path, offset);

    hPtr = Tcl_FindHashEntry(&tfPtr->files, (ClientData) fi->fh);
    if (hPtr == NULL) {
        fprintf(stderr, "no such file\n");
        return -1;
    }
    chan = Tcl_GetHashValue(hPtr);    
    Tcl_Seek(chan, offset, SEEK_SET);
    len = Tcl_Read(chan, buf, size);

    return len;
}

int
Tclfuse_Init(Tcl_Interp *interp)
{
    if (Tcl_InitStubs(interp, "8.4", 0) == NULL) {
        return TCL_ERROR;
    }
    if (Tcl_PkgRequire(interp, "Tcl", "8.4", 0) == NULL) {
        return TCL_ERROR;
    }
    if (Tcl_PkgProvide(interp, "tclfuse", "0.1") == TCL_ERROR) {
        return TCL_ERROR;
    }
    Tcl_CreateObjCommand(interp, "fuse::main", fuse_MainObjCmd,
                         NULL, NULL);
                         
    return TCL_OK;
}

static struct fuse_operations hello_oper = {
    .getattr	= hello_getattr,
    .readdir	= hello_readdir,
    .open	    = hello_open,
    .release	= hello_release,
    .read	    = hello_read,
    .access	    = hello_access,
};

static int
fuse_MainObjCmd(ClientData type, Tcl_Interp *interp, int objc,
                       Tcl_Obj *CONST objv[])
{
    int i, mthp, err;
    char *fmp;
    tclfuse *tfPtr;
    struct fuse *fuse=NULL;

    if (objc < 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "base mountpoint options");
        return TCL_ERROR;
    }
    tfPtr = (tclfuse *) Tcl_Alloc(sizeof(*tfPtr) + sizeof(char *) * objc);
    tfPtr->interp = interp;
    tfPtr->base = Tcl_GetString(objv[1]);
    fmp = Tcl_GetString(objv[2]);

    tfPtr->fargc = 0;
    for (i = 1; i < objc; i++, tfPtr->fargc++) {
        tfPtr->fargv[tfPtr->fargc] = Tcl_GetString(objv[i]);
        fprintf(stderr, "%d: %s\n", i, tfPtr->fargv[tfPtr->fargc]); 
    }
    tfPtr->fargv[tfPtr->fargc] = NULL;
    Tcl_InitHashTable(&tfPtr->files, TCL_ONE_WORD_KEYS);
    fprintf(stderr, "pre fuse_setup %s\n", fmp);

    Tcl_FSChdir(objv[1]);
    fuse = fuse_setup(tfPtr->fargc, tfPtr->fargv, &hello_oper, 
            sizeof(hello_oper), &fmp, &mthp, (void *) tfPtr);

    fprintf(stderr, "fuse_setup: %d\n", mthp);

    if (fuse == NULL) {
        fprintf(stderr, "filesystem initialization failed\n");
        return TCL_ERROR;
    }
    err = fuse_loop(fuse);
    fprintf(stderr, "post fuse_main: %d\n", err);
    fuse_teardown(fuse, fmp);

    return TCL_OK;
}
