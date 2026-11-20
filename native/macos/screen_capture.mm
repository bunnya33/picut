#import <AppKit/AppKit.h>
#import <AVFoundation/AVFoundation.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#include <QtCore/QElapsedTimer>
#include <QtCore/QDebug>
#include <QtGui/QCursor>
#include <QtGui/QGuiApplication>
#include <QtGui/QScreen>
#include <mutex>

#include "capture.h"
#include "platform.h"
#include "recording_capture.h"
#include "screen_capture.h"

namespace {
QString message(NSError *error) {
  return error ? QString::fromNSString(error.localizedDescription) : QString();
}

bool waitFor(dispatch_semaphore_t signal) {
  return dispatch_semaphore_wait(signal, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC)) == 0;
}

SCShareableContent *shareableContent() {
  auto signal = dispatch_semaphore_create(0);
  __block SCShareableContent *content = nil;
  __block NSError *failure = nil;
  [SCShareableContent getShareableContentExcludingDesktopWindows:NO
       onScreenWindowsOnly:NO completionHandler:^(SCShareableContent *result, NSError *error) {
    content = result;
    failure = error;
    dispatch_semaphore_signal(signal);
  }];
  if (!waitFor(signal)) throw std::runtime_error("读取 macOS 屏幕源超时。");
  if (failure) throw std::runtime_error(message(failure).toStdString());
  return content;
}

QRect rectangle(CGRect bounds) {
  return {qRound(bounds.origin.x), qRound(bounds.origin.y),
          qRound(bounds.size.width), qRound(bounds.size.height)};
}

CGRect cgrect(QRect rect) {
  return CGRectMake(rect.x(), rect.y(), rect.width(), rect.height());
}

SCDisplay *displayFor(SCShareableContent *content, QRect region) {
  for (SCDisplay *display in content.displays)
    if (rectangle(display.frame).contains(region)) return display;
  return nil;
}

SCContentFilter *displayFilter(SCShareableContent *content, SCDisplay *display,
                               bool excludePicut) {
  if (excludePicut) {
    for (SCRunningApplication *app in content.applications)
      if (app.processID == NSProcessInfo.processInfo.processIdentifier)
        return [[SCContentFilter alloc] initWithDisplay:display
                                  excludingApplications:@[app] exceptingWindows:@[]];
  }
  return [[SCContentFilter alloc] initWithDisplay:display excludingWindows:@[]];
}

QImage fromCGImage(CGImageRef image) {
  QImage result(int(CGImageGetWidth(image)), int(CGImageGetHeight(image)),
                QImage::Format_ARGB32_Premultiplied);
  if (result.isNull()) return {};
  const auto space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  const auto context = CGBitmapContextCreate(result.bits(), result.width(), result.height(), 8,
      result.bytesPerLine(), space, kCGBitmapByteOrder32Little | kCGImageAlphaPremultipliedFirst);
  CGColorSpaceRelease(space);
  if (!context) return {};
  CGContextSetBlendMode(context, kCGBlendModeCopy);
  CGContextDrawImage(context, CGRectMake(0, 0, result.width(), result.height()), image);
  CGContextRelease(context);
  return result;
}

QImage screenshot(SCContentFilter *filter, CGRect source, QSize size) {
  auto config = [[SCStreamConfiguration alloc] init];
  config.sourceRect = source;
  config.width = size.width();
  config.height = size.height();
  config.showsCursor = NO;
  config.colorSpaceName = kCGColorSpaceSRGB;
  auto signal = dispatch_semaphore_create(0);
  __block QImage result;
  __block NSError *failure = nil;
  [SCScreenshotManager captureImageWithFilter:filter configuration:config
                           completionHandler:^(CGImageRef image, NSError *error) {
    if (image) result = fromCGImage(image);
    failure = error;
    dispatch_semaphore_signal(signal);
  }];
  if (!waitFor(signal)) throw std::runtime_error("读取 macOS 屏幕画面超时。");
  if (failure) throw std::runtime_error(message(failure).toStdString());
  return result;
}

