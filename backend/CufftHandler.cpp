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

#include <map>


/**
 * @file   Backend.cpp
 * @author Giuseppe Coviello <giuseppe.coviello@uniparthenope.it>
 * @date   Sat Oct 10 10:51:58 2009
 *
 * @brief
 *
 *
 */

#define GL_GLEXT_PROTOTYPES

#include "GLHandler.h"

#include <errno.h>
#include <cstring>

#include "TcpCommunicator.h"
#include "Buffer.h"
#include "Result.h"

#include <X11/Xlib.h>
#include <GL/glext.h>
#include <GL/glx.h>
#include <semaphore.h>

using namespace std;

map<string, GLHandler::GLRoutineHandler> *GLHandler::mspHandlers = NULL;
static map<GLXDrawable, GLXDrawable> msDrawables;
static map<GLXDrawable, const char *> msDispensers;

extern "C" int HandlerInit() {
    return 0;
}

extern "C" Handler *GetHandler() {
    return new GLHandler();
}

GLHandler::GLHandler() {
    Initialize();
    mpFramebuffer = NULL;
    mpLock = NULL;
    sem_init(&mProducer, 0, 0);
    sem_init(&mConsumer, 0, 0);
}

GLHandler::~GLHandler() {

}

bool GLHandler::CanExecute(std::string routine) {
    return mspHandlers->find(routine) != mspHandlers->end();
}

Result * GLHandler::Execute(std::string routine, Buffer * input_buffer) {
    map<string, GLHandler::GLRoutineHandler>::iterator it;
    it = mspHandlers->find(routine);
    if (it == mspHandlers->end())
        throw "No handler for '" + routine + "' found!";
    try {
        return it->second(this, input_buffer);
    } catch (const char *ex) {
        cout << ex << endl;
        cout << strerror(errno) << endl;
    }
    return NULL;
}

struct DispenserArgs {
    GLHandler *mpHandler;
    Communicator *mpCommunicator;
};

static void *FramebufferDispenser(void *args) {
    cout << "DISPENSER" << endl;
    GLHandler *pThis = ((DispenserArgs *) args)->mpHandler;
    Communicator *s = ((DispenserArgs *) args)->mpCommunicator;
   try {
        Communicator *c = const_cast<Communicator *> (s->Accept());
        char token;
        while (true) {
            c->Read(&token, 1);
            pThis->RequestUpdate();
            c->Write((const char *) pThis->GetFramebuffer(), 512 * 512 * sizeof (int));
            c->Sync();
        }
    } catch (const char *ex) {
        cout << "FramebufferDispenser" << endl;
        cout << ex << endl;
        cout << strerror(errno) << endl;
    }
    return NULL;
}

const char *GLHandler::InitFramebuffer(size_t size, bool use_shm) {
    static bool initialized = false;
    static char *communicator = NULL;
    if (initialized)
        return communicator;
    initialized = true;
    cout << "InitFramebuffer. Use SHM: " << use_shm << ". Size: " << size << endl;

    if (!use_shm) {
        const char *comm = "tcp://10.0.2.2:4444";
        communicator = (char *) comm;
        mpFramebuffer = new char[size];
        memset(mpFramebuffer, 0, size);
        cout << (void *) mpFramebuffer << endl;
        DispenserArgs *da = new DispenserArgs;
        da->mpHandler = this;
        da->mpCommunicator = Communicator::Get(communicator);
        da->mpCommunicator->Serve();
        pthread_t tid;
        pthread_create(&tid, NULL, FramebufferDispenser, da);
        //FramebufferDispenser(this);
        return communicator;
    }
    char *name = new char[1024];
    snprintf(name, 1024, "/gvirtus-%d", getpid());
    shm_unlink(name);

    size += sizeof (pthread_spinlock_t);
    int fd = shm_open(name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    if (ftruncate(fd, size) == -1);

    mpLock = reinterpret_cast<pthread_spinlock_t *> (mmap(NULL, size,
            PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));

    mpFramebuffer = ((char *) mpLock) + sizeof (pthread_spinlock_t);

    pthread_spin_init(mpLock, PTHREAD_PROCESS_SHARED);
    communicator = name;
    return name;
}

char *GLHandler::GetFramebuffer() {
    return mpFramebuffer;
}

void GLHandler::Initialize() {
    if (mspHandlers != NULL)
        return;
    mspHandlers = new map<string, GLHandler::GLRoutineHandler > ();

#include "GLHandler_handlerspair.h"
}


static Display *mspDpy = NULL;
//static XVisualInfo *info = NULL;

Display *GetDisplay() {
    if (mspDpy == NULL) {
        cout << "Making a new Display connection." << endl;
        mspDpy = XOpenDisplay(NULL);
        cout << "Established Display connection: " << mspDpy << endl;
    }
    return mspDpy;
}

XVisualInfo *GetVisualInfo() {
    static XVisualInfo *vi = NULL;
    if (vi == NULL) {
        int attribList[] = {
            GLX_RGBA,
            GLX_RED_SIZE, 4,
            GLX_GREEN_SIZE, 4,
            GLX_BLUE_SIZE, 4,
            None
        };
        vi = glXChooseVisual(GetDisplay(), 0, attribList);
    }
    return vi;
}

GLXDrawable GetDrawable(GLXDrawable handler, GLHandler *pThis, bool use_shm) {
    map<GLXDrawable, GLXDrawable>::iterator i = msDrawables.find(handler);
    if (i != msDrawables.end())
        return i->second;
    cout << "Creating new Window with handler: " << handler << endl;
    GLXDrawable drawable = XCreateSimpleWindow(GetDisplay(),
            XRootWindow(GetDisplay(), DefaultScreen(GetDisplay())), 0, 0, 512,
            512, 0, 0, 0);
    XMapWindow(GetDisplay(), drawable);
    msDrawables.insert(make_pair(handler, drawable));
    msDispensers.insert(make_pair(handler, pThis->InitFramebuffer(512 * 512
            * sizeof (int), use_shm)));
    return drawable;
}

GL_ROUTINE_HANDLER(__ExecuteRoutines) {
    string routine;
    while (!in->Empty()) {
        routine = string(in->AssignString());
        pThis->Execute(routine, in);
    }
    return new Result(0);
}

GL_ROUTINE_HANDLER(__GetBuffer) {
    return new Result(0, new Buffer(pThis->GetFramebuffer(), 450 * 600 * sizeof (int)));
}

GL_ROUTINE_HANDLER(__GetDispenser) {
    GLXDrawable drawable = in->Get<GLXDrawable > ();
    map<GLXDrawable, const char *>::iterator i = msDispensers.find(drawable);
    Buffer *out = new Buffer();
    out->AddString(i->second);
    return new Result(0, out);
}

