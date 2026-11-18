// Copyright 2025 Tencent
// SPDX-License-Identifier: BSD-3-Clause
//
// SeeSay adaptation: standalone C++ OCR subprocess based on the official
// ncnn PP-OCRv5 example. Reads an image path, prints a JSON line of recognized
// text to stdout (same shape as seesay_readtext.py). CPU-only ncnn, no Vulkan.
// Model dir is configurable via SEESAY_OCR_MODELS (defaults to /usr/share/seesay/models).

// pip install paddlepaddle==3.0.0
// pip install paddleocr==3.0.0
// paddlex --install paddle2onnx
// paddleocr ocr -i test.png
// paddlex --paddle2onnx --paddle_model_dir ~/.paddlex/official_models/PP-OCRv5_mobile_det --onnx_model_dir PP-OCRv5_mobile_det
// paddlex --paddle2onnx --paddle_model_dir ~/.paddlex/official_models/PP-OCRv5_mobile_rec --onnx_model_dir PP-OCRv5_mobile_rec
// pnnx PP-OCRv5_mobile_det.onnx inputshape=[1,3,320,320] inputshape2=[1,3,256,256]
// pnnx PP-OCRv5_mobile_rec.onnx inputshape=[1,3,48,160] inputshape2=[1,3,48,256]
// pnnx PP-OCRv5_server_det.onnx inputshape=[1,3,320,320] inputshape2=[1,3,256,256] fp16=0
// pnnx PP-OCRv5_server_rec.onnx inputshape=[1,3,48,160] inputshape2=[1,3,48,256] fp16=0

#include "layer.h"
#include "net.h"

#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>
// opencv-mobile 5.1 dropped imgcodecs/highgui; use ncnn's bundled stb_image
// to load images instead of cv::imread. We only need core+imgproc for the
// detection contours / affine warp.

#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>

#include "ppocrv5_dict.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// Build a model file path from SEESAY_OCR_MODELS (or the packaged default).
static std::string model_path(const char* file) {
    const char* dir = getenv("SEESAY_OCR_MODELS");
    if (!dir || !*dir) dir = "/usr/share/seesay/models";
    return std::string(dir) + "/" + file;
}

// Load an image into a BGR cv::Mat using stb_image (opencv-mobile 5.x has no
// imgcodecs). Returns an empty Mat on failure, like cv::imread.
static cv::Mat imread_stb(const char* path) {
    int w = 0, h = 0, channels = 0;
    stbi_uc* data = stbi_load(path, &w, &h, &channels, 3);  // force 3 channels
    if (!data) return cv::Mat();
    cv::Mat bgr(h, w, CV_8UC3);
    // stb gives RGB; OpenCV detection code expects BGR -> swap R and B.
    for (int i = 0; i < w * h; ++i) {
        bgr.data[i * 3 + 0] = data[i * 3 + 2];
        bgr.data[i * 3 + 1] = data[i * 3 + 1];
        bgr.data[i * 3 + 2] = data[i * 3 + 0];
    }
    stbi_image_free(data);
    return bgr;
}

struct Character
{
    int id;
    float prob;
};

struct Object
{
    cv::RotatedRect rrect;
    int orientation;
    float prob;
    std::vector<Character> text;
};

static double contour_score(const cv::Mat& binary, const std::vector<cv::Point>& contour)
{
    cv::Rect rect = cv::boundingRect(contour);
    if (rect.x < 0)
        rect.x = 0;
    if (rect.y < 0)
        rect.y = 0;
    if (rect.x + rect.width > binary.cols)
        rect.width = binary.cols - rect.x;
    if (rect.y + rect.height > binary.rows)
        rect.height = binary.rows - rect.y;

    cv::Mat binROI = binary(rect);

    cv::Mat mask = cv::Mat::zeros(rect.height, rect.width, CV_8U);
    std::vector<cv::Point> roiContour;
    for (size_t i = 0; i < contour.size(); i++)
    {
        cv::Point pt = cv::Point(contour[i].x - rect.x, contour[i].y - rect.y);
        roiContour.push_back(pt);
    }

    std::vector<std::vector<cv::Point> > roiContours = {roiContour};
    cv::fillPoly(mask, roiContours, cv::Scalar(255));

    double score = cv::mean(binROI, mask).val[0];
    return score / 255.f;
}

