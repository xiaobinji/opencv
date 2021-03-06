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
// Copyright (C) 2000, Intel Corporation, all rights reserved.
// Third party copyrights are property of their respective owners.
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

#include "test_precomp.hpp"

#ifdef HAVE_CUDA

//#define DUMP

//////////////////////////////////////////////////////
// BroxOpticalFlow

#define BROX_OPTICAL_FLOW_DUMP_FILE            "opticalflow/brox_optical_flow.bin"
#define BROX_OPTICAL_FLOW_DUMP_FILE_CC20       "opticalflow/brox_optical_flow_cc20.bin"

struct BroxOpticalFlow : testing::TestWithParam<cv::gpu::DeviceInfo>
{
    cv::gpu::DeviceInfo devInfo;

    virtual void SetUp()
    {
        devInfo = GetParam();

        cv::gpu::setDevice(devInfo.deviceID());
    }
};

TEST_P(BroxOpticalFlow, Regression)
{
    cv::Mat frame0 = readImageType("opticalflow/frame0.png", CV_32FC1);
    ASSERT_FALSE(frame0.empty());

    cv::Mat frame1 = readImageType("opticalflow/frame1.png", CV_32FC1);
    ASSERT_FALSE(frame1.empty());

    cv::gpu::BroxOpticalFlow brox(0.197f /*alpha*/, 50.0f /*gamma*/, 0.8f /*scale_factor*/,
                                  10 /*inner_iterations*/, 77 /*outer_iterations*/, 10 /*solver_iterations*/);

    cv::gpu::GpuMat u;
    cv::gpu::GpuMat v;
    brox(loadMat(frame0), loadMat(frame1), u, v);

#ifndef DUMP
    std::string fname(cvtest::TS::ptr()->get_data_path());
    if (devInfo.majorVersion() >= 2)
        fname += BROX_OPTICAL_FLOW_DUMP_FILE_CC20;
    else
        fname += BROX_OPTICAL_FLOW_DUMP_FILE;

    std::ifstream f(fname.c_str(), std::ios_base::binary);

    int rows, cols;

    f.read((char*)&rows, sizeof(rows));
    f.read((char*)&cols, sizeof(cols));

    cv::Mat u_gold(rows, cols, CV_32FC1);

    for (int i = 0; i < u_gold.rows; ++i)
        f.read(u_gold.ptr<char>(i), u_gold.cols * sizeof(float));

    cv::Mat v_gold(rows, cols, CV_32FC1);

    for (int i = 0; i < v_gold.rows; ++i)
        f.read(v_gold.ptr<char>(i), v_gold.cols * sizeof(float));

    EXPECT_MAT_NEAR(u_gold, u, 0);
    EXPECT_MAT_NEAR(v_gold, v, 0);
#else
    std::string fname(cvtest::TS::ptr()->get_data_path());
    if (devInfo.majorVersion() >= 2)
        fname += BROX_OPTICAL_FLOW_DUMP_FILE_CC20;
    else
        fname += BROX_OPTICAL_FLOW_DUMP_FILE;

    std::ofstream f(fname.c_str(), std::ios_base::binary);

    f.write((char*)&u.rows, sizeof(u.rows));
    f.write((char*)&u.cols, sizeof(u.cols));

    cv::Mat h_u(u);
    cv::Mat h_v(v);

    for (int i = 0; i < u.rows; ++i)
        f.write(h_u.ptr<char>(i), u.cols * sizeof(float));

    for (int i = 0; i < v.rows; ++i)
        f.write(h_v.ptr<char>(i), v.cols * sizeof(float));

#endif
}

INSTANTIATE_TEST_CASE_P(GPU_Video, BroxOpticalFlow, ALL_DEVICES);

//////////////////////////////////////////////////////
// GoodFeaturesToTrack

IMPLEMENT_PARAM_CLASS(MinDistance, double)

PARAM_TEST_CASE(GoodFeaturesToTrack, cv::gpu::DeviceInfo, MinDistance)
{
    cv::gpu::DeviceInfo devInfo;
    double minDistance;

    virtual void SetUp()
    {
        devInfo = GET_PARAM(0);
        minDistance = GET_PARAM(1);

        cv::gpu::setDevice(devInfo.deviceID());
    }
};

