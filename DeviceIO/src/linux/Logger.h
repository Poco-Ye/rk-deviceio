/*
 * Logger.h
 *
 * Copyright 2017 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 *     http://aws.amazon.com/apache2.0/
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

#ifndef DEVICE_COMMON_LIB_LOGGER_UTILS_INCLUDE_LOGGER_H_
#define DEVICE_COMMON_LIB_LOGGER_UTILS_INCLUDE_LOGGER_H_

#include <atomic>
#include <chrono>
#include <mutex>
#include <sstream>
#include <vector>
#include <cstring>

#include "Level.h"

#include <time.h>
inline unsigned long GetTickCount()
{
     struct timespec ts;

     clock_gettime(CLOCK_MONOTONIC, &ts);

     return (ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

inline void PRINT_LOG_WITH_TICK(char *logBuffer) {
    printf("<%lu ms> %s %d %s %s\n", GetTickCount(), __FILE__, __LINE__, __FUNCTION__, logBuffer);
}

#define _2K   2048
extern char logBuffer[_2K];
extern std::mutex g_logBufferMutex;

#define FORMAT_LOG(room,level,formate, ...)                               \
    do {                                                                  \
            std::lock_guard<std::mutex> lock(g_logBufferMutex);           \
            memset(logBuffer, 0, sizeof(logBuffer));                      \
            snprintf(logBuffer, sizeof(logBuffer),formate,##__VA_ARGS__); \
            PRINT_LOG_WITH_TICK(logBuffer);                                \
        } while(0);

/**
 * Send APP log line.
 *
 * @param logger LEVEL
 * @param entry The text (or builder of the text) for the log entry.
 */
#define APP_DEBUG(formate, ...)  FORMAT_LOG("APP",deviceCommonLib::logger::Level::DEBUG0, formate, ##__VA_ARGS__)

/**
 * Send a INFO severity log line.
 *
 * @param loggerArg The Logger to send the line to.
 * @param entry The text (or builder of the text) for the log entry.
 */
#define APP_INFO(formate, ...)  FORMAT_LOG("APP",deviceCommonLib::logger::Level::INFO, formate, ##__VA_ARGS__)

/**
 * Send a WARN severity log line.
 *
 * @param loggerArg The Logger to send the line to.
 * @param entry The text (or builder of the text) for the log entry.
 */
#define APP_WARN(formate, ...)  FORMAT_LOG("APP",deviceCommonLib::logger::Level::WARN, formate, ##__VA_ARGS__)

/**
 * Send a ERROR severity log line.
 *
 * @param loggerArg The Logger to send the line to.
 * @param entry The text (or builder of the text) for the log entry.
 */
#define APP_ERROR(formate, ...)  FORMAT_LOG("APP",deviceCommonLib::logger::Level::ERROR, formate, ##__VA_ARGS__)

/**
 * Send a CRITICAL severity log line.
 *
 * @param loggerArg The Logger to send the line to.
 * @param entry The text (or builder of the text) for the log entry.
 */
#define APP_CRITICAL(formate, ...) FORMAT_LOG("APP",deviceCommonLib::logger::Level::CRITICAL, formate, ##__VA_ARGS__)

/**
 * Send a INFO severity log line.
 *
 * @param loggerArg The Logger to send the line to.
 * @param entry The text (or builder of the text) for the log entry.
 */
#define LINK_INFO(formate, ...)  FORMAT_LOG("LINK",deviceCommonLib::logger::Level::INFO, formate, ##__VA_ARGS__)

/**
 * Send a WARN severity log line.
 *
 * @param loggerArg The Logger to send the line to.
 * @param entry The text (or builder of the text) for the log entry.
 */
#define LINK_WARN(formate, ...)  FORMAT_LOG("LINK",deviceCommonLib::logger::Level::WARN, formate, ##__VA_ARGS__)

/**
 * Send a ERROR severity log line.
 *
 * @param loggerArg The Logger to send the line to.
 * @param entry The text (or builder of the text) for the log entry.
 */
#define LINK_ERROR(formate, ...)  FORMAT_LOG("LINK",deviceCommonLib::logger::Level::ERROR, formate, ##__VA_ARGS__)

/**
 * Send a CRITICAL severity log line.
 *
 * @param loggerArg The Logger to send the line to.
 * @param entry The text (or builder of the text) for the log entry.
 */
#define LINK_CRITICAL(formate, ...) FORMAT_LOG("LINK",deviceCommonLib::logger::Level::CRITICAL, formate, ##__VA_ARGS__)

#endif  // DEVICE_COMMON_LIB_LOGGER_UTILS_INCLUDE_LOGGER_H_
