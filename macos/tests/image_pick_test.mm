// The skin picker's host side: any picture becomes a PNG of the same pixels, and the answer
// reaches the frame loop once, from whichever thread posted it.
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include "image_pick.h"
#include <cstdio>
#include <string>
#include <thread>
static int fails = 0;
#define EXPECT(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

// A 64x32 skin: opaque red at (0,0), fully transparent at (63,31), saved as `type` at `dpi`.
static bool write_skin(NSURL *url, CFStringRef type, double dpi) {
  CGColorSpaceRef rgb = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  CGContextRef c = CGBitmapContextCreate(nullptr, 64, 32, 8, 0, rgb, kCGImageAlphaPremultipliedLast);
  CGContextSetRGBFillColor(c, 0, 0, 1, 1);
  CGContextFillRect(c, CGRectMake(0, 0, 64, 32));
  CGContextClearRect(c, CGRectMake(63, 0, 1, 1));  // CG's origin is bottom-left: pixel (63,31)
  CGContextSetRGBFillColor(c, 1, 0, 0, 1);
  CGContextFillRect(c, CGRectMake(0, 31, 1, 1));   // pixel (0,0)
  CGImageRef image = CGBitmapContextCreateImage(c);
  CGImageDestinationRef dest = CGImageDestinationCreateWithURL((__bridge CFURLRef)url, type, 1, nullptr);
  NSDictionary *props = @{(id)kCGImagePropertyDPIWidth : @(dpi), (id)kCGImagePropertyDPIHeight : @(dpi)};
  CGImageDestinationAddImage(dest, image, (__bridge CFDictionaryRef)props);
  bool ok = CGImageDestinationFinalize(dest);
  CFRelease(dest);
  CGImageRelease(image);
  CGContextRelease(c);
  CGColorSpaceRelease(rgb);
  return ok;
}

struct Png {
  bool ok = false;
  size_t width = 0, height = 0;
  unsigned char first[4] = {}, last[4] = {};  // RGBA of (0,0) and (w-1,h-1)
};

static Png read_png(const std::string &path) {
  Png p;
  NSURL *url = [NSURL fileURLWithPath:@(path.c_str())];
  CGImageSourceRef src = CGImageSourceCreateWithURL((__bridge CFURLRef)url, nullptr);
  if (!src) return p;
  bool is_png = [(__bridge NSString *)CGImageSourceGetType(src) isEqualToString:UTTypePNG.identifier];
  CGImageRef image = CGImageSourceCreateImageAtIndex(src, 0, nullptr);
  CFRelease(src);
  if (!image) return p;
  p.width = CGImageGetWidth(image);
  p.height = CGImageGetHeight(image);
  std::string pixels(p.width * p.height * 4, '\0');
  CGColorSpaceRef rgb = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  CGContextRef c = CGBitmapContextCreate(&pixels[0], p.width, p.height, 8, p.width * 4, rgb, kCGImageAlphaPremultipliedLast);
  CGContextDrawImage(c, CGRectMake(0, 0, p.width, p.height), image);  // row 0 of the buffer is the top
  CGContextRelease(c);
  CGColorSpaceRelease(rgb);
  CGImageRelease(image);
  for (int i = 0; i < 4; i++) {
    p.first[i] = static_cast<unsigned char>(pixels[i]);
    p.last[i] = static_cast<unsigned char>(pixels[pixels.size() - 4 + i]);
  }
  p.ok = is_png;
  return p;
}

static bool is_skin(const Png &p) {
  return p.ok && p.width == 64 && p.height == 32 && p.first[0] == 255 && p.first[1] == 0 && p.first[2] == 0 &&
         p.first[3] == 255 && p.last[3] == 0;
}

int main() {
  using namespace mcfm::launcher;
  @autoreleasepool {
    NSString *dir = [NSTemporaryDirectory() stringByAppendingPathComponent:[NSUUID UUID].UUIDString];
    [NSFileManager.defaultManager createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
    std::string out = std::string(dir.UTF8String) + "/tmp/newSkin.png";  // tmp/ does not exist yet

    // A 144-dpi TIFF keeps its pixels (not halved to its size in points) and its transparency.
    NSURL *tiff = [NSURL fileURLWithPath:[dir stringByAppendingPathComponent:@"skin.tiff"]];
    EXPECT(write_skin(tiff, (__bridge CFStringRef)UTTypeTIFF.identifier, 144));
    EXPECT(write_png(tiff.path.UTF8String, out));
    EXPECT(is_skin(read_png(out)));

    // A PNG is re-encoded too, replacing the previous pick.
    NSURL *png = [NSURL fileURLWithPath:[dir stringByAppendingPathComponent:@"skin.png"]];
    EXPECT(write_skin(png, (__bridge CFStringRef)UTTypePNG.identifier, 72));
    EXPECT(write_png(png.path.UTF8String, out));
    EXPECT(is_skin(read_png(out)));

    // Not a picture: false, and no half-written file is left behind.
    NSString *text = [dir stringByAppendingPathComponent:@"notes.png"];
    [@"not an image" writeToFile:text atomically:YES encoding:NSUTF8StringEncoding error:nil];
    std::string out2 = std::string(dir.UTF8String) + "/other.png";
    EXPECT(!write_png(text.UTF8String, out2));
    EXPECT(![NSFileManager.defaultManager fileExistsAtPath:@(out2.c_str())]);
    EXPECT(!write_png((std::string(dir.UTF8String) + "/missing.jpg"), out2));

    [NSFileManager.defaultManager removeItemAtPath:dir error:nil];
  }

  // The mailbox: one answer per post, "" meaning cancelled.
  PickMailbox box;
  std::string path = "unchanged";
  EXPECT(!box.take(&path) && path == "unchanged");
  box.post("/tmp/newSkin.png");
  EXPECT(box.take(&path) && path == "/tmp/newSkin.png");
  EXPECT(!box.take(&path));
  box.post("");
  EXPECT(box.take(&path) && path.empty());
  // Posted from the panel's queue, taken on the frame loop's thread.
  std::thread t([&box] { box.post("/from/another/thread.png"); });
  t.join();
  EXPECT(box.take(&path) && path == "/from/another/thread.png");

  if (fails) return 1;
  std::printf("image_pick_test: ok\n");
  return 0;
}
