/*
 * gVirtuS -- A GPGPU transparent virtualization component.
 *
 * Copyright (C) 2009-2011  The University of Napoli Parthenope at Naples.
 *
 * This file is part of gVirtuS.
 *
 * gVirtuS is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * gVirtuS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with gVirtuS; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Written by: Giuseppe Coviello <giuseppe.coviello@uniparthenope.it>,
 *             Department of Applied Science
 */

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/ioctl.h>

#include <iostream>

#include <Frontend.h>
#include <Communicator.h>
#include <TcpCommunicator.h>
#define GL_GLEXT_PROTOTYPES

#include <GL/glx.h>
#include <GL/glext.h>

#include "GL.h"
#include "GL_unsupported.h"

using namespace std;

enum ShmType {
    POSIX, VMSHM, NONE
};

static Buffer *mspRoutines;
static bool msRoutinesEmpty;
static ShmType mShmType;
static char *mspShmName;
static char *mspFramebuffer;
static TcpCommunicator *mspDispenserCommunicator;
static pthread_spinlock_t *mspLock;

static void __attribute__((constructor)) _GL_init(void) {
    const char *shmtype = getenv("SHM");
    if (shmtype == NULL)
        mShmType = NONE;
    else if (strcasecmp(shmtype, "posix") == 0)
        mShmType = POSIX;
    else if (strcasecmp(shmtype, "vmshm") == 0)
        mShmType = VMSHM;
    else {
        cerr << "ShmType not recognized, falling back to 'None'." << endl;
        mShmType = NONE;
    }
    Frontend::GetFrontend();
    mspRoutines = new Buffer();
    msRoutinesEmpty = true;
    if (!XInitThreads()) {
        cout << "Xlib not thread safe\n";
        exit(1);
    }
}

extern "C" void FlushRoutines() {
    if (msRoutinesEmpty)
        return;
    Frontend *f = Frontend::GetFrontend();
    f->Prepare();
    f->Execute("gl__ExecuteRoutines", mspRoutines);
    mspRoutines->Reset();
    msRoutinesEmpty = true;
}

Buffer *AddRoutine(const char *routine) {
    mspRoutines->AddString(routine);
    msRoutinesEmpty = false;
    return mspRoutines;
}

Frontend *GetFrontend() {
    FlushRoutines();
    Frontend *f = Frontend::GetFrontend();
    f->Prepare();
    return f;
}

struct WindowInfo {
    Display *mpDpy;
    GLXDrawable mDrawable;
    int mWidth;
    int mHeight;
};

static void InitFramebuffer() {
    int fd;
    cout << "InitFramebuffer: " << mShmType << endl;
    if (mShmType == NONE) {
        mspFramebuffer = new char[512 * 512 * sizeof (int) ];
        mspLock = NULL;
        mspDispenserCommunicator = new TcpCommunicator(mspShmName);
        mspDispenserCommunicator->Connect();
        return;
    } else if (mShmType == POSIX) {
        fd = shm_open(mspShmName, O_RDWR, S_IRUSR | S_IWUSR);
        if (ftruncate(fd, 512 * 512 * sizeof (int) + sizeof(pthread_spinlock_t)));
    } else {
        fd = open("/dev/vmshm0", O_RDWR);
        ioctl(fd, 0, mspShmName);
    }
    mspLock = reinterpret_cast<pthread_spinlock_t *> (mmap(NULL,
            512 * 512 * sizeof (int) + sizeof(pthread_spinlock_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd,
            0));
    mspFramebuffer = ((char *) mspLock) + sizeof (pthread_spinlock_t);
}

static void Lock() {
    if (mspLock)
        pthread_spin_lock(mspLock);
}

static void Unlock() {
    if (mspLock)
        pthread_spin_unlock(mspLock);
}

static inline char *GetFramebuffer() {
    if (mShmType != NONE)
        return mspFramebuffer;
#if 0
    Frontend *f = Frontend::GetFrontend();
    f->Prepare();
    f->Execute("gl__GetBuffer");
    memmove(mspFramebuffer, f->GetOutputBuffer()->GetBuffer(),
            300 * 300 * sizeof (int));
#endif
    char token = 0;
    mspDispenserCommunicator->Write(&token, 1);
    mspDispenserCommunicator->Sync();
    mspDispenserCommunicator->Read(mspFramebuffer, 512 * 512 * sizeof (int));
    return mspFramebuffer;
}

static void *update(void *__w) {
    WindowInfo *w = (WindowInfo *) __w;
    InitFramebuffer();
    char *buffer;
        buffer = GetFramebuffer();
        XImage *img = XCreateImage(w->mpDpy, CopyFromParent,
                24, ZPixmap, 0, buffer, w->mWidth, w->mHeight, 32,
                w->mWidth * 4);
    while (true) {
#if 0
        for (int i = 0; i < 300 / 2; i++) {
            memmove(row, buffer + (299 - i) * 300 * sizeof (int), 300 * sizeof (int));
            memmove(buffer + (299 - i) * 300 * sizeof (int), buffer + i * 300 * sizeof (int), 300 * sizeof (int));
            memmove(buffer + i * 300 * sizeof (int), row, 300 * sizeof (int));
        }
        XImage *img = XCreateImage(w->mpDpy, CopyFromParent,
                24, ZPixmap, 0, buffer, w->mWidth, w->mHeight, 32,
                w->mWidth * 4);
#endif
        Lock();
        buffer = GetFramebuffer();
        memmove(img->data, buffer, w->mWidth * w->mHeight * sizeof(int));
        img->bits_per_pixel = 32;
        XPutImage(w->mpDpy, w->mDrawable, DefaultGC(w->mpDpy, 0), img, 0, 0, 0,
                0, w->mWidth, w->mHeight);
        Unlock();
        usleep(1000);
    }
    return NULL;
}

map<GLXDrawable, pthread_t> msUpdaters;

void InstantiateUpdater(Display *dpy, GLXDrawable drawable) {
    map<GLXDrawable, pthread_t>::iterator i = msUpdaters.find(drawable);
    if (i != msUpdaters.end())
        return;
    Frontend *f = GetFrontend();
    Buffer *in = f->GetInputBuffer();
    in->Add(drawable);
    f->Execute("gl__GetDispenser");
    Buffer *out = f->GetOutputBuffer();
    mspShmName = strdup(out->AssignString());
    WindowInfo *w = new WindowInfo;
    w->mDrawable = drawable;
    w->mpDpy = dpy;
    w->mWidth = 512;
    w->mHeight = 512;
    pthread_t tid;
    pthread_create(&tid, NULL, update, w);
    msUpdaters.insert(make_pair(drawable, tid));
}

static void InvalidUsage() {
    cerr << "glXGetProcAddressARB is not yet supported" << endl;
}

extern "C" __GLXextFuncPtr glXGetProcAddressARB(const GLubyte *procName) {
#include "if.inc"
    cout << "if(strcasecmp((const char *) procName, \"" << (const char *) procName << "\") == 0) return (__GLXextFuncPtr) fake" << (const char *) procName << ";" << endl;
    return InvalidUsage;
}

