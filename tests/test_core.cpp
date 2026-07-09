#include "redactly/DetectionGeometry.hpp"
#include "redactly/ImageIo.hpp"
#include "redactly/ImageScanner.hpp"
#include "redactly/Mosaic.hpp"
#include "redactly/OnnxGraphPatch.hpp"
#include "redactly/PathSafety.hpp"
#include "redactly/ScrfdFaceDetector.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <set>
#include <vector>

#ifdef REDACTLY_HAVE_EXIV2
#include <exiv2/exiv2.hpp>
#endif

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
        assert(redactly::isSupportedImage("photo.jpg"));
        assert(redactly::isSupportedImage("photo.JPEG"));
        assert(redactly::isSupportedImage("photo.webp"));
        assert(!redactly::isSupportedImage("photo.txt"));
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

        const auto nonRecursive = redactly::scanImages({root.filePath("a")}, false);
        assert(nonRecursive.size() == 1);

        const auto recursive = redactly::scanImages({root.filePath("a"), first}, true);
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

        redactly::FaceDetections detections;
        detections.push_back({cv::Rect2f(2.0F, 2.0F, 4.0F, 4.0F), 1.0F});
        redactly::applyMosaic(image, detections, 4, 0.0F);

        assert(image.at<cv::Vec3b>(0, 0) == outsideBefore);
        assert(image.at<cv::Vec3b>(3, 3) != insideBefore);
    }

    void testSoftEdgesKeepDetectedRegionFullyCovered()
    {
        cv::Mat image(64, 64, CV_8UC3, cv::Scalar(100, 100, 100));
        const cv::Rect box(24, 24, 16, 16);

        redactly::FaceDetections detections;
        detections.push_back({cv::Rect2f(box), 1.0F});
        redactly::applyAnonymization(image, detections, redactly::AnonymizationMethod::Fill,
                                     4, 0.0F, redactly::MaskShape::Rectangle, true);

        for (int y = box.y; y < box.y + box.height; ++y)
        {
            for (int x = box.x; x < box.x + box.width; ++x)
            {
                assert(image.at<cv::Vec3b>(y, x) == cv::Vec3b(0, 0, 0));
            }
        }

        assert(image.at<cv::Vec3b>(0, 0) == cv::Vec3b(100, 100, 100));

        const cv::Vec3b feathered = image.at<cv::Vec3b>(box.y + box.height / 2, box.x + box.width + 1);
        assert(feathered != cv::Vec3b(0, 0, 0));
        assert(feathered != cv::Vec3b(100, 100, 100));
    }

    void testSoftEdgesEllipseKeepsCoreCovered()
    {
        cv::Mat image(64, 64, CV_8UC3, cv::Scalar(100, 100, 100));
        const cv::Rect box(20, 20, 24, 24);

        redactly::FaceDetections detections;
        detections.push_back({cv::Rect2f(box), 1.0F});
        redactly::applyAnonymization(image, detections, redactly::AnonymizationMethod::Fill,
                                     4, 0.0F, redactly::MaskShape::Ellipse, true);

        const int centerX = box.x + box.width / 2;
        const int centerY = box.y + box.height / 2;
        for (int dy = -box.height / 4; dy <= box.height / 4; ++dy)
        {
            for (int dx = -box.width / 4; dx <= box.width / 4; ++dx)
            {
                assert(image.at<cv::Vec3b>(centerY + dy, centerX + dx) == cv::Vec3b(0, 0, 0));
            }
        }

        assert(image.at<cv::Vec3b>(0, 0) == cv::Vec3b(100, 100, 100));
    }

    void testSoftEdgesAtImageBorderStayInBounds()
    {
        cv::Mat image(32, 32, CV_8UC3, cv::Scalar(100, 100, 100));

        redactly::FaceDetections detections;
        detections.push_back({cv::Rect2f(0.0F, 0.0F, 12.0F, 12.0F), 1.0F});
        detections.push_back({cv::Rect2f(24.0F, 24.0F, 8.0F, 8.0F), 1.0F});
        redactly::applyAnonymization(image, detections, redactly::AnonymizationMethod::Fill,
                                     4, 0.0F, redactly::MaskShape::Rectangle, true);

        assert(image.at<cv::Vec3b>(0, 0) == cv::Vec3b(0, 0, 0));
        assert(image.at<cv::Vec3b>(31, 31) == cv::Vec3b(0, 0, 0));
    }

    void testOrientationTransforms()
    {
        cv::Mat base(2, 3, CV_8UC1);
        for (int r = 0; r < base.rows; ++r)
        {
            for (int c = 0; c < base.cols; ++c)
            {
                base.at<uchar>(r, c) = static_cast<uchar>(r * 10 + c);
            }
        }

        cv::Mat identity = base.clone();
        redactly::applyOrientation(identity, 1);
        assert(cv::countNonZero(identity != base) == 0);

        cv::Mat rotated = base.clone();
        redactly::applyOrientation(rotated, 6);
        assert(rotated.rows == 3 && rotated.cols == 2);
        assert(rotated.at<uchar>(0, 0) == base.at<uchar>(base.rows - 1, 0));

        cv::Mat mirrored = base.clone();
        redactly::applyOrientation(mirrored, 2);
        assert(mirrored.rows == 2 && mirrored.cols == 3);
        assert(mirrored.at<uchar>(0, 0) == base.at<uchar>(0, base.cols - 1));
    }

    void testEncodeParams()
    {
        const auto jpeg = redactly::encodeParamsForExtension(".JPG");
        assert(std::find(jpeg.begin(), jpeg.end(), cv::IMWRITE_JPEG_QUALITY) != jpeg.end());
        assert(std::find(jpeg.begin(), jpeg.end(), 100) != jpeg.end());

        const auto png = redactly::encodeParamsForExtension("png");
        assert(std::find(png.begin(), png.end(), cv::IMWRITE_PNG_COMPRESSION) != png.end());

        assert(redactly::encodeParamsForExtension(".bmp").empty());
    }

    void testIntersectionOverUnion()
    {
        const cv::Rect2f a(0.0F, 0.0F, 10.0F, 10.0F);
        assert(std::abs(redactly::intersectionOverUnion(a, a) - 1.0F) < 1e-5F);

        const cv::Rect2f disjoint(100.0F, 100.0F, 10.0F, 10.0F);
        assert(redactly::intersectionOverUnion(a, disjoint) == 0.0F);

        const cv::Rect2f empty(0.0F, 0.0F, 0.0F, 0.0F);
        assert(redactly::intersectionOverUnion(a, empty) == 0.0F);

        const cv::Rect2f halfShifted(5.0F, 0.0F, 10.0F, 10.0F);
        assert(std::abs(redactly::intersectionOverUnion(a, halfShifted) - (50.0F / 150.0F)) < 1e-5F);
    }

    void testNonMaxSuppression()
    {
        redactly::FaceDetections detections;
        detections.push_back({cv::Rect2f(0.0F, 0.0F, 10.0F, 10.0F), 0.9F});
        detections.push_back({cv::Rect2f(1.0F, 1.0F, 10.0F, 10.0F), 0.8F});
        detections.push_back({cv::Rect2f(100.0F, 100.0F, 10.0F, 10.0F), 0.7F});

        const auto kept = redactly::nonMaxSuppression(detections, 0.4F);
        assert(kept.size() == 2);
        assert(kept[0].score == 0.9F);
        assert(kept[1].box.x == 100.0F);
    }

    void testDestinationPathSafety()
    {
        QTemporaryDir temp;
        assert(temp.isValid());
        const std::filesystem::path root =
            std::filesystem::path(temp.path().toStdString()) / "out";
        assert(std::filesystem::create_directories(root));

        assert(redactly::destinationIsSafe(root / "a.jpg", root));
        assert(redactly::destinationIsSafe(root / "sub" / "b.png", root));
        assert(redactly::destinationIsSafe(root, root));

        assert(!redactly::destinationIsSafe(root / ".." / "escape.jpg", root));

        assert(redactly::isWithinRoot(root / "x.jpg", root));
        assert(!redactly::isWithinRoot(root.parent_path() / "x.jpg", root));
    }