static cv::Mat get_rotate_crop_image(const cv::Mat& bgr, const Object& object)
{
    const int orientation = object.orientation;
    const float rw = object.rrect.size.width;
    const float rh = object.rrect.size.height;

    const int target_height = 48;
    const float target_width = rh * target_height / rw;

    // warpperspective shall be used to rotate the image
    // but actually they are all rectangles, so warpaffine is almost enough  :P

    cv::Mat dst;

    cv::Point2f corners[4];
    object.rrect.points(corners);

    if (orientation == 0)
    {
        // horizontal text
        // corner points order
        //  0--------1
        //  |        |rw  -> as angle=90
        //  3--------2
        //      rh

        std::vector<cv::Point2f> src_pts(3);
        src_pts[0] = corners[0];
        src_pts[1] = corners[1];
        src_pts[2] = corners[3];

        std::vector<cv::Point2f> dst_pts(3);
        dst_pts[0] = cv::Point2f(0, 0);
        dst_pts[1] = cv::Point2f(target_width, 0);
        dst_pts[2] = cv::Point2f(0, target_height);

        cv::Mat tm = cv::getAffineTransform(src_pts, dst_pts);

        cv::warpAffine(bgr, dst, tm, cv::Size(target_width, target_height), cv::INTER_LINEAR, cv::BORDER_REPLICATE);
    }
    else
    {
        // vertial text
        // corner points order
        //  1----2
        //  |    |
        //  |    |
        //  |    |rh  -> as angle=0
        //  |    |
        //  |    |
        //  0----3
        //    rw

        std::vector<cv::Point2f> src_pts(3);
        src_pts[0] = corners[2];
        src_pts[1] = corners[3];
        src_pts[2] = corners[1];

        std::vector<cv::Point2f> dst_pts(3);
        dst_pts[0] = cv::Point2f(0, 0);
        dst_pts[1] = cv::Point2f(target_width, 0);
        dst_pts[2] = cv::Point2f(0, target_height);

        cv::Mat tm = cv::getAffineTransform(src_pts, dst_pts);

        cv::warpAffine(bgr, dst, tm, cv::Size(target_width, target_height), cv::INTER_LINEAR, cv::BORDER_REPLICATE);
    }

    return dst;
}

class PPOCRv5
{
public:
    void init();

    void detect(const cv::Mat& bgr, std::vector<Object>& objects);

    void recognize(const cv::Mat& bgr, Object& object);

protected:
    ncnn::Net ppocrv5_det;
    ncnn::Net ppocrv5_rec;
};

void PPOCRv5::init()
{
    // the ncnn model https://github.com/nihui/ncnn-assets/tree/master/models
    // https://github.com/nihui/ncnn-android-ppocrv5/tree/master/app/src/main/assets

    ppocrv5_det.opt.use_vulkan_compute = false;  // SeeSay: CPU-only ncnn build
    // ppocrv5_det.opt.use_bf16_storage = true;

    // fp16 must be disabled for server model
    // ppocrv5_det.opt.use_fp16_packed = false;
    // ppocrv5_det.opt.use_fp16_storage = false;

    ppocrv5_det.load_param(model_path("PP_OCRv5_mobile_det.ncnn.param").c_str());
    ppocrv5_det.load_model(model_path("PP_OCRv5_mobile_det.ncnn.bin").c_str());
    // ppocrv5_det.load_param("PP_OCRv5_server_det.ncnn.param");
    // ppocrv5_det.load_model("PP_OCRv5_server_det.ncnn.bin");

    ppocrv5_rec.opt.use_vulkan_compute = false;  // SeeSay: CPU-only ncnn build
    // ppocrv5_rec.opt.use_bf16_storage = true;

    // fp16 must be disabled for server model
    // ppocrv5_rec.opt.use_fp16_packed = false;
    // ppocrv5_rec.opt.use_fp16_storage = false;

    ppocrv5_rec.load_param(model_path("PP_OCRv5_mobile_rec.ncnn.param").c_str());
    ppocrv5_rec.load_model(model_path("PP_OCRv5_mobile_rec.ncnn.bin").c_str());
    // ppocrv5_rec.load_param("PP_OCRv5_server_rec.ncnn.param");
    // ppocrv5_rec.load_model("PP_OCRv5_server_rec.ncnn.bin");
}

