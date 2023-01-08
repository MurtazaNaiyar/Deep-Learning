/*M///////////////////////////////////////////////////////////////////////////////////////
//
//  IMPORTANT: READ BEFORE DOWNLOADING, COPYING, INSTALLING OR USING.
//
//  By downloading, copying, installing or using the software you agree to this license.
//  If you do not agree to this license, do not download, install,
//  copy or use the software.
//
//
//                           License Agreement
//                For Open Source Computer Vision Library
//
// Copyright (C) 2010-2012, Institute Of Software Chinese Academy Of Science, all rights reserved.
// Copyright (C) 2010-2012, Advanced Micro Devices, Inc., all rights reserved.
// Copyright (C) 2010-2012, Multicoreware, Inc., all rights reserved.
// Third party copyrights are property of their respective owners.
//
// @Authors
//    Guoping Long, longguoping@gmail.com
//    Niko Li, newlife20080214@gmail.com
//    Yao Wang, bitwangyaoyao@gmail.com
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
//
//   * Redistribution's of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//
//   * Redistribution's in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other oclMaterials provided with the distribution.
//
//   * The name of the copyright holders may not be used to endorse or promote products
//     derived from this software without specific prior written permission.
//
// This software is provided by the copyright holders and contributors "as is" and
// any express or implied warranties, including, but not limited to, the implied
// warranties of merchantability and fitness for a particular purpose are disclaimed.
// In no event shall the Intel Corporation or contributors be liable for any direct,
// indirect, incidental, special, exemplary, or consequential damages
// (including, but not limited to, procurement of substitute goods or services;
// loss of use, data, or profits; or business interruption) however caused
// and on any theory of liability, whether in contract, strict liability,
// or tort (including negligence or otherwise) arising in any way out of
// the use of this software, even if advised of the possibility of such damage.
//
//M*/

#include "precomp.hpp"
#include <iomanip>
#include <fstream>
#include "binarycaching.hpp"

using namespace cv;
using namespace cv::ocl;
using namespace std;
using std::cout;
using std::endl;

//#define PRINT_KERNEL_RUN_TIME
#define RUN_TIMES 100
#ifndef CL_MEM_USE_PERSISTENT_MEM_AMD
#define CL_MEM_USE_PERSISTENT_MEM_AMD 0
#endif
//#define AMD_DOUBLE_DIFFER

namespace cv
{
    namespace ocl
    {
        extern void fft_teardown();
        /*
         * The binary caching system to eliminate redundant program source compilation.
         * Strictly, this is not a cache because we do not implement evictions right now.
         * We shall add such features to trade-off memory consumption and performance when necessary.
         */
        auto_ptr<ProgramCache> ProgramCache::programCache;
        ProgramCache *programCache = NULL;
        DevMemType gDeviceMemType = DEVICE_MEM_DEFAULT;
        DevMemRW gDeviceMemRW = DEVICE_MEM_R_W;
        int gDevMemTypeValueMap[5] = {0, 
                                      CL_MEM_ALLOC_HOST_PTR,
                                      CL_MEM_USE_HOST_PTR,
                                      CL_MEM_COPY_HOST_PTR,
                                      CL_MEM_USE_PERSISTENT_MEM_AMD};
        int gDevMemRWValueMap[3] = {CL_MEM_READ_WRITE, CL_MEM_READ_ONLY, CL_MEM_WRITE_ONLY};

        ProgramCache::ProgramCache()
        {
            codeCache.clear();
            cacheSize = 0;
        }

        ProgramCache::~ProgramCache()
        {
            releaseProgram();
        }

        cl_program ProgramCache::progLookup(string srcsign)
        {
            map<string, cl_program>::iterator iter;
            iter = codeCache.find(srcsign);
            if(iter != codeCache.end())
                return iter->second;
            else
                return NULL;
        }

        void ProgramCache::addProgram(string srcsign , cl_program program)
        {
            if(!progLookup(srcsign))
            {
                codeCache.insert(map<string, cl_program>::value_type(srcsign, program));
            }
        }

        void ProgramCache::releaseProgram()
        {
            map<string, cl_program>::iterator iter;
            for(iter = codeCache.begin(); iter != codeCache.end(); iter++)
            {
                openCLSafeCall(clReleaseProgram(iter->second));
            }
            codeCache.clear();
            cacheSize = 0;
        }

        ////////////////////////Common OpenCL specific calls///////////////
        int getDevMemType(DevMemRW& rw_type, DevMemType& mem_type)
        { 
            rw_type = gDeviceMemRW; 
            mem_type = gDeviceMemType; 
            return Context::getContext()->impl->unified_memory;
        }

