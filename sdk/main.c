#include <stdio.h>
#include "platform.h"
#include "xil_printf.h"
#include "xil_io.h"
#include "xil_cache.h"
#include "xparameters.h"
#include "xaxidma.h"


#ifndef XPAR_CANNY_CORE_0_S_AXI_CTRL_BUS_BASEADDR
#define XPAR_CANNY_CORE_0_S_AXI_CTRL_BUS_BASEADDR   0x43C00000
#endif
#define CANNY_CORE_BASEADDR   XPAR_CANNY_CORE_0_S_AXI_CTRL_BUS_BASEADDR

/* Offset thanh ghi */
#define CANNY_CTRL_ADDR_AP_CTRL           0x00
#define CANNY_CTRL_ADDR_GIE               0x04
#define CANNY_CTRL_ADDR_IER               0x08
#define CANNY_CTRL_ADDR_ISR               0x0C
#define CANNY_CTRL_ADDR_ROWS_DATA         0x10
#define CANNY_CTRL_ADDR_COLS_DATA         0x18
#define CANNY_CTRL_ADDR_LOW_THRESH_DATA   0x20
#define CANNY_CTRL_ADDR_HIGH_THRESH_DATA  0x28

/* Bit mask cho thanh ghi AP_CTRL */
#define CANNY_AP_START      (1 << 0)
#define CANNY_AP_DONE       (1 << 1)
#define CANNY_AP_IDLE       (1 << 2)
#define CANNY_AP_READY      (1 << 3)
#define CANNY_AUTO_RESTART  (1 << 7)


#define IMG_WIDTH      640
#define IMG_HEIGHT     480
#define IMG_PIXELS     (IMG_WIDTH * IMG_HEIGHT)

#define LOW_THRESH     30
#define HIGH_THRESH    80


#ifndef XPAR_AXIDMA_0_BASEADDR
#define XPAR_AXIDMA_0_BASEADDR   XPAR_AXI_DMA_0_BASEADDR
#endif
#define DMA_DEV_ID   XPAR_AXIDMA_0_DEVICE_ID


#define SRC_BUFFER_ADDR   0x10000000   /* Anh ARGB8888 dau vao  */
#define DST_BUFFER_ADDR   0x10200000   /* Anh ket qua dau ra    */

static XAxiDma AxiDma;

static inline void canny_write_reg(u32 offset, u32 value) {
    Xil_Out32(CANNY_CORE_BASEADDR + offset, value);
}

static inline u32 canny_read_reg(u32 offset) {
    return Xil_In32(CANNY_CORE_BASEADDR + offset);
}


void generate_test_image(u32 *buf, int width, int height) {
    int box_x0 = width  / 4;
    int box_x1 = (width  * 3) / 4;
    int box_y0 = height / 4;
    int box_y1 = (height * 3) / 4;

    for (int row = 0; row < height; row++) {
        for (int col = 0; col < width; col++) {
            u8 val;
            if (row >= box_y0 && row < box_y1 && col >= box_x0 && col < box_x1)
                val = 230;  /* vung sang - tao bien ro */
            else
                val = 20;   /* nen toi */

            u32 pixel = (0xFFu << 24) | (val << 16) | (val << 8) | val; /* ARGB */
            buf[row * width + col] = pixel;
        }
    }
}


void canny_configure(int rows, int cols, int low_thresh, int high_thresh) {
    canny_write_reg(CANNY_CTRL_ADDR_ROWS_DATA,        (u32)rows);
    canny_write_reg(CANNY_CTRL_ADDR_COLS_DATA,        (u32)cols);
    canny_write_reg(CANNY_CTRL_ADDR_LOW_THRESH_DATA,  (u32)low_thresh);
    canny_write_reg(CANNY_CTRL_ADDR_HIGH_THRESH_DATA, (u32)high_thresh);
}


void canny_start(void) {
    u32 ctrl = canny_read_reg(CANNY_CTRL_ADDR_AP_CTRL);
    ctrl |= CANNY_AP_START;
    canny_write_reg(CANNY_CTRL_ADDR_AP_CTRL, ctrl);
}


int canny_wait_done(u32 timeout_loops) {
    u32 loops = 0;
    while (loops < timeout_loops || timeout_loops == 0) {
        u32 status = canny_read_reg(CANNY_CTRL_ADDR_AP_CTRL);
        if (status & CANNY_AP_DONE) {
            return 0;
        }
        loops++;
        if (timeout_loops != 0 && loops >= timeout_loops) break;
    }
    return -1;  /* timeout - kiem tra lai ket noi AXI hoac clock */
}


int dma_init(void) {
    XAxiDma_Config *cfg = XAxiDma_LookupConfig(DMA_DEV_ID);
    if (!cfg) {
        xil_printf("[LOI] Khong tim thay cau hinh AXI DMA\r\n");
        return -1;
    }

    int status = XAxiDma_CfgInitialize(&AxiDma, cfg);
    if (status != XST_SUCCESS) {
        xil_printf("[LOI] Khoi tao AXI DMA that bai: %d\r\n", status);
        return -1;
    }

    /* Tat ca interrupt - dung polling mode cho ca 2 kenh */
    XAxiDma_IntrDisable(&AxiDma, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DEVICE_TO_DMA);
    XAxiDma_IntrDisable(&AxiDma, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DMA_TO_DEVICE);

    if (XAxiDma_HasSg(&AxiDma)) {
        xil_printf("[LOI] Thiet ke nay dung Scatter-Gather DMA, "
                   "code mau chi ho tro Simple DMA. Can cau hinh "
                   "lai IP AXI DMA trong Vivado (bo tick Scatter Gather Engine).\r\n");
        return -1;
    }

    return 0;
}


