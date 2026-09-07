#pragma once
#include <opencv2/opencv.hpp>
#include <vector>

enum class FixMode {
    None,
    Deskew,
    Perspective
};

struct FixResult {
    FixMode mode = FixMode::None;
    double angleDeg = 0.0;
    double blurScore = 0.0;
    double blurAfter = 0.0;
    std::vector<cv::Point2f> corners;
    cv::Mat quadDebug;
    cv::Mat correctedRaw;
    cv::Mat out;
};

double detectSkewAngle(const cv::Mat& gray);

double measureBlur(const cv::Mat& gray);

void sortQuadCorners(std::vector<cv::Point2f>& pts);

cv::Mat processManual(const cv::Mat& src, const std::vector<cv::Point2f>& corners);

bool detectQuad(const cv::Mat& src, std::vector<cv::Point2f>& corners,
                bool& touchesBorder);

cv::Mat enhanceBoard(const cv::Mat& src);

FixResult processPhoto(const cv::Mat& src);
