# libc++ filesystem for Android NDK r21

Unmodified files from LLVM `llvmorg-9.0.1`, `libcxx/`, used only when explicitly selected through `WENKU8_ANDROID_FILESYSTEM_SOURCE_DIR`. NDK r21's headers contain `std::filesystem`, while its shared C++ runtime omits the implementation. These sources supply that implementation without replacing Qt's runtime. They are not compiled in desktop or newer-NDK builds by default.

Upstream: https://github.com/llvm/llvm-project/tree/llvmorg-9.0.1/libcxx

License: Apache-2.0 WITH LLVM-exception, including its GPL compatibility exception; original notices and full LICENSE.TXT retained. `SHA256SUMS.json` records the files retrieved. Android ARM64 linking has been checked; device/runtime filesystem behavior remains to be tested.
