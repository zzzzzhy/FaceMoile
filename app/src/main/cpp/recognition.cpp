//
// Created by Taste on 2020/5/31.
//

#include "recognition.h"

namespace Face {
    Recognition::Recognition(const std::string &model_path) {
        std::string param_files = model_path + "/mobilefacenet.param";
        std::string bin_files = model_path + "/mobilefacenet.bin";
#if NCNN_VULKAN
        ncnn::create_gpu_instance();
        Recognet.use_vulkan_compute = true;
#endif // NCNN_VULKAN
        Recognet.load_param(param_files.c_str());
        Recognet.load_model(bin_files.c_str());
    }

    Recognition::~Recognition() {
        Recognet.clear();
#if NCNN_VULKAN
        ncnn::destroy_gpu_instance();
#endif // NCNN_VULKAN
    }

    void Recognition::setThreadNum(int threadNum) {
        threadnum = threadNum;
    }

    void Recognition::RecogNet(ncnn::Mat &img_) {
        feature_out.resize(128);
        ncnn::Extractor ex = Recognet.create_extractor();
        ex.set_num_threads(threadnum);
        ex.set_light_mode(true);
        ex.input("data", img_);     // input node
        ncnn::Mat out;
        ex.extract("fc1", out);     // output node
        for (int j = 0; j < 128; j++) {
            feature_out[j] = out[j];
        }
        normalize(feature_out);     // feature normalize(l2)
    }

    void Recognition::normalize(std::vector<float> &feature) {
        float sum = 0;
        for (auto it = feature.begin(); it != feature.end(); it++) {
            sum += (float) *it * (float) *it;
        }
        sum = sqrt(sum);
        for (auto it = feature.begin(); it != feature.end(); it++) {
            *it /= sum;
        }
    }

    void Recognition::start(ncnn::Mat &ncnn_img, std::vector<float> &feature128) {
        RecogNet(ncnn_img);
        feature128 = feature_out;
    }

    void Recognition::getAffineMatrix(float *src_5pts, const float *dst_5pts, float *M) {
        float src[10], dst[10];
        memcpy(src, src_5pts, sizeof(float) * 10);
        memcpy(dst, dst_5pts, sizeof(float) * 10);

        float ptmp[2];
        ptmp[0] = ptmp[1] = 0;
        for (int i = 0; i < 5; ++i) {
            ptmp[0] += src[i];
            ptmp[1] += src[5 + i];
        }
        ptmp[0] /= 5;
        ptmp[1] /= 5;
        for (int i = 0; i < 5; ++i) {
            src[i] -= ptmp[0];
            src[5 + i] -= ptmp[1];
            dst[i] -= ptmp[0];
            dst[5 + i] -= ptmp[1];
        }

        float dst_x = (dst[3] + dst[4] - dst[0] - dst[1]) / 2, dst_y =
                (dst[8] + dst[9] - dst[5] - dst[6]) / 2;
        float src_x = (src[3] + src[4] - src[0] - src[1]) / 2, src_y =
                (src[8] + src[9] - src[5] - src[6]) / 2;
        float theta = atan2(dst_x, dst_y) - atan2(src_x, src_y);

        float scale = sqrt(pow(dst_x, 2) + pow(dst_y, 2)) / sqrt(pow(src_x, 2) + pow(src_y, 2));
        float pts1[10];
        float pts0[2];
        float _a = sin(theta), _b = cos(theta);
        pts0[0] = pts0[1] = 0;
        for (int i = 0; i < 5; ++i) {
            pts1[i] = scale * (src[i] * _b + src[i + 5] * _a);
            pts1[i + 5] = scale * (-src[i] * _a + src[i + 5] * _b);
            pts0[0] += (dst[i] - pts1[i]);
            pts0[1] += (dst[i + 5] - pts1[i + 5]);
        }
        pts0[0] /= 5;
        pts0[1] /= 5;

        float sqloss = 0;
        for (int i = 0; i < 5; ++i) {
            sqloss += ((pts0[0] + pts1[i] - dst[i]) * (pts0[0] + pts1[i] - dst[i])
                       +
                       (pts0[1] + pts1[i + 5] - dst[i + 5]) * (pts0[1] + pts1[i + 5] - dst[i + 5]));
        }

        float square_sum = 0;
        for (int i = 0; i < 10; ++i) {
            square_sum += src[i] * src[i];
        }
        for (int t = 0; t < 200; ++t) {
            _a = 0;
            _b = 0;
            for (int i = 0; i < 5; ++i) {
                _a += ((pts0[0] - dst[i]) * src[i + 5] - (pts0[1] - dst[i + 5]) * src[i]);
                _b += ((pts0[0] - dst[i]) * src[i] + (pts0[1] - dst[i + 5]) * src[i + 5]);
            }
            if (_b < 0) {
                _b = -_b;
                _a = -_a;
            }
            float _s = sqrt(_a * _a + _b * _b);
            _b /= _s;
            _a /= _s;

            for (int i = 0; i < 5; ++i) {
                pts1[i] = scale * (src[i] * _b + src[i + 5] * _a);
                pts1[i + 5] = scale * (-src[i] * _a + src[i + 5] * _b);
            }

            float _scale = 0;
            for (int i = 0; i < 5; ++i) {
                _scale += ((dst[i] - pts0[0]) * pts1[i] + (dst[i + 5] - pts0[1]) * pts1[i + 5]);
            }
            _scale /= (square_sum * scale);
            for (int i = 0; i < 10; ++i) {
                pts1[i] *= (_scale / scale);
            }
            scale = _scale;

            pts0[0] = pts0[1] = 0;
            for (int i = 0; i < 5; ++i) {
                pts0[0] += (dst[i] - pts1[i]);
                pts0[1] += (dst[i + 5] - pts1[i + 5]);
            }
            pts0[0] /= 5;
            pts0[1] /= 5;

            float _sqloss = 0;
            for (int i = 0; i < 5; ++i) {
                _sqloss += ((pts0[0] + pts1[i] - dst[i]) * (pts0[0] + pts1[i] - dst[i])
                            + (pts0[1] + pts1[i + 5] - dst[i + 5]) *
                              (pts0[1] + pts1[i + 5] - dst[i + 5]));
            }
            if (abs(_sqloss - sqloss) < 1e-2) {
                break;
            }
            sqloss = _sqloss;
        }

        for (int i = 0; i < 5; ++i) {
            pts1[i] += (pts0[0] + ptmp[0]);
            pts1[i + 5] += (pts0[1] + ptmp[1]);
        }

        M[0] = _b * scale;
        M[1] = _a * scale;
        M[3] = -_a * scale;
        M[4] = _b * scale;
        M[2] = pts0[0] + ptmp[0] - scale * (ptmp[0] * _b + ptmp[1] * _a);
        M[5] = pts0[1] + ptmp[1] - scale * (-ptmp[0] * _a + ptmp[1] * _b);
    }