#ifndef _WIN32
    void testDestinationRejectsSymlinkEscape()
    {
        QTemporaryDir temp;
        assert(temp.isValid());
        const std::filesystem::path base = std::filesystem::path(temp.path().toStdString());
        const std::filesystem::path root = base / "out";
        const std::filesystem::path outside = base / "outside";
        assert(std::filesystem::create_directories(root));
        assert(std::filesystem::create_directories(outside));

        std::error_code ec;
        const std::filesystem::path link = root / "evil";
        std::filesystem::create_directory_symlink(outside, link, ec);
        assert(!ec);

        assert(!redactly::destinationIsSafe(link / "leak.jpg", root));
    }
#endif

#ifdef REDACTLY_HAVE_EXIV2
    void testMetadataCopyAndOrientationNormalize()
    {
        QTemporaryDir temp;
        assert(temp.isValid());
        const std::filesystem::path src = std::filesystem::path(temp.path().toStdString()) / "src.jpg";
        const std::filesystem::path dst = std::filesystem::path(temp.path().toStdString()) / "dst.jpg";

        cv::Mat img(16, 16, CV_8UC3, cv::Scalar(120, 120, 120));
        assert(cv::imwrite(src.string(), img));
        assert(cv::imwrite(dst.string(), img));

        {
            auto image = Exiv2::ImageFactory::open(src.string());
            image->readMetadata();
            image->exifData()["Exif.Image.Artist"] = "TestPhotographer";
            image->exifData()["Exif.Image.Orientation"] = static_cast<uint16_t>(6);
            image->writeMetadata();
        }

        assert(redactly::readExifOrientation(src) == 6);
        assert(redactly::copyMetadata(src, dst, true));

        {
            auto image = Exiv2::ImageFactory::open(dst.string());
            image->readMetadata();
            const Exiv2::ExifData &exif = image->exifData();

            const auto artist = exif.findKey(Exiv2::ExifKey("Exif.Image.Artist"));
            assert(artist != exif.end());
            assert(artist->toString() == "TestPhotographer");

            const auto orientation = exif.findKey(Exiv2::ExifKey("Exif.Image.Orientation"));
            assert(orientation != exif.end());
#if EXIV2_TEST_VERSION(0, 28, 0)
            assert(orientation->toInt64() == 1);
#else
            assert(orientation->toLong() == 1);
#endif
        }
    }

    long exifThumbnailBytes(Exiv2::ExifData &exif)
    {
        Exiv2::ExifThumb thumb(exif);
#if EXIV2_TEST_VERSION(0, 28, 0)
        return static_cast<long>(thumb.copy().size());
#else
        return thumb.copy().size_;
#endif
    }

    void testMetadataCopyStripsEmbeddedThumbnail()
    {
        QTemporaryDir temp;
        assert(temp.isValid());
        const std::filesystem::path base = std::filesystem::path(temp.path().toStdString());
        const std::filesystem::path src = base / "src.jpg";
        const std::filesystem::path dst = base / "dst.jpg";
        const std::filesystem::path thumbFile = base / "thumb.jpg";

        cv::Mat img(16, 16, CV_8UC3, cv::Scalar(120, 120, 120));
        assert(cv::imwrite(src.string(), img));
        assert(cv::imwrite(dst.string(), img));
        assert(cv::imwrite(thumbFile.string(), img));

        {
            auto image = Exiv2::ImageFactory::open(src.string());
            image->readMetadata();
            image->exifData()["Exif.Image.Artist"] = "TestPhotographer";
            Exiv2::ExifThumb thumb(image->exifData());
            thumb.setJpegThumbnail(thumbFile.string());
            image->writeMetadata();
        }

        {
            auto image = Exiv2::ImageFactory::open(src.string());
            image->readMetadata();
            assert(exifThumbnailBytes(image->exifData()) > 0);
        }

        assert(redactly::copyMetadata(src, dst, true));

        {
            auto image = Exiv2::ImageFactory::open(dst.string());
            image->readMetadata();
            assert(exifThumbnailBytes(image->exifData()) == 0);

            const Exiv2::ExifData &exif = image->exifData();
            const auto artist = exif.findKey(Exiv2::ExifKey("Exif.Image.Artist"));
            assert(artist != exif.end());
            assert(artist->toString() == "TestPhotographer");
        }
    }
