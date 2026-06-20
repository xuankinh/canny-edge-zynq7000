#include "ap_axi_sdata.h"
#include "hls_stream.h"
#include "ap_int.h"
#include "ap_fixed.h"

#define MAX_WIDTH   1920
#define MAX_HEIGHT  1080

typedef ap_axiu<32, 1, 1, 1> AXI_PIXEL;

struct pipe_data {
    unsigned char pixel;
    ap_uint<1>    last;
};

struct grad_data {
    unsigned short magnitude;
    ap_uint<2>    direction;
    ap_uint<1>    last;
};

void gaussian_filter(
    hls::stream<AXI_PIXEL>& stream_in,
    hls::stream<pipe_data>&  stream_out,
    int rows, int cols)
{
    static unsigned char line_buf[2][MAX_WIDTH];
    #pragma HLS ARRAY_PARTITION variable=line_buf complete dim=1

    unsigned char win[3][3];
    #pragma HLS ARRAY_PARTITION variable=win complete dim=0

    const short G[3][3] = {{1,2,1},{2,4,2},{1,2,1}};
    #pragma HLS ARRAY_PARTITION variable=G complete dim=0

    for (int row = 0; row < rows + 1; row++) {
        #pragma HLS LOOP_TRIPCOUNT min=480 max=1080
        for (int col = 0; col < cols + 1; col++) {
            #pragma HLS LOOP_TRIPCOUNT min=640 max=1920
            #pragma HLS PIPELINE II=1

            unsigned char gray = 0;
            bool reading = (row < rows && col < cols);

            if (reading) {
                AXI_PIXEL in_p = stream_in.read();
                unsigned char R = in_p.data.range(23, 16);
                unsigned char G_ch = in_p.data.range(15, 8);
                unsigned char B = in_p.data.range(7,  0);
                gray = (R >> 2) + (G_ch >> 1) + (B >> 2);
            }

            int col_buf = (col < cols) ? col : (cols - 1);

            unsigned char prev0 = line_buf[0][col_buf];
            unsigned char prev1 = line_buf[1][col_buf];
            if (reading) {
                line_buf[0][col_buf] = prev1;
                line_buf[1][col_buf] = gray;
            }

            for (int i = 0; i < 3; i++) {
                win[i][0] = win[i][1];
                win[i][1] = win[i][2];
            }
            win[0][2] = prev0;
            win[1][2] = prev1;
            win[2][2] = gray;

            unsigned char out_val = 0;
            if (row >= 1 && col >= 1) {
                short sum = 0;
                for (int i = 0; i < 3; i++)
                    for (int j = 0; j < 3; j++)
                        sum += (short)win[i][j] * G[i][j];
                out_val = (unsigned char)(sum >> 4);
            }

            int out_row = row - 1;
            int out_col = col - 1;
            if (out_row >= 0 && out_row < rows && out_col >= 0 && out_col < cols) {
                pipe_data out_p;
                out_p.pixel = out_val;
                out_p.last  = (out_row == rows - 1 && out_col == cols - 1) ? (ap_uint<1>)1 : (ap_uint<1>)0;
                stream_out.write(out_p);
            }
        }
    }
}

void sobel_gradient(
    hls::stream<pipe_data>& stream_in,
    hls::stream<grad_data>& stream_out,
    int rows, int cols)
{
    static unsigned char line_buf[2][MAX_WIDTH];
    #pragma HLS ARRAY_PARTITION variable=line_buf complete dim=1

    unsigned char win[3][3];
    #pragma HLS ARRAY_PARTITION variable=win complete dim=0

    const short Gx[3][3] = {{-1,0,1},{-2,0,2},{-1,0,1}};
    const short Gy[3][3] = {{-1,-2,-1},{0,0,0},{1,2,1}};
    #pragma HLS ARRAY_PARTITION variable=Gx complete dim=0
    #pragma HLS ARRAY_PARTITION variable=Gy complete dim=0

    for (int row = 0; row < rows + 1; row++) {
        #pragma HLS LOOP_TRIPCOUNT min=480 max=1080
        for (int col = 0; col < cols + 1; col++) {
            #pragma HLS LOOP_TRIPCOUNT min=640 max=1920
            #pragma HLS PIPELINE II=1

            unsigned char pix = 0;
            bool reading = (row < rows && col < cols);

            if (reading) {
                pipe_data in_p = stream_in.read();
                pix = in_p.pixel;
            }

            int col_buf = (col < cols) ? col : (cols - 1);

            unsigned char prev0 = line_buf[0][col_buf];
            unsigned char prev1 = line_buf[1][col_buf];
            if (reading) {
                line_buf[0][col_buf] = prev1;
                line_buf[1][col_buf] = pix;
            }

            for (int i = 0; i < 3; i++) {
                win[i][0] = win[i][1];
                win[i][1] = win[i][2];
            }
            win[0][2] = prev0;
            win[1][2] = prev1;
            win[2][2] = pix;

            grad_data out_p;
            out_p.last      = 0;
            out_p.magnitude = 0;
            out_p.direction = 0;

            if (row >= 1 && col >= 1) {
                short sum_x = 0, sum_y = 0;
                for (int i = 0; i < 3; i++)
                    for (int j = 0; j < 3; j++) {
                        sum_x += (short)win[i][j] * Gx[i][j];
                        sum_y += (short)win[i][j] * Gy[i][j];
                    }

                short abs_x = (sum_x < 0) ? -sum_x : sum_x;
                short abs_y = (sum_y < 0) ? -sum_y : sum_y;
                out_p.magnitude = (unsigned short)(abs_x + abs_y);

                short ax = abs_x;
                short ay = abs_y;
                bool px_sign = (sum_x >= 0), py_sign = (sum_y >= 0);

                if (5 * ay < 2 * ax) {
                    out_p.direction = 0;
                } else if (5 * ax < 2 * ay) {
                    out_p.direction = 2;
                } else if (px_sign == py_sign) {
                    out_p.direction = 1;
                } else {
                    out_p.direction = 3;
                }
            }

            int out_row = row - 1;
            int out_col = col - 1;
            if (out_row >= 0 && out_row < rows && out_col >= 0 && out_col < cols) {
                out_p.last = (out_row == rows - 1 && out_col == cols - 1) ? (ap_uint<1>)1 : (ap_uint<1>)0;
                stream_out.write(out_p);
            }
        }
    }
}

