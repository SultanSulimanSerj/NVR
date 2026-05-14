//! Fullscreen local client for the NVR web UI (WebKitGTK on Linux, WebView2 on Windows).

#[cfg(target_os = "linux")]
use wry::WebViewBuilderExtUnix;
use wry::WebViewBuilder;

use tao::{
    event::{Event, WindowEvent},
    event_loop::{ControlFlow, EventLoop},
    window::{Fullscreen, WindowBuilder},
};

#[cfg(not(any(
    target_os = "linux",
    target_os = "windows",
    target_os = "macos"
)))]
compile_error!("nvr-shell is only built for Linux, Windows, and macOS");

fn main() -> wry::Result<()> {
    let url = std::env::var("NVR_KIOSK_URL")
        .unwrap_or_else(|_| "https://nvr.local/local-dashboard".to_string());

    let event_loop = EventLoop::new();
    let window = WindowBuilder::new()
        .with_title("NVR")
        .with_fullscreen(Some(Fullscreen::Borderless(None)))
        .build(&event_loop)
        .expect("window");

    #[cfg(any(target_os = "windows", target_os = "macos"))]
    let _webview = WebViewBuilder::new(&window)
        .with_url(&url)
        .with_user_agent("NVR-Shell/1.0")
        .build()?;

    #[cfg(target_os = "linux")]
    let _webview = {
        use tao::platform::unix::WindowExtUnix;
        let vbox = window.default_vbox().expect("gtk vbox");
        WebViewBuilder::new_gtk(&vbox)
            .with_url(&url)
            .with_user_agent("NVR-Shell/1.0")
            .build()?
    };

    event_loop.run(move |event, _, control_flow| {
        *control_flow = ControlFlow::Wait;
        if let Event::WindowEvent {
            event: WindowEvent::CloseRequested,
            ..
        } = event
        {
            *control_flow = ControlFlow::Exit;
        }
    })
}
