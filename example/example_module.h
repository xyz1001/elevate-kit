#pragma once

#include <nlohmann/json.hpp>

#if defined(_WIN32) || defined(__CYGWIN__)
#ifdef EXAMPLE_MODULE_EXPORTS
#define EXAMPLE_MODULE_API __declspec(dllexport)
#else
#define EXAMPLE_MODULE_API __declspec(dllimport)
#endif
#elif defined(__GNUC__)
#define EXAMPLE_MODULE_API __attribute__((visibility("default")))
#else
#define EXAMPLE_MODULE_API
#endif

EXAMPLE_MODULE_API size_t CalcFileSize(const std::string &path);
