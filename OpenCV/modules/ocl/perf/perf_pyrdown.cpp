///////////////////////////////////////////////////////////////////////////////////////
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
// Copyright (C) 2010-2012, Multicoreware, Inc., all rights reserved.
// Copyright (C) 2010-2012, Advanced Micro Devices, Inc., all rights reserved.
// Third party copyrights are property of their respective owners.
//
// @Authors
//    fangfang bai, fangfang@multicorewareinc.com
//
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

#ifdef HAVE_OPENCL

using namespace cv;
using namespace cv::ocl;
using namespace cvtest;
using namespace testing;
using namespace std;

PARAM_TEST_CASE(PyrDown, MatType, int)
{
    int type;
    int channels;
    //src mat
    cv::Mat mat1;
    cv::Mat dst;

    //std::vector<cv::ocl::Info> oclinfo;
    //ocl dst mat for testing

    cv::ocl::oclMat gmat1;
    cv::ocl::oclMat gdst;


    virtual void SetUp()
    {
        type = GET_PARAM(0);
        channels = GET_PARAM(1);
        //int devnums = getDevice(oclinfo);
        //CV_Assert(devnums > 0);
    }


};

#define VARNAME(A) string(#A);

////////////////////////////////PyrDown/////////////////////////////////////////////////
TEST_P(PyrDown, Mat)
{
    cv::Size size(MWIDTH, MHEIGHT);
    cv::RNG &rng = TS::ptr()->get_rng();
    mat1 = randomMat(rng, size, CV_MAKETYPE(type, channels), 5, 16, false);


    cv::ocl::oclMat gdst;
    double totalgputick = 0;
    double totalgputick_kernel = 0;

    double t1 = 0;
    double t2 = 0;

    for (int j = 0; j < LOOP_TIMES + 1; j ++)
    {

        t1 = (double)cvGetTickCount();//gpu start1

        cv::ocl::oclMat gmat1(mat1);

        t2 = (double)cvGetTickCount(); //kernel
        cv::ocl::pyrDown(gmat1, gdst);
        t2 = (double)cvGetTickCount() - t2;//kernel

        cv::Mat cpu_dst;
        gdst.download(cpu_dst);

        t1 = (double)cvGetTickCount() - t1;//gpu end1

        if (j == 0)
        {
            continue;
        }

        totalgputick = t1 + totalgputick;

        totalgputick_kernel = t2 + totalgputick_kernel;

    }

    cout << "average gpu runtime is  " << totalgputick / ((double)cvGetTickFrequency()* LOOP_TIMES * 1000.) << "ms" << endl;
    cout << "average gpu runtime without data transfer is  " << totalgputick_kernel / ((double)cvGetTickFrequency()* LOOP_TIMES * 1000.) << "ms" << endl;

}

//********test****************
INSTANTIATE_TEST_CASE_P(GPU_ImgProc, PyrDown, Combine(
                            Values(CV_8U, CV_32F), Values(1, 4)));


#endif // HAVE_OPENCL
