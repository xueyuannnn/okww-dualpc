#pragma once
#include <opencv2/core/version.hpp>
// OpenCV 5 split computational geometry out of imgproc. The Win7 inference
// backend needs that release for the shared dynamic OCR models.
#if CV_VERSION_MAJOR >= 5
#include <opencv2/geometry.hpp>
#endif