        int setDevMemType(DevMemRW rw_type, DevMemType mem_type)
        { 
            if( (mem_type == DEVICE_MEM_PM && Context::getContext()->impl->unified_memory == 0) ||
                 mem_type == DEVICE_MEM_UHP ||
                 mem_type == DEVICE_MEM_CHP )
                return -1;
            gDeviceMemRW = rw_type;
            gDeviceMemType = mem_type;
            return 0; 
        }
 
       struct Info::Impl
        {
            cl_platform_id oclplatform;
            std::vector<cl_device_id> devices;
            std::vector<std::string> devName;

            cl_context oclcontext;
            cl_command_queue clCmdQueue;
            int devnum;
            cl_uint maxDimensions;
            size_t maxWorkGroupSize;
            size_t *maxWorkItemSizes;
            cl_uint maxComputeUnits;
            char extra_options[512];
            int  double_support;
            Impl()
            {
                memset(extra_options, 0, 512);
            }
        };

        inline int divUp(int total, int grain)
        {
            return (total + grain - 1) / grain;
        }

        int getDevice(std::vector<Info> &oclinfo, int devicetype)
        {
            switch(devicetype)
            {
            case CVCL_DEVICE_TYPE_DEFAULT:
            case CVCL_DEVICE_TYPE_CPU:
            case CVCL_DEVICE_TYPE_GPU:
            case CVCL_DEVICE_TYPE_ACCELERATOR:
            case CVCL_DEVICE_TYPE_ALL:
                break;
            default:
                CV_Error(CV_GpuApiCallError, "Unkown device type");
            }
            int devcienums = 0;
            // Platform info
            cl_int status = 0;
            cl_uint numPlatforms;
            Info ocltmpinfo;
            openCLSafeCall(clGetPlatformIDs(0, NULL, &numPlatforms));
            CV_Assert(numPlatforms > 0);
            cl_platform_id *platforms = new cl_platform_id[numPlatforms];

            openCLSafeCall(clGetPlatformIDs(numPlatforms, platforms, NULL));
            char deviceName[256];
            for (unsigned i = 0; i < numPlatforms; ++i)
            {
                cl_uint numsdev;
                status = clGetDeviceIDs(platforms[i], devicetype, 0, NULL, &numsdev);
                if(status != CL_DEVICE_NOT_FOUND)
                {
                    openCLVerifyCall(status);
                }
                if(numsdev > 0)
                {
                    devcienums += numsdev;
                    cl_device_id *devices = new cl_device_id[numsdev];
                    openCLSafeCall(clGetDeviceIDs(platforms[i], devicetype, numsdev, devices, NULL));
                    ocltmpinfo.impl->oclplatform = platforms[i];
                    for(unsigned j = 0; j < numsdev; j++)
                    {
                        ocltmpinfo.impl->devices.push_back(devices[j]);
                        openCLSafeCall(clGetDeviceInfo(devices[j], CL_DEVICE_NAME, 256, deviceName, NULL));
                        ocltmpinfo.impl->devName.push_back(std::string(deviceName));
                        ocltmpinfo.DeviceName.push_back(std::string(deviceName));
                    }
                    delete[] devices;
                    oclinfo.push_back(ocltmpinfo);
                    ocltmpinfo.release();
                }
            }
            delete[] platforms;
            if(devcienums > 0)
            {
                setDevice(oclinfo[0]);
            }
            return devcienums;
        }

        static void fillClcontext(Info &oclinfo)
        {
            //get device information
            size_t devnum = oclinfo.impl->devnum;

            openCLSafeCall(clGetDeviceInfo(oclinfo.impl->devices[devnum], CL_DEVICE_MAX_WORK_GROUP_SIZE,
                                           sizeof(size_t), (void *)&oclinfo.impl->maxWorkGroupSize, NULL));
            openCLSafeCall(clGetDeviceInfo(oclinfo.impl->devices[devnum], CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS,
                                           sizeof(cl_uint), (void *)&oclinfo.impl->maxDimensions, NULL));
            oclinfo.impl->maxWorkItemSizes = new size_t[oclinfo.impl->maxDimensions];
            openCLSafeCall(clGetDeviceInfo(oclinfo.impl->devices[devnum], CL_DEVICE_MAX_WORK_ITEM_SIZES,
                                           sizeof(size_t)*oclinfo.impl->maxDimensions, (void *)oclinfo.impl->maxWorkItemSizes, NULL));
            openCLSafeCall(clGetDeviceInfo(oclinfo.impl->devices[devnum], CL_DEVICE_MAX_COMPUTE_UNITS,
                                           sizeof(cl_uint), (void *)&oclinfo.impl->maxComputeUnits, NULL));
            //initialize extra options for compilation. Currently only fp64 is included.
            //Assume 4KB is enough to store all possible extensions.

            const int EXT_LEN = 4096 + 1 ;
            char extends_set[EXT_LEN];
            size_t extends_size;
            openCLSafeCall(clGetDeviceInfo(oclinfo.impl->devices[devnum], CL_DEVICE_EXTENSIONS,
                                           EXT_LEN, (void *)extends_set, &extends_size));
            CV_Assert(extends_size < (size_t)EXT_LEN);
            extends_set[EXT_LEN - 1] = 0;
            memset(oclinfo.impl->extra_options, 0, 512);
            oclinfo.impl->double_support = 0;
            int fp64_khr = string(extends_set).find("cl_khr_fp64");

            if(fp64_khr >= 0 && fp64_khr < EXT_LEN)
            {
                sprintf(oclinfo.impl->extra_options , "-D DOUBLE_SUPPORT");
                oclinfo.impl -> double_support = 1;
            }
            Context::setContext(oclinfo);

        }

