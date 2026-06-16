#include "rknn_runtime.hpp"

#include <dlfcn.h>

#include <stdexcept>
#include <string>

namespace signlang::handpose_det {
  namespace {

    auto dlopen_path(const std::string& library_path) -> void* {
      const auto* path = library_path.empty() ? "librknnrt.so" : library_path.c_str();
      auto* handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
      if (handle == nullptr) {
        throw std::runtime_error(std::string("Failed to load RKNN runtime library ") + path + ": " + dlerror());
      }

      return handle;
    }

  } // namespace

  RknnRuntime::RknnRuntime(std::string library_path) :
      library_handle_{dlopen_path(library_path)}, rknn_init_{load_symbol<decltype(rknn_init_)>("rknn_init")},
      rknn_destroy_{load_symbol<decltype(rknn_destroy_)>("rknn_destroy")},
      rknn_query_{load_symbol<decltype(rknn_query_)>("rknn_query")},
      rknn_set_core_mask_{load_symbol<decltype(rknn_set_core_mask_)>("rknn_set_core_mask")},
      rknn_run_{load_symbol<decltype(rknn_run_)>("rknn_run")},
      rknn_create_mem_{load_symbol<decltype(rknn_create_mem_)>("rknn_create_mem")},
      rknn_destroy_mem_{load_symbol<decltype(rknn_destroy_mem_)>("rknn_destroy_mem")},
      rknn_set_io_mem_{load_symbol<decltype(rknn_set_io_mem_)>("rknn_set_io_mem")},
      rknn_mem_sync_{load_symbol<decltype(rknn_mem_sync_)>("rknn_mem_sync")} {}

  RknnRuntime::~RknnRuntime() {
    if (library_handle_ != nullptr) {
      dlclose(library_handle_);
      library_handle_ = nullptr;
    }
  }

  auto RknnRuntime::init(rknn_context* context, void* model, std::uint32_t size, std::uint32_t flags,
                         rknn_init_extend* extend) const -> int {
    return rknn_init_(context, model, size, flags, extend);
  }

  auto RknnRuntime::destroy(rknn_context context) const -> int { return rknn_destroy_(context); }

  auto RknnRuntime::query(rknn_context context, rknn_query_cmd command, void* info, std::uint32_t size) const -> int {
    return rknn_query_(context, command, info, size);
  }

  auto RknnRuntime::set_core_mask(rknn_context context, rknn_core_mask core_mask) const -> int {
    return rknn_set_core_mask_(context, core_mask);
  }

  auto RknnRuntime::run(rknn_context context, rknn_run_extend* extend) const -> int {
    return rknn_run_(context, extend);
  }

  auto RknnRuntime::create_mem(rknn_context context, std::uint32_t size) const -> rknn_tensor_mem* {
    return rknn_create_mem_(context, size);
  }

  auto RknnRuntime::destroy_mem(rknn_context context, rknn_tensor_mem* mem) const -> int {
    return rknn_destroy_mem_(context, mem);
  }

  auto RknnRuntime::set_io_mem(rknn_context context, rknn_tensor_mem* mem, rknn_tensor_attr* attr) const -> int {
    return rknn_set_io_mem_(context, mem, attr);
  }

  auto RknnRuntime::mem_sync(rknn_context context, rknn_tensor_mem* mem, rknn_mem_sync_mode mode) const -> int {
    return rknn_mem_sync_(context, mem, mode);
  }

  template <typename Function>
  auto RknnRuntime::load_symbol(const char* symbol_name) const -> Function {
    dlerror();
    auto* symbol = dlsym(library_handle_, symbol_name);
    const auto* error = dlerror();
    if (error != nullptr) {
      throw std::runtime_error(std::string("Failed to load RKNN runtime symbol ") + symbol_name + ": " + error);
    }

    return reinterpret_cast<Function>(symbol); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
  }

} // namespace signlang::handpose_det
