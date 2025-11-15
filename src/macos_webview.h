#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __APPLE__

// Default-Fenster (kompatibel zu bisheriger Nutzung)
void macos_open_webview_window(const char *url,
                               const char *title,
                               int x,
                               int y,
                               int width,
                               int height);

// Mehrere Fenster, identifiziert über eine ID
void macos_open_webview_window_with_id(const char *id,
                                       const char *url,
                                       const char *title,
                                       int x,
                                       int y,
                                       int width,
                                       int height);

#else

static inline void macos_open_webview_window(const char *url,
    const char *title,
    int x,
    int y,
    int width,
    int height) {
  (void)url;
  (void)title;
  (void)x;
  (void)y;
  (void)width;
  (void)height;
}

static inline void macos_open_webview_window_with_id(const char *id,
    const char *url,
    const char *title,
    int x,
    int y,
    int width,
    int height) {
  (void)id;
  (void)url;
  (void)title;
  (void)x;
  (void)y;
  (void)width;
  (void)height;
}

#endif

#ifdef __cplusplus
}
#endif
