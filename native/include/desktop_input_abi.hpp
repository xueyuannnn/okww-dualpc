#pragma once
#include <cstdint>
#include <cstddef>
namespace okww::rust {
extern "C" int32_t okww_rs_desktop_recognition(const uint8_t *, size_t);
extern "C" int32_t okww_rs_desktop_hid(int32_t, int32_t, uint32_t *);
extern "C" int32_t okww_rs_desktop_absolute(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t,
                                            int32_t *);
} // namespace okww::rust