#endif
}

namespace
{
    void testOnnxPatchRejectsInvalidBytes()
    {
        assert(!redactly::makeOnnxSpatialDimsFixed({}, 320).has_value());
        const std::vector<std::uint8_t> garbage = {0xFF, 0xFF, 0xFF, 0x01, 0x02, 0x9C};
        assert(!redactly::makeOnnxSpatialDimsFixed(garbage, 320).has_value());
        assert(!redactly::makeOnnxSpatialDimsFixed(garbage, 0).has_value());
        assert(!redactly::makeOnnxSpatialDimsFixed(garbage, -320).has_value());
    }

    void testFixedScrfdModelRunsAtRequestedSize()
    {
        const auto modelPath = std::filesystem::path(__FILE__).parent_path().parent_path()
                               / "models" / "2.5g_bnkps.onnx";
        if (!std::filesystem::exists(modelPath))
        {
            std::puts("skipping fixed-model patch test: models/2.5g_bnkps.onnx not present");
            return;
        }

        redactly::ScrfdFaceDetector nativeSize(modelPath.string(), 640);
        assert(nativeSize.inputSize() == 640);

        redactly::ScrfdFaceDetector patchedSize(modelPath.string(), 320);
        assert(patchedSize.inputSize() == 320);

        const cv::Mat blank(180, 320, CV_8UC3, cv::Scalar(30, 30, 30));
        assert(patchedSize.detect(blank, 0.5F, 0.4F).empty());
    }

