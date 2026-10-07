# slender-ui

## 中文

slender-ui 是一个 stb 风格的**单头文件** C++20 UI 库：在恰好一个翻译单元中定义
`SLENDER_IMPLEMENTATION` 并包含 `slender_ui.h` 即可获得全部实现，无第三方依赖。

- **平台**：Windows 10 (1607+)；Win32 + Direct2D + DirectWrite，**纯自绘**渲染（无子控件 HWND）；编译器 MSVC，语言标准 C++20。
- **风格**：Fluent 设计——功能区以色阶面板区分、圆角与阴影表达层阶、非必要无动画；主题只是颜色合集（浅色/深色两套，运行时可切换）。
- **坐标**：一律 DIP，框架自动处理 Per-Monitor V2 DPI 缩放。
- **布局**：顶层容器自动填满窗口；Column/Row/Flow/Overlay/Banner/Card 支持内边距、间距、权重与按内容自适应；TitleBar/菜单栏/工具栏/状态栏可停靠。
- **控件**：Label、Button 六变体、TextBox（清除/密码/占位/撤销）、CheckBox/RadioButton/ToggleSwitch/Slider/ComboBox/SplitButton、ListView/TreeView、InfoBar/ProgressBar/ProgressRing/RatingControl/InfoBadge/Avatar、CommandBar/Expander/Tabs/ScrollViewer/NavigationView/TopBar，以及 Flyout/ToolTip/Toast/ContentDialog/上下文菜单等弹层。
- **源码形态**：单文件、结构紧凑、注释为英文，面向 AI 阅读与探索。

```cpp
#define SLENDER_IMPLEMENTATION
#include "slender_ui.h"

int wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    slender::Initialize();
    slender::Window wnd;
    wnd.SetTitle(L"Example");
    auto& col = wnd.Add<slender::Column>();
    col.Add<slender::Label>(L"Hello, world");
    wnd.Show();
    return slender::Run();
}
```

## English

slender-ui is a stb-style **single-header** C++20 UI library: define
`SLENDER_IMPLEMENTATION` in exactly one translation unit and include `slender_ui.h`
to get the full implementation. No third-party dependencies.

- **Platform**: Windows 10 (1607+); Win32 + Direct2D + DirectWrite with **pure owner-drawn** rendering (no per-widget HWNDs). MSVC, C++20.
- **Style**: Fluent design — tonal panels, rounded corners and shadows for elevation, no unnecessary animation; a theme is just a collection of colors (light/dark, switchable at runtime).
- **Coordinates**: all in DIPs; Per-Monitor V2 DPI scaling is handled by the framework.
- **Layout**: the top-level container auto-fills the window; Column/Row/Flow/Overlay/Banner/Card support padding, spacing, weights and content-based sizing; TitleBar/menu bar/tool bar/status bar dock.
- **Controls**: Label, six Button variants, TextBox (clear/password/placeholder/undo), CheckBox/RadioButton/ToggleSwitch/Slider/ComboBox/SplitButton, ListView/TreeView, InfoBar/ProgressBar/ProgressRing/RatingControl/InfoBadge/Avatar, CommandBar/Expander/Tabs/ScrollViewer/NavigationView/TopBar, plus Flyout/ToolTip/Toast/ContentDialog/context-menu popups.
- **Source shape**: one compact file with English comments, written for AI readability and exploration.

```cpp
#define SLENDER_IMPLEMENTATION
#include "slender_ui.h"

int wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    slender::Initialize();
    slender::Window wnd;
    wnd.SetTitle(L"Example");
    auto& col = wnd.Add<slender::Column>();
    col.Add<slender::Label>(L"Hello, world");
    wnd.Show();
    return slender::Run();
}
```
