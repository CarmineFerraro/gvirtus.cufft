#include <cuda_runtime_api.h>
#include "CudaUtil.h"
#include "CudaRtHandler.h"

CUDA_ROUTINE_HANDLER(Free) {
    /* cudaError_t cudaFree(void *devPtr) */
    char *dev_ptr_handler = input_buffer->Assign<char>(CudaUtil::MarshaledDevicePointerSize);
    void *devPtr = pThis->GetDevicePointer(dev_ptr_handler);
    
    cudaError_t exit_code = cudaFree(devPtr);
    pThis->UnregisterDevicePointer(dev_ptr_handler);

    return new Result(exit_code);
}

CUDA_ROUTINE_HANDLER(Malloc) {
    /* cudaError_t cudaMalloc(void **devPtr, size_t size) */
    void *devPtr = NULL;
    char *dev_ptr_handler = input_buffer->Assign<char>(CudaUtil::MarshaledDevicePointerSize);
    size_t *size = input_buffer->Assign<size_t>();

    cudaError_t exit_code = cudaMalloc(&devPtr, *size);
    pThis->RegisterDevicePointer(dev_ptr_handler, devPtr);

    return new Result(exit_code);
}

CUDA_ROUTINE_HANDLER(Memcpy) {
    /* cudaError_t cudaError_t cudaMemcpy(void *dst, const void *src,
        size_t count, cudaMemcpyKind kind) */
    void *dst = NULL;
    void *src = NULL;

    cudaMemcpyKind *kind = input_buffer->BackAssign<cudaMemcpyKind>();
    size_t *count = input_buffer->BackAssign<size_t>();
    char *dev_ptr_handler;
    cudaError_t exit_code;
    Result * result;

    switch(*kind) {
        case cudaMemcpyHostToHost:
            // This should never happen
            result = NULL;
            break;
        case cudaMemcpyHostToDevice:
            dev_ptr_handler = input_buffer->Assign<char>(CudaUtil::MarshaledDevicePointerSize);
            dst = pThis->GetDevicePointer(dev_ptr_handler);
            /* Achtung: this isn't strictly correct because here we assign just
             * a pointer to one character, any successive assign should
             * take inaxpectated result ... but it works here!
             */
            src = input_buffer->Assign<char>();
            exit_code = cudaMemcpy(dst, src, *count, *kind);
            result = new Result(exit_code);
            break;
        case cudaMemcpyDeviceToHost:
            dst = new char[*count];
            /* skipping a char for fake host pointer */
            input_buffer->Assign<char>();
            dev_ptr_handler = input_buffer->Assign<char>(CudaUtil::MarshaledDevicePointerSize);
            src = pThis->GetDevicePointer(dev_ptr_handler);
            exit_code = cudaMemcpy(dst, src, *count, *kind);
            result = new Result(exit_code, new Buffer((char *) dst, *count));
            break;
        case cudaMemcpyDeviceToDevice:
            dev_ptr_handler = input_buffer->Assign<char>(CudaUtil::MarshaledDevicePointerSize);
            dst = pThis->GetDevicePointer(dev_ptr_handler);
            dev_ptr_handler = input_buffer->Assign<char>(CudaUtil::MarshaledDevicePointerSize);
            src = pThis->GetDevicePointer(dev_ptr_handler);
            exit_code = cudaMemcpy(dst, src, *count, *kind);
            result = new Result(exit_code);
            break;
    }
    return result;
}

