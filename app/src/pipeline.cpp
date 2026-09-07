#include "pipeline.h"

#include <algorithm>
#include <cmath>

#include <opencv2/photo.hpp>

namespace {

const double kMinCorrectableAngle = 0.4;
const double kBorderTolerance = 3.0;

double normalizeAngle(double a) {
    while (a > 45.0) a -= 90.0;
    while (a < -45.0) a += 90.0;
    return a;
}

double medianOf(std::vector<double>& values) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    size_t mid = values.size() / 2;
    if (values.size() % 2 == 1) return values[mid];
    return 0.5 * (values[mid - 1] + values[mid]);
}

cv::Mat rotateFullCanvas(const cv::Mat& src, double angle, bool cwIsNegative) {
    int w = src.cols;
    int h = src.rows;
    double rad = angle * CV_PI / 180.0;
    double cs = std::abs(std::cos(rad));
    double sn = std::abs(std::sin(rad));
    int newW = static_cast<int>(std::ceil(w * cs + h * sn));
    int newH = static_cast<int>(std::ceil(w * sn + h * cs));

    double rot = (cwIsNegative ? -1.0 : 1.0) * angle;
    cv::Mat M = cv::getRotationMatrix2D(cv::Point2f(0.5f * w, 0.5f * h), rot, 1.0);
    M.at<double>(0, 2) += (newW - w) * 0.5;
    M.at<double>(1, 2) += (newH - h) * 0.5;

    cv::Mat out;
    cv::warpAffine(src, out, M, cv::Size(newW, newH), cv::INTER_CUBIC,
                   cv::BORDER_CONSTANT, cv::Scalar(255, 255, 255));
    return out;
}

}  // namespace

void sortQuadCorners(std::vector<cv::Point2f>& pts) {
    std::sort(pts.begin(), pts.end(), [](const cv::Point2f& a, const cv::Point2f& b) {
        return a.y < b.y;
    });
    cv::Point2f topLeft = pts[0].x < pts[1].x ? pts[0] : pts[1];
    cv::Point2f topRight = pts[0].x < pts[1].x ? pts[1] : pts[0];
    cv::Point2f bottomLeft = pts[2].x < pts[3].x ? pts[2] : pts[3];
    cv::Point2f bottomRight = pts[2].x < pts[3].x ? pts[3] : pts[2];
    pts = {topLeft, topRight, bottomRight, bottomLeft};
}

double detectSkewAngle(const cv::Mat& src) {
    cv::Mat gray;
    if (src.channels() == 1) {
        gray = src;
    } else {
        cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);
    }
    if (gray.cols > 1600) {
        double s = 1600.0 / gray.cols;
        cv::resize(gray, gray, cv::Size(1600, static_cast<int>(gray.rows * s)));
    }
    cv::GaussianBlur(gray, gray, cv::Size(5, 5), 0);

    cv::Mat bin;
    cv::threshold(gray, bin, 0, 255, cv::THRESH_BINARY_INV | cv::THRESH_OTSU);

    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(25, 1));
    cv::dilate(bin, bin, kernel);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(bin, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    std::vector<double> lineAngles;
    lineAngles.reserve(contours.size());
    for (const auto& c : contours) {
        if (c.size() < 8) continue;
        cv::RotatedRect box = cv::minAreaRect(c);
        double w = box.size.width;
        double h = box.size.height;
        double longSide = std::max(w, h);
        double shortSide = std::min(w, h);
        if (longSide < 40.0) continue;
        if (shortSide < 2.0 || longSide / std::max(shortSide, 1.0) > 40.0) continue;
        double angle = box.angle;
        if (w < h) angle += 90.0;
        else if (angle < -45.0) angle += 90.0;
        angle = normalizeAngle(angle);
        lineAngles.push_back(angle);
    }
    return medianOf(lineAngles);
}

double quadTiltAngle(const std::vector<cv::Point2f>& corners) {
    if (corners.size() != 4) return 0.0;
    double dx = corners[1].x - corners[0].x;
    double dy = corners[1].y - corners[0].y;
    if (std::abs(dx) < 1.0) return 0.0;
    double angle = std::atan2(dy, dx) * 180.0 / CV_PI;
    if (angle > 45.0) angle -= 90.0;
    if (angle < -45.0) angle += 90.0;
    return -angle;
}

