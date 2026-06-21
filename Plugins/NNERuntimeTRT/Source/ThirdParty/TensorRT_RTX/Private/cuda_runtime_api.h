#pragma once

#include <cuda.h>

// Alias runtime types to driver API types
typedef CUstream cudaStream_t;
typedef CUevent cudaEvent_t;

typedef CUresult cudaError_t;
#define cudaSuccess CUDA_SUCCESS