        void setDevice(Info &oclinfo, int devnum)
        {
            CV_Assert(devnum >= 0);
            cl_int status = 0;
            cl_context_properties cps[3] =
            {
                CL_CONTEXT_PLATFORM, (cl_context_properties)(oclinfo.impl->oclplatform), 0
            };
            oclinfo.impl->devnum = devnum;
            oclinfo.impl->oclcontext = clCreateContext(cps, 1, &oclinfo.impl->devices[devnum], NULL, NULL, &status);
            openCLVerifyCall(status);
            //create the command queue using the first device of the list
            oclinfo.impl->clCmdQueue = clCreateCommandQueue(oclinfo.impl->oclcontext, oclinfo.impl->devices[devnum],
                                       CL_QUEUE_PROFILING_ENABLE, &status);
            openCLVerifyCall(status);
            fillClcontext(oclinfo);
        }

        void setDeviceEx(Info &oclinfo, void *ctx, void *q, int devnum)
        {
            CV_Assert(devnum >= 0);
            oclinfo.impl->devnum = devnum;
            if(ctx && q)
            {
                oclinfo.impl->oclcontext = (cl_context)ctx;
                oclinfo.impl->clCmdQueue = (cl_command_queue)q;
                clRetainContext((cl_context)ctx);
                clRetainCommandQueue((cl_command_queue)q);
                fillClcontext(oclinfo);
             }
         }

        void *getoclContext()
        {
            return &(Context::getContext()->impl->clContext);
        }

        void *getoclCommandQueue()
        {
            return &(Context::getContext()->impl->clCmdQueue);
        }
        void openCLReadBuffer(Context *clCxt, cl_mem dst_buffer, void *host_buffer, size_t size)
        {
            cl_int status;
            status = clEnqueueReadBuffer(clCxt->impl->clCmdQueue, dst_buffer, CL_TRUE, 0,
                                         size, host_buffer, 0, NULL, NULL);
            openCLVerifyCall(status);
        }

        cl_mem openCLCreateBuffer(Context *clCxt, size_t flag , size_t size)
        {
            cl_int status;
            cl_mem buffer = clCreateBuffer(clCxt->impl->clContext, (cl_mem_flags)flag, size, NULL, &status);
            openCLVerifyCall(status);
            return buffer;
        }

        void openCLMallocPitch(Context *clCxt, void **dev_ptr, size_t *pitch,
                               size_t widthInBytes, size_t height)
        {
            openCLMallocPitchEx(clCxt, dev_ptr, pitch, widthInBytes, height, gDeviceMemRW, gDeviceMemType);
        }

        void openCLMallocPitchEx(Context *clCxt, void **dev_ptr, size_t *pitch,
                               size_t widthInBytes, size_t height, DevMemRW rw_type, DevMemType mem_type)
        {
            cl_int status;

            *dev_ptr = clCreateBuffer(clCxt->impl->clContext, gDevMemRWValueMap[rw_type]|gDevMemTypeValueMap[mem_type],
                                      widthInBytes * height, 0, &status);
            openCLVerifyCall(status);
            *pitch = widthInBytes;
        }

