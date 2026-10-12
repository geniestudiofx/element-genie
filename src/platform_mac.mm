// Element Genie - macOS helpers: file dialogs, installed fonts, Finder, folders
#import <Cocoa/Cocoa.h>
#import <CoreText/CoreText.h>
#include <mach-o/dyld.h>
#include <sys/stat.h>
#include <dirent.h>
#include <algorithm>
#include <strings.h>
#include "platform.h"

static NSString* ns(const std::string& s) { return [NSString stringWithUTF8String:s.c_str()] ?: @""; }
static std::string u8(NSString* s) { return s ? std::string([s UTF8String]) : std::string(); }

// "Models|*.glb;*.gltf|All files|*.*" -> {"glb","gltf"} (empty = anything)
static NSArray<NSString*>* extsFromFilter(const char* filter) {
    NSMutableArray* out = [NSMutableArray array];
    std::string f = filter ? filter : "";
    std::vector<std::string> parts; size_t a = 0;
    for (size_t i = 0; i <= f.size(); i++) if (i == f.size() || f[i] == '|') { parts.push_back(f.substr(a, i - a)); a = i + 1; }
    for (size_t i = 1; i < parts.size(); i += 2) {
        const std::string& pat = parts[i]; size_t b = 0;
        for (size_t k = 0; k <= pat.size(); k++) if (k == pat.size() || pat[k] == ';') {
            std::string p = pat.substr(b, k - b); b = k + 1;
            if (p == "*.*" || p == "*") return @[];
            size_t dot = p.find_last_of('.');
            if (dot != std::string::npos) { NSString* e = ns(p.substr(dot + 1)); if (![out containsObject:e]) [out addObject:e]; }
        }
        break;  // first filter group wins (the "All files" group comes later)
    }
    return out;
}

std::string openFileDialog(const char* title, const char* filter, void*) {
    @autoreleasepool {
        NSOpenPanel* p = [NSOpenPanel openPanel];
        p.message = ns(title ? title : "");
        p.canChooseFiles = YES; p.canChooseDirectories = NO; p.allowsMultipleSelection = NO;
        NSArray* ex = extsFromFilter(filter);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        if (ex.count) p.allowedFileTypes = ex;
#pragma clang diagnostic pop
        if ([p runModal] != NSModalResponseOK) return "";
        return u8(p.URL.path);
    }
}

std::string saveFileDialog(const char* title, const char* filter, const char* defExt, const std::string& defName, void*) {
    @autoreleasepool {
        NSSavePanel* p = [NSSavePanel savePanel];
        p.message = ns(title ? title : "");
        p.nameFieldStringValue = ns(defName);
        NSArray* ex = extsFromFilter(filter);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        if (ex.count) p.allowedFileTypes = ex;
        else if (defExt && *defExt) p.allowedFileTypes = @[ ns(defExt) ];
#pragma clang diagnostic pop
        if ([p runModal] != NSModalResponseOK) return "";
        return u8(p.URL.path);
    }
}

const std::vector<FontEntry>& systemFonts() {
    static std::vector<FontEntry> list; static bool done = false;
    if (done) return list;
    done = true;
    @autoreleasepool {
        CTFontCollectionRef col = CTFontCollectionCreateFromAvailableFonts(nullptr);
        CFArrayRef descs = col ? CTFontCollectionCreateMatchingFontDescriptors(col) : nullptr;
        NSMutableSet* seenFiles = [NSMutableSet set];
        if (descs) {
            for (CFIndex i = 0; i < CFArrayGetCount(descs); i++) {
                CTFontDescriptorRef d = (CTFontDescriptorRef)CFArrayGetValueAtIndex(descs, i);
                NSURL* url = CFBridgingRelease(CTFontDescriptorCopyAttribute(d, kCTFontURLAttribute));
                NSString* name = CFBridgingRelease(CTFontDescriptorCopyAttribute(d, kCTFontDisplayNameAttribute));
                if (!url || !name) continue;
                NSString* path = url.path; NSString* ext = path.pathExtension.lowercaseString;
                if (!([ext isEqualToString:@"ttf"] || [ext isEqualToString:@"otf"] || [ext isEqualToString:@"ttc"])) continue;
                // collections: we can only read the first face, so list each file once
                if ([ext isEqualToString:@"ttc"]) { if ([seenFiles containsObject:path]) continue; [seenFiles addObject:path]; }
                if ([name hasPrefix:@"."]) continue;  // hidden system UI fonts
                list.push_back({u8(name), u8(path)});
            }
            CFRelease(descs);
        }
        if (col) CFRelease(col);
    }
    std::sort(list.begin(), list.end(), [](const FontEntry& a, const FontEntry& b) { return strcasecmp(a.name.c_str(), b.name.c_str()) < 0; });
    list.erase(std::unique(list.begin(), list.end(), [](const FontEntry& a, const FontEntry& b) { return a.name == b.name; }), list.end());
    return list;
}

void revealInExplorer(const std::string& path) {
    @autoreleasepool { [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs:@[ [NSURL fileURLWithPath:ns(path)] ]]; }
}
void openUrl(const std::string& url) {
    @autoreleasepool { NSURL* u = [NSURL URLWithString:ns(url)]; if (u) [[NSWorkspace sharedWorkspace] openURL:u]; }
}
std::string exeDir() {
    char buf[4096]; uint32_t n = sizeof buf;
    if (_NSGetExecutablePath(buf, &n) != 0) return "./";
    char real[PATH_MAX]; std::string s = realpath(buf, real) ? real : buf;
    return s.substr(0, s.find_last_of('/') + 1);
}
std::string resDir() {
    @autoreleasepool {
        NSString* r = [[NSBundle mainBundle] resourcePath];
        return r ? u8(r) + "/" : exeDir();
    }
}
std::string appDataDir() {
    @autoreleasepool {
        NSArray* a = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES);
        std::string d = (a.count ? u8(a[0]) : std::string(getenv("HOME") ?: ".")) + "/Element Genie";
        mkdir(d.c_str(), 0755);
        return d + "/";
    }
}
FILE* fopenU8(const std::string& path, const char* mode) { return fopen(path.c_str(), mode); }
std::vector<std::string> listFiles(const std::string& dir) {
    std::vector<std::string> r; DIR* d = opendir(dir.c_str()); if (!d) return r;
    while (dirent* e = readdir(d)) {
        struct stat st;
        if (e->d_name[0] != '.' && stat((dir + e->d_name).c_str(), &st) == 0 && S_ISREG(st.st_mode)) r.push_back(e->d_name);
    }
    closedir(d);
    return r;
}
bool makeDir(const std::string& dir) { mkdir(dir.c_str(), 0755); return true; }
bool copyFileU8(const std::string& from, const std::string& to) {
    @autoreleasepool {
        NSFileManager* fm = [NSFileManager defaultManager];
        [fm removeItemAtPath:ns(to) error:nil];
        return [fm copyItemAtPath:ns(from) toPath:ns(to) error:nil];
    }
}
void showError(const std::string& msg) {
    @autoreleasepool {
        NSAlert* a = [[NSAlert alloc] init];
        a.messageText = @"Element Genie"; a.informativeText = ns(msg); a.alertStyle = NSAlertStyleCritical;
        [a runModal];
    }
}
void bringAppToFront(void*) {
    @autoreleasepool {
        [NSApp activateIgnoringOtherApps:YES];
    }
}
