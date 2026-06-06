/****************************************************************************
*
*    Copyright (c) 2017 - 2022 by Rockchip Corp.  All rights reserved.
*
*    The material in this file is confidential and contains trade secrets
*    of Rockchip Corporation. This is proprietary information owned by
*    Rockchip Corporation. No part of this work may be disclosed,
*    reproduced, copied, transmitted, or used in any way for any purpose,
*    without the express written permission of Rockchip Corporation.
*
*****************************************************************************/

#ifndef _RKNN_API_H
#define _RKNN_API_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define RKNN_FLAG_PRIOR_HIGH 0x00000000
#define RKNN_FLAG_PRIOR_MEDIUM 0x00000001
#define RKNN_FLAG_PRIOR_LOW 0x00000002
#define RKNN_FLAG_ASYNC_MASK 0x00000004
#define RKNN_FLAG_COLLECT_PERF_MASK 0x00000008
#define RKNN_FLAG_MEM_ALLOC_OUTSIDE 0x00000010
#define RKNN_FLAG_SHARE_WEIGHT_MEM 0x00000020
#define RKNN_FLAG_FENCE_IN_OUTSIDE 0x00000040
#define RKNN_FLAG_FENCE_OUT_OUTSIDE 0x00000080
#define RKNN_FLAG_COLLECT_MODEL_INFO_ONLY 0x00000100
#define RKNN_FLAG_INTERNAL_ALLOC_OUTSIDE 0x00000200
#define RKNN_FLAG_EXECUTE_FALLBACK_PRIOR_DEVICE_GPU 0x00000400
#define RKNN_FLAG_ENABLE_SRAM 0x00000800
#define RKNN_FLAG_SHARE_SRAM 0x00001000
#define RKNN_FLAG_DISABLE_PROC_HIGH_PRIORITY 0x00002000
#define RKNN_FLAG_DISABLE_FLUSH_INPUT_MEM_CACHE 0x00004000
#define RKNN_FLAG_DISABLE_FLUSH_OUTPUT_MEM_CACHE 0x00008000
#define RKNN_FLAG_MODEL_BUFFER_ZERO_COPY 0x00010000
#define RKNN_MEM_FLAG_ALLOC_NO_CONTEXT 0x00020000

#define RKNN_SUCC 0
#define RKNN_ERR_FAIL -1
#define RKNN_ERR_TIMEOUT -2
#define RKNN_ERR_DEVICE_UNAVAILABLE -3
#define RKNN_ERR_MALLOC_FAIL -4
#define RKNN_ERR_PARAM_INVALID -5
#define RKNN_ERR_MODEL_INVALID -6
#define RKNN_ERR_CTX_INVALID -7
#define RKNN_ERR_INPUT_INVALID -8
#define RKNN_ERR_OUTPUT_INVALID -9
#define RKNN_ERR_DEVICE_UNMATCH -10
#define RKNN_ERR_INCOMPATILE_PRE_COMPILE_MODEL -11
#define RKNN_ERR_INCOMPATILE_OPTIMIZATION_LEVEL_VERSION -12
#define RKNN_ERR_TARGET_PLATFORM_UNMATCH -13

#define RKNN_MAX_DIMS 16
#define RKNN_MAX_NUM_CHANNEL 15
#define RKNN_MAX_NAME_LEN 256
#define RKNN_MAX_DYNAMIC_SHAPE_NUM 512

#ifdef __arm__
typedef uint32_t rknn_context;
#else
typedef uint64_t rknn_context;
#endif

typedef enum _rknn_query_cmd {
    RKNN_QUERY_IN_OUT_NUM = 0,
    RKNN_QUERY_INPUT_ATTR = 1,
    RKNN_QUERY_OUTPUT_ATTR = 2,
    RKNN_QUERY_PERF_DETAIL = 3,
    RKNN_QUERY_PERF_RUN = 4,
    RKNN_QUERY_SDK_VERSION = 5,
    RKNN_QUERY_MEM_SIZE = 6,
    RKNN_QUERY_CUSTOM_STRING = 7,
    RKNN_QUERY_NATIVE_INPUT_ATTR = 8,
    RKNN_QUERY_NATIVE_OUTPUT_ATTR = 9,
    RKNN_QUERY_NATIVE_NC1HWC2_INPUT_ATTR = 8,
    RKNN_QUERY_NATIVE_NC1HWC2_OUTPUT_ATTR = 9,
    RKNN_QUERY_NATIVE_NHWC_INPUT_ATTR = 10,
    RKNN_QUERY_NATIVE_NHWC_OUTPUT_ATTR = 11,
    RKNN_QUERY_DEVICE_MEM_INFO = 12,
    RKNN_QUERY_INPUT_DYNAMIC_RANGE = 13,
    RKNN_QUERY_CURRENT_INPUT_ATTR = 14,
    RKNN_QUERY_CURRENT_OUTPUT_ATTR = 15,
    RKNN_QUERY_CURRENT_NATIVE_INPUT_ATTR = 16,
    RKNN_QUERY_CURRENT_NATIVE_OUTPUT_ATTR = 17,
    RKNN_QUERY_CMD_MAX
} rknn_query_cmd;