int dma_start_transfer(u32 src_addr, u32 dst_addr, u32 byte_len) {
    int status;

    /* Don dep cache truoc khi DMA doc/ghi DDR truc tiep */
    Xil_DCacheFlushRange(src_addr, byte_len);
    Xil_DCacheInvalidateRange(dst_addr, byte_len);

    status = XAxiDma_SimpleTransfer(&AxiDma, dst_addr, byte_len,
                                    XAXIDMA_DEVICE_TO_DMA);
    if (status != XST_SUCCESS) {
        xil_printf("[LOI] Cau hinh DMA S2MM (nhan) that bai: %d\r\n", status);
        return -1;
    }

    status = XAxiDma_SimpleTransfer(&AxiDma, src_addr, byte_len,
                                    XAXIDMA_DMA_TO_DEVICE);
    if (status != XST_SUCCESS) {
        xil_printf("[LOI] Cau hinh DMA MM2S (gui) that bai: %d\r\n", status);
        return -1;
    }

    return 0;
}


void dma_wait_done(void) {
    while (XAxiDma_Busy(&AxiDma, XAXIDMA_DMA_TO_DEVICE)) { }
    while (XAxiDma_Busy(&AxiDma, XAXIDMA_DEVICE_TO_DMA)) { }
}


u32 count_edge_pixels(u32 *result_buf, int width, int height) {
    u32 count = 0;
    for (int i = 0; i < width * height; i++) {
        u8 val = (u8)(result_buf[i] & 0xFF);
        if (val == 255) count++;
    }
    return count;
}


int main(void) {
    init_platform();

    xil_printf("\r\n=== Canny Edge Detection - Bare-metal Firmware ===\r\n");
    xil_printf("Zynq-7000 | IP: canny_core_0 | Resolution: %dx%d\r\n",
              IMG_WIDTH, IMG_HEIGHT);

    u32 *src_buf = (u32 *)SRC_BUFFER_ADDR;
    u32 *dst_buf = (u32 *)DST_BUFFER_ADDR;
    u32  img_bytes = IMG_PIXELS * sizeof(u32);

    /* Buoc 1: Sinh anh test trong DDR */
    xil_printf("[1/6] Sinh anh test (khoi vuong sang tren nen toi)...\r\n");
    generate_test_image(src_buf, IMG_WIDTH, IMG_HEIGHT);

    /* Buoc 2: Khoi tao AXI DMA */
    xil_printf("[2/6] Khoi tao AXI DMA...\r\n");
    if (dma_init() != 0) {
        xil_printf("Dung chuong trinh do loi DMA.\r\n");
        cleanup_platform();
        return -1;
    }

    /* Buoc 3: Cau hinh IP canny_core_0 qua AXI4-Lite */
    xil_printf("[3/6] Cau hinh canny_core_0: rows=%d cols=%d low=%d high=%d\r\n",
              IMG_HEIGHT, IMG_WIDTH, LOW_THRESH, HIGH_THRESH);
    canny_configure(IMG_HEIGHT, IMG_WIDTH, LOW_THRESH, HIGH_THRESH);

    /* Buoc 4: Bat dau IP (ap_start=1) - IP se cho du lieu tu stream_in */
    xil_printf("[4/6] Bat dau IP (ap_start)...\r\n");
    canny_start();

    /* Buoc 5: Kich hoat DMA de bom du lieu vao IP va nhan ket qua ra */
    xil_printf("[5/6] Kich hoat DMA truyen anh (MM2S + S2MM)...\r\n");
    if (dma_start_transfer(SRC_BUFFER_ADDR, DST_BUFFER_ADDR, img_bytes) != 0) {
        xil_printf("Dung chuong trinh do loi truyen DMA.\r\n");
        cleanup_platform();
        return -1;
    }
    dma_wait_done();

    if (canny_wait_done(1000000) != 0) {
        xil_printf("[CANH BAO] IP khong bao ap_done sau timeout - "
                   "kiem tra lai ket noi stream_in/stream_out voi DMA.\r\n");
    }

    /* Buoc 6: Doc lai ket qua va thong ke */
    xil_printf("[6/6] Doc ket qua va kiem tra...\r\n");
    Xil_DCacheInvalidateRange(DST_BUFFER_ADDR, img_bytes);

    u32 edge_count = count_edge_pixels(dst_buf, IMG_WIDTH, IMG_HEIGHT);
    float edge_pct = 100.0f * edge_count / IMG_PIXELS;

    xil_printf("\r\n=== KET QUA ===\r\n");
    xil_printf("So pixel bien phat hien : %lu / %d (%.1f%%)\r\n",
              (unsigned long)edge_count, IMG_PIXELS, edge_pct);
    xil_printf("Trang thai IP (AP_CTRL) : 0x%08lX\r\n",
              (unsigned long)canny_read_reg(CANNY_CTRL_ADDR_AP_CTRL));

    if (edge_count > 0) {
        xil_printf("[OK] IP phat hien duoc bien - he thong hoat dong dung.\r\n");
    } else {
        xil_printf("[CANH BAO] Khong phat hien bien nao - kiem tra lai ket noi "
                   "AXI-Stream hoac threshold.\r\n");
    }

    cleanup_platform();
    return 0;
}