DesktopSnapshot grabDesktop(QRect requested) {
  DesktopSnapshot result;
  auto content = shareableContent();
  for (SCDisplay *display in content.displays) {
    const auto bounds = rectangle(display.frame);
    const auto area = requested.isEmpty() ? bounds : bounds.intersected(requested);
    if (area.isEmpty()) continue;
    auto filter = displayFilter(content, display, false);
    const auto size = area.size() * filter.pointPixelScale;
    auto image = screenshot(filter, cgrect(area.translated(-bounds.topLeft())), size);
    if (image.isNull()) throw std::runtime_error("macOS 未返回屏幕画面。");
    result.frames.append({area, image});
    result.bounds = result.bounds.united(area);
  }
  return result;
}

NSArray<NSDictionary *> *windowList(CGWindowListOption options, CGWindowID window = kCGNullWindowID) {
  return CFBridgingRelease(CGWindowListCopyWindowInfo(options, window));
}

QRect windowBounds(NSDictionary *window) {
  CGRect bounds{};
  if (NSDictionary *value = window[(__bridge NSString *)kCGWindowBounds])
    CGRectMakeWithDictionaryRepresentation((__bridge CFDictionaryRef)value, &bounds);
  return rectangle(bounds);
}

bool regularWindow(NSDictionary *window) {
  return [window[(__bridge NSString *)kCGWindowLayer] intValue] == 0 &&
         [window[(__bridge NSString *)kCGWindowOwnerPID] intValue] != NSProcessInfo.processInfo.processIdentifier &&
         [window[(__bridge NSString *)kCGWindowAlpha] doubleValue] > 0;
}
}

bool checkMacScreenPermission(bool request) {
  return CGPreflightScreenCaptureAccess() || (request && CGRequestScreenCaptureAccess());
}

void showMacScreenSettings() {
  [NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:
      @"x-apple.systempreferences:com.apple.preference.security?Privacy_ScreenCapture"]];
}

bool checkMacMicrophonePermission() {
  switch ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio]) {
  case AVAuthorizationStatusAuthorized: return true;
  case AVAuthorizationStatusNotDetermined: {
    auto signal = dispatch_semaphore_create(0);
    __block bool granted = false;
    [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL allowed) {
      granted = allowed;
      dispatch_semaphore_signal(signal);
    }];
    dispatch_semaphore_wait(signal, DISPATCH_TIME_FOREVER);
    return granted;
  }
  default: return false;
  }
}

void activateMacWindow(quintptr handle) {
  for (NSDictionary *window in windowList(kCGWindowListOptionIncludingWindow, CGWindowID(handle))) {
    auto pid = [window[(__bridge NSString *)kCGWindowOwnerPID] intValue];
    [[NSRunningApplication runningApplicationWithProcessIdentifier:pid]
        activateWithOptions:0];
    return;
  }
}

DesktopSnapshot captureMacDesktop() {
  @autoreleasepool {
    QElapsedTimer timer;
    timer.start();
    auto snapshot = grabDesktop({});
    if (snapshot.frames.isEmpty()) throw std::runtime_error("没有可用的显示器。");
    auto cursor = NSCursor.currentSystemCursor ?: NSCursor.arrowCursor;
    auto cursorSize = cursor.image.size;
    CGRect cursorBounds = CGRectMake(0, 0, cursorSize.width, cursorSize.height);
    if (auto image = [cursor.image CGImageForProposedRect:&cursorBounds context:nil hints:nil]) {
      snapshot.cursorImage = fromCGImage(image);
      snapshot.cursorRect = QRectF(QPointF(QCursor::pos()) - QPointF(cursor.hotSpot.x, cursor.hotSpot.y),
                                  QSizeF(cursorSize.width, cursorSize.height));
    }
    snapshot.timings["screen_and_cursor_ms"] = timer.elapsed();
    timer.restart();
    for (NSDictionary *window in windowList(kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements)) {
      if (!regularWindow(window)) continue;
      const auto bounds = windowBounds(window);
      if (bounds.width() > 6 && bounds.height() > 6) snapshot.windows.append(bounds);
    }
    snapshot.timings["window_detection_ms"] = timer.elapsed();
    return snapshot;
  }
}

