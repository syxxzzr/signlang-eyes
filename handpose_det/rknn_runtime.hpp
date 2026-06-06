#ifndef SIGNLANG_EYES_EDGEAI_HANDPOSE_DET_RKNN_RUNTIME_HPP
#define SIGNLANG_EYES_EDGEAI_HANDPOSE_DET_RKNN_RUNTIME_HPP

#include "rknn/include/rknn_api.h"

#include <cstdint>
#include <string>

namespace signlang::handpose_det {

  class RknnRuntime {
  public:
    explicit RknnRuntime(std::string library_path);
    ~RknnRuntime();

    RknnRuntime(const RknnRuntime&) = delete;
    auto operator=(const RknnRuntime&) -> RknnRuntime& = delete;
    RknnRuntime(RknnRuntime&&) = delete;
    auto operator=(RknnRuntime&&) -> RknnRuntime& = delete;

    auto init(rknn_context* context, void* model, std::uint32_t size, std::uint32_t flags,
              rknn_init_extend* extend) const -> int;
    auto destroy(rknn_context context) const -> int;
    auto query(rknn_context context, rknn_query_cmd command, void* info, std::uint32_t size) const -> int;
    auto set_core_mask(rknn_context context, rknn_core_mask core_mask) const -> int;
    auto run(rknn_context context, rknn_run_extend* extend) const -> int;
    auto create_mem(rknn_context context, std::uint32_t size) const -> rknn_tensor_mem*;
    auto destroy_mem(rknn_context context, rknn_tensor_mem* mem) const -> int;
    auto set_io_mem(rknn_context context, rknn_tensor_mem* mem, rknn_tensor_attr* attr) const -> int;
    auto mem_sync(rknn_context context, rknn_tensor_mem* mem, rknn_mem_sync_mode mode) const -> int;

  private:
    template <typename Function>
    auto load_symbol(const char* symbol_name) const -> Function;

    void* library_handle_;

    int (*rknn_init_)(rknn_context*, void*, std::uint32_t, std::uint32_t, rknn_init_extend*);
    int (*rknn_destroy_)(rknn_context);
    int (*rknn_query_)(rknn_context, rknn_query_cmd, void*, std::uint32_t);
    int (*rknn_set_core_mask_)(rknn_context, rknn_core_mask);
    int (*rknn_run_)(rknn_context, rknn_run_extend*);
    rknn_tensor_mem* (*rknn_create_mem_)(rknn_context, std::uint32_t);
    int (*rknn_destroy_mem_)(rknn_context, rknn_tensor_mem*);
    int (*rknn_set_io_mem_)(rknn_context, rknn_tensor_mem*, rknn_tensor_attr*);
    int (*rknn_mem_sync_)(rknn_context, rknn_tensor_mem*, rknn_mem_sync_mode);
  };

} // namespace signlang::handpose_det

#endif // SIGNLANG_EYES_EDGEAI_HANDPOSE_DET_RKNN_RUNTIME_HPP