TEST_P(GoodFeaturesToTrack, Accuracy)
{
    cv::Mat image = readImage("opticalflow/frame0.png", cv::IMREAD_GRAYSCALE);
    ASSERT_FALSE(image.empty());

    int maxCorners = 1000;
    double qualityLevel = 0.01;

    cv::gpu::GoodFeaturesToTrackDetector_GPU detector(maxCorners, qualityLevel, minDistance);

    if (!supportFeature(devInfo, cv::gpu::GLOBAL_ATOMICS))
    {
        try
        {
            cv::gpu::GpuMat d_pts;
            detector(loadMat(image), d_pts);
        }
        catch (const cv::Exception& e)
        {
            ASSERT_EQ(CV_StsNotImplemented, e.code);
        }
    }
    else
    {
        cv::gpu::GpuMat d_pts;
        detector(loadMat(image), d_pts);

        std::vector<cv::Point2f> pts(d_pts.cols);
        cv::Mat pts_mat(1, d_pts.cols, CV_32FC2, (void*)&pts[0]);
        d_pts.download(pts_mat);

        std::vector<cv::Point2f> pts_gold;
        cv::goodFeaturesToTrack(image, pts_gold, maxCorners, qualityLevel, minDistance);

        ASSERT_EQ(pts_gold.size(), pts.size());

        size_t mistmatch = 0;
        for (size_t i = 0; i < pts.size(); ++i)
        {
            cv::Point2i a = pts_gold[i];
            cv::Point2i b = pts[i];

            bool eq = std::abs(a.x - b.x) < 1 && std::abs(a.y - b.y) < 1;

            if (!eq)
                ++mistmatch;
        }

        double bad_ratio = static_cast<double>(mistmatch) / pts.size();

        ASSERT_LE(bad_ratio, 0.01);
    }
}

TEST_P(GoodFeaturesToTrack, EmptyCorners)
{
    int maxCorners = 1000;
    double qualityLevel = 0.01;

    cv::gpu::GoodFeaturesToTrackDetector_GPU detector(maxCorners, qualityLevel, minDistance);

    cv::gpu::GpuMat src(100, 100, CV_8UC1, cv::Scalar::all(0));
    cv::gpu::GpuMat corners(1, maxCorners, CV_32FC2);

    detector(src, corners);

    ASSERT_TRUE( corners.empty() );
}

INSTANTIATE_TEST_CASE_P(GPU_Video, GoodFeaturesToTrack, testing::Combine(
    ALL_DEVICES,
    testing::Values(MinDistance(0.0), MinDistance(3.0))));

//////////////////////////////////////////////////////
// PyrLKOpticalFlow

IMPLEMENT_PARAM_CLASS(UseGray, bool)

PARAM_TEST_CASE(PyrLKOpticalFlow, cv::gpu::DeviceInfo, UseGray)
{
    cv::gpu::DeviceInfo devInfo;
    bool useGray;

    virtual void SetUp()
    {
        devInfo = GET_PARAM(0);
        useGray = GET_PARAM(1);

        cv::gpu::setDevice(devInfo.deviceID());
    }
};