void PPOCRv5::detect(const cv::Mat& bgr, std::vector<Object>& objects)
{
    // SeeSay: cap input long edge to 512 (was 960) to bound RAM on the 132MB device.
    const int target_size = 512;

    int img_w = bgr.cols;
    int img_h = bgr.rows;

    const int target_stride = 32;

    // letterbox pad to multiple of target_stride
    int w = img_w;
    int h = img_h;
    float scale = 1.f;
    if (std::max(w, h) > target_size)
    {
        if (w > h)
        {
            scale = (float)target_size / w;
            w = target_size;
            h = h * scale;
        }
        else
        {
            scale = (float)target_size / h;
            h = target_size;
            w = w * scale;
        }
    }

    ncnn::Mat in = ncnn::Mat::from_pixels_resize(bgr.data, ncnn::Mat::PIXEL_BGR, img_w, img_h, w, h);

    int wpad = (w + target_stride - 1) / target_stride * target_stride - w;
    int hpad = (h + target_stride - 1) / target_stride * target_stride - h;
    ncnn::Mat in_pad;
    ncnn::copy_make_border(in, in_pad, hpad / 2, hpad - hpad / 2, wpad / 2, wpad - wpad / 2, ncnn::BORDER_CONSTANT, 114.f);

    const float mean_vals[3] = {0.485f * 255.f, 0.456f * 255.f, 0.406f * 255.f};
    const float norm_vals[3] = {1 / 0.229f / 255.f, 1 / 0.224f / 255.f, 1 / 0.225f / 255.f};
    in_pad.substract_mean_normalize(mean_vals, norm_vals);

    ncnn::Extractor ex = ppocrv5_det.create_extractor();

    ex.input("in0", in_pad);

    ncnn::Mat out;
    ex.extract("out0", out);

    const float denorm_vals[1] = {255.f};
    out.substract_mean_normalize(0, denorm_vals);

    cv::Mat pred(out.h, out.w, CV_8UC1);
    out.to_pixels(pred.data, ncnn::Mat::PIXEL_GRAY);

    // threshold binary
    cv::Mat bitmap;
    const float threshold = 0.3f;
    cv::threshold(pred, bitmap, threshold * 255, 255, cv::THRESH_BINARY);

    // boxes from bitmap
    {
        // should use dbnet post process, but I think unclip process is difficult to write
        // so simply implement expansion. This may lose detection accuracy
        // original implementation can be referenced
        // https://github.com/MhLiao/DB/blob/master/structure/representers/seg_detector_representer.py

        const float box_thresh = 0.6f;
        const float enlarge_ratio = 1.95f;

        const float min_size = 3 * scale;
        const int max_candidates = 1000;

        std::vector<std::vector<cv::Point> > contours;
        std::vector<cv::Vec4i> hierarchy;

        cv::findContours(bitmap, contours, hierarchy, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);

        contours.resize(std::min(contours.size(), (size_t)max_candidates));

        for (size_t i = 0; i < contours.size(); i++)
        {
            const std::vector<cv::Point>& contour = contours[i];
            if (contour.size() <= 2)
                continue;

            double score = contour_score(pred, contour);
            if (score < box_thresh)
                continue;

            cv::RotatedRect rrect = cv::minAreaRect(contour);

            float rrect_maxwh = std::max(rrect.size.width, rrect.size.height);
            if (rrect_maxwh < min_size)
                continue;

            int orientation = 0;
            if (rrect.angle >= -30 && rrect.angle <= 30 && rrect.size.height > rrect.size.width * 2.7)
            {
                // vertical text
                orientation = 1;
            }
            if ((rrect.angle <= -60 || rrect.angle >= 60) && rrect.size.width > rrect.size.height * 2.7)
            {
                // vertical text
                orientation = 1;
            }

            if (rrect.angle < -30)
            {
                // make orientation from -90 ~ -30 to 90 ~ 150
                rrect.angle += 180;
            }
            if (orientation == 0 && rrect.angle < 30)
            {
                // make it horizontal
                rrect.angle += 90;
                std::swap(rrect.size.width, rrect.size.height);
            }
            if (orientation == 1 && rrect.angle >= 60)
            {
                // make it vertical
                rrect.angle -= 90;
                std::swap(rrect.size.width, rrect.size.height);
            }

            // enlarge
            rrect.size.height += rrect.size.width * (enlarge_ratio - 1);
            rrect.size.width *= enlarge_ratio;

            // adjust offset to original unpadded
            rrect.center.x = (rrect.center.x - (wpad / 2)) / scale;
            rrect.center.y = (rrect.center.y - (hpad / 2)) / scale;
            rrect.size.width = (rrect.size.width) / scale;
            rrect.size.height = (rrect.size.height) / scale;

            Object obj;
            obj.rrect = rrect;
            obj.orientation = orientation;
            obj.prob = score;
            objects.push_back(obj);
        }
    }
}

