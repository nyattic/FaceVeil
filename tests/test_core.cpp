#include "faceveil/ImageScanner.hpp"
#include "faceveil/Mosaic.hpp"

#include <opencv2/core.hpp>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cassert>
#include <filesystem>
#include <set>

namespace
{
    void writeBytes(const QString &path)
    {
        QFile file(path);
        assert(file.open(QIODevice::WriteOnly));
        assert(file.write("x") == 1);
    }

    void testSupportedImageExtensions()
    {
        assert(faceveil::isSupportedImage("photo.jpg"));
        assert(faceveil::isSupportedImage("photo.JPEG"));
        assert(faceveil::isSupportedImage("photo.webp"));
        assert(!faceveil::isSupportedImage("photo.txt"));
    }

    void testScanImagesRecursesAndDeduplicates()
    {
        QTemporaryDir temp;
        assert(temp.isValid());

        QDir root(temp.path());
        assert(root.mkpath("a/nested"));
        assert(root.mkpath("b"));

        const QString first = root.filePath("a/one.JPG");
        const QString second = root.filePath("a/nested/two.png");
        const QString ignored = root.filePath("b/notes.txt");
        writeBytes(first);
        writeBytes(second);
        writeBytes(ignored);

        const auto nonRecursive = faceveil::scanImages({root.filePath("a")}, false);
        assert(nonRecursive.size() == 1);

        const auto recursive = faceveil::scanImages({root.filePath("a"), first}, true);
        assert(recursive.size() == 2);

        std::set<std::string> relativePaths;
        for (const auto &item: recursive)
        {
            relativePaths.insert(item.relativePath.generic_string());
        }
        assert(relativePaths.contains("one.JPG"));
        assert(relativePaths.contains("nested/two.png"));
    }

    void testApplyMosaicTouchesOnlyDetectedRegion()
    {
        cv::Mat image(8, 8, CV_8UC3);
        for (int y = 0; y < image.rows; ++y)
        {
            for (int x = 0; x < image.cols; ++x)
            {
                image.at<cv::Vec3b>(y, x) = cv::Vec3b(static_cast<uchar>(x * 20),
                                                      static_cast<uchar>(y * 20),
                                                      static_cast<uchar>((x + y) * 10));
            }
        }

        const cv::Vec3b outsideBefore = image.at<cv::Vec3b>(0, 0);
        const cv::Vec3b insideBefore = image.at<cv::Vec3b>(3, 3);

        faceveil::FaceDetections detections;
        detections.push_back({cv::Rect2f(2.0F, 2.0F, 4.0F, 4.0F), 1.0F});
        faceveil::applyMosaic(image, detections, 4, 0.0F);

        assert(image.at<cv::Vec3b>(0, 0) == outsideBefore);
        assert(image.at<cv::Vec3b>(3, 3) != insideBefore);
    }
}

int main()
{
    testSupportedImageExtensions();
    testScanImagesRecursesAndDeduplicates();
    testApplyMosaicTouchesOnlyDetectedRegion();
    return 0;
}
