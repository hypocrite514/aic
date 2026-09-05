#include <opencv2/opencv.hpp>
#include <iostream>
#include <vector>
#include <algorithm>

using namespace cv;
using namespace std;

void sortPoints(vector<Point2f>& pts)
{
    sort(pts.begin(), pts.end(), [](Point2f a, Point2f b) {
        return a.y < b.y;
        });

    Point2f topLeft = pts[0].x < pts[1].x ? pts[0] : pts[1];
    Point2f topRight = pts[0].x < pts[1].x ? pts[1] : pts[0];
    Point2f bottomLeft = pts[2].x < pts[3].x ? pts[2] : pts[3];
    Point2f bottomRight = pts[2].x < pts[3].x ? pts[3] : pts[2];

    pts = { topLeft, topRight, bottomRight, bottomLeft };
}

int main()
{
    string imgPath = "D:/aic/test_images/test1.jpg"; // change to your image path

    Mat src = imread(imgPath);
    if (src.empty()) {
        cout << "Cannot read image: " << imgPath << endl;
        return -1;
    }

    Mat resized;
    float scale = 1.0f;
    if (src.cols > 1200) {
        scale = 1200.0f / src.cols;
        resize(src, resized, Size(1200, int(src.rows * scale)));
    }
    else {
        resized = src.clone();
    }

    Mat gray, blurred;
    cvtColor(resized, gray, COLOR_BGR2GRAY);
    GaussianBlur(gray, blurred, Size(5, 5), 0);

    Mat edges;
    Canny(blurred, edges, 50, 150);

    vector<vector<Point>> contours;
    findContours(edges, contours, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE);

    int maxIdx = -1;
    double maxArea = 0;
    for (int i = 0; i < contours.size(); i++) {
        double area = contourArea(contours[i]);
        if (area > maxArea) {
            maxArea = area;
            maxIdx = i;
        }
    }

    if (maxIdx < 0 || maxArea < 1000) {
        cout << "No paper found, try another image" << endl;
        return -1;
    }

    vector<Point> approx;
    approxPolyDP(contours[maxIdx], approx, 0.2 * arcLength(contours[maxIdx], true), true);

    cout << "approx corners: " << approx.size() << endl;
    if (approx.size() != 4) {
        drawContours(resized, contours, maxIdx, Scalar(0, 255, 0), 2);
        imshow("Debug", resized);
        waitKey(0);
        return -1;
    }

    vector<Point2f> quad;
    for (size_t i = 0; i < approx.size(); i++) {
        quad.push_back(Point2f(approx[i].x, approx[i].y));
    }
    sortPoints(quad);

    float widthA = norm(quad[1] - quad[0]);
    float widthB = norm(quad[2] - quad[3]);
    float maxWidth = max(widthA, widthB);

    float heightA = norm(quad[3] - quad[0]);
    float heightB = norm(quad[2] - quad[1]);
    float maxHeight = max(heightA, heightB);

    vector<Point2f> dst = {
        Point2f(0, 0),
        Point2f(maxWidth - 1, 0),
        Point2f(maxWidth - 1, maxHeight - 1),
        Point2f(0, maxHeight - 1)
    };

    Mat M = getPerspectiveTransform(quad, dst);
    Mat corrected;
    warpPerspective(resized, corrected, M, Size(maxWidth, maxHeight));

    imshow("Original", resized);
    imshow("Corrected", corrected);

    string outPath = "D:/aic/test_images/test1_corrected.jpg";
    imwrite(outPath, corrected);
    cout << "Done: " << outPath << endl;

    waitKey(0);
    return 0;
}