TEST_P(PyrLKOpticalFlow, Sparse)
{
    cv::Mat frame0 = readImage("opticalflow/frame0.png", useGray ? cv::IMREAD_GRAYSCALE : cv::IMREAD_COLOR);
    ASSERT_FALSE(frame0.empty());

    cv::Mat frame1 = readImage("opticalflow/frame1.png", useGray ? cv::IMREAD_GRAYSCALE : cv::IMREAD_COLOR);
    ASSERT_FALSE(frame1.empty());

    cv::Mat gray_frame;
    if (useGray)
        gray_frame = frame0;
    else
        cv::cvtColor(frame0, gray_frame, cv::COLOR_BGR2GRAY);

    std::vector<cv::Point2f> pts;
    cv::goodFeaturesToTrack(gray_frame, pts, 1000, 0.01, 0.0);

    cv::gpu::GpuMat d_pts;
    cv::Mat pts_mat(1, (int)pts.size(), CV_32FC2, (void*)&pts[0]);
    d_pts.upload(pts_mat);

    cv::gpu::PyrLKOpticalFlow pyrLK;

    cv::gpu::GpuMat d_nextPts;
    cv::gpu::GpuMat d_status;
    pyrLK.sparse(loadMat(frame0), loadMat(frame1), d_pts, d_nextPts, d_status);

    std::vector<cv::Point2f> nextPts(d_nextPts.cols);
    cv::Mat nextPts_mat(1, d_nextPts.cols, CV_32FC2, (void*)&nextPts[0]);
    d_nextPts.download(nextPts_mat);

    std::vector<unsigned char> status(d_status.cols);
    cv::Mat status_mat(1, d_status.cols, CV_8UC1, (void*)&status[0]);
    d_status.download(status_mat);

    std::vector<cv::Point2f> nextPts_gold;
    std::vector<unsigned char> status_gold;
    cv::calcOpticalFlowPyrLK(frame0, frame1, pts, nextPts_gold, status_gold, cv::noArray());

    ASSERT_EQ(nextPts_gold.size(), nextPts.size());
    ASSERT_EQ(status_gold.size(), status.size());

    size_t mistmatch = 0;
    for (size_t i = 0; i < nextPts.size(); ++i)
    {
        cv::Point2i a = nextPts[i];
        cv::Point2i b = nextPts_gold[i];

        if (status[i] != status_gold[i])
        {
            ++mistmatch;
            continue;
        }

        if (status[i])
        {
            bool eq = std::abs(a.x - b.x) <= 1 && std::abs(a.y - b.y) <= 1;

            if (!eq)
                ++mistmatch;
        }
    }

    double bad_ratio = static_cast<double>(mistmatch) / nextPts.size();

    ASSERT_LE(bad_ratio, 0.01);
}

INSTANTIATE_TEST_CASE_P(GPU_Video, PyrLKOpticalFlow, testing::Combine(
    ALL_DEVICES,
    testing::Values(UseGray(true), UseGray(false))));

//////////////////////////////////////////////////////
// FarnebackOpticalFlow

IMPLEMENT_PARAM_CLASS(PyrScale, double)
IMPLEMENT_PARAM_CLASS(PolyN, int)
CV_FLAGS(FarnebackOptFlowFlags, 0, cv::OPTFLOW_FARNEBACK_GAUSSIAN)
IMPLEMENT_PARAM_CLASS(UseInitFlow, bool)

PARAM_TEST_CASE(FarnebackOpticalFlow, cv::gpu::DeviceInfo, PyrScale, PolyN, FarnebackOptFlowFlags, UseInitFlow)
{
    cv::gpu::DeviceInfo devInfo;
    double pyrScale;
    int polyN;
    int flags;
    bool useInitFlow;

    virtual void SetUp()
    {
        devInfo = GET_PARAM(0);
        pyrScale = GET_PARAM(1);
        polyN = GET_PARAM(2);
        flags = GET_PARAM(3);
        useInitFlow = GET_PARAM(4);

        cv::gpu::setDevice(devInfo.deviceID());
    }
};

TEST_P(FarnebackOpticalFlow, Accuracy)
{
    cv::Mat frame0 = readImage("opticalflow/rubberwhale1.png", cv::IMREAD_GRAYSCALE);
    ASSERT_FALSE(frame0.empty());

    cv::Mat frame1 = readImage("opticalflow/rubberwhale2.png", cv::IMREAD_GRAYSCALE);
    ASSERT_FALSE(frame1.empty());

    double polySigma = polyN <= 5 ? 1.1 : 1.5;

    cv::gpu::FarnebackOpticalFlow calc;
    calc.pyrScale = pyrScale;
    calc.polyN = polyN;
    calc.polySigma = polySigma;
    calc.flags = flags;

    cv::gpu::GpuMat d_flowx, d_flowy;
    calc(loadMat(frame0), loadMat(frame1), d_flowx, d_flowy);

    cv::Mat flow;
    if (useInitFlow)
    {
        cv::Mat flowxy[] = {cv::Mat(d_flowx), cv::Mat(d_flowy)};
        cv::merge(flowxy, 2, flow);
    }

    if (useInitFlow)
    {
        calc.flags |= cv::OPTFLOW_USE_INITIAL_FLOW;
        calc(loadMat(frame0), loadMat(frame1), d_flowx, d_flowy);
    }

    cv::calcOpticalFlowFarneback(
        frame0, frame1, flow, calc.pyrScale, calc.numLevels, calc.winSize,
        calc.numIters,  calc.polyN, calc.polySigma, calc.flags);

    std::vector<cv::Mat> flowxy;
    cv::split(flow, flowxy);

    EXPECT_MAT_SIMILAR(flowxy[0], d_flowx, 0.1);
    EXPECT_MAT_SIMILAR(flowxy[1], d_flowy, 0.1);
}

