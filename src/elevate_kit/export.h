#pragma once

// clang-format off
// Generic helper definitions for shared library support
#if defined _WIN32 || defined __CYGWIN__
  #define ELEVATE_KIT_HELPER_DLL_IMPORT __declspec(dllimport)
  #define ELEVATE_KIT_HELPER_DLL_EXPORT __declspec(dllexport)
  #define ELEVATE_KIT_HELPER_DLL_LOCAL
#else
  #if __GNUC__ >= 4
    #define ELEVATE_KIT_HELPER_DLL_IMPORT __attribute__ ((visibility ("default")))
    #define ELEVATE_KIT_HELPER_DLL_EXPORT __attribute__ ((visibility ("default")))
    #define ELEVATE_KIT_HELPER_DLL_LOCAL  __attribute__ ((visibility ("hidden")))
  #else
    #define ELEVATE_KIT_HELPER_DLL_IMPORT
    #define ELEVATE_KIT_HELPER_DLL_EXPORT
    #define ELEVATE_KIT_HELPER_DLL_LOCAL
  #endif
#endif

// Now we use the generic helper definitions above to define ELEVATE_KIT_API and ELEVATE_KIT_LOCAL.
// ELEVATE_KIT_API is used for the public API symbols. It either DLL imports or DLL exports (or does nothing for static build)
// ELEVATE_KIT_LOCAL is used for non-api symbols.

#ifdef ELEVATE_KIT_DLL // defined if ELEVATE_KIT is compiled as a DLL
  #ifdef ELEVATE_KIT_DLL_EXPORTS // defined if we are building the ELEVATE_KIT DLL (instead of using it)
    #define ELEVATE_KIT_API ELEVATE_KIT_HELPER_DLL_EXPORT
  #else
    #define ELEVATE_KIT_API ELEVATE_KIT_HELPER_DLL_IMPORT
  #endif // ELEVATE_KIT_DLL_EXPORTS
  #define ELEVATE_KIT_LOCAL ELEVATE_KIT_HELPER_DLL_LOCAL
#else // ELEVATE_KIT_DLL is not defined: this means ELEVATE_KIT is a static lib.
  #define ELEVATE_KIT_API
  #define ELEVATE_KIT_LOCAL
#endif // ELEVATE_KIT_DLL
// clang-format on