typedef enum _rknn_tensor_type {
    RKNN_TENSOR_FLOAT32 = 0,
    RKNN_TENSOR_FLOAT16,
    RKNN_TENSOR_INT8,
    RKNN_TENSOR_UINT8,
    RKNN_TENSOR_INT16,
    RKNN_TENSOR_UINT16,
    RKNN_TENSOR_INT32,
    RKNN_TENSOR_UINT32,
    RKNN_TENSOR_INT64,
    RKNN_TENSOR_BOOL,
    RKNN_TENSOR_INT4,
    RKNN_TENSOR_TYPE_MAX
} rknn_tensor_type;

typedef enum _rknn_tensor_qnt_type {
    RKNN_TENSOR_QNT_NONE = 0,
    RKNN_TENSOR_QNT_DFP,
    RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC,
    RKNN_TENSOR_QNT_MAX
} rknn_tensor_qnt_type;

typedef enum _rknn_tensor_format {
    RKNN_TENSOR_NCHW = 0,
    RKNN_TENSOR_NHWC,
    RKNN_TENSOR_NC1HWC2,
    RKNN_TENSOR_UNDEFINED,
    RKNN_TENSOR_FORMAT_MAX
} rknn_tensor_format;

typedef enum _rknn_core_mask {
    RKNN_NPU_CORE_AUTO = 0,
    RKNN_NPU_CORE_0 = 1,
    RKNN_NPU_CORE_1 = 2,
    RKNN_NPU_CORE_2 = 4,
    RKNN_NPU_CORE_0_1 = RKNN_NPU_CORE_0 | RKNN_NPU_CORE_1,
    RKNN_NPU_CORE_0_1_2 = RKNN_NPU_CORE_0_1 | RKNN_NPU_CORE_2,
    RKNN_NPU_CORE_ALL = 0xffff,
    RKNN_NPU_CORE_UNDEFINED,
} rknn_core_mask;

typedef struct _rknn_input_output_num {
    uint32_t n_input;
    uint32_t n_output;
} rknn_input_output_num;

typedef struct _rknn_tensor_attr {
    uint32_t index;
    uint32_t n_dims;
    uint32_t dims[RKNN_MAX_DIMS];
    char name[RKNN_MAX_NAME_LEN];
    uint32_t n_elems;
    uint32_t size;
    rknn_tensor_format fmt;
    rknn_tensor_type type;
    rknn_tensor_qnt_type qnt_type;
    int8_t fl;
    int32_t zp;
    float scale;
    uint32_t w_stride;
    uint32_t size_with_stride;
    uint8_t pass_through;
    uint32_t h_stride;
} rknn_tensor_attr;

typedef struct _rknn_input_range {
    uint32_t index;
    uint32_t shape_number;
    rknn_tensor_format fmt;
    char name[RKNN_MAX_NAME_LEN];
    uint32_t dyn_range[RKNN_MAX_DYNAMIC_SHAPE_NUM][RKNN_MAX_DIMS];
    uint32_t n_dims;
} rknn_input_range;

typedef struct _rknn_perf_detail {
    char* perf_data;
    uint64_t data_len;
} rknn_perf_detail;

typedef struct _rknn_perf_run {
    int64_t run_duration;
} rknn_perf_run;

typedef struct _rknn_sdk_version {
    char api_version[256];
    char drv_version[256];
} rknn_sdk_version;

typedef struct _rknn_mem_size {
    uint32_t total_weight_size;
    uint32_t total_internal_size;
    uint64_t total_dma_allocated_size;
    uint32_t total_sram_size;
    uint32_t free_sram_size;
    uint32_t reserved[10];
} rknn_mem_size;

typedef struct _rknn_custom_string {
    char string[1024];
} rknn_custom_string;