QImage captureMacRegion(QRect region) {
  @autoreleasepool {
    if (region.isEmpty()) return {};
    try { return grabDesktop(region).crop(region); }
    catch (const std::exception &error) { qWarning() << error.what(); return {}; }
  }
}

quintptr recordingWindowAt(QPoint logicalPoint) {
  @autoreleasepool {
    for (NSDictionary *window in windowList(kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements))
      if (regularWindow(window) && windowBounds(window).contains(logicalPoint))
        return [window[(__bridge NSString *)kCGWindowNumber] unsignedIntValue];
    return 0;
  }
}

RecordingWindowInfo recordingWindowInfo(quintptr window) {
  if (!window) return {};
  @autoreleasepool {
    for (NSDictionary *details in windowList(kCGWindowListOptionIncludingWindow, CGWindowID(window))) {
      const auto bounds = windowBounds(details);
      NSString *title = details[(__bridge NSString *)kCGWindowName] ?:
                         details[(__bridge NSString *)kCGWindowOwnerName];
      return {physicalRect(bounds), bounds, QString::fromNSString(title), !bounds.isEmpty(),
              ![details[(__bridge NSString *)kCGWindowIsOnscreen] boolValue]};
    }
    return {};
  }
}

struct ScreenFrames {
  mutable std::mutex mutex;
  QImage image;
  QString error;
  std::function<void(int, const void *)> audio;
};

@interface PicutStreamOutput : NSObject <SCStreamOutput, SCStreamDelegate> {
@public
  std::shared_ptr<ScreenFrames> frames;
}
@end

@implementation PicutStreamOutput
- (void)stream:(SCStream *)stream didStopWithError:(NSError *)error {
  std::lock_guard lock(frames->mutex);
  frames->error = message(error);
}

