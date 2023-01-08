#include "precomp.hpp"
#include <iomanip>
#include "opencv2/imgproc/imgproc_c.h"

#ifdef HAVE_OPENCL

using namespace cv;
using namespace cv::ocl;
using namespace cvtest;
using namespace testing;
using namespace std;
extern string workdir;
PARAM_TEST_CASE(MomentsTestBase, MatType, bool)
{
    int type;
    cv::Mat mat1;
    bool test_contours;

    virtual void SetUp()
    {
        type = GET_PARAM(0);
        test_contours = GET_PARAM(1);
        cv::RNG &rng = TS::ptr()->get_rng();
        cv::Size size(10*MWIDTH, 10*MHEIGHT);
        mat1 = randomMat(rng, size, type, 5, 16, false);
    }

    void Compare(Moments& cpu, Moments& gpu)
    {
        Mat gpu_dst, cpu_dst;
        HuMoments(cpu, cpu_dst);
        HuMoments(gpu, gpu_dst);
        EXPECT_MAT_NEAR(gpu_dst,cpu_dst, .5, "");
    }

};
struct ocl_Moments : MomentsTestBase {};

TEST_P(ocl_Moments, Mat)
{
    bool binaryImage = 0;
    SetUp();

    for(int j = 0; j < LOOP_TIMES; j++)
    {
        if(test_contours)
        {
            Mat src = imread( workdir + "../cpp/pic3.png", 1 );
            Mat src_gray, canny_output;
            cvtColor( src, src_gray, CV_BGR2GRAY );
            vector<vector<Point> > contours;
            vector<Vec4i> hierarchy;
            Canny( src_gray, canny_output, 100, 200, 3 );
            findContours( canny_output, contours, hierarchy, CV_RETR_TREE, CV_CHAIN_APPROX_SIMPLE, Point(0, 0) );
            for( size_t i = 0; i < contours.size(); i++ )
            {
                Moments m = moments( contours[i], false );
                Moments dm = ocl::ocl_moments( contours[i], false );
                Compare(m, dm);
            }
        }
        cv::_InputArray _array(mat1);
        cv::Moments CvMom = cv::moments(_array, binaryImage);
        cv::Moments oclMom = cv::ocl::ocl_moments(_array, binaryImage);

        Compare(CvMom, oclMom);

    }
}
INSTANTIATE_TEST_CASE_P(Moments, ocl_Moments, Combine(
                            Values(CV_8UC1, CV_16UC1, CV_16SC1, CV_64FC1), Values(true,false)));
#endif // HAVE_OPENCL
