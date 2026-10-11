// The picked picture as the PNG the engine reads (image_pick.h).
#import <AppKit/AppKit.h>
#import <ImageIO/ImageIO.h>

#include "image_pick.h"

namespace mcfm {
namespace launcher {

bool write_png(const std::string &source_path, const std::string &png_path) {
  @autoreleasepool {
    // ImageIO, not NSImage: NSImage sizes a 144-dpi picture in points and would halve a skin.
    NSURL *url = [NSURL fileURLWithPath:@(source_path.c_str())];
    CGImageSourceRef src = CGImageSourceCreateWithURL((__bridge CFURLRef)url, nullptr);
    if (!src) return false;
    CGImageRef image = CGImageSourceCreateImageAtIndex(src, 0, nullptr);
    CFRelease(src);
    if (!image) return false;
    NSBitmapImageRep *rep = [[NSBitmapImageRep alloc] initWithCGImage:image];
    CGImageRelease(image);
    NSData *png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    if (!png) return false;
    NSString *out = @(png_path.c_str());
    [NSFileManager.defaultManager createDirectoryAtPath:out.stringByDeletingLastPathComponent
                            withIntermediateDirectories:YES attributes:nil error:nil];
    return [png writeToFile:out atomically:YES];
  }
}

}  // namespace launcher
}  // namespace mcfm
