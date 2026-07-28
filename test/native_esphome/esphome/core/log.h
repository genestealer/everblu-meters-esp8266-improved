/**
 * @file log.h
 * @brief Minimal stand-in for esphome/core/log.h for host builds
 *
 * Only used by the PlatformIO `native_esphome` environment. It exists so the
 * USE_ESPHOME code paths in src/ can be compiled and tested on a desktop host
 * without checking out the ESPHome source tree.
 *
 * Log output is discarded unless EVERBLU_NATIVE_SERIAL is set, matching the
 * behaviour of the Serial shim used by the MQTT-mode host tests.
 */

#ifndef EVERBLU_NATIVE_ESPHOME_LOG_H
#define EVERBLU_NATIVE_ESPHOME_LOG_H

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace esphome
{
    inline bool native_log_enabled()
    {
        static const bool on = (std::getenv("EVERBLU_NATIVE_SERIAL") != nullptr);
        return on;
    }

    inline void native_log(const char *level, const char *tag, const char *fmt, ...)
    {
        if (!native_log_enabled())
        {
            return;
        }
        std::printf("[%s][%s]: ", level, tag ? tag : "?");
        va_list args;
        va_start(args, fmt);
        std::vfprintf(stdout, fmt, args);
        va_end(args);
        std::printf("\n");
    }
}

#define ESP_LOGV(tag, ...) ::esphome::native_log("V", tag, __VA_ARGS__)
#define ESP_LOGD(tag, ...) ::esphome::native_log("D", tag, __VA_ARGS__)
#define ESP_LOGI(tag, ...) ::esphome::native_log("I", tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) ::esphome::native_log("W", tag, __VA_ARGS__)
#define ESP_LOGE(tag, ...) ::esphome::native_log("E", tag, __VA_ARGS__)

#endif // EVERBLU_NATIVE_ESPHOME_LOG_H