        void openCLMemcpy2D(Context *clCxt, void *dst, size_t dpitch,
                            const void *src, size_t spitch,
                            size_t width, size_t height, enum openCLMemcpyKind kind, int channels)
        {
            size_t buffer_origin[3] = {0, 0, 0};
            size_t host_origin[3] = {0, 0, 0};
            size_t region[3] = {width, height, 1};
            if(kind == clMemcpyHostToDevice)
            {
                if(dpitch == width || channels == 3 || height == 1)
                {
                    openCLSafeCall(clEnqueueWriteBuffer(clCxt->impl->clCmdQueue, (cl_mem)dst, CL_TRUE,
                                                        0, width * height, src, 0, NULL, NULL));
                }
                else
                {
                    openCLSafeCall(clEnqueueWriteBufferRect(clCxt->impl->clCmdQueue, (cl_mem)dst, CL_TRUE,
                                                            buffer_origin, host_origin, region, dpitch, 0, spitch, 0, src, 0, 0, 0));
                }
            }
            else if(kind == clMemcpyDeviceToHost)
            {
                if(spitch == width || channels == 3 || height == 1)
                {
                    openCLSafeCall(clEnqueueReadBuffer(clCxt->impl->clCmdQueue, (cl_mem)src, CL_TRUE,
                                                       0, width * height, dst, 0, NULL, NULL));
                }
                else
                {
                    openCLSafeCall(clEnqueueReadBufferRect(clCxt->impl->clCmdQueue, (cl_mem)src, CL_TRUE,
                                                           buffer_origin, host_origin, region, spitch, 0, dpitch, 0, dst, 0, 0, 0));
                }
            }
        }

        void openCLCopyBuffer2D(Context *clCxt, void *dst, size_t dpitch, int dst_offset,
                                const void *src, size_t spitch,
                                size_t width, size_t height, int src_offset)
        {
            size_t src_origin[3] = {src_offset % spitch, src_offset / spitch, 0};
            size_t dst_origin[3] = {dst_offset % dpitch, dst_offset / dpitch, 0};
            size_t region[3] = {width, height, 1};

            openCLSafeCall(clEnqueueCopyBufferRect(clCxt->impl->clCmdQueue, (cl_mem)src, (cl_mem)dst, src_origin, dst_origin,
                                                   region, spitch, 0, dpitch, 0, 0, 0, 0));
        }

        void openCLFree(void *devPtr)
        {
            openCLSafeCall(clReleaseMemObject((cl_mem)devPtr));
        }
        cl_kernel openCLGetKernelFromSource(const Context *clCxt, const char **source, string kernelName)
        {
            return openCLGetKernelFromSource(clCxt, source, kernelName, NULL);
        }


        void setBinpath(const char *path)
        {
            Context *clcxt = Context::getContext();
            clcxt->impl->Binpath = path;
        }

        int savetofile(const Context*,  cl_program &program, const char *fileName)
        {
            size_t binarySize;
            openCLSafeCall(clGetProgramInfo(program,
                                    CL_PROGRAM_BINARY_SIZES,
                                    sizeof(size_t),
                                    &binarySize, NULL));
            char* binary = (char*)malloc(binarySize);
            if(binary == NULL)
            {
                CV_Error(CV_StsNoMem, "Failed to allocate host memory.");
            }
            openCLSafeCall(clGetProgramInfo(program,
                                    CL_PROGRAM_BINARIES,
                                    sizeof(char *),
                                    &binary,
                                    NULL));

            FILE *fp = fopen(fileName, "wb+");
            if(fp != NULL)
            {
                fwrite(binary, binarySize, 1, fp);
                free(binary);
                fclose(fp);
            }
            return 1;
        }