typedef enum _rknn_tensor_mem_flags {
    RKNN_TENSOR_MEMORY_FLAGS_ALLOC_INSIDE = 1,
    RKNN_TENSOR_MEMORY_FLAGS_FROM_FD = 2,
    RKNN_TENSOR_MEMORY_FLAGS_FROM_PHYS = 3,
    RKNN_TENSOR_MEMORY_FLAGS_UNKNOWN
} rknn_tensor_mem_flags;

typedef enum _rknn_mem_alloc_flags {
    RKNN_FLAG_MEMORY_FLAGS_DEFAULT = 0 << 0,
    RKNN_FLAG_MEMORY_CACHEABLE = 1 << 0,
    RKNN_FLAG_MEMORY_NON_CACHEABLE = 1 << 1,
} rknn_mem_alloc_flags;

typedef enum _rknn_mem_sync_mode {
    RKNN_MEMORY_SYNC_TO_DEVICE = 0x1,
    RKNN_MEMORY_SYNC_FROM_DEVICE = 0x2,
    RKNN_MEMORY_SYNC_BIDIRECTIONAL = RKNN_MEMORY_SYNC_TO_DEVICE | RKNN_MEMORY_SYNC_FROM_DEVICE,
} rknn_mem_sync_mode;

typedef struct _rknn_tensor_memory {
    void* virt_addr;
    uint64_t phys_addr;
    int32_t fd;
    int32_t offset;
    uint32_t size;
    uint32_t flags;
    void* priv_data;
} rknn_tensor_mem;

typedef struct _rknn_input {
    uint32_t index;
    void* buf;
    uint32_t size;
    uint8_t pass_through;
    rknn_tensor_type type;
    rknn_tensor_format fmt;
} rknn_input;

typedef struct _rknn_output {
    uint8_t want_float;
    uint8_t is_prealloc;
    uint32_t index;
    void* buf;
    uint32_t size;
} rknn_output;

typedef struct _rknn_init_extend {
    rknn_context ctx;
    int32_t real_model_offset;
    uint32_t real_model_size;
    int32_t model_buffer_fd;
    uint32_t model_buffer_flags;
    uint8_t reserved[112];
} rknn_init_extend;

typedef struct _rknn_run_extend {
    uint64_t frame_id;
    int32_t non_block;
    int32_t timeout_ms;
    int32_t fence_fd;
} rknn_run_extend;

typedef struct _rknn_output_extend {
    uint64_t frame_id;
} rknn_output_extend;

int rknn_init(rknn_context* context, void* model, uint32_t size, uint32_t flag, rknn_init_extend* extend);
int rknn_dup_context(rknn_context* context_in, rknn_context* context_out);
int rknn_destroy(rknn_context context);
int rknn_query(rknn_context context, rknn_query_cmd cmd, void* info, uint32_t size);
int rknn_inputs_set(rknn_context context, uint32_t n_inputs, rknn_input inputs[]);
int rknn_set_batch_core_num(rknn_context context, int core_num);
int rknn_set_core_mask(rknn_context context, rknn_core_mask core_mask);
int rknn_run(rknn_context context, rknn_run_extend* extend);
int rknn_wait(rknn_context context, rknn_run_extend* extend);
int rknn_outputs_get(rknn_context context, uint32_t n_outputs, rknn_output outputs[], rknn_output_extend* extend);
int rknn_outputs_release(rknn_context context, uint32_t n_ouputs, rknn_output outputs[]);
rknn_tensor_mem* rknn_create_mem_from_phys(rknn_context ctx, uint64_t phys_addr, void* virt_addr, uint32_t size);
rknn_tensor_mem* rknn_create_mem_from_fd(rknn_context ctx, int32_t fd, void* virt_addr, uint32_t size, int32_t offset);
rknn_tensor_mem* rknn_create_mem_from_mb_blk(rknn_context ctx, void* mb_blk, int32_t offset);
rknn_tensor_mem* rknn_create_mem(rknn_context ctx, uint32_t size);
rknn_tensor_mem* rknn_create_mem2(rknn_context ctx, uint64_t size, uint64_t alloc_flags);
int rknn_destroy_mem(rknn_context ctx, rknn_tensor_mem* mem);
int rknn_set_weight_mem(rknn_context ctx, rknn_tensor_mem* mem);
int rknn_set_internal_mem(rknn_context ctx, rknn_tensor_mem* mem);
int rknn_set_io_mem(rknn_context ctx, rknn_tensor_mem* mem, rknn_tensor_attr* attr);
int rknn_set_input_shape(rknn_context ctx, rknn_tensor_attr* attr);
int rknn_mem_sync(rknn_context context, rknn_tensor_mem* mem, rknn_mem_sync_mode mode);

#ifdef __cplusplus
}
#endif

#endif