double detectBoardAngle(const cv::Mat& bgr) {
    cv::Mat hsv;
    cv::cvtColor(bgr, hsv, cv::COLOR_BGR2HSV);

    cv::Mat green, dark, mask;
    cv::inRange(hsv, cv::Scalar(35, 40, 40), cv::Scalar(85, 255, 255), green);
    cv::inRange(hsv, cv::Scalar(0, 0, 0), cv::Scalar(180, 110, 90), dark);
    mask = green | dark;

    cv::Mat labels, stats, cents;
    int n = cv::connectedComponentsWithStats(mask, labels, stats, cents);

    double frameArea = static_cast<double>(bgr.cols) * bgr.rows;
    double bestAngle = 0.0;
    double bestScore = -1.0;
    for (int i = 1; i < n; ++i) {
        int area = stats.at<int>(i, cv::CC_STAT_AREA);
        if (area < frameArea * 0.03 || area > frameArea * 0.45) continue;

        cv::Mat comp = (labels == i);
        std::vector<std::vector<cv::Point>> cs;
        cv::findContours(comp, cs, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        if (cs.empty()) continue;
        cv::RotatedRect r = cv::minAreaRect(*std::max_element(
            cs.begin(), cs.end(),
            [](const std::vector<cv::Point>& a, const std::vector<cv::Point>& b) {
                return cv::contourArea(a) < cv::contourArea(b);
            }));
        double aspect = std::max(r.size.width, r.size.height) /
                        std::max(std::min(r.size.width, r.size.height), 1.0f);
        if (aspect < 0.9f || aspect > 4.0f) continue;

        double a = r.angle;
        if (r.size.width < r.size.height) a += 90.0;
        while (a > 45.0) a -= 90.0;
        while (a < -45.0) a += 90.0;

        double score = area;
        if (std::fabs(a) >= 2.0) score *= 10.0;
        if (score > bestScore) {
            bestScore = score;
            bestAngle = a;
        }
    }
    return bestAngle;
}

double detectHoughAngle(const cv::Mat& gray) {
    cv::Mat edges;
    cv::Canny(gray, edges, 30, 90);

    std::vector<cv::Vec4i> lines;
    cv::HoughLinesP(edges, lines, 1, CV_PI / 180.0, 50, 120.0, 25.0);

    if (lines.size() < 4) return 0.0;

    const int bins = 90;
    std::vector<double> hist(bins, 0.0);
    double totalWeight = 0.0;
    for (const auto& l : lines) {
        double dx = l[2] - l[0];
        double dy = l[3] - l[1];
        double len = std::sqrt(dx * dx + dy * dy);
        if (len < 80.0) continue;
        double a = std::atan2(dy, dx) * 180.0 / CV_PI;
        while (a > 45.0) a -= 90.0;
        while (a < -45.0) a += 90.0;
        int bin = static_cast<int>(std::round(a)) + 45;
        bin = std::max(0, std::min(bins - 1, bin));
        hist[bin] += len;
        totalWeight += len;
    }
    if (totalWeight < 300.0) return 0.0;

    int peak = 0;
    for (int i = 1; i < bins; ++i) {
        if (hist[i] > hist[peak]) peak = i;
    }
    double angle = peak - 45.0;
    if (std::abs(angle) < 1.0) return 0.0;
    if (hist[peak] < totalWeight * 0.18) return 0.0;
    return -angle;
}

bool detectQuad(const cv::Mat& src, std::vector<cv::Point2f>& corners,
                bool& touchesBorder) {
    corners.clear();
    touchesBorder = false;

    int srcW = src.cols;
    double scale = 1.0;
    cv::Mat work = src;
    if (srcW > 1600) {
        scale = 1600.0 / srcW;
        cv::resize(src, work, cv::Size(1600, static_cast<int>(src.rows * scale)));
    }

    cv::Mat gray, blurred, edges;
    cv::cvtColor(work, gray, cv::COLOR_BGR2GRAY);
    cv::GaussianBlur(gray, blurred, cv::Size(5, 5), 0);
    cv::Canny(blurred, edges, 30, 90);

    cv::Mat kernel3 = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
    cv::dilate(edges, edges, kernel3);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(edges, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    double frameArea = static_cast<double>(work.cols) * work.rows;
    const double minArea = frameArea * 0.03;

    std::vector<size_t> order(contours.size());
    for (size_t i = 0; i < contours.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return cv::contourArea(contours[a]) > cv::contourArea(contours[b]);
    });

    std::vector<cv::Point> bestApprox;
    bool bestTouches = false;
    for (const size_t idx : order) {
        double area = cv::contourArea(contours[idx]);
        if (area < minArea) break;
        std::vector<cv::Point> hull;
        cv::convexHull(contours[idx], hull);
        double perim = cv::arcLength(hull, true);
        for (const double epsScale : {0.010, 0.02, 0.035, 0.05}) {
            std::vector<cv::Point> approx;
            cv::approxPolyDP(hull, approx, epsScale * perim, true);
            if (approx.size() != 4) continue;
            std::vector<cv::Point2f> cand;
            for (const auto& p : approx) cand.emplace_back(p.x, p.y);
            sortQuadCorners(cand);
            float sideA = static_cast<float>(cv::norm(cand[1] - cand[0]));
            float sideB = static_cast<float>(cv::norm(cand[2] - cand[1]));
            float sideC = static_cast<float>(cv::norm(cand[3] - cand[2]));
            float sideD = static_cast<float>(cv::norm(cand[0] - cand[3]));
            float minSide = std::min({sideA, sideB, sideC, sideD});
            float width = std::max(sideA, sideC);
            float height = std::max(sideB, sideD);
            if (minSide < 40.0f) continue;
            float aspect = width / std::max(height, 1.0f);
            if (aspect < 0.15f || aspect > 6.5f) continue;
            bool touch = false;
            for (const auto& p : approx) {
                if (p.x <= kBorderTolerance || p.y <= kBorderTolerance ||
                    p.x >= work.cols - kBorderTolerance ||
                    p.y >= work.rows - kBorderTolerance) {
                    touch = true;
                    break;
                }
            }
            if (!touch) {
                bestApprox = approx;
                bestTouches = false;
                goto found;
            }
            if (!bestTouches) {
                bestApprox = approx;
                bestTouches = true;
            }
            break;
        }
    }
found:
    if (bestApprox.empty()) return false;
    touchesBorder = bestTouches;
    for (const auto& p : bestApprox) {
        corners.emplace_back(static_cast<float>(p.x / scale),
                             static_cast<float>(p.y / scale));
    }
    sortQuadCorners(corners);
    return true;
}

double measureBlur(const cv::Mat& gray) {
    cv::Mat lap;
    cv::Laplacian(gray, lap, CV_64F);
    cv::Scalar mean, stddev;
    cv::meanStdDev(lap, mean, stddev);
    double variance = stddev[0] * stddev[0];
    return variance;
}

cv::Mat enhanceBoard(const cv::Mat& src, double blurScore) {
    cv::Mat out = src;

    cv::Mat gray;
    cv::cvtColor(out, gray, cv::COLOR_BGR2GRAY);
    double meanBri = cv::mean(gray)[0];

    double gamma = 1.0;
    if (meanBri < 95.0) gamma = 0.55;
    else if (meanBri < 130.0) gamma = 0.8;
    else if (meanBri > 185.0) gamma = 1.25;

    if (std::fabs(gamma - 1.0) > 1e-6) {
        cv::Mat lut(1, 256, CV_8U);
        uchar* p = lut.ptr();
        for (int i = 0; i < 256; ++i) {
            p[i] = cv::saturate_cast<uchar>(std::pow(i / 255.0, gamma) * 255.0);
        }
        cv::LUT(out, lut, out);
    }

    cv::Mat lab;
    cv::cvtColor(out, lab, cv::COLOR_BGR2Lab);
    std::vector<cv::Mat> ch(3);
    cv::split(lab, ch);

    cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE(2.0, cv::Size(8, 8));
    clahe->apply(ch[0], ch[0]);
    cv::merge(ch, lab);
    cv::cvtColor(lab, out, cv::COLOR_Lab2BGR);

    if (blurScore < 250.0) {
        cv::detailEnhance(out, out, 10.0f, 0.15f);
    }

    double sharpA, sharpB;
    if (blurScore < 80.0) {
        sharpA = 1.3;
        sharpB = -0.3;
    } else if (blurScore < 200.0) {
        sharpA = 1.7;
        sharpB = -0.7;
    } else {
        sharpA = 1.4;
        sharpB = -0.4;
    }

    cv::Mat smooth;
    cv::GaussianBlur(out, smooth, cv::Size(0, 0), 3.0);
    cv::addWeighted(out, sharpA, smooth, sharpB, 0, out);
    return out;
}

FixResult processPhoto(const cv::Mat& src) {
    FixResult res;
    if (src.empty()) return res;

    cv::Mat base = src.clone();
    bool needColor = base.channels() == 3;
    res.quadDebug = src.clone();

    std::vector<cv::Point2f> corners;
    bool touchesBorder = false;
    bool quadOk = detectQuad(base, corners, touchesBorder);

    if (quadOk) {
        cv::polylines(res.quadDebug,
                      std::vector<std::vector<cv::Point>>{std::vector<cv::Point>{
                          cv::Point(cvRound(corners[0].x), cvRound(corners[0].y)),
                          cv::Point(cvRound(corners[1].x), cvRound(corners[1].y)),
                          cv::Point(cvRound(corners[2].x), cvRound(corners[2].y)),
                          cv::Point(cvRound(corners[3].x), cvRound(corners[3].y))}},
                      true, cv::Scalar(0, 0, 255), 6);
        for (const auto& c : corners) {
            cv::circle(res.quadDebug, cv::Point(cvRound(c.x), cvRound(c.y)), 14,
                       cv::Scalar(0, 255, 0), -1);
        }
    }

    if (quadOk && !touchesBorder) {
        float widthA = static_cast<float>(cv::norm(corners[1] - corners[0]));
        float widthB = static_cast<float>(cv::norm(corners[2] - corners[3]));
        float maxWidth = std::max(widthA, widthB);
        float heightA = static_cast<float>(cv::norm(corners[3] - corners[0]));
        float heightB = static_cast<float>(cv::norm(corners[2] - corners[1]));
        float maxHeight = std::max(heightA, heightB);

        std::vector<cv::Point2f> dst = {
            cv::Point2f(0, 0), cv::Point2f(maxWidth - 1, 0),
            cv::Point2f(maxWidth - 1, maxHeight - 1), cv::Point2f(0, maxHeight - 1)};
        cv::Mat M = cv::getPerspectiveTransform(corners, dst);
        int outW = static_cast<int>(maxWidth);
        int outH = static_cast<int>(maxHeight);
        cv::warpPerspective(base, base, M, cv::Size(outW, outH),
                            cv::INTER_LINEAR, cv::BORDER_CONSTANT,
                            cv::Scalar(255, 255, 255));
        res.mode = FixMode::Perspective;
        res.corners = corners;
    } else {
        cv::Mat gray;
        if (needColor) {
            cv::cvtColor(base, gray, cv::COLOR_BGR2GRAY);
        } else {
            gray = base.clone();
        }

        double totalAngle = 0.0;
        double angle = quadOk ? quadTiltAngle(corners) : 0.0;

        bool boardFixed = false;
        if (std::fabs(angle) < kMinCorrectableAngle) {
            angle = -detectBoardAngle(base);
            if (std::fabs(angle) >= kMinCorrectableAngle) boardFixed = true;
        }
        if (std::fabs(angle) < kMinCorrectableAngle) {
            angle = detectHoughAngle(gray);
        }
        if (std::fabs(angle) >= kMinCorrectableAngle) {
            base = rotateFullCanvas(base, angle, false);
            totalAngle += angle;
            res.angleDeg = totalAngle;
            res.mode = FixMode::Deskew;
            cv::cvtColor(base, gray, cv::COLOR_BGR2GRAY);
        }
        if (!boardFixed) {
            for (int pass = 0; pass < 2; ++pass) {
                double textAngle = detectSkewAngle(gray);
                if (std::fabs(textAngle) < kMinCorrectableAngle) break;
                base = rotateFullCanvas(base, textAngle, false);
                totalAngle += textAngle;
                res.angleDeg = totalAngle;
                res.mode = FixMode::Deskew;
                cv::cvtColor(base, gray, cv::COLOR_BGR2GRAY);
            }
        }
    }

    res.correctedRaw = base.clone();

    cv::Mat grayOut;
    cv::cvtColor(base, grayOut, cv::COLOR_BGR2GRAY);
    res.blurScore = measureBlur(grayOut);

    res.out = enhanceBoard(base, res.blurScore);

    cv::Mat grayAfter;
    cv::cvtColor(res.out, grayAfter, cv::COLOR_BGR2GRAY);
    res.blurAfter = measureBlur(grayAfter);
    return res;
}

cv::Mat processManual(const cv::Mat& src, const std::vector<cv::Point2f>& corners) {
    if (src.empty() || corners.size() != 4) return cv::Mat();
    std::vector<cv::Point2f> c = corners;
    sortQuadCorners(c);

    float widthA = static_cast<float>(cv::norm(c[1] - c[0]));
    float widthB = static_cast<float>(cv::norm(c[2] - c[3]));
    float maxWidth = std::max(widthA, widthB);
    float heightA = static_cast<float>(cv::norm(c[3] - c[0]));
    float heightB = static_cast<float>(cv::norm(c[2] - c[1]));
    float maxHeight = std::max(heightA, heightB);

    if (maxWidth < 20.0f || maxHeight < 20.0f) return cv::Mat();

    std::vector<cv::Point2f> dst = {
        cv::Point2f(0, 0), cv::Point2f(maxWidth - 1, 0),
        cv::Point2f(maxWidth - 1, maxHeight - 1), cv::Point2f(0, maxHeight - 1)};
    cv::Mat M = cv::getPerspectiveTransform(c, dst);
    cv::Mat out;
    cv::warpPerspective(src, out, M, cv::Size(static_cast<int>(maxWidth),
                                              static_cast<int>(maxHeight)),
                        cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(255, 255, 255));

    cv::Mat grayOut;
    cv::cvtColor(out, grayOut, cv::COLOR_BGR2GRAY);
    return enhanceBoard(out, measureBlur(grayOut));
}
