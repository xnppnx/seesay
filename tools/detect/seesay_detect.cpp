// Copyright 2025 Tencent
// SPDX-License-Identifier: BSD-3-Clause
//
// SeeSay adaptation: standalone C++ object-detection subprocess based on the
// official ncnn NanoDet example (examples/nanodet.cpp). Reads an image path,
// prints one JSON line of detected objects to stdout, matching the shape of
// seesay_describe.py's "objects" field so the pipeline needs no other changes:
//   {"ok":true,"objects":[{"label":"cup","confidence":0.92},...]}
// CPU-only ncnn (no Vulkan), image loaded via bundled stb_image (opencv-mobile
// has no imgcodecs). Model dir configurable via SEESAY_DETECT_MODELS.

#include "layer.h"
#include "net.h"

#include <opencv2/opencv.hpp>

#include <algorithm>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// COCO 80 classes, same order as the official ncnn nanodet example.
static const char* class_names[] = {
    "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train", "truck", "boat", "traffic light",
    "fire hydrant", "stop sign", "parking meter", "bench", "bird", "cat", "dog", "horse", "sheep", "cow",
    "elephant", "bear", "zebra", "giraffe", "backpack", "umbrella", "handbag", "tie", "suitcase", "frisbee",
    "skis", "snowboard", "sports ball", "kite", "baseball bat", "baseball glove", "skateboard", "surfboard",
    "tennis racket", "bottle", "wine glass", "cup", "fork", "knife", "spoon", "bowl", "banana", "apple",
    "sandwich", "orange", "broccoli", "carrot", "hot dog", "pizza", "donut", "cake", "chair", "couch",
    "potted plant", "bed", "dining table", "toilet", "tv", "laptop", "mouse", "remote", "keyboard", "cell phone",
    "microwave", "oven", "toaster", "sink", "refrigerator", "book", "clock", "vase", "scissors", "teddy bear",
    "hair drier", "toothbrush"
};
static const int class_count = (int)(sizeof(class_names) / sizeof(class_names[0]));

static std::string model_path(const char* file) {
    const char* dir = getenv("SEESAY_DETECT_MODELS");
    if (!dir || !*dir) dir = "/usr/share/seesay/models";
    return std::string(dir) + "/" + file;
}

// Load image via stb_image into a BGR cv::Mat (stb returns RGB -> swap R/B).
static cv::Mat imread_stb(const char* path) {
    int w = 0, h = 0, channels = 0;
    stbi_uc* data = stbi_load(path, &w, &h, &channels, 3);
    if (!data) return cv::Mat();
    cv::Mat bgr(h, w, CV_8UC3);
    for (int i = 0; i < w * h; ++i) {
        bgr.data[i * 3 + 0] = data[i * 3 + 2];
        bgr.data[i * 3 + 1] = data[i * 3 + 1];
        bgr.data[i * 3 + 2] = data[i * 3 + 0];
    }
    stbi_image_free(data);
    return bgr;
}

struct Object {
    cv::Rect_<float> rect;
    int label;
    float prob;
};

static inline float intersection_area(const Object& a, const Object& b) {
    cv::Rect_<float> inter = a.rect & b.rect;
    return inter.area();
}

static void qsort_descent_inplace(std::vector<Object>& objects, int left, int right) {
    int i = left;
    int j = right;
    float p = objects[(left + right) / 2].prob;
    while (i <= j) {
        while (objects[i].prob > p) i++;
        while (objects[j].prob < p) j--;
        if (i <= j) {
            std::swap(objects[i], objects[j]);
            i++;
            j--;
        }
    }
    if (left < j) qsort_descent_inplace(objects, left, j);
    if (i < right) qsort_descent_inplace(objects, i, right);
}

static void nms_sorted_bboxes(const std::vector<Object>& objects, std::vector<int>& picked,
                              float nms_threshold) {
    picked.clear();
    const int n = (int)objects.size();
    std::vector<float> areas(n);
    for (int i = 0; i < n; i++) areas[i] = objects[i].rect.area();
    for (int i = 0; i < n; i++) {
        const Object& a = objects[i];
        int keep = 1;
        for (size_t j = 0; j < picked.size(); j++) {
            const Object& b = objects[picked[j]];
            if (a.label != b.label) continue;
            float inter_area = intersection_area(a, b);
            float union_area = areas[i] + areas[picked[j]] - inter_area;
            if (union_area > 0.f && inter_area / union_area > nms_threshold) keep = 0;
        }
        if (keep) picked.push_back(i);
    }
}

