/*************************************************************************
 * Copyright (C) [2019] by Cambricon, Inc. All rights reserved
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *************************************************************************/

#include "mluop.h"
#include "mluop_list.h"
#include "test_mluop.h"

void Rgbx2RgbxConvertOp(void *ctx_, char **argv) {
  param_ctx_t *ctx = (param_ctx_t *)ctx_;
  ctx->algo = atoi(argv[1]);
  ctx->input_file = argv[2];
  ctx->width = atoi(argv[3]);
  ctx->height = atoi(argv[4]);
  ctx->output_file = argv[5];
  ctx->src_pix_fmt = argv[6];
  ctx->dst_pix_fmt = argv[7];
  ctx->frame_num = atoi(argv[8]);
  ctx->thread_num = atoi(argv[9]);
  ctx->save_flag = atoi(argv[10]);
  ctx->device_id = atoi(argv[11]);
  ctx->depth_size = 1;  // depth_size: 1->uint8, 2->f16, 4->f32

  char depth_[3] = "8U";
  ctx->depth = depth_;

  if (ctx->algo <= 0) ctx->algo = 3;
  if (ctx->save_flag <= 0) ctx->save_flag = 0;
  if (ctx->frame_num <= 0) ctx->frame_num = 10;
  if (ctx->thread_num <= 0) ctx->thread_num = THREADS_NUM;

  int ret = 0;
  void *status = NULL;
  pthread_t tids[THREADS_NUM];
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

  for (uint32_t i = 0; i < ctx->thread_num; i++) {
    printf("create thead [%d]\n", i);
    ret = pthread_create(&tids[i], &attr, ProcessConvertRgbx2Rgbx, (void *)ctx);
  }

  pthread_attr_destroy(&attr);
  for (uint32_t i = 0; i < ctx->thread_num; i++) {
    ret = pthread_join(tids[i], &status);
    if (ret != 0) {
      printf("pthread_join error(thread id :%lu): error_code=%d\n",
             (long unsigned)tids[i], ret);
    } else {
      printf("pthread_join ok(thread id :%lu): get status:=%ld\n",
             (long unsigned)tids[i], (long)status);
    }
  }
}

void *ProcessConvertRgbx2Rgbx(void *ctx_) {
  param_ctx_t *ctx = (param_ctx_t *)ctx_;
  bool save_flag = ctx->save_flag;
  uint32_t width = ctx->width;
  uint32_t height = ctx->height;
  uint32_t device_id = ctx->device_id;
  uint32_t src_pix_chn_num = getPixFmtChannelNum(getCNCVPixFmtFromPixindex(ctx->src_pix_fmt));
  uint32_t dst_pix_chn_num = getPixFmtChannelNum(getCNCVPixFmtFromPixindex(ctx->dst_pix_fmt));
  uint32_t depth_size = ctx->depth_size;
  uint32_t frame_num = ctx->frame_num;
  const char *depth = ctx->depth;
  const char *filename = ctx->input_file;
  const char *output_file = ctx->output_file;

  set_cnrt_ctx(device_id, CNRT_CHANNEL_TYPE_NONE /* CNRT_CHANNEL_TYPE_0 */);

  uint32_t src_stride = PAD_UP(width, ALIGN_R2Y_CVT) * src_pix_chn_num * depth_size;
  uint32_t dst_stride = PAD_UP(width, ALIGN_R2Y_CVT) * dst_pix_chn_num * depth_size;
  uint32_t src_size = height * src_stride;
  uint32_t dst_size = height * dst_stride;

  uint8_t *src_cpu = (uint8_t *)malloc(src_size);
  uint8_t *dst_cpu = (uint8_t *)malloc(dst_size);

  FILE *fp = fopen(filename, "rb");
  size_t read_size = fread((void *)src_cpu, 1, src_size, fp);
  fclose(fp);
  if (read_size != src_size) {
    printf("read file size failed!\n");
    free(src_cpu);
    free(dst_cpu);
    return NULL;
  }

  void *src_rgbx_mlu;
  void *dst_rgbx_mlu;
  cnrtMalloc((void **)(&src_rgbx_mlu), src_size);
  cnrtMalloc((void **)(&dst_rgbx_mlu), dst_size);
  cnrtMemcpy(src_rgbx_mlu, src_cpu, src_size, CNRT_MEM_TRANS_DIR_HOST2DEV);

  HANDLE handle;
#if PRINT_TIME
  float time_use = 0;
  struct timeval end;
  struct timeval start;
  gettimeofday(&start, NULL);
#endif
  /*--------init op--------*/
  MluopConvertRgbx2RgbxInit(&handle, width, height, ctx->src_pix_fmt,
                            ctx->dst_pix_fmt, depth);
#if PRINT_TIME
  gettimeofday(&end, NULL);
  time_use = (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec);
  printf("[init] time: %.3f ms\n", time_use / 1000);
#endif

  /*-------execute op-------*/
  cnrtMemcpy(src_rgbx_mlu, src_cpu, src_size, CNRT_MEM_TRANS_DIR_HOST2DEV);
  for (uint32_t i = 0; i < frame_num; i++) {
#if PRINT_TIME
    gettimeofday(&start, NULL);
#endif
    MluopConvertRgbx2RgbxExec(handle, src_rgbx_mlu, dst_rgbx_mlu);
#if PRINT_TIME
    gettimeofday(&end, NULL);
    time_use =
        (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec);
    printf("[exec] time(ave.): %.3f ms, total frame: %d\n",
           (time_use / 1000.0) / frame_num, frame_num);
#endif
  /*----------D2H-----------*/
    cnrtMemcpy(dst_cpu, dst_rgbx_mlu, dst_size, CNRT_MEM_TRANS_DIR_DEV2HOST);
  }
#if PRINT_TIME
  gettimeofday(&start, NULL);
#endif
  /*-------destroy op-------*/
  MluopConvertRgbx2RgbxDestroy(handle);
#if PRINT_TIME
  gettimeofday(&end, NULL);
  time_use = (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec);
  printf("[destroy] time: %.3f ms\n", time_use / 1000);
#endif
  /*-------save file-------*/
  if (save_flag) {
    FILE *fp_out = fopen(output_file, "wb");
    fwrite(dst_cpu, 1, dst_size, fp_out);
    fclose(fp_out);
  }
  if (src_cpu) free(src_cpu);
  if (src_rgbx_mlu) cnrtFree(src_rgbx_mlu);
  if (dst_cpu) free(dst_cpu);
  if (dst_rgbx_mlu) cnrtFree(dst_rgbx_mlu);
  return NULL;
}
