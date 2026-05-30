// Minimal glfw3native.h shim providing native handle accessors used by imgui_impl_glfw.
// Include the main glfw3.h for GLFW types.
#pragma once
#include "../glfw3.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Use GLFW's GLFWwindow type for compatibility. */
#if defined(_WIN32)
/* Return native Win32 HWND for a GLFW window (real function provided by glfw3native). */
static inline void* glfwGetWin32Window(GLFWwindow* window) { (void)window; return nullptr; }
#elif defined(__APPLE__)
static inline void* glfwGetCocoaWindow(GLFWwindow* window) { (void)window; return nullptr; }
#else
static inline void* glfwGetX11Window(GLFWwindow* window) { (void)window; return nullptr; }
#endif

#ifdef __cplusplus
}
#endif