static void generate_proposals(const ncnn::Mat& cls_pred, const ncnn::Mat& dis_pred, int stride,
                               const ncnn::Mat& in_pad, float prob_threshold,
                               std::vector<Object>& objects) {
    const int num_grid = cls_pred.h;
    int num_grid_x, num_grid_y;
    if (in_pad.w > in_pad.h) {
        num_grid_x = in_pad.w / stride;
        num_grid_y = num_grid / num_grid_x;
    } else {
        num_grid_y = in_pad.h / stride;
        num_grid_x = num_grid / num_grid_y;
    }
    const int num_class = cls_pred.w;
    const int reg_max_1 = dis_pred.w / 4;

    for (int i = 0; i < num_grid_y; i++) {
        for (int j = 0; j < num_grid_x; j++) {
            const int idx = i * num_grid_x + j;
            const float* scores = cls_pred.row(idx);
            int label = -1;
            float score = -FLT_MAX;
            for (int k = 0; k < num_class; k++) {
                if (scores[k] > score) { score = scores[k]; label = k; }
            }
            if (score < prob_threshold || label < 0 || label >= class_count) continue;

            ncnn::Mat bbox_pred(reg_max_1, 4, (void*)dis_pred.row(idx));
            {
                ncnn::Layer* softmax = ncnn::create_layer("Softmax");
                ncnn::ParamDict pd;
                pd.set(0, 1);
                pd.set(1, 1);
                softmax->load_param(pd);
                ncnn::Option opt;
                opt.num_threads = 1;
                opt.use_packing_layout = false;
                softmax->create_pipeline(opt);
                softmax->forward_inplace(bbox_pred, opt);
                softmax->destroy_pipeline(opt);
                delete softmax;
            }
            float pred_ltrb[4];
            for (int k = 0; k < 4; k++) {
                float dis = 0.f;
                const float* dis_after_sm = bbox_pred.row(k);
                for (int l = 0; l < reg_max_1; l++) dis += l * dis_after_sm[l];
                pred_ltrb[k] = dis * stride;
            }
            float pb_cx = (j + 0.5f) * stride;
            float pb_cy = (i + 0.5f) * stride;
            Object obj;
            obj.rect.x = pb_cx - pred_ltrb[0];
            obj.rect.y = pb_cy - pred_ltrb[1];
            obj.rect.width = pred_ltrb[2] + pred_ltrb[0];
            obj.rect.height = pred_ltrb[3] + pred_ltrb[1];
            obj.label = label;
            obj.prob = score;
            objects.push_back(obj);
        }
    }
}

