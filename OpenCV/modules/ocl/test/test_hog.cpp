/*M///////////////////////////////////////////////////////////////////////////////////////
//
//  IMPORTANT: READ BEFORE DOWNLOADING, COPYING, INSTALLING OR USING.
//
//  By downloading, copying, installing or using the software you agree to this license.
//  If you do not agree to this license, do not download, install,
//  copy or use the software.
//
//
//                        Intel License Agreement
//                For Open Source Computer Vision Library
//
// Copyright (C) 2010-2012, Multicoreware, Inc., all rights reserved.
// Copyright (C) 2010-2012, Advanced Micro Devices, Inc., all rights reserved.
// Third party copyrights are property of their respective owners.
//
// @Authors
//		Wenju He, wenju@multicorewareinc.com
//
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
//
//   * Redistribution's of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//
//   * Redistribution's in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//
//   * The name of Intel Corporation may not be used to endorse or promote products
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
#include "opencv2/core/core.hpp"
using namespace std;
#ifdef HAVE_OPENCL

extern string workdir;
PARAM_TEST_CASE(HOG, cv::Size, int)
{
    cv::Size winSize;
    int type;
    virtual void SetUp()
    {
        winSize = GET_PARAM(0);
        type = GET_PARAM(1);
    }
};

TEST_P(HOG, GetDescriptors)
{
    // Load image
    cv::Mat img_rgb = readImage(workdir + "lena.jpg");
    ASSERT_FALSE(img_rgb.empty());

    // Convert image
    cv::Mat img;
    switch (type)
    {
    case CV_8UC1:
        cv::cvtColor(img_rgb, img, CV_BGR2GRAY);
        break;
    case CV_8UC4:
    default:
        cv::cvtColor(img_rgb, img, CV_BGR2BGRA);
        break;
    }
    cv::ocl::oclMat d_img(img);

    // HOGs
    cv::ocl::HOGDescriptor ocl_hog;
    ocl_hog.gamma_correction = true;
    cv::HOGDescriptor hog;
    hog.gammaCorrection = true;

    // Compute descriptor
    cv::ocl::oclMat d_descriptors;
    ocl_hog.getDescriptors(d_img, ocl_hog.win_size, d_descriptors, ocl_hog.DESCR_FORMAT_COL_BY_COL);
    cv::Mat down_descriptors;
    d_descriptors.download(down_descriptors);
    down_descriptors = down_descriptors.reshape(0, down_descriptors.cols * down_descriptors.rows);

    hog.setSVMDetector(hog.getDefaultPeopleDetector());
    std::vector<float> descriptors;
    switch (type)
    {
    case CV_8UC1:
        hog.compute(img, descriptors, ocl_hog.win_size);
        break;
    case CV_8UC4:
    default:
        hog.compute(img_rgb, descriptors, ocl_hog.win_size);
        break;
    }
    cv::Mat cpu_descriptors(descriptors);

    EXPECT_MAT_SIMILAR(down_descriptors, cpu_descriptors, 1e-2);
}


bool match_rect(cv::Rect r1, cv::Rect r2, int threshold)
{
    return ((abs(r1.x - r2.x) < threshold) && (abs(r1.y - r2.y) < threshold) &&
            (abs(r1.width - r2.width) < threshold) && (abs(r1.height - r2.height) < threshold));
}

TEST_P(HOG, Detect)
{
    // Load image
    cv::Mat img_rgb = readImage(workdir + "lena.jpg");
    ASSERT_FALSE(img_rgb.empty());

    // Convert image
    cv::Mat img;
    switch (type)
    {
    case CV_8UC1:
        cv::cvtColor(img_rgb, img, CV_BGR2GRAY);
        break;
    case CV_8UC4:
    default:
        cv::cvtColor(img_rgb, img, CV_BGR2BGRA);
        break;
    }
    cv::ocl::oclMat d_img(img);

    // HOGs
    if ((winSize != cv::Size(48, 96)) && (winSize != cv::Size(64, 128)))
        winSize = cv::Size(64, 128);
    cv::ocl::HOGDescriptor ocl_hog(winSize);
    ocl_hog.gamma_correction = true;

    cv::HOGDescriptor hog;
    hog.winSize = winSize;
    hog.gammaCorrection = true;

    if (winSize.width == 48 && winSize.height == 96)
    {
        // daimler's base
        ocl_hog.setSVMDetector(ocl_hog.getPeopleDetector48x96());
        hog.setSVMDetector(hog.getDaimlerPeopleDetector());
    }
    else if (winSize.width == 64 && winSize.height == 128)
    {
        ocl_hog.setSVMDetector(ocl_hog.getPeopleDetector64x128());
        hog.setSVMDetector(hog.getDefaultPeopleDetector());
    }
    else
    {
        ocl_hog.setSVMDetector(ocl_hog.getDefaultPeopleDetector());
        hog.setSVMDetector(hog.getDefaultPeopleDetector());
    }

    // OpenCL detection
    std::vector<cv::Rect> d_found;
    ocl_hog.detectMultiScale(d_img, d_found, 0, cv::Size(8, 8), cv::Size(0, 0), 1.05, 2);

    // CPU detection
    std::vector<cv::Rect> found;
    switch (type)
    {
    case CV_8UC1:
        hog.detectMultiScale(img, found, 0, cv::Size(8, 8), cv::Size(0, 0), 1.05, 2);
        break;
    case CV_8UC4:
    default:
        hog.detectMultiScale(img_rgb, found, 0, cv::Size(8, 8), cv::Size(0, 0), 1.05, 2);
        break;
    }

    // Ground-truth rectangular people window
    cv::Rect win1_64x128(231, 190, 72, 144);
    cv::Rect win2_64x128(621, 156, 97, 194);
    cv::Rect win1_48x96(238, 198, 63, 126);
    cv::Rect win2_48x96(619, 161, 92, 185);
    cv::Rect win3_48x96(488, 136, 56, 112);

    // Compare whether ground-truth windows are detected and compare the number of windows detected.
    std::vector<int> d_comp(4);
    std::vector<int> comp(4);
    for(int i = 0; i < (int)d_comp.size(); i++)
    {
        d_comp[i] = 0;
        comp[i] = 0;
    }

    int threshold = 10;
    int val = 32;
    d_comp[0] = (int)d_found.size();
    comp[0] = (int)found.size();
    if (winSize == cv::Size(48, 96))
    {
        for(int i = 0; i < (int)d_found.size(); i++)
        {
            if (match_rect(d_found[i], win1_48x96, threshold))
                d_comp[1] = val;
            if (match_rect(d_found[i], win2_48x96, threshold))
                d_comp[2] = val;
            if (match_rect(d_found[i], win3_48x96, threshold))
                d_comp[3] = val;
        }
        for(int i = 0; i < (int)found.size(); i++)
        {
            if (match_rect(found[i], win1_48x96, threshold))
                comp[1] = val;
            if (match_rect(found[i], win2_48x96, threshold))
                comp[2] = val;
            if (match_rect(found[i], win3_48x96, threshold))
                comp[3] = val;
        }
    }
    else if (winSize == cv::Size(64, 128))
    {
        for(int i = 0; i < (int)d_found.size(); i++)
        {
            if (match_rect(d_found[i], win1_64x128, threshold))
                d_comp[1] = val;
            if (match_rect(d_found[i], win2_64x128, threshold))
                d_comp[2] = val;
        }
        for(int i = 0; i < (int)found.size(); i++)
        {
            if (match_rect(found[i], win1_64x128, threshold))
                comp[1] = val;
            if (match_rect(found[i], win2_64x128, threshold))
                comp[2] = val;
        }
    }

    char s[100] = {0};
    EXPECT_MAT_NEAR(cv::Mat(d_comp), cv::Mat(comp), 3, s);
}


INSTANTIATE_TEST_CASE_P(GPU_ImgProc, HOG, testing::Combine(
                            testing::Values(cv::Size(64, 128), cv::Size(48, 96)),
                            testing::Values(MatType(CV_8UC1), MatType(CV_8UC4))));


#endif //HAVE_OPENCL