    void
    Recognition::warpAffineMatrix(ncnn::Mat src, ncnn::Mat &dst, float *M, int dst_w, int dst_h) {
        int src_w = src.w;
        int src_h = src.h;

        unsigned char *src_u = new unsigned char[src_w * src_h * 3]{0};
        unsigned char *dst_u = new unsigned char[dst_w * dst_h * 3]{0};

        src.to_pixels(src_u, ncnn::Mat::PIXEL_RGB);

        float m[6];
        for (int i = 0; i < 6; i++)
            m[i] = M[i];
        float D = m[0] * m[4] - m[1] * m[3];
        D = D != 0 ? 1. / D : 0;
        float A11 = m[4] * D, A22 = m[0] * D;
        m[0] = A11;
        m[1] *= -D;
        m[3] *= -D;
        m[4] = A22;
        float b1 = -m[0] * m[2] - m[1] * m[5];
        float b2 = -m[3] * m[2] - m[4] * m[5];
        m[2] = b1;
        m[5] = b2;

        for (int y = 0; y < dst_h; y++) {
            for (int x = 0; x < dst_w; x++) {
                float fx = m[0] * x + m[1] * y + m[2];
                float fy = m[3] * x + m[4] * y + m[5];

                int sy = (int) floor(fy);
                fy -= sy;
                if (sy < 0 || sy >= src_h) continue;

                short cbufy[2];
                cbufy[0] = (short) ((1.f - fy) * 2048);
                cbufy[1] = 2048 - cbufy[0];

                int sx = (int) floor(fx);
                fx -= sx;
                if (sx < 0 || sx >= src_w) continue;

                short cbufx[2];
                cbufx[0] = (short) ((1.f - fx) * 2048);
                cbufx[1] = 2048 - cbufx[0];

                if (sy == src_h - 1 || sx == src_w - 1)
                    continue;
                for (int c = 0; c < 3; c++) {
                    dst_u[3 * (y * dst_w + x) + c] = (
                            src_u[3 * (sy * src_w + sx) + c] * cbufx[0] * cbufy[0] +
                            src_u[3 * ((sy + 1) * src_w + sx) + c] * cbufx[0] * cbufy[1] +
                            src_u[3 * (sy * src_w + sx + 1) + c] * cbufx[1] * cbufy[0] +
                            src_u[3 * ((sy + 1) * src_w + sx + 1) + c] * cbufx[1] * cbufy[1]
                    ) >> 22;
                }
            }
        }

        dst = ncnn::Mat::from_pixels(dst_u, ncnn::Mat::PIXEL_BGR, dst_w, dst_h);
        delete[] src_u;
        delete[] dst_u;
    }

    ncnn::Mat Recognition::preprocess(ncnn::Mat img, int *info) {
        int image_w = 112; //96 or 112
        int image_h = 112;

        float dst[10] = {30.2946, 65.5318, 48.0252, 33.5493, 62.7299,
                         51.6963, 51.5014, 71.7366, 92.3655, 92.2041};

        if (image_w == 112)
            for (int i = 0; i < 5; i++)
                dst[i] += 8.0;

        float src[10];
        for (int i = 0; i < 10; i++) {
            src[i] = info[i];
        }

        float M[6];
        getAffineMatrix(src, dst, M);
        ncnn::Mat out;
        warpAffineMatrix(img, out, M, image_w, image_h);
        return out;
    }

    double calculSimilar(std::vector<float> &v1, std::vector<float> &v2, int distance_metric) {
        if (v1.size() != v2.size() || !v1.size())
            return 0;
        double ret = 0.0, mod1 = 0.0, mod2 = 0.0, dist = 0.0, diff = 0.0;

        if (distance_metric == 0) {         // Euclidian distance
            for (std::vector<double>::size_type i = 0; i != v1.size(); ++i) {
                diff = v1[i] - v2[i];
                dist += (diff * diff);
            }
            dist = sqrt(dist);
        } else {                              // Distance based on cosine similarity
            for (std::vector<double>::size_type i = 0; i != v1.size(); ++i) {
                ret += v1[i] * v2[i];
                mod1 += v1[i] * v1[i];
                mod2 += v2[i] * v2[i];
            }
            dist = ret / (sqrt(mod1) * sqrt(mod2));
        }
        return dist;
    }
}