static int detect_nanodet(const cv::Mat& bgr, std::vector<Object>& objects) {
    ncnn::Net nanodet;
    nanodet.opt.use_vulkan_compute = false;  // SeeSay: CPU-only (A53)

    if (nanodet.load_param(model_path("nanodet_m.param").c_str())) return -1;
    if (nanodet.load_model(model_path("nanodet_m.bin").c_str())) return -1;

    int width = bgr.cols;
    int height = bgr.rows;
    const int target_size = 320;
    const float prob_threshold = 0.4f;   // candidate threshold (official)
    const float nms_threshold = 0.5f;

    int w = width, h = height;
    float scale = 1.f;
    if (w > h) { scale = (float)target_size / w; w = target_size; h = (int)(h * scale); }
    else       { scale = (float)target_size / h; h = target_size; w = (int)(w * scale); }

    ncnn::Mat in = ncnn::Mat::from_pixels_resize(bgr.data, ncnn::Mat::PIXEL_BGR, width, height, w, h);
    int wpad = (w + 31) / 32 * 32 - w;
    int hpad = (h + 31) / 32 * 32 - h;
    ncnn::Mat in_pad;
    ncnn::copy_make_border(in, in_pad, hpad / 2, hpad - hpad / 2, wpad / 2, wpad - wpad / 2,
                           ncnn::BORDER_CONSTANT, 0.f);
    const float mean_vals[3] = {103.53f, 116.28f, 123.675f};
    const float norm_vals[3] = {0.017429f, 0.017507f, 0.017125f};
    in_pad.substract_mean_normalize(mean_vals, norm_vals);

    ncnn::Extractor ex = nanodet.create_extractor();
    ex.input("input.1", in_pad);

    std::vector<Object> proposals;
    const int strides[3] = {8, 16, 32};
    // IMPORTANT: these are ncnn blob NAMES (unnamed layers use their layer id
    // as a string), NOT integer blob indices. The official example uses
    // ex.extract("792", ...) — passing the int 792 would index a different blob.
    const char* cls_names[3] = {"792", "814", "836"};
    const char* dis_names[3] = {"795", "817", "839"};
    int layers_ok = 0;
    for (int s = 0; s < 3; s++) {
        ncnn::Mat cls_pred, dis_pred;
        ex.extract(cls_names[s], cls_pred);
        ex.extract(dis_names[s], dis_pred);
        if (cls_pred.empty() || dis_pred.empty()) continue;
        layers_ok++;
        std::vector<Object> objs;
        generate_proposals(cls_pred, dis_pred, strides[s], in_pad, prob_threshold, objs);
        proposals.insert(proposals.end(), objs.begin(), objs.end());
    }
    // If no output layer could be extracted, the model/param is incompatible.
    // Report failure so the pipeline falls back to Mirage instead of silently
    // claiming "success with no objects" (which would speak "I am not sure").
    if (layers_ok == 0) return -2;

    if (!proposals.empty()) qsort_descent_inplace(proposals, 0, (int)proposals.size() - 1);
    std::vector<int> picked;
    nms_sorted_bboxes(proposals, picked, nms_threshold);

    objects.clear();
    for (size_t i = 0; i < picked.size(); i++) {
        Object obj = proposals[picked[i]];
        float x0 = (obj.rect.x - wpad / 2) / scale;
        float y0 = (obj.rect.y - hpad / 2) / scale;
        float x1 = (obj.rect.x + obj.rect.width - wpad / 2) / scale;
        float y1 = (obj.rect.y + obj.rect.height - hpad / 2) / scale;
        obj.rect.x = std::max(std::min(x0, (float)(width - 1)), 0.f);
        obj.rect.y = std::max(std::min(y0, (float)(height - 1)), 0.f);
        obj.rect.width = std::max(std::min(x1, (float)(width - 1)), 0.f) - obj.rect.x;
        obj.rect.height = std::max(std::min(y1, (float)(height - 1)), 0.f) - obj.rect.y;
        objects.push_back(obj);  // already sorted by prob descending
    }
    return 0;
}

int main(int argc, char** argv) {
    if (argc != 2) {
        printf("{\"ok\":false,\"error\":\"usage: seesay_detect IMAGE\"}\n");
        return 1;
    }
    cv::Mat m = imread_stb(argv[1]);
    if (m.empty()) {
        printf("{\"ok\":false,\"error\":\"image not found or decode failed\"}\n");
        return 1;
    }

    // Output confidence filter: matches Mirage's SEESAY_MIN_CONFIDENCE (0.5).
    const char* conf_env = getenv("SEESAY_DETECT_MIN_CONFIDENCE");
    float min_conf = conf_env ? (float)atof(conf_env) : 0.5f;

    std::vector<Object> objects;
    if (detect_nanodet(m, objects) != 0) {
        printf("{\"ok\":false,\"error\":\"nanodet model load or inference failed\"}\n");
        return 1;
    }

    // objects are already sorted by prob descending (highest first).
    std::string out = "{\"ok\":true,\"objects\":[";
    bool first = true;
    int kept = 0;
    for (size_t i = 0; i < objects.size() && kept < 10; i++) {
        if (objects[i].prob < min_conf) continue;
        char buf[256];
        snprintf(buf, sizeof(buf), "%s{\"label\":\"%s\",\"confidence\":%.4f}",
                 first ? "" : ",", class_names[objects[i].label], objects[i].prob);
        out += buf;
        first = false;
        kept++;
    }
    out += "]}";
    printf("%s\n", out.c_str());
    return 0;
}