INSTANTIATE_TEST_CASE_P(GPU_Video, FarnebackOpticalFlow, testing::Combine(
    ALL_DEVICES,
    testing::Values(PyrScale(0.3), PyrScale(0.5), PyrScale(0.8)),
    testing::Values(PolyN(5), PolyN(7)),
    testing::Values(FarnebackOptFlowFlags(0), FarnebackOptFlowFlags(cv::OPTFLOW_FARNEBACK_GAUSSIAN)),
    testing::Values(UseInitFlow(false), UseInitFlow(true))));

struct OpticalFlowNan : public BroxOpticalFlow {};

TEST_P(OpticalFlowNan, Regression)
{
    cv::Mat frame0 = readImageType("opticalflow/frame0.png", CV_32FC1);
    ASSERT_FALSE(frame0.empty());
    cv::Mat r_frame0, r_frame1;
    cv::resize(frame0, r_frame0, cv::Size(1380,1000));

    cv::Mat frame1 = readImageType("opticalflow/frame1.png", CV_32FC1);
    ASSERT_FALSE(frame1.empty());
    cv::resize(frame1, r_frame1, cv::Size(1380,1000));

    cv::gpu::BroxOpticalFlow brox(0.197f /*alpha*/, 50.0f /*gamma*/, 0.8f /*scale_factor*/,
                                  5 /*inner_iterations*/, 150 /*outer_iterations*/, 10 /*solver_iterations*/);

    cv::gpu::GpuMat u;
    cv::gpu::GpuMat v;
    brox(loadMat(r_frame0), loadMat(r_frame1), u, v);

    cv::Mat h_u, h_v;
    u.download(h_u);
    v.download(h_v);
    EXPECT_TRUE(cv::checkRange(h_u));
    EXPECT_TRUE(cv::checkRange(h_v));
};

INSTANTIATE_TEST_CASE_P(GPU_Video, OpticalFlowNan, ALL_DEVICES);

//////////////////////////////////////////////////////
// FGDStatModel

namespace cv
{
    template<> void Ptr<CvBGStatModel>::delete_obj()
    {
        cvReleaseBGStatModel(&obj);
    }
}

PARAM_TEST_CASE(FGDStatModel, cv::gpu::DeviceInfo, std::string, Channels)
{
    cv::gpu::DeviceInfo devInfo;
    std::string inputFile;
    int out_cn;

    virtual void SetUp()
    {
        devInfo = GET_PARAM(0);
        cv::gpu::setDevice(devInfo.deviceID());

        inputFile = std::string(cvtest::TS::ptr()->get_data_path()) + "video/" + GET_PARAM(1);

        out_cn = GET_PARAM(2);
    }
};

TEST_P(FGDStatModel, Update)
{
    cv::VideoCapture cap(inputFile);
    ASSERT_TRUE(cap.isOpened());

    cv::Mat frame;
    cap >> frame;
    ASSERT_FALSE(frame.empty());

    IplImage ipl_frame = frame;
    cv::Ptr<CvBGStatModel> model(cvCreateFGDStatModel(&ipl_frame));

    cv::gpu::GpuMat d_frame(frame);
    cv::gpu::FGDStatModel d_model(out_cn);
    d_model.create(d_frame);

    cv::Mat h_background;
    cv::Mat h_foreground;
    cv::Mat h_background3;

    cv::Mat backgroundDiff;
    cv::Mat foregroundDiff;

    for (int i = 0; i < 5; ++i)
    {
        cap >> frame;
        ASSERT_FALSE(frame.empty());

        ipl_frame = frame;
        int gold_count = cvUpdateBGStatModel(&ipl_frame, model);

        d_frame.upload(frame);

        int count = d_model.update(d_frame);

        ASSERT_EQ(gold_count, count);

        cv::Mat gold_background(model->background);
        cv::Mat gold_foreground(model->foreground);

        if (out_cn == 3)
            d_model.background.download(h_background3);
        else
        {
            d_model.background.download(h_background);
            cv::cvtColor(h_background, h_background3, cv::COLOR_BGRA2BGR);
        }
        d_model.foreground.download(h_foreground);

        ASSERT_MAT_NEAR(gold_background, h_background3, 1.0);
        ASSERT_MAT_NEAR(gold_foreground, h_foreground, 0.0);
    }
}