void nms_filter(
    hls::stream<grad_data>& stream_in,
    hls::stream<pipe_data>& stream_out,
    int rows, int cols)
{
    static unsigned short mag_buf[2][MAX_WIDTH];
    #pragma HLS ARRAY_PARTITION variable=mag_buf complete dim=1
    static ap_uint<2> dir_buf[2][MAX_WIDTH];
    #pragma HLS ARRAY_PARTITION variable=dir_buf complete dim=1

    unsigned short mag_win[3][3];
    #pragma HLS ARRAY_PARTITION variable=mag_win complete dim=0
    ap_uint<2> dir_win[3][3];
    #pragma HLS ARRAY_PARTITION variable=dir_win complete dim=0

    for (int row = 0; row < rows + 1; row++) {
        #pragma HLS LOOP_TRIPCOUNT min=480 max=1080
        for (int col = 0; col < cols + 1; col++) {
            #pragma HLS LOOP_TRIPCOUNT min=640 max=1920
            #pragma HLS PIPELINE II=1

            unsigned short mag = 0;
            ap_uint<2>     dir = 0;
            bool           reading = (row < rows && col < cols);

            if (reading) {
                grad_data in_p = stream_in.read();
                mag = in_p.magnitude;
                dir = in_p.direction;
            }

            int col_buf = (col < cols) ? col : (cols - 1);

            unsigned short prev_mag0 = mag_buf[0][col_buf];
            unsigned short prev_mag1 = mag_buf[1][col_buf];
            ap_uint<2>     prev_dir0 = dir_buf[0][col_buf];
            ap_uint<2>     prev_dir1 = dir_buf[1][col_buf];

            if (reading) {
                mag_buf[0][col_buf] = prev_mag1;
                mag_buf[1][col_buf] = mag;
                dir_buf[0][col_buf] = prev_dir1;
                dir_buf[1][col_buf] = dir;
            }

            for (int i = 0; i < 3; i++) {
                mag_win[i][0] = mag_win[i][1];
                mag_win[i][1] = mag_win[i][2];
                dir_win[i][0] = dir_win[i][1];
                dir_win[i][1] = dir_win[i][2];
            }
            mag_win[0][2] = prev_mag0;
            mag_win[1][2] = prev_mag1;
            mag_win[2][2] = mag;
            dir_win[0][2] = prev_dir0;
            dir_win[1][2] = prev_dir1;
            dir_win[2][2] = dir;

            unsigned char out_val = 0;
            if (row >= 1 && col >= 1) {
                unsigned short center = mag_win[1][1];
                ap_uint<2>     d      = dir_win[1][1];

                unsigned short n1 = 0, n2 = 0;
                if      (d == 0) { n1 = mag_win[1][0]; n2 = mag_win[1][2]; }
                else if (d == 2) { n1 = mag_win[0][1]; n2 = mag_win[2][1]; }
                else if (d == 1) { n1 = mag_win[0][2]; n2 = mag_win[2][0]; }
                else             { n1 = mag_win[0][0]; n2 = mag_win[2][2]; }

                out_val = (center >= n1 && center >= n2) ? (unsigned char)(center > 255 ? 255 : center) : 0;
            }

            int out_row = row - 1;
            int out_col = col - 1;
            if (out_row >= 0 && out_row < rows && out_col >= 0 && out_col < cols) {
                pipe_data out_p;
                out_p.pixel = out_val;
                out_p.last  = (out_row == rows - 1 && out_col == cols - 1) ? (ap_uint<1>)1 : (ap_uint<1>)0;
                stream_out.write(out_p);
            }
        }
    }
}