        cl_kernel openCLGetKernelFromSource(const Context *clCxt, const char **source, string kernelName,
                                            const char *build_options)
        {
            cl_kernel kernel;
            cl_program program ;
            cl_int status = 0;
            stringstream src_sign;
            string srcsign;
            string filename;
            CV_Assert(programCache != NULL);

            if(NULL != build_options)
            {
                src_sign << (int64)(*source) << clCxt->impl->clContext << "_" << build_options;
            }
            else
            {
                src_sign << (int64)(*source) << clCxt->impl->clContext;
            }
            srcsign = src_sign.str();

            program = NULL;
            program = programCache->progLookup(srcsign);

            if(!program)
            {
                //config build programs
                char all_build_options[1024];
                memset(all_build_options, 0, 1024);
                char zeromem[512] = {0};
                if(0 != memcmp(clCxt -> impl->extra_options, zeromem, 512))
                    strcat(all_build_options, clCxt -> impl->extra_options);
                strcat(all_build_options, " ");
                if(build_options != NULL)
                    strcat(all_build_options, build_options);
                if(all_build_options != NULL)
                {
                    filename = clCxt->impl->Binpath  + kernelName + "_" + clCxt->impl->devName + all_build_options + ".clb";
                }
                else
                {
                    filename = clCxt->impl->Binpath  + kernelName + "_" + clCxt->impl->devName + ".clb";
                }

                FILE *fp = fopen(filename.c_str(), "rb");
                if(fp == NULL || clCxt->impl->Binpath.size() == 0)    //we should generate a binary file for the first time.
                {
                    if(fp != NULL)
                        fclose(fp);

                    program = clCreateProgramWithSource(
                                  clCxt->impl->clContext, 1, source, NULL, &status);
                    openCLVerifyCall(status);
                    status = clBuildProgram(program, 1, &(clCxt->impl->devices), all_build_options, NULL, NULL);
                    if(status == CL_SUCCESS && clCxt->impl->Binpath.size())
                        savetofile(clCxt, program, filename.c_str());
                }
                else
                {
                    fseek(fp, 0, SEEK_END);
                    size_t binarySize = ftell(fp);
                    fseek(fp, 0, SEEK_SET);
                    char *binary = new char[binarySize];
                    CV_Assert(1 == fread(binary, binarySize, 1, fp));
                    fclose(fp);
                    cl_int status = 0;
                    program = clCreateProgramWithBinary(clCxt->impl->clContext,
                                                        1,
                                                        &(clCxt->impl->devices),
                                                        (const size_t *)&binarySize,
                                                        (const unsigned char **)&binary,
                                                        NULL,
                                                        &status);
                    openCLVerifyCall(status);
                    status = clBuildProgram(program, 1, &(clCxt->impl->devices), all_build_options, NULL, NULL);
                    delete[] binary;
                }

                if(status != CL_SUCCESS)
                {
                    if(status == CL_BUILD_PROGRAM_FAILURE)
                    {
                        cl_int logStatus;
                        char *buildLog = NULL;
                        size_t buildLogSize = 0;
                        logStatus = clGetProgramBuildInfo(program,
                                                          clCxt->impl->devices, CL_PROGRAM_BUILD_LOG, buildLogSize,
                                                          buildLog, &buildLogSize);
                        if(logStatus != CL_SUCCESS)
                            cout << "Failed to build the program and get the build info." << endl;
                        buildLog = new char[buildLogSize];
                        CV_DbgAssert(!!buildLog);
                        memset(buildLog, 0, buildLogSize);
                        openCLSafeCall(clGetProgramBuildInfo(program, clCxt->impl->devices,
                                                             CL_PROGRAM_BUILD_LOG, buildLogSize, buildLog, NULL));
                        cout << "\n\t\t\tBUILD LOG\n";
                        cout << buildLog << endl;
                        delete [] buildLog;
                    }
                    openCLVerifyCall(status);
                }
                //Cache the binary for future use if build_options is null
                if( (programCache->cacheSize += 1) < programCache->MAX_PROG_CACHE_SIZE)
                    programCache->addProgram(srcsign, program);
                else
                    cout << "Warning: code cache has been full.\n";
            }
            kernel = clCreateKernel(program, kernelName.c_str(), &status);
            openCLVerifyCall(status);
            return kernel;
        }

        void openCLVerifyKernel(const Context *clCxt, cl_kernel kernel, size_t *localThreads)
        {
            size_t kernelWorkGroupSize;
            openCLSafeCall(clGetKernelWorkGroupInfo(kernel, clCxt->impl->devices,
                                                    CL_KERNEL_WORK_GROUP_SIZE, sizeof(size_t), &kernelWorkGroupSize, 0));
            CV_Assert( (localThreads[0] <= clCxt->impl->maxWorkItemSizes[0]) &&
                          (localThreads[1] <= clCxt->impl->maxWorkItemSizes[1]) &&
                          (localThreads[2] <= clCxt->impl->maxWorkItemSizes[2]) &&
                          ((localThreads[0] * localThreads[1] * localThreads[2]) <= kernelWorkGroupSize) &&
                          (localThreads[0] * localThreads[1] * localThreads[2]) <= clCxt->impl->maxWorkGroupSize);
        }

#ifdef PRINT_KERNEL_RUN_TIME
        static double total_execute_time = 0;
        static double total_kernel_time = 0;
#endif
        void openCLExecuteKernel_(Context *clCxt , const char **source, string kernelName, size_t globalThreads[3],
                                  size_t localThreads[3],  vector< pair<size_t, const void *> > &args, int channels,
                                  int depth, const char *build_options)
        {
            //construct kernel name
            //The rule is functionName_Cn_Dn, C represent Channels, D Represent DataType Depth, n represent an integer number
            //for exmaple split_C2_D2, represent the split kernel with channels =2 and dataType Depth = 2(Data type is char)
            stringstream idxStr;
            if(channels != -1)
                idxStr << "_C" << channels;
            if(depth != -1)
                idxStr << "_D" << depth;
            kernelName += idxStr.str();

            cl_kernel kernel;
            kernel = openCLGetKernelFromSource(clCxt, source, kernelName, build_options);

            if ( localThreads != NULL)
            {
                globalThreads[0] = divUp(globalThreads[0], localThreads[0]) * localThreads[0];
                globalThreads[1] = divUp(globalThreads[1], localThreads[1]) * localThreads[1];
                globalThreads[2] = divUp(globalThreads[2], localThreads[2]) * localThreads[2];

                //size_t blockSize = localThreads[0] * localThreads[1] * localThreads[2];
                cv::ocl::openCLVerifyKernel(clCxt, kernel, localThreads);
            }
            for(size_t i = 0; i < args.size(); i ++)
                openCLSafeCall(clSetKernelArg(kernel, i, args[i].first, args[i].second));

#ifndef PRINT_KERNEL_RUN_TIME
            openCLSafeCall(clEnqueueNDRangeKernel(clCxt->impl->clCmdQueue, kernel, 3, NULL, globalThreads,
                                                  localThreads, 0, NULL, NULL));
#else
            cl_event event = NULL;
            openCLSafeCall(clEnqueueNDRangeKernel(clCxt->impl->clCmdQueue, kernel, 3, NULL, globalThreads,
                                                  localThreads, 0, NULL, &event));

            cl_ulong start_time, end_time, queue_time;
            double execute_time = 0;
            double total_time   = 0;

            openCLSafeCall(clWaitForEvents(1, &event));
            openCLSafeCall(clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_START,
                                                   sizeof(cl_ulong), &start_time, 0));