INSTANTIATE_TEST_CASE_P(GPU_Video, FGDStatModel, testing::Combine(
    ALL_DEVICES,
    testing::Values(std::string("768x576.avi")),
    testing::Values(Channels(3), Channels(4))));

//////////////////////////////////////////////////////
// MOG

IMPLEMENT_PARAM_CLASS(LearningRate, double)

PARAM_TEST_CASE(MOG, cv::gpu::DeviceInfo, std::string, UseGray, LearningRate, UseRoi)
{
    cv::gpu::DeviceInfo devInfo;
    std::string inputFile;
    bool useGray;
    double learningRate;
    bool useRoi;

    virtual void SetUp()
    {
        devInfo = GET_PARAM(0);
        cv::gpu::setDevice(devInfo.deviceID());

        inputFile = std::string(cvtest::TS::ptr()->get_data_path()) + "video/" + GET_PARAM(1);

        useGray = GET_PARAM(2);

        learningRate = GET_PARAM(3);

        useRoi = GET_PARAM(4);
    }
};

TEST_P(MOG, Update)
{
    cv::VideoCapture cap(inputFile);
    ASSERT_TRUE(cap.isOpened());

    cv::Mat frame;
    cap >> frame;
    ASSERT_FALSE(frame.empty());

    cv::gpu::MOG_GPU mog;
    cv::gpu::GpuMat foreground = createMat(frame.size(), CV_8UC1, useRoi);

    cv::BackgroundSubtractorMOG mog_gold;
    cv::Mat foreground_gold;

    for (int i = 0; i < 10; ++i)
    {
        cap >> frame;
        ASSERT_FALSE(frame.empty());

        if (useGray)
        {
            cv::Mat temp;
            cv::cvtColor(frame, temp, cv::COLOR_BGR2GRAY);
            cv::swap(temp, frame);
        }

        mog(loadMat(frame, useRoi), foreground, (float)learningRate);

        mog_gold(frame, foreground_gold, learningRate);

        ASSERT_MAT_NEAR(foreground_gold, foreground, 0.0);
    }
}

INSTANTIATE_TEST_CASE_P(GPU_Video, MOG, testing::Combine(
    ALL_DEVICES,
    testing::Values(std::string("768x576.avi")),
    testing::Values(UseGray(true), UseGray(false)),
    testing::Values(LearningRate(0.0), LearningRate(0.01)),
    WHOLE_SUBMAT));

//////////////////////////////////////////////////////
// MOG2

PARAM_TEST_CASE(MOG2, cv::gpu::DeviceInfo, std::string, UseGray, UseRoi)
{
    cv::gpu::DeviceInfo devInfo;
    std::string inputFile;
    bool useGray;
    bool useRoi;

    virtual void SetUp()
    {
        devInfo = GET_PARAM(0);
        cv::gpu::setDevice(devInfo.deviceID());

        inputFile = std::string(cvtest::TS::ptr()->get_data_path()) + "video/" + GET_PARAM(1);

        useGray = GET_PARAM(2);

        useRoi = GET_PARAM(3);
    }
};

TEST_P(MOG2, Update)
{
    cv::VideoCapture cap(inputFile);
    ASSERT_TRUE(cap.isOpened());

    cv::Mat frame;
    cap >> frame;
    ASSERT_FALSE(frame.empty());

    cv::gpu::MOG2_GPU mog2;
    cv::gpu::GpuMat foreground = createMat(frame.size(), CV_8UC1, useRoi);

    cv::BackgroundSubtractorMOG2 mog2_gold;
    cv::Mat foreground_gold;

    for (int i = 0; i < 10; ++i)
    {
        cap >> frame;
        ASSERT_FALSE(frame.empty());

        if (useGray)
        {
            cv::Mat temp;
            cv::cvtColor(frame, temp, cv::COLOR_BGR2GRAY);
            cv::swap(temp, frame);
        }

        mog2(loadMat(frame, useRoi), foreground);

        mog2_gold(frame, foreground_gold);

        double norm = cv::norm(foreground_gold, cv::Mat(foreground), cv::NORM_L1);

        norm /= foreground_gold.size().area();

        ASSERT_LE(norm, 0.09);
    }
}