    void testDynamicScrfdModelRunsAtRequestedSize()
    {
        const auto modelPath = std::filesystem::path(__FILE__).parent_path().parent_path()
                               / "models" / "10g_bnkps.onnx";
        if (!std::filesystem::exists(modelPath))
        {
            std::puts("skipping dynamic-model patch test: models/10g_bnkps.onnx not present");
            return;
        }

        redactly::ScrfdFaceDetector patchedSize(modelPath.string(), 320);
        assert(patchedSize.inputSize() == 320);

        const cv::Mat blank(180, 320, CV_8UC3, cv::Scalar(30, 30, 30));
        assert(patchedSize.detect(blank, 0.5F, 0.4F).empty());
    }
}

int main()
{
    testSupportedImageExtensions();
    testScanImagesRecursesAndDeduplicates();
    testApplyMosaicTouchesOnlyDetectedRegion();
    testSoftEdgesKeepDetectedRegionFullyCovered();
    testSoftEdgesEllipseKeepsCoreCovered();
    testSoftEdgesAtImageBorderStayInBounds();
    testOrientationTransforms();
    testEncodeParams();
    testIntersectionOverUnion();
    testNonMaxSuppression();
    testOnnxPatchRejectsInvalidBytes();
    testFixedScrfdModelRunsAtRequestedSize();
    testDynamicScrfdModelRunsAtRequestedSize();
    testDestinationPathSafety();
#ifndef _WIN32
    testDestinationRejectsSymlinkEscape();
#endif
#ifdef REDACTLY_HAVE_EXIV2
    testMetadataCopyAndOrientationNormalize();
    testMetadataCopyStripsEmbeddedThumbnail();
#endif
    return 0;
}
