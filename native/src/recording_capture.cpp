#include "recording_capture.h"
#include "native_windows.h"
#include <dwmapi.h>
#include <d3d11.h>
#include <roapi.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.Graphics.DirectX.h>

using namespace winrt;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;

RecordingWindowInfo recordingWindowInfo(quintptr value) {
  RecordingWindowInfo result;
  const auto window = reinterpret_cast<HWND>(value);
  if (!window || !IsWindow(window)) return result;
  result.valid = true;
  result.minimized = IsIconic(window) || !IsWindowVisible(window);
  RECT bounds{};
  if (FAILED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &bounds, sizeof(bounds))))
    GetWindowRect(window, &bounds);
  result.physical = QRect(bounds.left, bounds.top, bounds.right-bounds.left, bounds.bottom-bounds.top);
  for (auto screen : QGuiApplication::screens()) {
    const auto physical = physicalRect(screen->geometry());
    if (!physical.contains(result.physical.center())) continue;
    const double ratio = screen->devicePixelRatio();
    result.logical = QRect(screen->geometry().topLeft() +
        QPoint(qRound((bounds.left-physical.x())/ratio), qRound((bounds.top-physical.y())/ratio)),
        QSize(qRound(result.physical.width()/ratio), qRound(result.physical.height()/ratio)));
    break;
  }
  if (result.logical.isEmpty()) result.logical = result.physical;
  wchar_t title[512]{}; GetWindowTextW(window,title,512); result.title=QString::fromWCharArray(title);
  return result;
}
quintptr recordingWindowAt(QPoint logicalPoint) {
  struct Search { QPoint point; HWND window = nullptr; } search{physicalRect(QRect(logicalPoint,QSize(1,1))).topLeft()};
  EnumWindows([](HWND window, LPARAM parameter)->BOOL {
    auto &search=*reinterpret_cast<Search *>(parameter);
    DWORD process=0; GetWindowThreadProcessId(window,&process);
    if (process==GetCurrentProcessId() || !IsWindowVisible(window) || IsIconic(window) || GetWindow(window,GW_OWNER)) return TRUE;
    DWORD cloaked=0; DwmGetWindowAttribute(window,DWMWA_CLOAKED,&cloaked,sizeof(cloaked));
    if (cloaked) return TRUE;
    RECT bounds{}; GetWindowRect(window,&bounds);
    if (!PtInRect(&bounds,POINT{search.point.x(),search.point.y()})) return TRUE;
    search.window=window; return FALSE;
  },reinterpret_cast<LPARAM>(&search));
  return reinterpret_cast<quintptr>(search.window);
}

