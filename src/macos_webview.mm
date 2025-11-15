#ifdef __APPLE__

#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>
#import <dispatch/dispatch.h>

#include "macos_webview.h"

// Dictionary: Fenster pro ID
static NSMutableDictionary<NSString *, NSWindow *> *g_webview_windows = nil;

// interne Implementierung – MUSS auf dem Main-Thread laufen
static void macos_open_webview_window_impl(NSString *idStr,
                                           NSString *urlStr,
                                           NSString *titleStr,
                                           int x,
                                           int y,
                                           int width,
                                           int height)
{
    if (!g_webview_windows) {
        g_webview_windows = [[NSMutableDictionary alloc] init];
    }

    NSApplication *app = [NSApplication sharedApplication];

    // 1. Prüfen, ob Fenster für diese ID bereits existiert
    NSWindow *win = [g_webview_windows objectForKey:idStr];
    if (win) {
        // vorhandenes Fenster repositionieren und nach vorn holen
        [win setFrameOrigin:NSMakePoint(x, y)];
        [win makeKeyAndOrderFront:nil];
        [app activateIgnoringOtherApps:YES];
        return;
    }

    // 2. Neues Fenster erzeugen
    NSString *title = titleStr ?: @"WebView";

    NSRect frame = NSMakeRect(x,
                              y,
                              width  > 0 ? width  : 800,
                              height > 0 ? height : 600);

    NSWindowStyleMask style =
        NSWindowStyleMaskTitled   |
        NSWindowStyleMaskClosable |
        NSWindowStyleMaskResizable;

    win = [[NSWindow alloc] initWithContentRect:frame
                                      styleMask:style
                                        backing:NSBackingStoreBuffered
                                          defer:NO];

    [win setTitle:title];

    // WebView erzeugen
    NSView *contentView = [win contentView];
    NSRect contentBounds = [contentView bounds];

    WKWebViewConfiguration *config = [[WKWebViewConfiguration alloc] init];
    WKWebView *wv = [[WKWebView alloc] initWithFrame:contentBounds
                                       configuration:config];

    [wv setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
    [contentView addSubview:wv];

    if (urlStr && [urlStr length] > 0) {
        NSURL *url = [NSURL URLWithString:urlStr];
        if (url) {
            NSURLRequest *req = [NSURLRequest requestWithURL:url];
            [wv loadRequest:req];
        }
    }

    // 3. Fenster in Dictionary eintragen
    [g_webview_windows setObject:win forKey:idStr];

    // 4. Beim Schließen: Eintrag entfernen
    [[NSNotificationCenter defaultCenter]
        addObserverForName:NSWindowWillCloseNotification
                    object:win
                     queue:nil
                usingBlock:^(NSNotification * _Nonnull note) {
                    [g_webview_windows removeObjectForKey:idStr];
                }];

    // 5. Fenster anzeigen
    [win makeKeyAndOrderFront:nil];
    [app activateIgnoringOtherApps:YES];
}

void macos_open_webview_window_with_id(const char *id_cstr,
                                       const char *url_cstr,
                                       const char *title_cstr,
                                       int x,
                                       int y,
                                       int width,
                                       int height)
{
    @autoreleasepool {
        NSString *idStr =
            (id_cstr && id_cstr[0])   ? [NSString stringWithUTF8String:id_cstr]   : @"default";

        NSString *urlStr =
            (url_cstr && url_cstr[0]) ? [NSString stringWithUTF8String:url_cstr]  : nil;

        NSString *titleStr =
            (title_cstr && title_cstr[0]) ? [NSString stringWithUTF8String:title_cstr] : nil;

        if (![NSThread isMainThread]) {
            dispatch_async(dispatch_get_main_queue(), ^{
                macos_open_webview_window_impl(idStr, urlStr, titleStr,
                                               x, y, width, height);
            });
        } else {
            macos_open_webview_window_impl(idStr, urlStr, titleStr,
                                           x, y, width, height);
        }
    }
}

// Kompatibilitäts-Funktion: nutzt eine Default-ID "default"
void macos_open_webview_window(const char *url_cstr,
                               const char *title_cstr,
                               int x,
                               int y,
                               int width,
                               int height)
{
    macos_open_webview_window_with_id("default",
                                      url_cstr,
                                      title_cstr,
                                      x,
                                      y,
                                      width,
                                      height);
}

#endif // __APPLE__
