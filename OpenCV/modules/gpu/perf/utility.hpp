#ifndef __OPENCV_PERF_GPU_UTILITY_HPP__
#define __OPENCV_PERF_GPU_UTILITY_HPP__

#include "opencv2/core/core.hpp"
#include "opencv2/imgproc/imgproc.hpp"
#include "opencv2/ts/ts_perf.hpp"

cv::Mat readImage(const std::string& fileName, int flags = cv::IMREAD_COLOR);

using perf::MatType;
using perf::MatDepth;

CV_ENUM(BorderMode, cv::BORDER_REFLECT101, cv::BORDER_REPLICATE, cv::BORDER_CONSTANT, cv::BORDER_REFLECT, cv::BORDER_WRAP)
#define ALL_BORDER_MODES testing::ValuesIn(BorderMode::all())

CV_ENUM(Interpolation, cv::INTER_NEAREST, cv::INTER_LINEAR, cv::INTER_CUBIC, cv::INTER_AREA)
#define ALL_INTERPOLATIONS testing::ValuesIn(Interpolation::all())

CV_ENUM(NormType, cv::NORM_INF, cv::NORM_L1, cv::NORM_L2, cv::NORM_HAMMING, cv::NORM_MINMAX)

enum { Gray = 1, TwoChannel = 2, BGR = 3, BGRA = 4 };
CV_ENUM(MatCn, Gray, TwoChannel, BGR, BGRA)
#define GPU_CHANNELS_1_3_4 testing::Values(MatCn(Gray), MatCn(BGR), MatCn(BGRA))
#define GPU_CHANNELS_1_3 testing::Values(MatCn(Gray), MatCn(BGR))

struct CvtColorInfo
{
    int scn;
    int dcn;
    int code;

    CvtColorInfo() {}
    explicit CvtColorInfo(int scn_, int dcn_, int code_) : scn(scn_), dcn(dcn_), code(code_) {}
};
void PrintTo(const CvtColorInfo& info, std::ostream* os);

#define GET_PARAM(k) std::tr1::get< k >(GetParam())

#define DEF_PARAM_TEST(name, ...) typedef ::perf::TestBaseWithParam< std::tr1::tuple< __VA_ARGS__ > > name
#define DEF_PARAM_TEST_1(name, param_type) typedef ::perf::TestBaseWithParam< param_type > name

DEF_PARAM_TEST_1(Sz, cv::Size);
typedef perf::Size_MatType Sz_Type;
DEF_PARAM_TEST(Sz_Depth, cv::Size, MatDepth);
DEF_PARAM_TEST(Sz_Depth_Cn, cv::Size, MatDepth, MatCn);

#define GPU_TYPICAL_MAT_SIZES testing::Values(perf::sz720p, perf::szSXGA, perf::sz1080p)

#define FAIL_NO_CPU() FAIL() << "No such CPU implementation analogy"

#define GPU_SANITY_CHECK(mat, ...) \
    do{ \
        cv::Mat gpu_##mat(mat); \
        SANITY_CHECK(gpu_##mat, ## __VA_ARGS__); \
    } while(0)

#define CPU_SANITY_CHECK(mat, ...) \
    do{ \
        cv::Mat cpu_##mat(mat); \
        SANITY_CHECK(cpu_##mat, ## __VA_ARGS__); \
    } while(0)

#endif // __OPENCV_PERF_GPU_UTILITY_HPP__
