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

/**
 * @file   CudaRtHandler.h
 * @author Giuseppe Coviello <giuseppe.coviello@uniparthenope.it>
 * @date   Sat Oct 10 10:51:58 2009
 * 
 * @brief  
 * 
 * 
 */

#ifndef _GLHANDLER_H
#define	_GLHANDLER_H

#include <iostream>
#include <map>
#include <string>
#include <cstdio>

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "Handler.h"
#include "Result.h"

#include <X11/Xlib.h>
#include <GL/glx.h>

#include <semaphore.h>

/**						
 * CudaRtHandler is used by Backend's Process(es) for storing and retrieving
 * device related data and functions. 
 * CudaRtHandler has also the method Execute() that is responsible to execute a
 * named CUDA Runtime routine unmarshalling the input parameters from the
 * provided Buffer.
 */
class GLHandler : public Handler {
public:
    GLHandler();
    virtual ~GLHandler();
    bool CanExecute(std::string routine);
    Result * Execute(std::string routine, Buffer * input_buffer);
    const char *InitFramebuffer(size_t size, bool use_shm);
    char *GetFramebuffer();
    inline void Lock() {
        if(mpLock)
            pthread_spin_lock(mpLock);
    }
    inline void Unlock() {
        if(mpLock)
            pthread_spin_unlock(mpLock);
    }
    inline void RequestUpdate() {
        sem_post(&mProducer);
        sem_wait(&mConsumer);
    }
    inline bool RequestPending() {
        return sem_trywait(&mProducer) == 0;
    }
    inline void Updated() {
        sem_post(&mConsumer);
    }
private:
    sem_t mProducer, mConsumer;
    void Initialize();
    typedef Result * (*GLRoutineHandler)(GLHandler *, Buffer *);
    static std::map<std::string, GLRoutineHandler> * mspHandlers;
    char *mpFramebuffer;
    pthread_spinlock_t *mpLock;
};

Display *GetDisplay();
GLXDrawable GetDrawable(GLXDrawable handler, GLHandler *pThis, bool use_shm);
XVisualInfo *GetVisualInfo();

#define GL_ROUTINE_HANDLER(name) Result * handle##name(GLHandler * pThis, Buffer * in)
#define GL_ROUTINE_HANDLER_PAIR(name) make_pair("gl" #name, handle##name)

#include "GLHandler_handlers.h"

#endif	/* _CUDARTHANDLER_H */