TEST_P(MOG2, getBackgroundImage)
{
    if (useGray)
        return;

    cv::VideoCapture cap(inputFile);
    ASSERT_TRUE(cap.isOpened());

    cv::Mat frame;

    cv::gpu::MOG2_GPU mog2;
    cv::gpu::GpuMat foreground;

    cv::BackgroundSubtractorMOG2 mog2_gold;
    cv::Mat foreground_gold;

    for (int i = 0; i < 10; ++i)
    {
        cap >> frame;
        ASSERT_FALSE(frame.empty());

        mog2(loadMat(frame, useRoi), foreground);

        mog2_gold(frame, foreground_gold);
    }

    cv::gpu::GpuMat background = createMat(frame.size(), frame.type(), useRoi);
    mog2.getBackgroundImage(background);

    cv::Mat background_gold;
    mog2_gold.getBackgroundImage(background_gold);

    ASSERT_MAT_NEAR(background_gold, background, 0);
}

INSTANTIATE_TEST_CASE_P(GPU_Video, MOG2, testing::Combine(
    ALL_DEVICES,
    testing::Values(std::string("768x576.avi")),
    testing::Values(UseGray(true), UseGray(false)),
    WHOLE_SUBMAT));

//////////////////////////////////////////////////////
// VIBE

PARAM_TEST_CASE(VIBE, cv::gpu::DeviceInfo, cv::Size, MatType, UseRoi)
{
};

TEST_P(VIBE, Accuracy)
{
    const cv::gpu::DeviceInfo devInfo = GET_PARAM(0);
    cv::gpu::setDevice(devInfo.deviceID());
    const cv::Size size = GET_PARAM(1);
    const int type = GET_PARAM(2);
    const bool useRoi = GET_PARAM(3);

    const cv::Mat fullfg(size, CV_8UC1, cv::Scalar::all(255));

    cv::Mat frame = randomMat(size, type, 0.0, 100);
    cv::gpu::GpuMat d_frame = loadMat(frame, useRoi);

    cv::gpu::VIBE_GPU vibe;
    cv::gpu::GpuMat d_fgmask = createMat(size, CV_8UC1, useRoi);
    vibe.initialize(d_frame);

    for (int i = 0; i < 20; ++i)
        vibe(d_frame, d_fgmask);

    frame = randomMat(size, type, 160, 255);
    d_frame = loadMat(frame, useRoi);
    vibe(d_frame, d_fgmask);

    // now fgmask should be entirely foreground
    ASSERT_MAT_NEAR(fullfg, d_fgmask, 0);
}

INSTANTIATE_TEST_CASE_P(GPU_Video, VIBE, testing::Combine(
    ALL_DEVICES,
    DIFFERENT_SIZES,
    testing::Values(MatType(CV_8UC1), MatType(CV_8UC3), MatType(CV_8UC4)),
    WHOLE_SUBMAT));

//////////////////////////////////////////////////////
// GMG

PARAM_TEST_CASE(GMG, cv::gpu::DeviceInfo, cv::Size, MatDepth, Channels, UseRoi)
{
};

