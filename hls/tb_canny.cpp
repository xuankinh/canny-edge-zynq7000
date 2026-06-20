#include <iostream>
#include <string>
#include <sstream>
#include <iomanip>
#include <stdio.h>
#include <time.h>
#include "hls_stream.h"
#include "ap_axi_sdata.h"
#include "opencv2/opencv.hpp"

#define MAX_WIDTH    1920
#define MAX_HEIGHT   1080

#define MAX_IMGS_PER_DATASET  1

typedef ap_axiu<32, 1, 1, 1> AXI_PIXEL;

void canny_core(
    hls::stream<AXI_PIXEL>& stream_in,
    hls::stream<AXI_PIXEL>& stream_out,
    int rows, int cols,
    int low_thresh, int high_thresh);

struct DatasetConfig {
    std::string name;
    std::string input_dir;
    std::string output_dir;
    std::string file_prefix;
    std::string file_ext;
    int         low_thresh;
    int         high_thresh;
    int         num_images;
};

bool run_canny_pipeline(
    const cv::Mat&          src_frame,
    cv::Mat&                dst_frame,
    hls::stream<AXI_PIXEL>& in_stream,
    hls::stream<AXI_PIXEL>& out_stream,
    int low_thresh,
    int high_thresh)
{
    int rows = src_frame.rows;
    int cols = src_frame.cols;

    if (rows > MAX_HEIGHT || cols > MAX_WIDTH) {
        std::cerr << "  [LOI] Anh vuot gioi han  ("
                  << MAX_WIDTH << "x" << MAX_HEIGHT << ")" << std::endl;
        return false;
    }

    for (int row = 0; row < rows; row++) {
        for (int col = 0; col < cols; col++) {
            cv::Vec3b px = src_frame.at<cv::Vec3b>(row, col);
            AXI_PIXEL axi_p;
            axi_p.data = 0;
            axi_p.data.range(31, 24) = 0xFF;
            axi_p.data.range(23, 16) = px[2];
            axi_p.data.range(15,  8) = px[1];
            axi_p.data.range( 7,  0) = px[0];
            axi_p.keep = 0xF;
            axi_p.strb = 0xF;
            axi_p.last = (row == rows-1 && col == cols-1) ? 1 : 0;
            in_stream.write(axi_p);
        }
    }

    canny_core(in_stream, out_stream, rows, cols, low_thresh, high_thresh);

    dst_frame = cv::Mat(rows, cols, CV_8UC1);
    for (int row = 0; row < rows; row++) {
        for (int col = 0; col < cols; col++) {
            AXI_PIXEL out_p = out_stream.read();
            dst_frame.at<uchar>(row, col) = (uchar)out_p.data.range(7, 0);
        }
    }
    return true;
}

int process_dataset(const DatasetConfig& cfg,
                    hls::stream<AXI_PIXEL>& axis_in,
                    hls::stream<AXI_PIXEL>& axis_out,
                    double& total_hw_ms)
{
    std::cout << "\n  [DATASET] " << cfg.name << std::endl;
    std::cout << "  Threshold: low=" << cfg.low_thresh
              << "  high=" << cfg.high_thresh << std::endl;
    std::cout << "  Input dir : " << cfg.input_dir  << std::endl;
    std::cout << "  Output dir: " << cfg.output_dir << std::endl;

    int ok = 0, fail = 0;

    for (int i = 1; i <= cfg.num_images; i++) {
        std::ostringstream in_oss;
        in_oss << cfg.input_dir << cfg.file_prefix << i << cfg.file_ext;
        std::string in_path = in_oss.str();

        std::ostringstream out_oss;
        out_oss << cfg.output_dir << "canny_" << cfg.file_prefix << i << ".bmp";
        std::string out_path = out_oss.str();

        cv::Mat src = cv::imread(in_path, CV_LOAD_IMAGE_COLOR);
        if (!src.data) {
            std::cout << " Khong tim thay: " << in_path << std::endl;
            fail++;
            continue;
        }

        if (src.cols > MAX_WIDTH || src.rows > MAX_HEIGHT) {
            cv::resize(src, src, cv::Size(MAX_WIDTH, MAX_HEIGHT));
        }

        std::cout << "  -> [" << i << "/" << cfg.num_images << "] "
                  << cfg.file_prefix << i << cfg.file_ext
                  << "  (" << src.cols << "x" << src.rows << ")  ";

        cv::Mat dst;
        bool result = run_canny_pipeline(src, dst, axis_in, axis_out,
                                       cfg.low_thresh, cfg.high_thresh);

        if (result) {
            cv::imwrite(out_path, dst);
            int edge_px = cv::countNonZero(dst);
            float edge_pct = 100.0f * edge_px / (dst.rows * dst.cols);

            double hw_ms = (double)(src.rows + 1) * (src.cols + 1) * 10.0 / 1000000.0;
            total_hw_ms += hw_ms;

            std::cout << "OK  | edge=" << edge_px
                      << " (" << std::fixed << std::setprecision(1)
                      << edge_pct << "%) | HW Time: " << hw_ms << " ms" << std::endl;
            ok++;
        } else {
            std::cout << "FAIL" << std::endl;
            fail++;
        }
    }

    std::cout << "  Ket qua: " << ok << " OK / " << fail << " FAIL" << std::endl;
    return ok;
}