- (void)stream:(SCStream *)stream didOutputSampleBuffer:(CMSampleBufferRef)sample
        ofType:(SCStreamOutputType)type {
  if (type != SCStreamOutputTypeScreen) {
    if (frames->audio) frames->audio(type == SCStreamOutputTypeAudio ? 0 : 1, sample);
    return;
  }
  auto attachments = (__bridge NSArray *)CMSampleBufferGetSampleAttachmentsArray(sample, false);
  NSNumber *status = [attachments.firstObject objectForKey:SCStreamFrameInfoStatus];
  if (!status || [status intValue] != SCFrameStatusComplete) return;
  auto pixels = CMSampleBufferGetImageBuffer(sample);
  if (!pixels || CVPixelBufferLockBaseAddress(pixels, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess) return;
  QImage image(static_cast<const uchar *>(CVPixelBufferGetBaseAddress(pixels)),
               int(CVPixelBufferGetWidth(pixels)), int(CVPixelBufferGetHeight(pixels)),
               qsizetype(CVPixelBufferGetBytesPerRow(pixels)), QImage::Format_RGB32);
  auto copy = image.copy();
  CVPixelBufferUnlockBaseAddress(pixels, kCVPixelBufferLock_ReadOnly);
  std::lock_guard lock(frames->mutex);
  frames->image = std::move(copy);
}
@end

struct MacScreenStream::Impl {
  SCStream *stream = nil;
  SCShareableContent *content = nil;
  SCDisplay *display = nil;
  SCStreamConfiguration *config = nil;
  PicutStreamOutput *output = nil;
  dispatch_queue_t audioQueue = nil;
  QRect region;
  std::shared_ptr<ScreenFrames> frames = std::make_shared<ScreenFrames>();
};

MacScreenStream::MacScreenStream(QRect logical, QSize pixels, unsigned fps, bool cursor,
                                 quintptr window, bool systemAudio, bool microphone,
                                 const QString &micId, std::function<void(int, const void *)> audio)
    : impl(std::make_unique<Impl>()) {
  @autoreleasepool {
    impl->content = shareableContent();
    impl->config = [[SCStreamConfiguration alloc] init];
    auto config = impl->config;
    config.width = pixels.width();
    config.height = pixels.height();
    config.minimumFrameInterval = CMTimeMake(1, fps);
    config.queueDepth = 3;
    config.pixelFormat = kCVPixelFormatType_32BGRA;
    config.colorSpaceName = kCGColorSpaceSRGB;
    config.showsCursor = cursor;
    config.ignoreShadowsSingleWindow = YES;
    config.ignoreGlobalClipSingleWindow = YES;
    config.preservesAspectRatio = NO;
    config.capturesAudio = systemAudio;
    config.sampleRate = 48000;
    config.channelCount = 2;
    config.excludesCurrentProcessAudio = YES;
    config.captureMicrophone = microphone;
    if (microphone && !micId.isEmpty()) config.microphoneCaptureDeviceID = micId.toNSString();

    SCContentFilter *filter = nil;
    if (window) {
      for (SCWindow *candidate in impl->content.windows) {
        if (candidate.windowID != window) continue;
        filter = [[SCContentFilter alloc] initWithDesktopIndependentWindow:candidate];
        break;
      }
    } else if (auto display = displayFor(impl->content, logical)) {
      impl->display = display;
      filter = displayFilter(impl->content, display, true);
      config.sourceRect = cgrect(logical.translated(-rectangle(display.frame).topLeft()));
    }
    if (!filter) throw std::runtime_error("找不到录制窗口或显示器。");
    impl->region = logical;
    impl->frames->audio = std::move(audio);
    impl->output = [[PicutStreamOutput alloc] init];
    impl->output->frames = impl->frames;
    impl->stream = [[SCStream alloc] initWithFilter:filter configuration:config delegate:impl->output];
    auto queue = dispatch_queue_create("picut.screen.frames", DISPATCH_QUEUE_SERIAL);
    impl->audioQueue = dispatch_queue_create("picut.screen.audio", DISPATCH_QUEUE_SERIAL);
    NSError *failure = nil;
    if (![impl->stream addStreamOutput:impl->output type:SCStreamOutputTypeScreen sampleHandlerQueue:queue error:&failure] ||
        (systemAudio && ![impl->stream addStreamOutput:impl->output type:SCStreamOutputTypeAudio sampleHandlerQueue:impl->audioQueue error:&failure]) ||
        (microphone && ![impl->stream addStreamOutput:impl->output type:SCStreamOutputTypeMicrophone sampleHandlerQueue:impl->audioQueue error:&failure]))
      throw std::runtime_error(message(failure).toStdString());

    auto signal = dispatch_semaphore_create(0);
    __block NSError *startError = nil;
    [impl->stream startCaptureWithCompletionHandler:^(NSError *error) {
      startError = error;
      dispatch_semaphore_signal(signal);
    }];
    if (!waitFor(signal) || startError)
      throw std::runtime_error(startError ? message(startError).toStdString() : "启动屏幕采集超时。");
  }
}

MacScreenStream::~MacScreenStream() {
  @autoreleasepool {
    auto signal = dispatch_semaphore_create(0);
    [impl->stream stopCaptureWithCompletionHandler:^(NSError *) { dispatch_semaphore_signal(signal); }];
    waitFor(signal);
    dispatch_sync(impl->audioQueue, ^{});
  }
}

QImage MacScreenStream::frame() const {
  std::lock_guard lock(impl->frames->mutex);
  return impl->frames->image;
}

QString MacScreenStream::failure() const {
  std::lock_guard lock(impl->frames->mutex);
  return impl->frames->error;
}

void MacScreenStream::moveTo(QRect logical) {
  if (!impl->display || impl->region == logical) return;
  @autoreleasepool {
    auto display = displayFor(impl->content, logical);
    if (!display) return;
    if (display != impl->display) {
      auto signal = dispatch_semaphore_create(0);
      __block bool success = false;
      [impl->stream updateContentFilter:displayFilter(impl->content, display, true)
                      completionHandler:^(NSError *error) {
        success = !error;
        dispatch_semaphore_signal(signal);
      }];
      if (!waitFor(signal) || !success) return;
      impl->display = display;
    }
    impl->config.sourceRect = cgrect(logical.translated(-rectangle(display.frame).topLeft()));
    auto signal = dispatch_semaphore_create(0);
    __block bool success = false;
    [impl->stream updateConfiguration:impl->config completionHandler:^(NSError *error) {
      success = !error;
      dispatch_semaphore_signal(signal);
    }];
    if (waitFor(signal) && success) impl->region = logical;
  }
}