TEST_P(GMG, Accuracy)
{
    const cv::gpu::DeviceInfo devInfo = GET_PARAM(0);
    cv::gpu::setDevice(devInfo.deviceID());
    const cv::Size size = GET_PARAM(1);
    const int depth = GET_PARAM(2);
    const int channels = GET_PARAM(3);
    const bool useRoi = GET_PARAM(4);

    const int type = CV_MAKE_TYPE(depth, channels);

    const cv::Mat zeros(size, CV_8UC1, cv::Scalar::all(0));
    const cv::Mat fullfg(size, CV_8UC1, cv::Scalar::all(255));

    cv::Mat frame = randomMat(size, type, 0, 100);
    cv::gpu::GpuMat d_frame = loadMat(frame, useRoi);

    cv::gpu::GMG_GPU gmg;
    gmg.numInitializationFrames = 5;
    gmg.smoothingRadius = 0;
    gmg.initialize(d_frame.size(), 0, 255);

    cv::gpu::GpuMat d_fgmask = createMat(size, CV_8UC1, useRoi);

    for (int i = 0; i < gmg.numInitializationFrames; ++i)
    {
        gmg(d_frame, d_fgmask);

        // fgmask should be entirely background during training
        ASSERT_MAT_NEAR(zeros, d_fgmask, 0);
    }

    frame = randomMat(size, type, 160, 255);
    d_frame = loadMat(frame, useRoi);
    gmg(d_frame, d_fgmask);

    // now fgmask should be entirely foreground
    ASSERT_MAT_NEAR(fullfg, d_fgmask, 0);
}

INSTANTIATE_TEST_CASE_P(GPU_Video, GMG, testing::Combine(
    ALL_DEVICES,
    DIFFERENT_SIZES,
    testing::Values(MatType(CV_8U), MatType(CV_16U), MatType(CV_32F)),
    testing::Values(Channels(1), Channels(3), Channels(4)),
    WHOLE_SUBMAT));

//////////////////////////////////////////////////////
// VideoWriter

#ifdef WIN32

PARAM_TEST_CASE(VideoWriter, cv::gpu::DeviceInfo, std::string)
{
    cv::gpu::DeviceInfo devInfo;
    std::string inputFile;

    std::string outputFile;

    virtual void SetUp()
    {
        devInfo = GET_PARAM(0);
        inputFile = GET_PARAM(1);

        cv::gpu::setDevice(devInfo.deviceID());

        inputFile = std::string(cvtest::TS::ptr()->get_data_path()) + "video/" + inputFile;
        outputFile = cv::tempfile(".avi");
    }
};

TEST_P(VideoWriter, Regression)
{
    const double FPS = 25.0;

    cv::VideoCapture reader(inputFile);
    ASSERT_TRUE( reader.isOpened() );

    cv::gpu::VideoWriter_GPU d_writer;

    cv::Mat frame;
    cv::gpu::GpuMat d_frame;

    for (int i = 0; i < 10; ++i)
    {
        reader >> frame;
        ASSERT_FALSE(frame.empty());

        d_frame.upload(frame);

        if (!d_writer.isOpened())
            d_writer.open(outputFile, frame.size(), FPS);

        d_writer.write(d_frame);
    }

    reader.release();
    d_writer.close();

    reader.open(outputFile);
    ASSERT_TRUE( reader.isOpened() );

    for (int i = 0; i < 5; ++i)
    {
        reader >> frame;
        ASSERT_FALSE( frame.empty() );
    }
}

INSTANTIATE_TEST_CASE_P(GPU_Video, VideoWriter, testing::Combine(
    ALL_DEVICES,
    testing::Values(std::string("768x576.avi"), std::string("1920x1080.avi"))));

#endif // WIN32

//////////////////////////////////////////////////////
// VideoReader

PARAM_TEST_CASE(VideoReader, cv::gpu::DeviceInfo, std::string)
{
    cv::gpu::DeviceInfo devInfo;
    std::string inputFile;

    virtual void SetUp()
    {
        devInfo = GET_PARAM(0);
        inputFile = GET_PARAM(1);

        cv::gpu::setDevice(devInfo.deviceID());

        inputFile = std::string(cvtest::TS::ptr()->get_data_path()) + "video/" + inputFile;
    }
};

TEST_P(VideoReader, Regression)
{
    cv::gpu::VideoReader_GPU reader(inputFile);
    ASSERT_TRUE( reader.isOpened() );

    cv::gpu::GpuMat frame;

    for (int i = 0; i < 10; ++i)
    {
        ASSERT_TRUE( reader.read(frame) );
        ASSERT_FALSE( frame.empty() );
    }

    reader.close();
    ASSERT_FALSE( reader.isOpened() );
}

INSTANTIATE_TEST_CASE_P(GPU_Video, VideoReader, testing::Combine(
    ALL_DEVICES,
    testing::Values(std::string("768x576.avi"), std::string("1920x1080.avi"))));

#endif // HAVE_CUDA