void hysteresis_threshold(
    hls::stream<pipe_data>& stream_in,
    hls::stream<AXI_PIXEL>& stream_out,
    int rows, int cols,
    int low_thresh, int high_thresh)
{
    static unsigned char line_buf[2][MAX_WIDTH];
    #pragma HLS ARRAY_PARTITION variable=line_buf complete dim=1

    unsigned char win[3][3];
    #pragma HLS ARRAY_PARTITION variable=win complete dim=0

    for (int row = 0; row < rows + 1; row++) {
        #pragma HLS LOOP_TRIPCOUNT min=480 max=1080
        for (int col = 0; col < cols + 1; col++) {
            #pragma HLS LOOP_TRIPCOUNT min=640 max=1920
            #pragma HLS PIPELINE II=1

            unsigned char pix = 0;
            bool reading = (row < rows && col < cols);

            if (reading) {
                pipe_data in_p = stream_in.read();
                if      (in_p.pixel >= (unsigned char)high_thresh) pix = 255;
                else if (in_p.pixel >= (unsigned char)low_thresh)  pix = 128;
                else                                               pix = 0;
            }

            int col_buf = (col < cols) ? col : (cols - 1);

            unsigned char prev0 = line_buf[0][col_buf];
            unsigned char prev1 = line_buf[1][col_buf];
            if (reading) {
                line_buf[0][col_buf] = prev1;
                line_buf[1][col_buf] = pix;
            }

            for (int i = 0; i < 3; i++) {
                win[i][0] = win[i][1];
                win[i][1] = win[i][2];
            }
            win[0][2] = prev0;
            win[1][2] = prev1;
            win[2][2] = pix;

            unsigned char final_val = 0;
            if (row >= 1 && col >= 1) {
                unsigned char center = win[1][1];

                if (center == 255) {
                    final_val = 255;
                } else if (center == 128) {
                    bool has_strong = false;
                    for (int i = 0; i < 3; i++)
                        for (int j = 0; j < 3; j++)
                            if (win[i][j] == 255) has_strong = true;
                    final_val = has_strong ? 255 : 0;
                }
            }

            int out_row = row - 1;
            int out_col = col - 1;
            if (out_row >= 0 && out_row < rows && out_col >= 0 && out_col < cols) {
                AXI_PIXEL out_p;
                out_p.last = (out_row == rows - 1 && out_col == cols - 1) ? (ap_uint<1>)1 : (ap_uint<1>)0;
                out_p.keep = 0xF;
                out_p.strb = 0xF;
                out_p.data.range(31, 24) = 0xFF;
                out_p.data.range(23, 16) = final_val;
                out_p.data.range(15,  8) = final_val;
                out_p.data.range( 7,  0) = final_val;
                stream_out.write(out_p);
            }
        }
    }
}

void canny_core(
    hls::stream<AXI_PIXEL>& stream_in,
    hls::stream<AXI_PIXEL>& stream_out,
    int rows,
    int cols,
    int low_thresh,
    int high_thresh
) {
    #pragma HLS INTERFACE axis      port=stream_in
    #pragma HLS INTERFACE axis      port=stream_out
    #pragma HLS INTERFACE s_axilite port=rows        bundle=CTRL_BUS
    #pragma HLS INTERFACE s_axilite port=cols        bundle=CTRL_BUS
    #pragma HLS INTERFACE s_axilite port=low_thresh  bundle=CTRL_BUS
    #pragma HLS INTERFACE s_axilite port=high_thresh bundle=CTRL_BUS
    #pragma HLS INTERFACE s_axilite port=return      bundle=CTRL_BUS

    #pragma HLS DATAFLOW

    hls::stream<pipe_data> stream_gauss("s_gauss");
    hls::stream<grad_data> stream_grad ("s_grad");
    hls::stream<pipe_data> stream_nms  ("s_nms");
    #pragma HLS STREAM variable=stream_gauss depth=2048
    #pragma HLS STREAM variable=stream_grad  depth=2048
    #pragma HLS STREAM variable=stream_nms   depth=2048

    gaussian_filter     (stream_in,    stream_gauss, rows, cols);
    sobel_gradient      (stream_gauss, stream_grad,  rows, cols);
    nms_filter          (stream_grad,  stream_nms,   rows, cols);
    hysteresis_threshold(stream_nms,   stream_out,   rows, cols, low_thresh, high_thresh);
}