void PPOCRv5::recognize(const cv::Mat& bgr, Object& object)
{
    cv::Mat roi = get_rotate_crop_image(bgr, object);

    ncnn::Mat in = ncnn::Mat::from_pixels(roi.data, ncnn::Mat::PIXEL_BGR, roi.cols, roi.rows);

    // ~/.paddlex/official_models/PP-OCRv5_mobile_rec/inference.yml
    const float mean_vals[3] = {127.5, 127.5, 127.5};
    const float norm_vals[3] = {1.0 / 127.5, 1.0 / 127.5, 1.0 / 127.5};
    in.substract_mean_normalize(mean_vals, norm_vals);

    ncnn::Extractor ex = ppocrv5_rec.create_extractor();

    ex.input("in0", in);

    ncnn::Mat out;
    ex.extract("out0", out);

    // 18385 x len
    int last_token = 0;

    for (int i = 0; i < out.h; i++)
    {
        const float* p = out.row(i);

        int index = 0;
        float max_score = -9999.f;
        for (int j = 0; j < out.w; j++)
        {
            float score = *p++;
            if (score > max_score)
            {
                max_score = score;
                index = j;
            }
        }

        if (last_token == index) // CTC rule, if index is same as last one, they will be merged into one token
            continue;

        last_token = index;

        if (index <= 0)
            continue;

        Character ch;
        ch.id = index - 1;
        ch.prob = max_score;

        object.text.push_back(ch);
    }
}

static int detect_ppocrv5(const cv::Mat& bgr, std::vector<Object>& objects)
{
    PPOCRv5 ppocrv5;

    ppocrv5.init();

    ppocrv5.detect(bgr, objects);

    for (size_t i = 0; i < objects.size(); i++)
    {
        ppocrv5.recognize(bgr, objects[i]);
    }

    return 0;
}

// SeeSay: JSON string escaping (recognized text may contain quotes/backslashes).
static std::string json_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    return out;
}

// Build one text line from a recognized object (same logic as the official
// example's draw_objects, minus the on-screen drawing).
static std::string object_text(const Object& obj) {
    // Match Python RapidOCR's MIN_CONFIDENCE filter: drop low-confidence
    // characters so we never speak a misread (safety-critical for blind users).
    const char* conf_env = getenv("SEESAY_OCR_MIN_CONFIDENCE");
    const float min_conf = conf_env ? (float)atof(conf_env) : 0.5f;

    std::string text;
    for (size_t j = 0; j < obj.text.size(); ++j) {
        const Character& ch = obj.text[j];
        if (ch.prob < min_conf) continue;  // low-confidence char: skip
        if (ch.id >= character_dict_size) {
            if (!text.empty() && text.back() != ' ') text += " ";
            continue;
        }
        text += character_dict[ch.id];
    }
    return text;
}

// Print the recognized lines as a JSON object on stdout, matching the shape
// of seesay_readtext.py: {"ok": true, "lines": ["...", "..."]}
static int print_result_json(const cv::Mat& bgr, const std::vector<Object>& objects) {
    // Read top-to-bottom so spoken order matches the visual layout.
    std::vector<const Object*> sorted;
    sorted.reserve(objects.size());
    for (size_t i = 0; i < objects.size(); ++i) sorted.push_back(&objects[i]);
    std::stable_sort(sorted.begin(), sorted.end(), [](const Object* a, const Object* b) {
        return a->rrect.center.y < b->rrect.center.y;
    });

    std::string lines_json;
    for (size_t i = 0; i < sorted.size(); ++i) {
        std::string line = object_text(*sorted[i]);
        // A vertical text block may contain '\n' internally; split into lines.
        size_t start = 0;
        while (start <= line.size()) {
            size_t nl = line.find('\n', start);
            std::string piece = line.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
            // trim leading/trailing spaces
            size_t a = piece.find_first_not_of(" \t");
            size_t b = piece.find_last_not_of(" \t");
            if (a != std::string::npos) {
                std::string clean = piece.substr(a, b - a + 1);
                if (!clean.empty()) {
                    if (!lines_json.empty()) lines_json += ",";
                    lines_json += "\"" + json_escape(clean) + "\"";
                }
            }
            if (nl == std::string::npos) break;
            start = nl + 1;
        }
    }

    printf("{\"ok\":true,\"lines\":[%s]}\n", lines_json.c_str());
    return 0;
}

int main(int argc, char** argv) {
    if (argc != 2) {
        printf("{\"ok\":false,\"error\":\"usage: seesay_ocr IMAGE\"}\n");
        return 1;
    }

    const char* imagepath = argv[1];
    cv::Mat m = imread_stb(imagepath);
    if (m.empty()) {
        printf("{\"ok\":false,\"error\":\"image not found or decode failed\"}\n");
        return 1;
    }

    std::vector<Object> objects;
    detect_ppocrv5(m, objects);
    return print_result_json(m, objects);
}