struct RecordingWindowCapture::Impl {
  bool apartment = false;
  com_ptr<ID3D11Device> device;
  com_ptr<ID3D11DeviceContext> context;
  com_ptr<ID3D11Texture2D> staging;
  IDirect3DDevice runtime{nullptr};
  GraphicsCaptureItem item{nullptr};
  Direct3D11CaptureFramePool pool{nullptr};
  GraphicsCaptureSession session{nullptr};
  winrt::Windows::Graphics::SizeInt32 poolSize{};
  QSize stagingSize;
  QImage last;
  quintptr window = 0;
  Impl(quintptr value, bool cursor):window(value) {
    const HRESULT initialized=RoInitialize(RO_INIT_MULTITHREADED);
    apartment=SUCCEEDED(initialized);
    check_hresult(initialized);
    try {
      if (!GraphicsCaptureSession::IsSupported()) throw std::runtime_error("系统不支持窗口捕获，请使用区域录屏。");
      auto hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
          nullptr,0,D3D11_SDK_VERSION,device.put(),nullptr,context.put());
      if (FAILED(hr)) hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
          nullptr,0,D3D11_SDK_VERSION,device.put(),nullptr,context.put());
      check_hresult(hr);
      auto dxgi=device.as<IDXGIDevice>();
      com_ptr<IInspectable> inspectable;
      check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.get(),inspectable.put()));
      runtime=inspectable.as<IDirect3DDevice>();
      auto factory=get_activation_factory<GraphicsCaptureItem,IGraphicsCaptureItemInterop>();
      check_hresult(factory->CreateForWindow(reinterpret_cast<HWND>(window),guid_of<GraphicsCaptureItem>(),put_abi(item)));
      poolSize=item.Size();
      pool=Direct3D11CaptureFramePool::CreateFreeThreaded(runtime,DirectXPixelFormat::B8G8R8A8UIntNormalized,2,poolSize);
      session=pool.CreateCaptureSession(item);
      if (winrt::Windows::Foundation::Metadata::ApiInformation::IsPropertyPresent(
          L"Windows.Graphics.Capture.GraphicsCaptureSession",L"IsCursorCaptureEnabled")) session.IsCursorCaptureEnabled(cursor);
      session.StartCapture();
    } catch (...) { close(); throw; }
  }
  void close() {
    if (session) { try { session.Close(); } catch (...) {} session=nullptr; }
    if (pool) { try { pool.Close(); } catch (...) {} pool=nullptr; }
    item=nullptr; runtime=nullptr; staging=nullptr; context=nullptr; device=nullptr;
    if (apartment) { RoUninitialize(); apartment=false; }
  }
  ~Impl() { close(); }
  QImage grab() {
    auto frame=pool.TryGetNextFrame();
    while (auto next=pool.TryGetNextFrame()) { if (frame) frame.Close(); frame=std::move(next); }
    if (!frame) return last;
    const auto size=frame.ContentSize();
    if (size.Width<1 || size.Height<1) { frame.Close(); return last; }
    auto access=frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
    com_ptr<ID3D11Texture2D> texture;
    check_hresult(access->GetInterface(__uuidof(ID3D11Texture2D),texture.put_void()));
    D3D11_TEXTURE2D_DESC source{}; texture->GetDesc(&source);
    // During growth, the old pool can deliver one texture smaller than the
    // new content. Keep the previous complete frame until the resized pool is ready.
    if (size.Width>int(source.Width) || size.Height>int(source.Height)) {
      frame.Close(); poolSize=size;
      pool.Recreate(runtime,DirectXPixelFormat::B8G8R8A8UIntNormalized,2,poolSize); return last;
    }
    const QSize dimensions(qMin(int(source.Width),size.Width),qMin(int(source.Height),size.Height));
    if (dimensions!=stagingSize) {
      staging=nullptr;
      D3D11_TEXTURE2D_DESC desc{};
      desc.Width=dimensions.width(); desc.Height=dimensions.height(); desc.MipLevels=1; desc.ArraySize=1;
      desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count=1;
      desc.Usage=D3D11_USAGE_STAGING; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
      check_hresult(device->CreateTexture2D(&desc,nullptr,staging.put())); stagingSize=dimensions;
    }
    D3D11_BOX box{0,0,0,UINT(dimensions.width()),UINT(dimensions.height()),1};
    context->CopySubresourceRegion(staging.get(),0,0,0,0,texture.get(),0,&box);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    check_hresult(context->Map(staging.get(),0,D3D11_MAP_READ,0,&mapped));
    last=QImage(static_cast<uchar *>(mapped.pData),dimensions.width(),dimensions.height(),mapped.RowPitch,QImage::Format_RGB32).copy();
    context->Unmap(staging.get(),0);
    frame.Close();
    if (size.Width!=poolSize.Width || size.Height!=poolSize.Height) {
      poolSize=size; pool.Recreate(runtime,DirectXPixelFormat::B8G8R8A8UIntNormalized,2,poolSize);
    }
    return last;
  }
};
RecordingWindowCapture::RecordingWindowCapture(quintptr window,bool cursor) {
  try { impl=std::make_unique<Impl>(window,cursor); }
  catch (const hresult_error &error) {
    throw std::runtime_error(QString("无法捕获此窗口（需要 Windows 10 1903 或更新版本）；可改用区域录屏。%1")
        .arg(QString::fromWCharArray(error.message().c_str())).toUtf8().constData());
  }
}
RecordingWindowCapture::~RecordingWindowCapture() = default;
QImage RecordingWindowCapture::grab() {
  try { return impl->grab(); }
  catch (const hresult_error &error) {
    if (!IsWindow(reinterpret_cast<HWND>(impl->window))) return impl->last;
    throw std::runtime_error((QString("窗口画面采集中断：")+QString::fromWCharArray(error.message().c_str())).toUtf8().constData());
  }
}