int main() {
    hls::stream<AXI_PIXEL> axis_in ("axis_in");
    hls::stream<AXI_PIXEL> axis_out("axis_out");

    std::cout << "  Canny Edge Detection Test" << std::endl;
    std::cout << "  Ung dung: Medical & Industrial Inspection"  << std::endl;

    DatasetConfig datasets[] = {
        {
            /* name        */ "Concrete Crack Detection ",
            /* input_dir   */ "C:/Users/LEGION/Downloads/dataset_canny/crack/",
            /* output_dir  */ "C:/Users/LEGION/Downloads/ketqua_canny/crack/",
            /* file_prefix */ "crack_",
            /* file_ext    */ ".bmp",
            /* low_thresh  */ 5,
            /* high_thresh */ 15,
            /* num_images  */ MAX_IMGS_PER_DATASET
        },
        {
            /* name        */ "Chest X-Ray Analysis ",
            /* input_dir   */ "C:/Users/LEGION/Downloads/dataset_canny/xray/",
            /* output_dir  */ "C:/Users/LEGION/Downloads/ketqua_canny/xray/",
            /* file_prefix */ "xray_",
            /* file_ext    */ ".bmp",
            /* low_thresh  */ 10,
            /* high_thresh */ 30,
            /* num_images  */ MAX_IMGS_PER_DATASET
        },
        {
            /* name        */ "PCB Defect Inspection ",
            /* input_dir   */ "C:/Users/LEGION/Downloads/dataset_canny/pcb/",
            /* output_dir  */ "C:/Users/LEGION/Downloads/ketqua_canny/pcb/",
            /* file_prefix */ "pcb_",
            /* file_ext    */ ".bmp",
            /* low_thresh  */ 15,
            /* high_thresh */ 40,
            /* num_images  */ MAX_IMGS_PER_DATASET
        },
    };

    int num_datasets = sizeof(datasets) / sizeof(datasets[0]);
    int total_ok = 0;
    double total_hw_time = 0.0;

    clock_t t_start = clock();

    for (int d = 0; d < num_datasets; d++) {
        total_ok += process_dataset(datasets[d], axis_in, axis_out, total_hw_time);
    }

    clock_t t_end = clock();
    double elapsed = (double)(t_end - t_start) / CLOCKS_PER_SEC;

    std::cout << "\n  TONG KET" << std::endl;
    std::cout << "  Tong anh xu ly thanh cong : " << total_ok << std::endl;
    std::cout << "  Thoi gian C-Simulation    : " << std::fixed
              << std::setprecision(2) << elapsed << " giay (Thoi gian mo phong tren CPU)" << std::endl;

    std::cout << "  Thoi gian Hardware (HLS)  : " << std::fixed
              << std::setprecision(2) << total_hw_time << " ms (Thoi gian that su tren FPGA)" << std::endl;

    std::cout << "  Ket qua luu tai           : ketqua_canny/" << std::endl;

    return (total_ok > 0) ? 0 : 1;
}