            openCLSafeCall(clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_END,
                                                   sizeof(cl_ulong), &end_time, 0));

            openCLSafeCall(clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_QUEUED,
                                                   sizeof(cl_ulong), &queue_time, 0));

            execute_time = (double)(end_time - start_time) / (1000 * 1000);
            total_time = (double)(end_time - queue_time) / (1000 * 1000);

            //	cout << setiosflags(ios::left) << setw(15) << execute_time;
            //	cout << setiosflags(ios::left) << setw(15) << total_time - execute_time;
            //	cout << setiosflags(ios::left) << setw(15) << total_time << endl;

            total_execute_time += execute_time;
            total_kernel_time += total_time;
            clReleaseEvent(event);
#endif

            clFinish(clCxt->impl->clCmdQueue);
            openCLSafeCall(clReleaseKernel(kernel));
        }

        void openCLExecuteKernel(Context *clCxt , const char **source, string kernelName,
                                 size_t globalThreads[3], size_t localThreads[3],
                                 vector< pair<size_t, const void *> > &args, int channels, int depth)
        {
            openCLExecuteKernel(clCxt, source, kernelName, globalThreads, localThreads, args,
                                channels, depth, NULL);
        }
        void openCLExecuteKernel(Context *clCxt , const char **source, string kernelName,
                                 size_t globalThreads[3], size_t localThreads[3],
                                 vector< pair<size_t, const void *> > &args, int channels, int depth, const char *build_options)

        {
#ifndef PRINT_KERNEL_RUN_TIME
            openCLExecuteKernel_(clCxt, source, kernelName, globalThreads, localThreads, args, channels, depth,
                                 build_options);
#else
            string data_type[] = { "uchar", "char", "ushort", "short", "int", "float", "double"};
            cout << endl;
            cout << "Function Name: " << kernelName;
            if(depth >= 0)
                cout << " |data type: " << data_type[depth];
            cout << " |channels: " << channels;
            cout << " |Time Unit: " << "ms" << endl;

            total_execute_time = 0;
            total_kernel_time = 0;
            cout << "-------------------------------------" << endl;

            cout << setiosflags(ios::left) << setw(15) << "excute time";
            cout << setiosflags(ios::left) << setw(15) << "lauch time";
            cout << setiosflags(ios::left) << setw(15) << "kernel time" << endl;
            int i = 0;
            for(i = 0; i < RUN_TIMES; i++)
                openCLExecuteKernel_(clCxt, source, kernelName, globalThreads, localThreads, args, channels, depth,
                                     build_options);

            cout << "average kernel excute time: " << total_execute_time / RUN_TIMES << endl; // "ms" << endl;
            cout << "average kernel total time:  " << total_kernel_time / RUN_TIMES << endl; // "ms" << endl;
#endif
        }
 
       double openCLExecuteKernelInterop(Context *clCxt , const char **source, string kernelName,
                                 size_t globalThreads[3], size_t localThreads[3],
                                 vector< pair<size_t, const void *> > &args, int channels, int depth, const char *build_options, 
                                 bool finish, bool measureKernelTime, bool cleanUp)

        {
            //construct kernel name
            //The rule is functionName_Cn_Dn, C represent Channels, D Represent DataType Depth, n represent an integer number
            //for exmaple split_C2_D2, represent the split kernel with channels =2 and dataType Depth = 2(Data type is char)
            stringstream idxStr;
            if(channels != -1)
                idxStr << "_C" << channels;
            if(depth != -1)
                idxStr << "_D" << depth;
            kernelName += idxStr.str();

            cl_kernel kernel;
            kernel = openCLGetKernelFromSource(clCxt, source, kernelName, build_options);

            double kernelTime = 0.0;

            if( globalThreads != NULL)
            {
                if ( localThreads != NULL)
                {
                    globalThreads[0] = divUp(globalThreads[0], localThreads[0]) * localThreads[0];
                    globalThreads[1] = divUp(globalThreads[1], localThreads[1]) * localThreads[1];
                    globalThreads[2] = divUp(globalThreads[2], localThreads[2]) * localThreads[2];

                    //size_t blockSize = localThreads[0] * localThreads[1] * localThreads[2];
                    cv::ocl::openCLVerifyKernel(clCxt, kernel, localThreads);
                }
                for(size_t i = 0; i < args.size(); i ++)
                    openCLSafeCall(clSetKernelArg(kernel, i, args[i].first, args[i].second));

                if(measureKernelTime == false)
                {
                    openCLSafeCall(clEnqueueNDRangeKernel(clCxt->impl->clCmdQueue, kernel, 3, NULL, globalThreads,
                                    localThreads, 0, NULL, NULL));
                }
                else
                {
                    cl_event event = NULL;
                    openCLSafeCall(clEnqueueNDRangeKernel(clCxt->impl->clCmdQueue, kernel, 3, NULL, globalThreads,
                                    localThreads, 0, NULL, &event));

                    cl_ulong end_time, queue_time;

                    openCLSafeCall(clWaitForEvents(1, &event));

                    openCLSafeCall(clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_END,
                                    sizeof(cl_ulong), &end_time, 0));

                    openCLSafeCall(clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_QUEUED,
                                    sizeof(cl_ulong), &queue_time, 0));

                    kernelTime = (double)(end_time - queue_time) / (1000 * 1000);

                    clReleaseEvent(event);
                }
            }

            if(finish)
            {
                clFinish(clCxt->impl->clCmdQueue);
            }

            if(cleanUp)
            {
                openCLSafeCall(clReleaseKernel(kernel));
            }

            return kernelTime;
        }

        // Converts the contents of a file into a string
        static int convertToString(const char *filename, std::string& s)
        {
            size_t size;
            char*  str;

            std::fstream f(filename, (std::fstream::in | std::fstream::binary));
            if(f.is_open())
            {
                size_t fileSize;
                f.seekg(0, std::fstream::end);
                size = fileSize = (size_t)f.tellg();
                f.seekg(0, std::fstream::beg);

                str = new char[size+1];
                if(!str)
                {
                    f.close();
                    return -1;
                }

                f.read(str, fileSize);
                f.close();
                str[size] = '\0';
            
                s = str;
                delete[] str;
                return 0;
            }
            printf("Error: Failed to open file %s\n", filename);
            return -1;
        }

        double openCLExecuteKernelInterop(Context *clCxt , const char **fileName, const int numFiles, string kernelName,
                                 size_t globalThreads[3], size_t localThreads[3],
                                 vector< pair<size_t, const void *> > &args, int channels, int depth, const char *build_options, 
                                 bool finish, bool measureKernelTime, bool cleanUp)

        {
            std::vector<std::string> fsource;
            for (int i = 0 ; i < numFiles ; i++)
            {
                std::string str;
                if (convertToString(fileName[i], str) >= 0)
                    fsource.push_back(str);
            }
            const char **source = new const char *[numFiles];
            for (int i = 0 ; i < numFiles ; i++)
                source[i] = fsource[i].c_str();
            double kernelTime = openCLExecuteKernelInterop(clCxt ,source, kernelName, globalThreads, localThreads,
                                 args, channels, depth, build_options, finish, measureKernelTime, cleanUp);
            fsource.clear();
            delete []source;
            return kernelTime;
        }
 
       cl_mem load_constant(cl_context context, cl_command_queue command_queue, const void *value,
                             const size_t size)
        {
            int status;
            cl_mem con_struct;

            con_struct = clCreateBuffer(context, CL_MEM_READ_ONLY, size, NULL, &status);
            openCLSafeCall(status);

            openCLSafeCall(clEnqueueWriteBuffer(command_queue, con_struct, 1, 0, size,
                                                value, 0, 0, 0));

            return con_struct;

        }

        /////////////////////////////OpenCL initialization/////////////////
        auto_ptr<Context> Context::clCxt;
        int Context::val = 0;
        Mutex cs;
        Context *Context::getContext()
        {
            if(val == 0)
            {
                AutoLock al(cs);
                if( NULL == clCxt.get())
                    clCxt.reset(new Context);

                val = 1;
                return clCxt.get();
            }
            else
            {
                return clCxt.get();
            }
        }
        void Context::setContext(Info &oclinfo)
        {
            Context *clcxt = getContext();
            clcxt->impl->clContext = oclinfo.impl->oclcontext;
            clcxt->impl->clCmdQueue = oclinfo.impl->clCmdQueue;
            clcxt->impl->devices = oclinfo.impl->devices[oclinfo.impl->devnum];
            clcxt->impl->devName = oclinfo.impl->devName[oclinfo.impl->devnum];
            clcxt->impl->maxDimensions = oclinfo.impl->maxDimensions;
            clcxt->impl->maxWorkGroupSize = oclinfo.impl->maxWorkGroupSize;
            for(size_t i=0; i<clcxt->impl->maxDimensions && i<4; i++)
                clcxt->impl->maxWorkItemSizes[i] = oclinfo.impl->maxWorkItemSizes[i];
            clcxt->impl->maxComputeUnits = oclinfo.impl->maxComputeUnits;
            clcxt->impl->double_support = oclinfo.impl->double_support;
            //extra options to recognize compiler options
            memcpy(clcxt->impl->extra_options, oclinfo.impl->extra_options, 512);
            cl_bool unfymem = false;
            openCLSafeCall(clGetDeviceInfo(clcxt->impl->devices, CL_DEVICE_HOST_UNIFIED_MEMORY,
                                           sizeof(cl_bool), (void *)&unfymem, NULL));
            if(unfymem)
                clcxt->impl->unified_memory = 1;
        }
        Context::Context()
        {
            impl = new Impl;
            //Information of the OpenCL context
            impl->clContext = NULL;
            impl->clCmdQueue = NULL;
            impl->devices = NULL;
            impl->maxDimensions = 0;
            impl->maxWorkGroupSize = 0;
            for(int i=0; i<4; i++)
                impl->maxWorkItemSizes[i] = 0;
            impl->maxComputeUnits = 0;
            impl->double_support = 0;
            //extra options to recognize vendor specific fp64 extensions
            memset(impl->extra_options, 0, 512);
            impl->unified_memory = 0; 
            programCache = ProgramCache::getProgramCache();
        }

        Context::~Context()
        {
            delete impl;
            programCache->releaseProgram();
        }
        Info::Info()
        {
            impl = new Impl;
            impl->oclplatform = 0;
            impl->oclcontext = 0;
            impl->clCmdQueue = 0;
            impl->devnum = 0;
            impl->maxDimensions = 0;
            impl->maxWorkGroupSize = 0;
            impl->maxWorkItemSizes = 0;
            impl->maxComputeUnits = 0;
            impl->double_support = 0;
            //extra_options = 0;
        }
        void Info::release()
        {
            fft_teardown();
            if(impl->oclplatform)
            {
                impl->oclplatform = 0;
            }
            if(impl->clCmdQueue)
            {
                openCLSafeCall(clReleaseCommandQueue(impl->clCmdQueue));
            }
            ProgramCache::getProgramCache()->releaseProgram();
            if(impl->oclcontext)
            {
                openCLSafeCall(clReleaseContext(impl->oclcontext));
            }
            if(impl->maxWorkItemSizes)
            {
                delete[] impl->maxWorkItemSizes;
                impl->maxWorkItemSizes = 0;
            }
            //if(extra_options)
            //{
            //	delete[] extra_options;
            //	extra_options = 0;
            //}
            impl->devices.clear();
            impl->devName.clear();
            DeviceName.clear();
        }
        Info::~Info()
        {
            release();
            delete impl;
        }
        Info &Info::operator = (const Info &m)
        {
            impl->oclplatform = m.impl->oclplatform;
            impl->oclcontext = m.impl->oclcontext;
            impl->clCmdQueue = m.impl->clCmdQueue;
            impl->devnum = m.impl->devnum;
            impl->maxDimensions = m.impl->maxDimensions;
            impl->maxWorkGroupSize = m.impl->maxWorkGroupSize;
            impl->maxWorkItemSizes = m.impl->maxWorkItemSizes;
            impl->maxComputeUnits = m.impl->maxComputeUnits;
            impl->double_support = m.impl->double_support;
            memcpy(impl->extra_options, m.impl->extra_options, 512);
            for(size_t i = 0; i < m.impl->devices.size(); i++)
            {
                impl->devices.push_back(m.impl->devices[i]);
                impl->devName.push_back(m.impl->devName[i]);
                DeviceName.push_back(m.DeviceName[i]);
            }
            return *this;
        }
        Info::Info(const Info &m)
        {
            impl = new Impl;
            *this = m;
        }
    }//namespace ocl

}//namespace cv
