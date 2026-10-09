// ============================================================================
//  slender_ui.h — slender-ui single-header library
//
//  Fluent-style UI library for Windows. Pure owner-drawn, built on Win32 + Direct2D + DirectWrite.
//  Minimum OS: Windows 10 (1607+). Language standard: C++20. Compiler: MSVC.
//
//  Usage (stb-style: define the implementation macro in exactly one translation unit):
//
//      #define SLENDER_IMPLEMENTATION
//      #include "slender_ui.h"
//
//  Quick start:
//
//      int wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
//          slender::Initialize();
//          slender::Window wnd;
//          wnd.SetTitle(L"Example");
//          wnd.SetClientSize(480, 320);
//
//          // Layout container: a top-level Column auto-fills the content area and re-flows on window resize
//          auto& col = wnd.Add<slender::Column>();
//          col.SetPadding(16);
//          col.SetSpacing(8);
//          col.Add<slender::Label>(L"Hello, world");
//          col.Add<slender::Button>(L"Button", [] { /* click callback */ });
//
//          wnd.Show();
//          return slender::Run();
//      }
//
//  Conventions:
//    - All coordinates and sizes are DIPs (device-independent pixels; at 96 DPI, 1 DIP = 1 physical pixel);
//      the framework handles Per-Monitor V2 DPI scaling automatically.
//    - Fluent style: functional bands are distinguished by tonal panels, no border lines; rounded corners and shadows convey depth;
//      a theme is just a collection of colors.
//    - User-defined widgets are not supported; all apps share a consistent style.
//    - Thread affinity (ARCH-14): all APIs (including Initialize/Shutdown/Run) may be called only
//      on the thread that created the first window — the D2D factory is SINGLE_THREADED, and process-level state
//      (g_windows/font and text caches, etc.) is lock-free.
//    - protected/private members and protected virtual functions (OnPaint/OnKeydown, etc.) are the library's
//      internal extension points, not a supported public API (ARCH-12/API-03): subclassing widgets does
//      compile, but may change at any time; not supporting custom widgets remains an explicit trade-off.
//
//  Code map (grep anchors; the file reads top-down in four bands):
//    band 1  public API     : "namespace slender {" — value types (Color/Point/Size/Rect/Font/
//                             GradientStop/Icon/Theme/ShadowSpec), Painter, then Widget and every
//                             control (grep "^class <Name>"); the namespace closes before the impl gate.
//    band 2  implementation : "#ifdef SLENDER_IMPLEMENTATION" — text/icon caches, the SVG path parser,
//                             RenderPainter (D2D backend of Painter), the four popups
//                             ("class PopupWindow" + MenuPopup/ToastPopup/Flyout/ToolTipPopup),
//                             window icon baking, theme tables ("Theme::Light"/"Theme::Dark").
//    band 3  window core    : "struct WindowImpl :" = nine CRTP concern bases
//                             ("struct Window*Ops") + the thin core; "WndProc" is a 24-line
//                             dispatcher over 14 named concern handlers (OnKeydown/OnMouseButton/...).
//    band 4  widget impls   : one block per control ("// <name> implementation"), then global
//                             services Initialize/Shutdown/Run/PumpOnce and process state (g_*).
//    Cross-refs: "docs/项目约定.md" (registered trade-offs); finding IDs (GAPxx-xx/M-xx/Rxx-xx)
//    in comments map 1:1 to the round reports under docs/.
// ============================================================================

#ifndef SLENDER_H_INCLUDED
#define SLENDER_H_INCLUDED

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// Library version (API-13): semantic major/minor/patch numbers plus a combined string, for consumers to do
/// compile-time version checks and compatibility branching. Bumped on public API changes (API39-02): 0.38.0 =
/// ComboBox::AddItem changed to chainable return (breaking signature change); 0.42.0 = Container
/// notification surface completion (OnChildAdded/OnChildrenReordered virtual hooks, GAP43-01/02) +
/// Font::family empty string = default font family stack (PERF43-01, semantically compatible default-value change);
/// 0.43.0 = Overlay notification surface wired in + SetInset/ClearInset new public methods
/// (GAP44-01/PERF44-01, round-45); 0.44.0 = ListView::SetSelectedIndex
/// notify default changed to false (API50-01, behavior change); 0.45.0 =
/// NavigationView::SetSelectedIndex gains optional notify parameter (OBS52-02, round-52);
/// 0.46.0 = dialog footer single button spans full width / divider only with two buttons (GAP53-01) +
/// NavigationView::SetSelectedIndex rejects out-of-range (OBS53-01, behavior change, round-53);
/// 0.46.1 = four-popups four-copies refactor (GAP56-01: MenuPopup/ToastPopup/Flyout/
/// ToolTipPopup consolidated into a shared PopupWindow base class, pure internal-implementation refactor,
/// public API and rendering pixel-identical, round-56); 0.46.2 = hot-path de-heap-allocation (PERF57-01 mask-string
/// member scratch + PERF57-02 LayoutChildren scheduling pool, pure internal implementation, round-57);
/// 0.46.3 = WndProc split by concern (ARCH58-01 phase 1: the 35-case/678-line single
/// switch split into 14 concern handlers, case bodies kept verbatim, pure structural refactor, round-58);
/// 0.46.4 = WindowImpl class-level split (ARCH58-01 phase 2: nine CRTP concern-operation
/// base classes + thin core, core direct members 49→16, member/method names and public surface unchanged, round-59);
/// 0.46.5 = WindowImpl destruction-order invariant documented (ARCH60-02) + the nine base classes' impl()
/// demoted to protected (IFACE60-01 encapsulation narrowing, no external call sites inside detail,
/// public surface unchanged, round-61; REG60-01 erratum: the thin core's direct member declaration-line count is 19, not 16);
/// 0.47.0 = entry-table completion round (round-62): Menu/MenuBar/ToolBar/StatusBar/CommandBar/
/// NavigationView/TreeView gain Clear() (API62-01), CommandBar gains SetItemText/EnableItem
/// (API62-03), TreeView::Node::expanded made private behind IsExpanded() + TreeView::SetNodeText
/// (API62-02), Label ellipsis truncation de-allocated via binary search (PERF62-01, rendering-identical);
/// 0.47.1 = caption-button hover/press highlight extends up to the title bar's top edge (round-64:
/// the system highlight box is taller than its hit box and touches the window top, R32-06/M-05;
/// hit geometry unchanged, paint-only);
/// 0.48.0 = consumer-requirement capability round (round-65): title-bar control handover
/// (Window::GetTitleBar + TitleBar badge/system-button configuration + custom caption buttons with
/// toggle state and per-button tooltips, R1), Window::Hide (R2.1), Window::SetTopmost/IsTopmost
/// (R2.2), Window::SetMaximizable full-path maximize suppression (R2.3).
/// 0.49.0 = consumer-requirement round 2 (round-66): Window::Minimized/Restore for tray-click
/// restore (R4), the caption right-click takeover made reachable on the real physical path
/// (R5: WM_NCRBUTTONUP with HTCAPTION is delivered but DefWindowProc synthesizes nothing from it
/// on this OS — no WM_CONTEXTMENU, no system menu; verified r107ctx), the title-bar context menu
/// also anchored at the cursor when nothing has focus (R5 preferred option), TitleBar::SetHeight
/// with edge-to-edge caption buttons (R6) and TitleBar::SetToggledFillVisible (R6).
/// 0.50.0 = consumer-requirement round 3 (round-67): TitleBar::SetCaptionInset/CaptionInset (R7) —
/// the caption button row's right-edge inset becomes a consumer-settable value (0 = the close
/// button hugs the window's top-right corner, like a maximized system window; negative restores
/// the automatic one-border-width inset), hit and draw geometry move together.
/// 0.51.0 = consumer-requirement round 4 (round-68): Window::SetRoundedCorners/RoundedCorners
/// (R8) — Windows 11 system rounded corners for the main window, opt-in (default stays square);
/// the same DWM corner-preference path the popup host already uses, OS-truth readback, Windows 10
/// silently keeps square corners; geometry, hit-testing and all other window states untouched.
/// 0.52.0 = consumer-requirement round 5 (round-69): TreeView programmatic selection (R9) —
/// SetSelectedRow/Select/SelectedRow. The selection is the flat visible-row index (0 = first
/// visible row) and follows its node across row-table rebuilds (-1 once the row is not
/// rendered, e.g. after an ancestor was collapsed — previously the stale index could read
/// out of bounds from OnKeydown); notify=true replays one activation (node onClick then
/// OnClick, expansion never toggled), out-of-range rows and out-of-tree nodes are rejected,
/// keyboard focus untouched.
/// 0.53.0 = consumer-requirement round 6 (round-70): the public event surface the first
/// plugin (cozy-todo) needs (R10-R13). Widget::OnHoverChanged/Hovered — hover-chain
/// enter/leave exactly once per transition, propagated along the hit chain like the
/// container highlight state (whose walk it generalizes; highlight rendering unchanged),
/// cleared on WM_MOUSELEAVE, on window deactivation and on subtree detach. Widget::
/// OnDoubleClickCb — multi-click activation (clickCount from BumpClickCount, 2 / ≥3; single
/// clicks never fire; a callback that moves focus is respected by the click-focus defaults).
/// TextBox::OnCommitted (Enter; unsubscribed keeps the previous fall-through) and
/// TextBox::OnFocusLost (genuine focus transfers only — DetachTree clears focus silently so
/// commit-and-rebuild flows cannot double-fire). Window::SetSizeChangedHandler — the client
/// DIP size actually changed, fired after layout is current; SIZE_MINIMIZED and DPI-only
/// rescales never fire.
/// 0.53.1 = consumer-requirement round 7 (round-71): R14 — SetFocused no longer writes a
/// target the blur callbacks detached. The old widget's public callbacks (OnUnfocused /
/// TextBox::OnFocusLost) may legitimately rebuild the tree (blur-commit-rebuild), which
/// can detach the new target: the pre-callback pointer was re-installed unconditionally,
/// and FlushPendingDestroy's root-equality heal cannot reach a target nested inside a
/// rebuilt row (next input = use-after-free, consumer crash 0xC0000005). The target is
/// now re-validated with the click path's own ARCH39-01 criterion (window_ non-null) —
/// a detached target is dropped and focus lands nowhere; DetachTree likewise clears
/// `captured` subtree-wide (the R10 hover / R12 focus contract), so WM_LBUTTONUP /
/// WM_CAPTURECHANGED can never dereference a detached widget.
#define SLENDER_UI_VERSION_MAJOR 0
#define SLENDER_UI_VERSION_MINOR 53
#define SLENDER_UI_VERSION_PATCH 1
#define SLENDER_UI_VERSION "0.53.1"

// The public API (SetIcon) needs HICON, but this header's public section does not force windows.h to be included first.
// Forward declaration identical in shape to the Windows SDK: a legal repeated typedef when windows.h is already included.
struct HICON__;
typedef struct HICON__* HICON;

namespace slender {

namespace detail {
struct WindowImpl;
class MenuPopup;
class Flyout;
class ToolTipPopup;
struct IconData;
enum class Dock { None, Top, Bottom, Left, Right };
// ARCH58-01 phase 2: the nine concern-operation base classes of WindowImpl (CRTP templates),
// defined before WindowImpl; forward-declared here for template friend references in widget classes.
template <class W> struct WindowDialogOps;
template <class W> struct WindowFocusOps;
template <class W> struct WindowHitOps;
template <class W> struct WindowPopupOps;
template <class W> struct WindowIconOps;
template <class W> struct WindowGfxOps;
template <class W> struct WindowAnimOps;
template <class W> struct WindowDestroyOps;
template <class W> struct WindowChromeOps;
}

// ---------------------------------------------------------------------------
// Basic types
// ---------------------------------------------------------------------------

/// RGBA color, each component 0..1.
struct Color {
    float r = 0, g = 0, b = 0, a = 1;

    /// Constructs an opaque color from 0xRRGGBB.
    static constexpr Color Rgb(unsigned rgb, float alpha = 1.0f) {
        return Color{ ((rgb >> 16) & 0xFF) / 255.0f,
                      ((rgb >> 8) & 0xFF) / 255.0f,
                      (rgb & 0xFF) / 255.0f, alpha };
    }
};

struct Point {
    float x = 0, y = 0;
};

struct Size {
    float w = 0, h = 0;
};

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;

    constexpr float Right()  const { return x + w; }
    constexpr float Bottom() const { return y + h; }
    constexpr bool Contains(float px, float py) const {
        return px >= x && px < Right() && py >= y && py < Bottom();
    }
    constexpr Rect Translated(float dx, float dy) const {
        return { x + dx, y + dy, w, h };
    }
    constexpr bool Intersects(const Rect& o) const {
        return x < o.Right() && o.x < Right() && y < o.Bottom() && o.y < Bottom();
    }
};

enum class HAlign { Left, Center, Right };
enum class VAlign { Top, Center, Bottom };
enum class FontWeight { Normal, SemiBold, Bold };

/// Cross-axis alignment for layout containers. Baseline (first-line text baseline alignment) only takes effect
/// on horizontal containers (Row / Flow inline); vertical containers (Column) and Overlay treat it as Start
/// (horizontal baselines are meaningless in horizontal writing mode).
enum class CrossAlign { Stretch, Start, Center, End, Baseline };

/// Default font family stack (PERF43-01): resolved when Font::family is empty. The constant stays before Font
/// so that Font{}'s default construction no longer carries the 49-char wstring NSDMI — the source of per-frame
/// heap allocation in the draw hot path across rounds (one operator new per Font{...}).
inline constexpr wchar_t kDefaultFontFamily[] =
    L"Segoe UI Variable Text;Segoe UI;Microsoft YaHei UI";

/// Font (logical description). Size unit is DIP.
/// family supports a ";"-separated fallback stack (e.g. L"Segoe UI Variable Text;Segoe UI;Microsoft YaHei UI");
/// at render time the first family actually present on the system wins; missing glyphs fall back via the DirectWrite system fallback.
/// **Empty string = default family stack** (kDefaultFontFamily): Font{} and Font{.size=14} etc.
/// therefore default-construct with zero heap allocation; explicitly passing a non-empty family behaves as before.
/// All in-library consumption resolves via EffectiveFontFamily/ResolveFontFamily; never read
/// family directly and split the stack yourself (M-77/PERF44-01: MakeBadgeIcon bypassed this once; the closure criterion became
/// "all read points of this field" rather than "call sites of known resolution entry points").
struct Font {
    std::wstring family;
    float size = 14.0f;
    FontWeight weight = FontWeight::Normal;
    float lineHeight = 0.0f;       ///< line-height multiple (0 = DWrite default; reference: body 1.5, hero 1.7)
    bool tabularNumerals = false;  ///< tabular numerals (reference: font-variant-numeric:tabular-nums)
};

/// Gradient stop (pos ranges 0..1).
struct GradientStop {
    float pos = 0;
    Color color{};
};

// ---------------------------------------------------------------------------
// Icons
// ---------------------------------------------------------------------------

/// Vector icon: SVG path data on a 24×24 viewbox, rasterized on demand into D2D path geometry and
/// rendered with a solid-color fill (icon objects are copyable; geometry is globally shared and cached).
class Icon {
public:
    /// Constructs from the d attribute content of an SVG path (supports M/L/H/V/C/S/Q/T/A/Z commands,
    /// absolute and relative coordinates). Yields an empty icon on parse failure.
    static Icon FromSvgPath(std::string_view pathData);

    Icon();
    ~Icon();
    Icon(const Icon&);
    Icon& operator=(const Icon&);
    Icon(Icon&&) noexcept;
    Icon& operator=(Icon&&) noexcept;

    bool IsEmpty() const;

private:
    friend class RenderPainter;
    std::shared_ptr<detail::IconData> data_;
};

// ---------------------------------------------------------------------------
// Theme: color collection
// ---------------------------------------------------------------------------

/// A theme is just a collection of colors — no layout, fonts, or shapes.
/// Elevation top-down: popup > card > functional bands > content area/base.
/// Overlay-type tokens (control/stroke/divider/track, etc.) are translucent colors composited
/// by D2D source-over onto the carrying surface (card face/page base), not pre-blended.
struct Theme {
    Color windowBackground;   ///< window base (also the swap-chain fill color in resize gaps)
    Color titleBackground;    ///< title bar band
    Color menuBackground;     ///< menu bar band
    Color toolbarBackground;  ///< toolbar band
    Color contentBackground;  ///< content area band
    Color cardBackground;     ///< floating card
    Color cardSecondary;      ///< secondary card face (command bar plate etc., closer to the base than cards)
    Color statusBackground;   ///< status bar band
    Color text;               ///< primary text
    Color textSecondary;      ///< secondary text
    Color textDisabled;       ///< disabled text
    Color titleText;          ///< title bar text and icons
    Color statusText;         ///< status bar text
    Color textOnAccent;       ///< text on top of accent
    Color control;            ///< regular control fill (buttons)
    Color controlHover;       ///< control hover fill
    Color controlPressed;     ///< control pressed fill
    Color controlText;        ///< control text
    Color inputBackground;    ///< text box fill
    Color accent;             ///< accent
    Color accentHover;        ///< accent hover (top of the primary-button gradient)
    Color accentPressed;      ///< accent pressed
    Color accentSoft;         ///< accent soft tint (active/selected background, low opacity)
    Color hoverSoft;          ///< neutral soft tint (hover background, low opacity)
    Color popupBackground;    ///< popup layer (menu/toast) background — highest tonal tier
    Color popupHover;         ///< popup item hover
    Color divider;            ///< internal functional divider (low opacity)
    Color selectedSoft;       ///< neutral selection background (lists/toggle buttons/selection)
    Color textTertiary;       ///< tertiary text (placeholders, icons, captions)
    Color accentText;         ///< accent text (hyperlinks, InfoBar actions)
    Color track;              ///< slider/progress bar track
    Color controlStroke;      ///< control stroke
    Color controlStrokeBottom;///< control bottom-edge stroke (darker, for depth)
    Color disabledBackground; ///< disabled control fill
    Color sliderThumb;        ///< slider thumb (white in both themes, ensures visibility on dark)
    Color blob1;              ///< window background decorative light blob one (top-left)
    Color blob2;              ///< window background decorative light blob two (bottom-right)
    Color success;            ///< semantic color: success
    Color successBackground;  ///< semantic color: success background
    Color warning;            ///< semantic color: warning
    Color warningBackground;  ///< semantic color: warning background
    Color error;              ///< semantic color: error
    Color errorBackground;    ///< semantic color: error background
    Color info;               ///< semantic color: info
    Color infoBackground;     ///< semantic color: info background
    Color bg2;                ///< neutral tonal level 2 (reference --bg2 alias, = statusBackground)
    Color bg3;                ///< neutral tonal level 3 (reference --bg3 alias, one step lighter than cardSecondary)
    Color subtleActive;       ///< neutral pressed state (reference --subtle-act: secondary/icon/command-bar button pressed)
    Color tooltipBackground;  ///< tooltip background (reference --tooltip-bg solid, one step darker than the popup background)
    Color captionCloseHover;  ///< title bar close-button hover red (Win11: same #C42B1C in dark and light, R32-04)
    Color captionClosePressed;///< close button pressed (Win11 measured: dark #B22A1C / light #C74031, R32-05)
    Color captionPressed;     ///< min/max button pressed tint (Win11 measured equivalent overlay, R32-05)

    /// Built-in light theme.
    static const Theme& Light();

    /// Built-in dark theme.
    static const Theme& Dark();
};

/// Shadow tiers (shape/blur parameters are not part of the color theme; only opacity varies with light/dark).
struct ShadowSpec {
    float offsetY = 0;
    float blur = 0;
    float alphaLight = 0;
    float alphaDark = 0;
};

/// Three shadow tiers: content card (weak) → popup (medium) → dialog (strong).
inline constexpr ShadowSpec kShadowCard{ 2, 4, 0.05f, 0.23f };
inline constexpr ShadowSpec kShadowFly{ 8, 16, 0.14f, 0.37f };
inline constexpr ShadowSpec kShadowDialog{ 32, 64, 0.24f, 0.56f };
inline constexpr ShadowSpec kShadowDialogNear{ 2, 21, 0.14f, 0.37f };

/// Shadow tier constants (unified semantics for Label and Card): explicit "none" + three tiers; one-to-one
/// with ShadowSpec's kShadowCard/kShadowFly/kShadowDialog.
inline constexpr int kShadowTierNone = -1, kShadowTierCard = 0,
                     kShadowTierFlyout = 1, kShadowTierDialog = 2;

/// Returns shadow opacity by theme light/dark (dark-theme shadows are heavier).
inline float ShadowAlpha(const ShadowSpec& s, const Theme& t) {
    float lum = t.windowBackground.r * 0.299f +
                t.windowBackground.g * 0.587f +
                t.windowBackground.b * 0.114f;
    return lum < 0.5f ? s.alphaDark : s.alphaLight;
}

// ---------------------------------------------------------------------------
// Shape constants: corner radii are not in the theme (the theme carries colors only)
// ---------------------------------------------------------------------------

/// Corner radius for panels and popups (content cards, popup menus, etc.).
inline constexpr float kRadiusPanel = 8.0f;

/// Corner radius for controls (buttons, text boxes, hover pills, etc.).
inline constexpr float kRadiusControl = 4.0f;

// ---------------------------------------------------------------------------
// Painter: the sole entry point for widget owner-drawing
// ---------------------------------------------------------------------------

/// Passed in by the framework at draw time; widgets draw only through it.
class Painter {
public:
    virtual ~Painter() = default;
    virtual void FillRect(const Rect& rect, const Color& color) = 0;
    virtual void StrokeRect(const Rect& rect, const Color& color,
                            float width = 1.0f) = 0;

    /// Rounded-rectangle stroke.
    virtual void StrokeRoundedRect(const Rect& rect, float radius,
                                   const Color& color, float width = 1.0f) = 0;

    /// Dashed stroke of a square-corner rectangle (demo area borders etc.).
    virtual void StrokeRectDashed(const Rect& rect, const Color& color,
                                  float width = 1.0f) = 0;

    /// Dashed stroke of a rounded rectangle (dashes follow the rounded arc segments; equivalent to the square version when radius ≤ 0).
    virtual void StrokeRoundedRectDashed(const Rect& rect, float radius,
                                         const Color& color,
                                         float width = 1.0f) {
        (void)radius;
        StrokeRectDashed(rect, color, width);
    }

    /// Rounded-rectangle fill; radius auto-clamps to half the shorter side when it exceeds it.
    virtual void FillRoundedRect(const Rect& rect, float radius,
                                 const Color& color) = 0;

    /// Rounded-rectangle vertical linear gradient fill (accent buttons, badges, etc.).
    virtual void FillRoundedRectGradient(const Rect& rect, float radius,
                                         const Color& top,
                                         const Color& bottom) = 0;

    /// Rounded-rectangle linear gradient fill in any direction (from/to are the two endpoints of the gradient axis).
    virtual void FillRoundedRectGradientStops(const Rect& rect, float radius,
                                              const GradientStop* stops,
                                              size_t count,
                                              const Point& from,
                                              const Point& to) = 0;

    /// Ellipse radial gradient fill (inner at center, outer at edge; for decorative light blobs).
    virtual void FillEllipseGradient(const Rect& area, const Color& inner,
                                     const Color& outer) {
        if (area.w <= 0 || area.h <= 0) return;
        const GradientStop pair[2]{ { 0.0f, inner }, { 1.0f, outer } };
        FillEllipseGradientStops(area, pair, 2);
    }

    /// Ellipse radial multi-stop gradient fill (area is the bounding rectangle; for CSS radial-gradient
    /// "transparent 70% cutoff"-style stops).
    virtual void FillEllipseGradientStops(const Rect& area, const GradientStop* stops,
                                          size_t count) = 0;

    /// Ellipse fill (area is the bounding rectangle; a square bounding rectangle gives a circle).
    virtual void FillEllipse(const Rect& area, const Color& color) = 0;

    /// Ellipse stroke (bounding rectangle).
    virtual void StrokeEllipse(const Rect& area, const Color& color,
                               float width = 1.0f) = 0;

    /// Straight line segment.
    virtual void DrawLine(const Point& from, const Point& to, const Color& color,
                          float width = 1.0f) = 0;

    /// Arc stroke (progress ring). center is the circle center, angles in degrees, 0° points to 12 o'clock,
    /// positive is clockwise; sweepDeg may be negative.
    virtual void StrokeArc(const Point& center, float radius, float width,
                           float startDeg, float sweepDeg,
                           const Color& color) = 0;

    /// Draws a soft shadow below the rectangle: a Gaussian-blurred rounded rectangle offset down as a whole by offsetY;
    /// color includes opacity (for a black shadow pass an alpha-carrying color such as Color::Rgb(0x000000, 0.15f);
    /// tinted shadows also work). Purely decorative effect, silently skipped when the device does not support it.
    virtual void DrawShadow(const Rect& rect, float radius, float offsetY,
                            float blur, const Color& color) = 0;

    virtual void DrawText(std::wstring_view text, const Font& font,
                          const Rect& area, const Color& color,
                          HAlign h = HAlign::Left, VAlign v = VAlign::Center,
                          bool wrap = false) = 0;

    /// Draws a vector icon: scaled uniformly and centered within area.
    virtual void DrawIcon(const Icon& icon, const Rect& area,
                          const Color& color) = 0;

    /// Pushes a rectangular clip; subsequent drawing is confined to the rectangle (must be paired with PopClip).
    virtual void PushClip(const Rect& rect) = 0;
    virtual void PopClip() = 0;

    /// Pushes a rounded-rectangle clip layer: child drawing is masked by rounded-corner geometry (rounded semantics
    /// axis-aligned PushClip cannot express, e.g. dialog full-width buttons trimmed to the panel corner radius — reference .dlg{overflow:hidden}).
    /// Must be paired with PopClipRounded; the layer stack is covered by the FlattenLayers frame-start fallback.
    virtual void PushClipRounded(const Rect& rect, float radius) = 0;
    virtual void PopClipRounded() = 0;

    /// Pushes a group opacity: all subsequent drawing composites within the group first, then blends onto the
    /// background as a whole at opacity (CSS opacity group semantics, unlike per-element alpha multiplication). Must
    /// be paired with PopOpacity; the default implementation is empty (callers degrade to per-element approximation when unsupported).
    virtual void PushOpacity(float /*opacity*/) {}
    virtual void PopOpacity() {}

protected:
    Painter() = default;
};

// ---------------------------------------------------------------------------
// Widgets
// ---------------------------------------------------------------------------

class Window;

/// Base class of all widgets. User code does not derive from it directly (custom widgets unsupported).
class Widget {
public:
    virtual ~Widget() = default;
    Widget(const Widget&) = delete;
    Widget& operator=(const Widget&) = delete;

    const Rect& Bounds() const { return bounds_; }
    virtual void SetBounds(const Rect& bounds);

    bool Visible() const { return visible_; }
    Widget& SetVisible(bool visible);

    /// Enable/disable: a disabled widget ignores input and is drawn by each widget in disabled style.
    bool Enabled() const { return enabled_; }
    Widget& SetEnabled(bool enabled);

    /// Gives this widget keyboard focus (effective only for Focusable widgets).
    Widget& SetFocus();

    /// Whether this widget holds keyboard focus.
    bool Focused() const;

    /// Tooltip: after ~0.6s of hover the framework pops up a small tip flyout below this widget.
    Widget& SetToolTip(std::wstring_view text);
    const std::wstring& ToolTip() const { return tooltip_; }

    /// Right-click callback (context-menu entry point; coordinates are window coordinates).
    std::function<void(const Point&)> OnContextMenuCb;

    /// Multi-click activation callback (round-70 R11): fires after the protected OnDoubleClick
    /// for WM_LBUTTONDBLCLK (clickCount 2) and after the synthesized multi-click path
    /// (clickCount ≥ 3, delivered as WM_LBUTTONDOWN). clickCount is BumpClickCount's ordinal
    /// (system double-click time/rect); pos is in window DIP coordinates. Single clicks
    /// (count 1) never fire; widgets that do not subscribe are unaffected — existing
    /// double-click semantics (TextBox word-select etc.) run unchanged before this callback.
    std::function<void(const Point&, int clickCount)> OnDoubleClickCb;

    /// Hover enter/leave callback (round-70 R10): fires exactly once per transition of the
    /// hover-chain state — true when this widget or any descendant becomes the hover target,
    /// false when it leaves the chain (moving inside the same subtree never churns ancestors:
    /// a row stays "hovered" while the mouse crosses its children). Shares the framework's
    /// single hover judgment with the tooltip and the container hover highlight; no repaint is
    /// forced by the callback itself (hide/show children inside it invalidate on their own).
    /// The state clears on WM_MOUSELEAVE (mouse left the window), on window deactivation
    /// (WM_ACTIVATE WA_INACTIVE — R32-02's no-leak rule extended from the caption to the
    /// client area) and when the widget's subtree is detached (silently — the subscription is
    /// being destroyed with it); a hidden (SetVisible(false)) hovered widget resets on the
    /// next mouse move. Occlusion without a mouse move produces no OS message — native
    /// behavior, self-correcting on the next move.
    std::function<void(bool)> OnHoverChanged;

    /// Whether this widget or any descendant is the current hover target — the same state
    /// OnHoverChanged reports; always matches the last callback value.
    bool Hovered() const { return hoverLit_; }

    // ---- Sizing policies in layout containers (see Row/Column; not used by widgets with manual SetBounds) ----

    /// Fixed width. On the cross axis it takes priority over other policies; on the main axis over weight and hug-content.
    Widget& SetFixedWidth(float w){ fixedW_ = w; Relayout(); 
        return *this;
    }
    /// Fixed height. Same semantics as SetFixedWidth.
    Widget& SetFixedHeight(float h){ fixedH_ = h; Relayout(); 
        return *this;
    }
    /// Max-width constraint: layout containers never stretch the widget beyond this width (e.g. the section 1080px cap).
    Widget& SetMaxWidth(float w){ maxWidth_ = w; Relayout(); 
        return *this;
    }
    /// Occupies a full row in a Flow container (reference .ex-card.wide's grid-column:1/-1).
    /// Full-row width is min(available width, maxWidth constraint) (extra-wide cards like the settings card cap at 640).
    Widget& SetFlowFullRow(bool full = true) { flowFullRow_ = full; Relayout(); return *this; }
    /// Weight for distributing leftover main-axis space. 0 means excluded from distribution (fixed size or hug-content).
    Widget& SetWeight(float weight){ weight_ = weight; Relayout(); 
        return *this;
    }
    /// Cross-axis alignment, default Stretch (fill the cross-axis space). After an explicit call this acts as
    /// "align-self": container SetChildCrossAlign no longer overrides it (round-15 §3.14).
    Widget& SetCrossAlign(CrossAlign align){ crossAlign_ = align; crossAlignSet_ = true; Relayout(); 
        return *this;
    }
    /// Default cross-axis alignment for container children: applies to children without explicit SetCrossAlign. Effective
    /// only when explicitly called (previously SetChildCrossAlign(Start) was indistinguishable from the Start default — dead config)
    Widget& SetChildCrossAlign(CrossAlign align){ childCrossAlign_ = align; childCrossAlignSet_ = true; Relayout(); 
        return *this;
    }
    /// Unified cross-axis alignment resolution (shared by LayoutChildren/Flow/Overlay, round-15 §3.14):
    /// child's explicit SetCrossAlign (align-self) wins, then the container's explicit SetChildCrossAlign,
    /// otherwise the child's own value (constructor-preset hug semantics unchanged)
    CrossAlign EffectiveCrossAlign(bool containerSet, CrossAlign containerAlign) const {
        return !crossAlignSet_ && containerSet ? containerAlign : crossAlign_;
    }

    /// Cross-axis baseline alignment (CrossAlign::Baseline): distance (DIP) from the first-line text baseline to
    /// the widget's top edge. crossSize is the resolved cross-axis box height (Baseline children are not stretched,
    /// equal to desired or fixed height). Default has no text baseline and synthesizes from the box bottom per CSS;
    /// text widgets (Label/TextBox/Button) override with the DWrite-measured baseline. Read only
    /// by the Baseline alignment branch.
    virtual float BaselineOffset(float crossSize) const { return crossSize; }

    /// Collects the visible text carried by the widget (appended to out, space-separated). Containers recurse the subtree; hidden
    /// subtrees count too — matching the reference textContent semantics that include display:none subtrees
    /// (the match corpus for demo search filtering, round-17 U-01). The edited value of input widgets
    /// is not collected (reference <input>'s value is not part of textContent).
    virtual void CollectText(std::wstring& out) const { (void)out; }

protected:
    Widget() = default;

    /// Triggers a repaint of the hosting window.
    void Invalidate();

    /// Triggers a relayout of the hosting window (dock bars and layout containers both re-flow).
    void Relayout();

    /// Height of a docked widget (menu bar/toolbar/status bar); non-docked widgets return 0.
    virtual float DockHeight() const { return 0.0f; }

    /// Width of a left/right docked widget (navigation sidebar etc.); non-docked widgets return 0.
    virtual float DockWidth() const { return 0.0f; }

    /// Desired size (DIP) queried when a layout container hugs content; default 0.
    virtual Size DesiredSize() const { return {}; }

    /// Whether the widget can take keyboard focus; default no.
    virtual bool Focusable() const { return false; }

    // Keyboard and focus events, delivered only to Focusable widgets:
    virtual void OnFocused() {}
    virtual void OnUnfocused() {}
    virtual void OnChar(wchar_t ch) { (void)ch; }
    /// IME composition window anchor (R35-03): out is a window client-area DIP coordinate — the insertion point
    /// where the composition/candidate window's top-left should stick. Widgets that can provide an anchor (TextBox) override
    /// and return true; default has no anchor, the composition window lands at the system default position.
    virtual bool ImeAnchorPoint(Point& out) const { (void)out; return false; }
    /// Virtual-key code (WM_KEYDOWN's wParam); query Ctrl/Shift state via GetKeyState.
    virtual void OnKeydown(uint32_t vk) { (void)vk; }
    /// Multi-click (clickCount≥2; after the window class includes CS_DBLCLKS this is routed via WM_LBUTTONDBLCLK,
    /// the 3rd click arrives here as 3). pos is in window DIP coordinates.
    virtual void OnDoubleClick(const Point& pos, int clickCount) {
        (void)pos; (void)clickCount;
    }
    /// Window timer (~every 500ms; used for caret blinking etc.).
    virtual void OnTimer() {}

    virtual void OnPaint(Painter& p, const Theme& theme) { (void)p; (void)theme; }
    /// Called after all children finish painting (ScrollViewer pops the clip and draws scrollbars here).
    virtual void OnPostPaint(Painter& p, const Theme& theme) { (void)p; (void)theme; }
    virtual void OnMouseMove(const Point& pos) { (void)pos; }
    virtual void OnMouseLeave() {}
    /// Mouse capture taken away by the system (WM_CAPTURECHANGED: Alt+Tab, popups, etc.): cancel the pressed state.
    /// Do not reuse OnMouseLeave — most widgets' leave clears only hover_, not pressed_,
    /// which would leave the pressed state stuck (round-15 §3.15)
    virtual void OnCaptureLost() {}
    virtual void OnMouseDown(const Point& pos) { (void)pos; }
    virtual void OnMouseUp(const Point& pos) { (void)pos; }
    /// Right-button release (context menu). Default triggers OnContextMenuCb.
    virtual void OnContextMenu(const Point& pos) {
        if (OnContextMenuCb) OnContextMenuCb(pos);
    }
    /// Client-area cursor resource (R36-01): return an IDC_* constant (its expansion is MAKEINTRESOURCEW/A
    /// depending on UNICODE; just pair it with the matching LoadCursorW/A; M-23's "must use
    /// LoadCursorA" was only a ctypes-probe artifact), nullptr means the system default arrow.
    /// Text input widgets override to the I-beam (reference HTML:58
    /// input,textarea{cursor:text} is an explicit declaration, not an accidental browser default).
    virtual const wchar_t* CursorForClient() const { return nullptr; }
    /// Wheel scroll, delta in units of WHEEL_DELTA (120); return true if handled,
    /// otherwise the framework keeps bubbling up through parent widgets.
    virtual bool OnWheel(float delta) { (void)delta; return false; }
    /// Animation frame (~30fps; dispatch starts after StartAnimation).
    virtual void OnAnimate() {}

    /// Subscribes to animation frames (repeat calls have no side effect); StopAnimation cancels.
    void StartAnimation();
    void StopAnimation();

    /// Hit-test (window coordinates), default is rectangle containment; composite widgets (top bars with
    /// embedded child input boxes, etc.) can override to yield clicks to inner widgets.
    virtual Widget* HitTarget(const Point& windowPos) {
        return bounds_.Contains(windowPos.x, windowPos.y) ? this : nullptr;
    }

    /// Child clip region during hit-testing (window coordinates), default no clipping. When a viewport container
    /// (ScrollViewer) overrides, children scrolled out of the viewport no longer participate in hit-testing —
    /// aligned with the draw-side PushClip, otherwise scrolled-away cards would swallow clicks of widgets above/outside the viewport.
    virtual bool ChildHitClip(Rect* out) const { (void)out; return false; }

    /// Viewport clip region for child drawing (window coordinates), default no clipping. When a viewport container overrides,
    /// PaintTree skips subtrees fully scrolled out of the viewport and clips partially visible subtrees to the viewport
    /// (symmetric with ChildHitClip; PERF-02: measured per-frame cost is independent of window area and
    /// proportional to widget count; skipping out-of-viewport subtrees is the dominant win for idle repaint cost).
    virtual bool ChildPaintClip(Rect* out) const { (void)out; return false; }

    friend class Window;
    friend class Container;
    friend struct detail::WindowImpl;
    template <class W> friend struct detail::WindowHitOps;   // UpdateContainerHover touches hoverLit_/parent_
    template <class W> friend struct detail::WindowChromeOps;   // UpdateImeAnchor touches ImeAnchorPoint (R35-03)
    template <class W> friend struct detail::WindowFocusOps;   // SetFocused touches OnFocused/OnUnfocused

    Window* window_ = nullptr;
    Widget* parent_ = nullptr;    // parent/child chain of containers/window (wheel bubbling etc.)
    Rect bounds_{};   // window coordinates (container children too, assigned by layout)
    bool visible_ = true;
    bool enabled_ = true;
    detail::Dock dock_ = detail::Dock::None;
    std::wstring tooltip_;

    float fixedW_ = 0, fixedH_ = 0, weight_ = 0;
    float maxWidth_ = 0;   // 0 means unlimited
    bool flowFullRow_ = false;   // full row in Flow
    CrossAlign crossAlign_ = CrossAlign::Stretch;
    bool crossAlignSet_ = false;   // explicit SetCrossAlign (align-self; container does not override)
    CrossAlign childCrossAlign_ = CrossAlign::Start;   // container children default alignment (effective only after explicit set)
    bool childCrossAlignSet_ = false;
    bool hoverHighlight_ = false;   // container hover highlight (Container::SetHoverHighlight)
    float hoverRadius_ = 6.0f;
    bool hoverLit_ = false;   // currently hovered (framework propagates along the hit chain to ancestor containers)
};

/// Semantic role of label text: color resolved live from the theme (explicit SetTextColor wins).
enum class TextRole { Primary, Secondary, Tertiary, Disabled };

/// Text label.
class Label : public Widget {
public:
    explicit Label(std::wstring_view text = L"") { text_.assign(text.begin(), text.end()); }

    const std::wstring& Text() const { return text_; }
    Label& SetText(std::wstring_view text);

    const Font& GetFont() const { return font_; }
    Label& SetFont(const Font& font);

    /// When unset, the text color is taken from the theme by role (default Primary = theme.text).
    bool HasTextColor() const { return textColor_.has_value(); }
    Color TextColor() const { return textColor_.value_or(Color{}); }
    Label& SetTextColor(const Color& color){ textColor_ = color; Invalidate(); 
        return *this;
    }

    void CollectText(std::wstring& out) const override { out += text_; out += L' '; }
    void ResetTextColor() { textColor_.reset(); Invalidate(); }
    Label& SetTextRole(TextRole role) { role_ = role; Invalidate(); return *this; }

    /// Adds a rounded backing panel to the label (color swatch/demo panel); without it there is no panel.
    /// With a panel, padding is automatically reserved around the text.
    Label& SetPanel(const Color& color){ panel_ = color; gradientStops_.clear(); Relayout(); Invalidate(); 
        return *this;
    }

    /// Panel becomes a linear gradient (material banners etc.). stops needs at least 2 entries; angle follows the CSS convention,
    /// default 135° ("to bottom right"); overridable via SetGradientAngle.
    Label& SetPanelGradient(const std::vector<GradientStop>& stops){
        gradientStops_ = stops;
        panel_.reset();
        Relayout();
        Invalidate();
    
        return *this;
    }

    /// Gradient angle, CSS convention (0° up, 90° right; default 135°, converted per B-01's same convention).
    Label& SetGradientAngle(float deg) { gradientAngle_ = deg; Invalidate(); return *this; }

    /// Panel stroke (dashed for dashed line; used by the right-click area demo).
    Label& SetBorder(const Color& color, bool dashed = false){
        borderColor_ = color;
        borderDashed_ = dashed;
        Invalidate();
    
        return *this;
    }

    /// Leading icon (settings-row icon tiles etc.), drawn 16px left of the text; centered when text is empty.
    Label& SetIcon(const Icon& icon) { icon_ = icon; Relayout(); Invalidate(); return *this; }
    /// Icon ink size (default 16; bare-icon scenes like the icon gallery use 20, A-06).
    Label& SetIconSize(float size) { iconSize_ = size; Relayout(); Invalidate(); return *this; }

    /// Shadow tier (unified with Card): kShadowTierNone=none (default)
    /// kShadowTierCard=content card, kShadowTierFlyout=popup,
    /// kShadowTierDialog=dialog; used by the elevation stack demo.
    Label& SetShadowTier(int tier) { shadowTier_ = tier; Invalidate(); return *this; }

    /// Overrides the shadow color (default black, tiered by theme light/dark; used by the tinted large drop shadow of the top block in the elevation stack demo).
    Label& SetShadowColor(const Color& c) { shadowColor_ = c; Invalidate(); return *this; }

    /// Custom shadow geometry (offset/blur/light-dark opacities); takes priority over SetShadowTier's
    /// tiers (reference .hero .stack .l3's dedicated 0 16px 32px tier).
    Label& SetShadowSpec(const ShadowSpec& s) { customShadow_ = s; Invalidate(); return *this; }

    HAlign HAlignment() const { return hAlign_; }
    Label& SetHAlignment(HAlign align){ hAlign_ = align; Invalidate(); 
        return *this;
    }

    VAlign VAlignment() const { return vAlign_; }
    Label& SetVAlignment(VAlign align){ vAlign_ = align; Invalidate(); 
        return *this;
    }

    bool Wrap() const { return wrap_; }
    Label& SetWrap(bool wrap){ wrap_ = wrap; Invalidate(); 
        return *this;
    }

    /// Corner radius of panel/stroke (default 8; small-area swatches use 6).
    Label& SetCornerRadius(float r) { cornerRadius_ = r; Invalidate(); return *this; }

    /// Text exceeding the content width is truncated with an ellipsis (maps to text-overflow:ellipsis;
    /// prevents non-wrapping labels from crowding adjacent widgets).
    Label& SetEllipsis(bool e = true) { ellipsis_ = e; Invalidate(); return *this; }

protected:
    void OnPaint(Painter& p, const Theme& theme) override;
    Size DesiredSize() const override;   // single-line text measurement (fix width/height for wrapping text)
    float BaselineOffset(float crossSize) const override;

private:
    std::wstring text_;
    Font font_{};
    std::optional<Color> textColor_;
    TextRole role_ = TextRole::Primary;
    std::optional<Color> panel_;  // rounded backing panel (absent = plain text label)
    std::vector<GradientStop> gradientStops_;   // when non-empty the panel is a gradient
    float gradientAngle_ = 135.0f;   // gradient angle (CSS convention, B-01)
    std::optional<Color> borderColor_;
    bool borderDashed_ = false;
    Icon icon_{};
    float iconSize_ = 16.0f;   // icon ink size (overridden by SetIconSize)
    int shadowTier_ = kShadowTierNone;
    std::optional<Color> shadowColor_;   // shadow color override (tinted large drop shadow)
    std::optional<ShadowSpec> customShadow_;   // shadow geometry override (takes priority over tiers)
    HAlign hAlign_ = HAlign::Left;
    VAlign vAlign_ = VAlign::Center;
    bool wrap_ = false;
    float cornerRadius_ = 8.0f;
    bool ellipsis_ = false;
    // ellipsis draw-string scratch (PERF62-01): a local std::wstring here exceeded SSO on every
    // paint of an overflowing label; a member reuses its capacity across frames (PERF57-01 pattern)
    std::wstring ellipsisScratch_;
};

/// Button style variants (maps to the demo library: standard/accent/subtle/outline/hyperlink/icon button).
enum class ButtonStyle { Standard, Accent, Subtle, Outline, Hyperlink, IconOnly };

/// Button: 32-high, corner-radius-4 control with hover and pressed states, optional icon, toggle state, and dot badge.
class Button : public Widget {
public:
    explicit Button(std::wstring_view text = L"",
                    std::function<void()> onClick = {})
        : OnClick(std::move(onClick)) {
        text_.assign(text.begin(), text.end());
        crossAlign_ = CrossAlign::Start;   // button hugs its width, not stretched by the container
    }

    const std::wstring& Text() const { return text_; }
    Button& SetText(std::wstring_view text);

    const Font& GetFont() const { return font_; }
    Button& SetFont(const Font& font);

    ButtonStyle Style() const { return style_; }
    Button& SetStyle(ButtonStyle style) { style_ = style; Relayout(); Invalidate(); return *this; }

    /// Convenience: accent button.
    bool Accent() const { return style_ == ButtonStyle::Accent; }
    Button& SetAccent(bool accent){
        style_ = accent ? ButtonStyle::Accent : ButtonStyle::Standard;
        Relayout();
        Invalidate();
    
        return *this;
    }

    /// Overrides the icon ink size (0 = auto: 20 for 36px+ controls, otherwise 16). The reference top bar's
    /// theme button uses .i.lg 20px on a 32×32 button, requiring an explicit override (round-17 I-04).
    Button& SetIconSize(float size) { iconSize_ = size; Invalidate(); return *this; }

    void CollectText(std::wstring& out) const override { out += text_; out += L' '; }

    /// Leading icon.
    Button& SetIcon(const Icon& icon) { icon_ = icon; Relayout(); Invalidate(); return *this; }

    /// Toggle button: clicks alternate between pressed/released, shown with the selected background while toggled.
    Button& SetToggleable(bool toggleable) { toggleable_ = toggleable; return *this; }
    bool Toggled() const { return toggled_; }
    Button& SetToggled(bool toggled) { toggled_ = toggled; Invalidate(); return *this; }

    /// Accent-colored dot badge at the icon button's top-right (2px background-color stroke ring; attachable to any style).
    Button& SetDot(bool dot) { dot_ = dot; Invalidate(); return *this; }

    /// Count badge for text buttons (small accent capsule right of the text, e.g. "Inbox 12").
    Button& SetBadge(int count) { badge_ = count; Relayout(); Invalidate(); return *this; }

    /// Overrides the text color (e.g. error color for destructive actions); when unset, color follows the style.
    Button& SetTextColorOverride(const Color& color) { textOverride_ = color; Invalidate(); return *this; }

    /// Click callback: fires when fully pressed and released inside the button.
    std::function<void()> OnClick;

protected:
    bool Focusable() const override { return true; }
    void OnKeydown(uint32_t vk) override;
    void OnPaint(Painter& p, const Theme& theme) override;
    Size DesiredSize() const override;
    float BaselineOffset(float crossSize) const override;
    void OnMouseMove(const Point& pos) override;
    void OnMouseLeave() override;
    void OnCaptureLost() override;
    void OnMouseDown(const Point& pos) override;
    void OnMouseUp(const Point& pos) override;

private:
    void Activate();   // space/enter trigger shared with mouse click

    std::wstring text_;
    Font font_{};
    ButtonStyle style_ = ButtonStyle::Standard;
    Icon icon_{};
    float iconSize_ = 0.0f;   // >0 overrides auto icon size (I-04)
    bool toggleable_ = false;
    bool toggled_ = false;
    bool dot_ = false;
    int badge_ = 0;
    std::optional<Color> textOverride_;
    bool hover_ = false;
    bool pressed_ = false;
};

/// Single-line text input: click to focus; supports selection, clipboard (Ctrl+C/X/V/A),
/// arrow keys/Home/End, and horizontal scrolling; direct input from CJK IMEs goes through the system default WM_CHAR
/// path, with composition/candidate windows anchored via IMM32 to the owner-drawn caret (R35-03).
class TextBox : public Widget {
public:
    explicit TextBox(std::wstring_view text = L"") {
        text_.assign(text.begin(), text.end());
        caret_ = anchor_ = text_.size();
        crossAlign_ = CrossAlign::Start;
    }

    const std::wstring& Text() const { return text_; }
    /// Sets text programmatically (does not fire OnChanged).
    TextBox& SetText(std::wstring_view text);

    const Font& GetFont() const { return font_; }
    TextBox& SetFont(const Font& font);

    /// Placeholder text: shown in disabled color when the content is empty and the box is unfocused.
    const std::wstring& Placeholder() const { return placeholder_; }
    TextBox& SetPlaceholder(std::wstring_view text){
        placeholder_.assign(text.begin(), text.end());
        Invalidate();
    
        return *this;
    }

    /// Fires when the text changes.
    std::function<void()> OnChanged;

    /// Password mode: content shown as dots; when revealable is true a small eye button appears in the box,
    /// clicking toggles plaintext display.
    TextBox& SetPassword(bool revealable = true);
    bool Password() const { return password_; }
    bool Revealed() const { return revealed_; }
    TextBox& SetRevealed(bool revealed);

    /// When there is content, shows a clear button at the right inside the box (one click clears all).
    TextBox& SetClearButton(bool enabled){ clearButton_ = enabled; Invalidate(); 
        return *this;
    }

    /// Leading icon inside the box (search box magnifier etc.), occupying 22px on the left of the content area (24px in the search box variant).
    TextBox& SetLeadingIcon(const Icon& icon) { leadingIcon_ = icon; Relayout(); Invalidate(); return *this; }

    /// Search box variant (reference .searchbox): embedded buttons 22×22 (.tb is 24×24),
    /// 8px gap after the leading icon (.tb's flex gap is 6px), font size 13px (.tb input is 14px);
    /// turning the variant off restores the .tb font size. Used by the TopBar search bar.
    TextBox& SetSearchBoxStyle(bool search = true) {
        searchStyle_ = search;
        font_.size = search ? 13.0f : 14.0f;   // reference .searchbox input 13px / .tb input 14px (N-03)
        EnsureCaretVisible();
        Relayout(); Invalidate(); return *this;
    }

    /// AutoSuggest: pops a filtered suggestion list on focus or input.
    /// Clicking a suggestion replaces the text with the suggestion content and fires OnSuggestion.
    TextBox& SetSuggestions(std::vector<std::wstring> items);
    std::function<void(const std::wstring&)> OnSuggestion;

    /// Enter commit (round-70 R12): fires when the box receives VK_RETURN and this callback is
    /// subscribed (the current text is passed; fire-on-Enter regardless of emptiness — filter
    /// in the callback). The key is fully consumed either way: with a focused widget,
    /// WM_KEYDOWN never reaches DefWindowProc. Higher-priority interceptors are unchanged and
    /// run first — the dialog modal branch (Enter commits the focused footer button), window
    /// accelerators (API-11, matched before the focused widget) and an open AutoSuggest popup
    /// (Enter accepts the suggestion). Enter during IME composition arrives as VK_PROCESSKEY
    /// and never reaches here. Enter-commit does not blur — a later blur fires OnFocusLost
    /// again, so "Enter or blur" consumers fence double commits (the requirement's documented
    /// contract; the fence on our side: removal clears focus silently, see OnFocusLost).
    std::function<void(const std::wstring&)> OnCommitted;

    /// Focus lost (round-70 R12): fires after OnUnfocused for genuine focus transfers — click
    /// elsewhere, Tab, programmatic SetFocus, and window deactivation (WM_ACTIVATE WA_INACTIVE
    /// already blurs: long-standing library behavior, unlike native controls that keep focus
    /// while inactive). Fires regardless of whether the text changed. NOT fired when the box
    /// is removed mid-edit: DetachTree clears focus silently (no user callbacks while a
    /// subtree is mid-detach, ARCH39-02), so a commit-and-rebuild flow cannot double-fire
    /// through the removal it performs inside its own commit callback.
    std::function<void()> OnFocusLost;

protected:
    bool Focusable() const override { return true; }
    Size DesiredSize() const override;   // height 32, width at least 200
    float BaselineOffset(float crossSize) const override;
    void SetBounds(const Rect& bounds) override;   // R38-01: re-clamp scrollX_ after bounds assignment
    void OnPaint(Painter& p, const Theme& theme) override;
    void OnMouseMove(const Point& pos) override;
    void OnMouseDown(const Point& pos) override;
    void OnMouseUp(const Point& pos) override;
    void OnMouseLeave() override;
    void OnCaptureLost() override;
    void OnFocused() override;
    void OnUnfocused() override;
    void OnChar(wchar_t ch) override;
    void OnKeydown(uint32_t vk) override;
    bool ImeAnchorPoint(Point& out) const override;
    void OnDoubleClick(const Point& pos, int clickCount) override;
    void OnTimer() override;
    const wchar_t* CursorForClient() const override;
    /// Context menu: when OnContextMenuCb is unset, pops the default text menu (cut/copy/paste/
    /// select all, matching a browser-native input's right-click capability, R36-03); if set, the user takes over.
    void OnContextMenu(const Point& pos) override;

private:
    size_t IndexAt(float localX) const;   // insertion point from widget-local x
    float CaretX(size_t index) const;     // widget-local x of the insertion point
    /// Mask string (PERF57-01): member scratch shared by drawing (OnPaint) and measurement (CaretX);
    /// grow-only, content always U+2022; resized on demand then a view of the first n code units taken — mask lengths
    /// ≥8 code units exceed wstring SSO; previously every repaint/caret update built a new string (1 heap allocation,
    /// ≥2 per repaint for a focused masked box). The returned view points into the member string; do not keep it across subsequent calls
    std::wstring_view MaskedText(size_t n) const {
        if (maskScratch_.size() < n) maskScratch_.resize(n, L'\x2022');
        return std::wstring_view(maskScratch_).substr(0, n);
    }
    size_t WordStart(size_t pos) const;   // word start (skips the preceding punctuation/whitespace run)
    size_t WordEnd(size_t pos) const;     // word end (trailing whitespace skipped with the word)
    /// UTF-16 code-point stepping (R37-01): surrogate pairs (emoji, CJK Extension B and other non-BMP
    /// characters) move as a whole. Shared by backspace/delete/arrow keys — previously backspace/delete cut at code-unit granularity,
    /// leaving orphan surrogates (rendered as replacement glyphs, corrupting all subsequent measurement)
    size_t PrevCodePoint(size_t pos) const;
    size_t NextCodePoint(size_t pos) const;
    float ContentLeft() const;            // leading icon space + left padding
    float ContentRight() const;           // width occupied by embedded buttons on the right
    Rect ButtonRect(int index) const;     // 0=clear 1=eye (widget-local)
    void MoveCaret(size_t index, bool extendSelection);
    void EraseSelection();
    void InsertText(std::wstring_view text, bool coalesceUndo = false);
    void CopyToClipboard(bool cut);
    void PasteFromClipboard();
    void EnsureCaretVisible();
    void UpdateSuggestPopup();
    void CloseSuggestPopup();
    void NotifyChanged();

    // ---- Undo/redo (R36-02, aligned with the reference native <input> built-in capability) ----
    struct UndoState { std::wstring text; size_t caret; size_t anchor; };
    static constexpr size_t kUndoLimit = 100;   // step cap (prevents unbounded growth in long sessions)
    std::vector<UndoState> undoStack_, redoStack_;
    bool typingGroup_ = false;   // true=consecutive character input coalesces into the previous undo step
    void SaveUndo(bool coalesce);   // push before a change; coalesce=merge into the consecutive-input step
    void Undo();
    void Redo();
    void RestoreUndoState(UndoState&& st);

    std::wstring text_;
    std::wstring placeholder_;
    Font font_{};
    size_t caret_ = 0;    // insertion point (UTF-16 code-unit index)
    size_t anchor_ = 0;   // selection anchor
    float scrollX_ = 0;   // horizontal scroll offset
    bool dragging_ = false;
    bool hover_ = false;
    bool caretVisible_ = true;
    bool password_ = false, revealed_ = false, revealable_ = false;
    bool clearButton_ = false;
    bool searchStyle_ = false;   // reference .searchbox variant (G-1/G-2)
    int hoverBtn_ = -1;   // hovered embedded button: 0=clear 1=eye
    int pressedBtn_ = -1;   // pressed embedded button (pressed state, committed on release; reference .icon-btn:active)
    std::vector<std::wstring> suggestions_;
    bool suggestActive_ = false;
    Icon leadingIcon_{};
    mutable std::wstring maskScratch_;   // MaskedText scratch (reused even though CaretX is const)
};

// ---------------------------------------------------------------------------
// Title bar
// ---------------------------------------------------------------------------

/// Which built-in system buttons the title bar shows (bitwise OR-able; round-65 R1).
/// Visibility only — the maximize *behavior* (Win+Up, caption double-click, Aero Snap,
/// system-menu entry) is governed independently by Window::SetMaximizable.
enum class WindowButtons : unsigned {
    None     = 0,
    Minimize = 1 << 0,
    Maximize = 1 << 1,
    Close    = 1 << 2,
    Default  = Minimize | Maximize | Close,
};

constexpr WindowButtons operator|(WindowButtons a, WindowButtons b) {
    return static_cast<WindowButtons>(static_cast<unsigned>(a) |
                                      static_cast<unsigned>(b));
}
constexpr WindowButtons operator&(WindowButtons a, WindowButtons b) {
    return static_cast<WindowButtons>(static_cast<unsigned>(a) &
                                      static_cast<unsigned>(b));
}
constexpr bool HasWindowButton(WindowButtons buttons, WindowButtons flag) {
    return (static_cast<unsigned>(buttons) & static_cast<unsigned>(flag)) != 0;
}

/// Owner-drawn title bar (created and docked at the very top automatically by the window): app badge, window title, and
/// minimize/maximize/close buttons. Apps need not and should not create or lay out this widget manually; they configure it
/// through Window::GetTitleBar() (round-65 R1): badge visibility, the system-button set, custom caption buttons (with
/// toggle state and per-button tooltips), a right-click takeover via the inherited OnContextMenuCb (round-66 R5: fires on
/// the real physical caption right-click, not only on synthetic messages), the band height (round-66 R6), the toggled
/// fill visibility (round-66 R6) and the row's right-edge inset (round-67 R7).
class TitleBar : public Widget {
public:
    const std::wstring& Text() const { return text_; }
    TitleBar& SetText(std::wstring_view text);

    /// Shows/hides the app badge (gradient rounded square + title first letter) at the left edge.
    /// Default visible. When hidden the badge area is caption drag space (no HTSYSMENU).
    TitleBar& SetBadgeVisible(bool visible);
    bool BadgeVisible() const { return badgeVisible_; }

    /// Configures which built-in system buttons are shown (default WindowButtons::Default).
    /// Visibility only; the maximize behavior is Window::SetMaximizable's concern.
    TitleBar& SetSystemButtons(WindowButtons buttons);
    WindowButtons SystemButtons() const { return systemButtons_; }

    /// Adds a custom caption button to the LEFT of the system buttons (same 40x32 hit box, hover/press
    /// fills and 16 DIP icon area as the built-in ones; round-65 R1.2-2). The callback fires on release
    /// inside the button (dragging out cancels, same semantics as dialog buttons U-02). Returns the
    /// button index (insertion order, left-to-right) for Toggled/SetToggled/SetButtonToolTip. Buttons
    /// live for the window's lifetime (no removal).
    int AddButton(const Icon& icon, std::function<void()> onClick);

    /// Toggle variant: clicking flips the state first, then fires onToggled(newState). A toggled
    /// button keeps a subtle active fill and tints its icon with the accent color.
    int AddToggleButton(const Icon& icon, std::function<void(bool)> onToggled);

    /// Reads/writes a toggle button's state. SetToggled repaints but does NOT fire onToggled
    /// (out-of-range index: no-op / false).
    bool Toggled(int index) const;
    TitleBar& SetToggled(int index, bool on);

    /// Per-button hover tooltip (shown by the standard hover tooltip machinery).
    TitleBar& SetButtonToolTip(int index, std::wstring_view text);
    const std::wstring& ButtonToolTip(int index) const;

    /// Caption band height in DIP (default 42; round-66 R6). The band layout, hit test and every
    /// caption geometry derive from it. Caption buttons keep their 32 DIP box and are centered
    /// vertically, so a 32 DIP bar (the Windows standard height) makes them fill the band
    /// edge-to-edge with no gap above or below. Values below the button height clamp to it —
    /// the box is never clipped. Triggers a window relayout.
    TitleBar& SetHeight(float height);
    float Height() const { return height_; }

    /// A toggled custom button draws a persistent subtleActive fill by default (extending up to
    /// the band top, like every caption fill since round-64). Turn this off to signal the state
    /// through the icon accent tint alone (round-66 R6); icon colors are unaffected.
    TitleBar& SetToggledFillVisible(bool visible);
    bool ToggledFillVisible() const { return toggledFill_; }

    /// Caption button row right-edge inset in DIP (round-67 R7): the gap between the rightmost
    /// caption button and the window's right edge. The default is automatic: one scaled border
    /// width (R32-07), leaving the rightmost b DIP of the band to the HTRIGHT resize zone so the
    /// buttons' hit box and draw box coincide; maximized, the automatic inset is 0, consistent
    /// with the system. Pass an explicit value to override it in every window state (0 = the
    /// close button hugs the window's top-right corner, like a maximized system window; the
    /// topmost corner strip keeps its HTTOPRIGHT priority) or a negative value to restore the
    /// automatic behavior. Hit geometry and drawing share the same rects: hover/press fills and
    /// WM_NCHITTEST move together. Changing the value clears any stale caption hover/press.
    TitleBar& SetCaptionInset(float insetDip);
    /// Effective right-edge inset (DIP): the explicit value when set, otherwise the automatic
    /// one above (border width scaled by the window DPI; 0 while maximized or before the
    /// window exists).
    float CaptionInset() const;

protected:
    float DockHeight() const override;

    void OnPaint(Painter& p, const Theme& theme) override;

    // Custom caption buttons are client islands inside the non-client band (the rest of the bar is
    // HTCAPTION): clicks and moves over them arrive as client messages dispatched to the title bar.
    void OnMouseMove(const Point& pos) override;
    void OnMouseLeave() override;
    void OnMouseDown(const Point& pos) override;
    void OnMouseUp(const Point& pos) override;
    void OnCaptureLost() override;

private:
    friend struct detail::WindowImpl;
    template <class W> friend struct detail::WindowChromeOps;   // SetCaptionHover private write (R32-02)
    friend class Window;

    /// Non-client hit: returns HTMINBUTTON/HTMAXBUTTON/HTCLOSE/HTSYSMENU/HTCAPTION
    /// (Win32 values); returns 0 outside the title bar.
    int HitTest(const Point& windowDip) const;
    int CaptionHover() const { return captionHover_; }
    TitleBar& SetCaptionHover(int ht);
    int CaptionPressed() const { return captionPressed_; }
    TitleBar& SetCaptionPressed(int ht);
    bool WindowActive() const;
    bool WindowMaximizable() const;
    Rect ButtonRect(int index) const;   // slot index of the visible system buttons (rightmost = last)
    Rect BadgeRect() const;             // app badge (HTSYSMENU icon area, R32-09)

    struct CaptionButton {
        Icon icon;
        std::function<void()> onClick;
        std::function<void(bool)> onToggled;
        bool toggle = false;
        bool on = false;
        std::wstring tooltip;
    };
    static constexpr float kCapW = 40.0f;   // caption button hit/draw box (R32-06)
    static constexpr float kCapH = 32.0f;
    int SystemSlotCount() const;
    Rect CustomButtonRect(int index) const;
    int CustomButtonAt(const Point& windowDip) const;   // index into customButtons_, or -1
    void CommitCustom(int index);

    std::wstring text_;
    int captionHover_ = 0;        // hit code, 0 means no hover
    int captionPressed_ = 0;      // hit code of the pressed button, 0 means none (R32-01/05)
    WindowButtons systemButtons_ = WindowButtons::Default;
    bool badgeVisible_ = true;
    std::vector<CaptionButton> customButtons_;   // insertion order = left-to-right
    int customHover_ = -1;        // index into customButtons_, -1 = none
    int customPressed_ = -1;      // index of the pressed custom button, -1 = none
    float height_ = 42.0f;        // caption band height (R6): default keeps every pre-0.49 geometry
    bool toggledFill_ = true;     // toggled custom buttons draw the subtleActive fill (R6)
    float captionInsetOverride_ = -1.0f;   // explicit right-edge inset (R7); <0 = automatic (R32-07)
};

// ---------------------------------------------------------------------------
// Menu bar
// ---------------------------------------------------------------------------

/// Dropdown menu (created by MenuBar).
class Menu {
public:
    /// Adds a menu item; the callback fires on click.
    Menu& AddItem(std::wstring_view text, std::function<void()> onClick);

    /// Adds a menu item with icon and shortcut hint (the shortcut is display-only text, no hotkey registered).
    Menu& AddItem(const Icon& icon, std::wstring_view text,
                  std::wstring_view shortcut, std::function<void()> onClick);

    /// Adds a separator.
    Menu& AddSeparator();

    /// Adds a checkable menu item; clicking automatically flips the checked state.
    Menu& AddCheckItem(std::wstring_view text, bool checked,
                       std::function<void()> onClick);

    /// Removes all items (API62-01, round-62): one Menu object becomes reusable across shows.
    /// Popups already open hold their own copies (ShowContextMenu/AddMenuButton copy), unaffected; MenuBar popups instead reference the Menu live (raw pointer), so Clear() on a Menu returned by MenuBar::AddMenu while its popup is open blanks that popup (stale geometry, empty list) until closed.
    void Clear();

    /// Marks the previously added item disabled (chainable: AddItem(...).Disabled()).
    Menu& Disabled();

    /// Marks the previously added item selected (chainable: AddItem(...).Selected();
    /// reference .mi.on full-bar accent fill).
    Menu& Selected();

    const std::wstring& Title() const { return title_; }

private:
    friend class MenuBar;
    friend class detail::MenuPopup;
    friend class detail::Flyout;
    friend class SplitButton;
    friend class CommandBar;
    friend class Window;
    struct Item {
        bool separator = false;
        bool checkable = false;
        bool checked = false;
        bool disabled = false;
        bool selected = false;
        Icon icon{};
        std::wstring text;
        std::wstring shortcut;
        std::function<void()> onClick;
    };
    std::wstring title_;
    std::vector<Item> items_;
};

/// Top menu bar (docked widget).
class MenuBar : public Widget {
public:
    ~MenuBar() override;

    /// Adds a dropdown menu; the returned reference can be further filled with AddItem/AddSeparator/AddCheckItem.
    Menu& AddMenu(std::wstring_view title);

    /// Removes all menus and closes an open popup (API62-01, round-62).
    void Clear();

protected:
    float DockHeight() const override;

    void OnPaint(Painter& p, const Theme& theme) override;
    void OnMouseMove(const Point& pos) override;
    void OnMouseLeave() override;
    void OnMouseUp(const Point& pos) override;

private:
    friend class Window;
    friend struct detail::WindowImpl;
    template <class W> friend struct detail::WindowPopupOps;   // CloseMenus collapses the menu bar (R28)
    friend class detail::MenuPopup;

    Rect TitleRect(int index) const;   // widget-local coordinates
    int TitleAt(const Point& local) const;
    void OpenMenu(int index);
    void SwitchMenu(int index);
    void CloseMenu();
    void LayoutTitles() const;

    std::vector<std::unique_ptr<Menu>> menus_;
    mutable std::vector<Rect> titleRects_;
    mutable bool titlesDirty_ = true;
    int hover_ = -1;
    int open_ = -1;
    std::unique_ptr<detail::MenuPopup> popup_;
};

/// Top toolbar (docked widget): horizontal arrangement of icon/text buttons and separators.
class ToolBar : public Widget {
public:
    /// Adds a text button.
    ToolBar& AddButton(std::wstring_view text, std::function<void()> onClick);

    /// Adds an icon button (icon + optional text).
    ToolBar& AddButton(const Icon& icon, std::function<void()> onClick);
    ToolBar& AddButton(const Icon& icon, std::wstring_view text,
                       std::function<void()> onClick);

    /// Adds a separator (rendered as a spacing gap, no line drawn).
    ToolBar& AddSeparator();

    /// Adds a flexible spacer: pushes subsequent items toward the toolbar's right side.
    ToolBar& AddSpace();

    /// Adds a chip button (white rounded pill + icon + text), suited to the toolbar's right side;
    /// returns the item index (used by SetItemText/EnableItem).
    int AddChip(const Icon& icon, std::wstring_view text,
                std::function<void()> onClick);

    /// Updates item text (text button or chip).
    ToolBar& SetItemText(int index, std::wstring_view text);

    /// Enables/disables an item (disabled renders gray and ignores clicks).
    ToolBar& EnableItem(int index, bool enabled);

    /// Removes all items (API62-01, round-62); hover/pressed state reset.
    void Clear();

protected:
    float DockHeight() const override;

    void OnPaint(Painter& p, const Theme& theme) override;
    void OnMouseMove(const Point& pos) override;
    void OnMouseLeave() override;
    void OnMouseDown(const Point& pos) override;
    void OnMouseUp(const Point& pos) override;
    void OnCaptureLost() override;

private:
    friend struct detail::WindowImpl;
    struct Item {
        bool separator = false;
        bool space = false;
        bool chip = false;
        bool enabled = true;
        Icon icon{};
        std::wstring text;
        std::function<void()> onClick;
        Rect rect{};   // widget-local coordinates
    };
    int ItemAt(const Point& local) const;
    void LayoutItems() const;

    mutable std::vector<Item> items_; // rect field is layout cache
    mutable bool itemsDirty_ = true;
    int hover_ = -1;
    int pressed_ = -1;
};

/// Bottom status bar (docked widget): segmented text; segments with width <= 0 are flexible (split remaining width evenly,
/// text right-aligned, for status info on the window's right side).
class StatusBar : public Widget {
public:
    /// Adds a segment, returning its index. width <= 0 means a flexible segment.
    int AddSection(std::wstring_view text, float width = 0.0f);

    StatusBar& SetSectionText(int index, std::wstring_view text);
    std::wstring SectionText(int index) const;

    /// Shows an accent-colored dot before a segment's text (e.g. a "Ready" status indicator).
    StatusBar& SetSectionDot(int index, bool dot);

    /// Removes all segments (API62-01, round-62); later AddSection calls re-index from 0.
    void Clear();

protected:
    float DockHeight() const override;

    void OnPaint(Painter& p, const Theme& theme) override;

private:
    friend struct detail::WindowImpl;
    struct Section {
        std::wstring text;
        float width;
        bool dot = false;
    };
    std::vector<Section> sections_;
};

// ---------------------------------------------------------------------------
// Layout containers
// ---------------------------------------------------------------------------

/// Layout container base: children are owned and auto-arranged by the container; not directly instantiable (use Row/Column).
/// A top-level container (added directly to the window) auto-fills the content area outside dock bars and re-flows on window changes;
/// nested containers are arranged by their parent under the same sizing policies. Child coordinates remain window coordinates — callers need not care.
///
/// Main-axis size: fixed (SetFixedHeight/SetFixedWidth) > weight (SetWeight) >
/// hug-content (decided by the widget's DesiredSize);
/// cross-axis size: fixed > hug-content, alignment decided by SetCrossAlign (default Stretch).
class Container : public Widget {
public:
    Container(const Container&) = delete;
    Container& operator=(const Container&) = delete;

    /// Adds a child widget; the returned reference allows further configuration (the container owns the widget).
    template <class T, class... Args>
    T& Add(Args&&... args);

    /// Removes a child (API-06): returns whether it was found and removed. Widgets are owned by the container; destruction is deferred
    /// to end of frame — **attached containers only** (ARCH40-04: on a detached container this function destroys children
    /// synchronously; do not hold their references after the callback returns; attached path ARCH38-07: the callback stack may
    /// still hold the widget pointer, flushed uniformly by Run/PumpOnce; consumers with their own message loops must
    /// call PumpOnce periodically for destruction to happen); animation-frame subscriptions, keyboard focus, mouse capture, and
    /// hover within that subtree are cleaned up as well.
    bool Remove(Widget& child);

    /// Removes and destroys all children (API-06; attached containers also defer destruction to end of frame, detached
    /// containers destroy synchronously — same ARCH40-04 convention).
    void ClearChildren();

    /// Child count and by-index access (API-07; order is Add order, which BringToFront/
    /// SendToBack can change). Consumers may also keep the reference returned by Add themselves.
    /// Out-of-range semantics (API50-02): ChildAt throws std::out_of_range (.at() convention),
    /// alongside SetLastInset's "empty container is silently harmless" and ListView::SetSelectedIndex's
    /// "out-of-range silently ignored" as explicit library conventions.
    size_t ChildCount() const { return children_.size(); }
    Widget& ChildAt(size_t index) const { return *children_.at(index); }

    /// Z-order control (API-08): stacking and paint order equal child order (Overlay too).
    /// BringToFront moves to the topmost layer, SendToBack to the bottommost; returns whether a move happened.
    bool BringToFront(Widget& child);
    bool SendToBack(Widget& child);

    /// Main-axis spacing between children.
    Container& SetSpacing(float spacing){ spacing_ = spacing; Relayout(); 
        return *this;
    }

    /// Padding on all four container sides.
    Container& SetPadding(float padding){
        padL_ = padR_ = padT_ = padB_ = padding;
        Relayout();
    
        return *this;
    }

    /// Independent per-side padding (for content-area whitespace).
    Container& SetPadding(float left, float top, float right, float bottom){
        padL_ = left; padT_ = top; padR_ = right; padB_ = bottom;
        Relayout();
    
        return *this;
    }

    /// Hover highlight: when the mouse hovers over itself or any descendant, draws a soft tinted
    /// rounded backing before the children (maps to .srow:hover / .ic-cell:hover full-row/full-cell feedback).
    Container& SetHoverHighlight(bool enable = true, float cornerRadius = 6.0f) {
        hoverHighlight_ = enable;
        hoverRadius_ = cornerRadius;
        Invalidate();
        return *this;
    }

    void CollectText(std::wstring& out) const override {
        for (const auto& c : children_) c->CollectText(out);
    }

protected:
    explicit Container(bool vertical) : vertical_(vertical) {}

    /// Arranges visible children into area (the padding-adjusted available region), recursing into sub-containers.
    virtual void AssignBounds(const Rect& area);
    void LayoutChildren(const Rect& area);
    Size DesiredSize() const override;
    void OnPaint(Painter& p, const Theme& theme) override;

    /// A child is about to be removed (ARCH39-03): Remove calls back before erasing (child is still in children_,
    /// so compare/locate by pointer); ClearChildren calls back once with nullptr before the batch clear.
    /// Composite widgets that cached child pointers (Expander/TopBar/Tabs) reset their caches here,
    /// preventing dangling pointers after public Remove/ClearChildren.
    /// ARCH39-03: notifies the **direct parent container** when a child is removed (Remove calls back with child before erasing,
    /// ClearChildren calls back once with nullptr before the batch clear) — composite widgets caching child pointers
    /// reset their caches here. Note the notification surface reaches only the direct parent (ARCH40-03, structural gap): other composite
    /// widgets' caches inside the removed node's subtree get no notification — under the current "removal is destruction" ownership model
    /// they are destroyed with the subtree via the deferred-destruction queue and thus unreachable; if a "detach without
    /// destroying" API is added later, the notification surface must widen in lockstep, otherwise composite-widget caches become UAF immediately
    virtual void OnChildRemoved(Widget* child) { (void)child; }

    /// Called after a child enters the tree (GAP43-02, §2.4 notification-surface completion): invoked after Container::Add
    /// successfully pushes into children_ (child is already in the tree; register by dynamic_cast type check),
    /// symmetric with OnChildRemoved. **The virtual call dispatches on the actual object** — via base-class upcast
    /// (`Container& c = tabs; c.Add<TabPage>()`, the M-66 path) it is covered too,
    /// which name hiding cannot do. Composite widgets whose caches/registries depend on children_ content
    /// register new children here (Tabs uses it to converge Add<TabPage>() and AddPage onto one registration path).
    virtual void OnChildAdded(Widget* child) { (void)child; }

    /// Called after child Z-order reorder (GAP43-01, §2.4 notification-surface completion): invoked after BringToFront/
    /// SendToBack successfully permutes children_ order (not called when nothing moved). Derived caches mirroring
    /// children_ order (e.g. Tabs::tabRects_) mark dirty here. Before round-42, among the "add/remove/reorder"
    /// three change kinds, reorder had no notification — the structural gap behind mirror-cache desync
    /// (root cause of the 4th recurrence of this defect class, see the round-43 review §2.4).
    virtual void OnChildrenReordered() {}

    // For derived containers (Flow/Banner/Overlay) to manipulate children (protected cross-object access must go through these)
    static Size ChildDesired(const Widget* w) { return w->DesiredSize(); }
    static float ChildFixedW(const Widget* w) { return w->fixedW_; }
    static float ChildFixedH(const Widget* w) { return w->fixedH_; }
    static float ChildMaxWidth(const Widget* w) { return w->maxWidth_; }
    static bool ChildFlowFullRow(const Widget* w) { return w->flowFullRow_; }
    static void AssignChild(Widget* w, const Rect& r) {
        if (auto* nested = dynamic_cast<Container*>(w)) nested->AssignBounds(r);
        else w->SetBounds(r);
    }

    friend struct detail::WindowImpl;
    template <class W> friend struct detail::WindowHitOps;    // HitTestAt/FindScroller iterate children_
    template <class W> friend struct detail::WindowFocusOps;   // CycleFocus/radio-group mutual exclusion iterate children_

    bool vertical_ = false;
    float spacing_ = 0;
    float padL_ = 0, padT_ = 0, padR_ = 0, padB_ = 0;
    std::vector<std::unique_ptr<Widget>> children_;

    // ---- LayoutChildren scheduling snapshot pool (PERF57-02) ----
    struct Plan {
        Widget* widget = nullptr;
        float main = 0;          // main-axis size (weighted items computed at distribution time)
        float fixedCross = 0;    // fixed cross-axis size, 0 means not fixed
        float desiredCross = 0;  // desired cross-axis size
        float weight = 0;
        bool weighted = false;
        float baseline = 0;      // Baseline alignment: first-line baseline to top-edge distance
    };
    /// Acquires this pass's plans slot (each depth allocates once on first arrival, then clear-and-reuse).
    /// deque: pool growth never invalidates references to outer slots already taken (safe under nested recursion)
    std::vector<Plan>& AcquirePlanBuf() {
        if (planDepth_ == planPool_.size()) planPool_.emplace_back();
        return planPool_[planDepth_++];
    }
    void ReleasePlanBuf() { --planDepth_; }
    std::deque<std::vector<Plan>> planPool_;
    size_t planDepth_ = 0;   // depth currently in use (LayoutChildren nesting/re-entry level)
};

/// Vertical layout container: main axis is vertical, children stacked top to bottom.
class Column : public Container {
public:
    Column() : Container(true) {}
};

/// Horizontal layout container: main axis is horizontal, children laid out left to right.
class Row : public Container {
public:
    Row() : Container(false) {}
};

/// Flow container: children laid out left to right by content width, wrapping automatically when exceeding
/// the available width (maps to the web auto-fill/minmax grid). Children without a fixed width get
/// max(content width, minItemWidth), capped at the available width.
class Flow : public Container {
public:
    Flow() : Container(false) {}

    /// Minimum width for children without a fixed width (0 means hug-content).
    Flow& SetMinItemWidth(float w) { minItemW_ = w; Relayout(); return *this; }

protected:
    void AssignBounds(const Rect& area) override;
    Size DesiredSize() const override;

private:
    float minItemW_ = 0;
    mutable float desiredH_ = 0;   // total wrapped height from the last actual layout
};

/// Absolute-positioning container: child rect = container rect inset by the child's four sides (maps to web absolute +
/// inset); later children draw on top. Used for overlapping compositions like the Hero elevation stack. Each child, once
/// added, needs one SetLastInset call (or SetInset for explicit per-child positioning; both sides constrained =
/// stretch, one side constrained = dock to that side at desired size, -1 = that side unconstrained); the container's own size
/// must be fixed (SetFixedWidth/Height) or given by the parent container.
class Overlay : public Container {
public:
    Overlay() : Container(true) {}

    /// Sets the inset (top/right/bottom/left) of the last-added child; -1 means that side is
    /// unconstrained (auto; the child docks to the constrained side at desired size). Repeated calls on the same child:
    /// the last one wins (GAP44-01). Silently harmless on an empty container (GAP50-04: unrecorded =
    /// all 0 = full fill, same semantics as calling after ClearChildren).
    Overlay& SetLastInset(float top, float right, float bottom, float left) {
        if (children_.empty()) return *this;   // GAP50-04: back() on an empty container is UB
        return SetInset(*children_.back(), top, right, bottom, left);
    }

    /// Sets the inset of the given child (need not be the last added; child must belong to this container).
    /// Repeated calls on the same child: the last one wins.
    Overlay& SetInset(Widget& child, float top, float right, float bottom, float left) {
        EraseInset(&child);
        insets_.push_back({ &child, { top, right, bottom, left } });
        Relayout();
        return *this;
    }

    /// Clears the given child's inset (back to "unrecorded = all 0" = full fill).
    Overlay& ClearInset(Widget& child) {
        EraseInset(&child);
        Relayout();
        return *this;
    }

protected:
    void AssignBounds(const Rect& area) override;
    Size DesiredSize() const override;
    /// GAP44-01: insets_ is mirror state keyed by raw pointers; if an entry is not withdrawn when a child is removed,
    /// a later same-sized child reusing the freed address resurrects the old inset onto the new child
    /// (silent misplacement; 5th recurrence of the mirror-state + incomplete invalidation-protocol defect class). ClearChildren
    /// batch notification passes nullptr ⇒ whole table cleared. Entries are Z-order-independent (keyed by pointer, not index),
    /// so OnChildrenReordered needs no override; entries are appended only on explicit Set*, so OnChildAdded
    /// needs none either.
    void OnChildRemoved(Widget* child) override {
        if (!child) { insets_.clear(); return; }   // ClearChildren batch notification
        EraseInset(child);
    }

private:
    struct Insets { float t = 0, r = 0, b = 0, l = 0; };
    const Insets& InsetOf(const Widget* w) const;
    /// Removes all entries for one child — the common maintenance point of two
    /// invariants ("at most one entry per child; every entry belongs to a registered child") (shared by SetInset re-set / OnChildRemoved removal).
    void EraseInset(const Widget* w) {
        for (size_t i = 0; i < insets_.size();) {
            if (insets_[i].first == w) insets_.erase(insets_.begin() + static_cast<std::ptrdiff_t>(i));
            else ++i;
        }
    }
    std::vector<std::pair<const Widget*, Insets>> insets_;   // unrecorded children treated as all 0
};

/// Gradient banner: multi-stop linear gradient backplate; the single child content (flyout card etc.) docks bottom-right
/// (the elevation banner of the material and elevation section).
class Banner : public Container {
public:
    Banner() : Container(true) {}

    /// Gradient stops (at least 2).
    Banner& SetStops(const std::vector<GradientStop>& stops) { stops_ = stops; Invalidate(); return *this; }

    /// Gradient angle, CSS convention (0° up, 90° right; default 115° = baseline banner).
    Banner& SetGradientAngle(float deg) { angleDeg_ = deg; Invalidate(); return *this; }

protected:
    void AssignBounds(const Rect& area) override;
    Size DesiredSize() const override;
    void OnPaint(Painter& p, const Theme& theme) override;

private:
    std::vector<GradientStop> stops_;
    float angleDeg_ = 115.0f;
};

/// Floating card: white rounded panel + soft shadow + optional title; content laid out vertically.
/// Arranged by the generic size policy when added to a window or layout container (fixed / weight / content-sized).
class Card : public Container {
public:
    Card() : Container(true) {}

    /// Card title (semi-bold at top); empty string means no title.
    const std::wstring& Title() const { return title_; }
    Card& SetTitle(std::wstring_view title);

    /// Secondary description below the title (the sample card's desc line).
    const std::wstring& Description() const { return desc_; }
    Card& SetDescription(std::wstring_view desc);

    void CollectText(std::wstring& out) const override {
        out += title_; out += L' ';
        out += desc_;  out += L' ';
        Container::CollectText(out);
    }

    /// Shadow tier (unified with Label): kShadowTierCard = content card (default)
    /// kShadowTierFlyout = popup, kShadowTierDialog = dialog
    /// kShadowTierNone = no shadow.
    Card& SetShadowTier(int tier) { shadowTier_ = tier; Invalidate(); return *this; }

    /// Content-area padding (default 14/16/16, matching baseline .ex-card; banner flyout card
    /// .acard uses 16/18/16).
    Card& SetCardPadding(float top, float side, float bottom) {
        padTop_ = top; padSide_ = side; padBottom_ = bottom;
        titleDirty_ = true; Relayout(); Invalidate(); return *this;
    }

    /// Title font size (default 14, matching baseline .ex-card h3; flyout card .acard b is 15).
    Card& SetTitleSize(float size) { titleSize_ = size; titleDirty_ = true; Relayout(); Invalidate(); return *this; }

    /// Background color override (default theme cardBackground; baseline .e-tile uses --solid solid fill).
    Card& SetBackground(const Color& c) { background_ = c; Invalidate(); return *this; }

protected:
    void AssignBounds(const Rect& area) override;
    Size DesiredSize() const override;
    void OnPaint(Painter& p, const Theme& theme) override;

private:
    friend struct detail::WindowImpl;
    float TitleHeight() const;
    const ShadowSpec& Shadow() const;

    std::wstring title_;
    std::wstring desc_;
    int shadowTier_ = 0;
    float padTop_ = 14.0f, padSide_ = 16.0f, padBottom_ = 16.0f;
    float titleSize_ = 14.0f;
    std::optional<Color> background_;
    mutable float titleH_ = 0;    // cached title height (includes spacing to content)
    mutable bool titleDirty_ = true;
};

// ---------------------------------------------------------------------------
// Selection controls: check box / radio button / toggle
// ---------------------------------------------------------------------------

/// Check box: label + 18×18 rounded check square; supports indeterminate state (short dash) and disabled.
class CheckBox : public Widget {
public:
    enum class State { Unchecked, Checked, Indeterminate };

    explicit CheckBox(std::wstring_view text = L"");

    const std::wstring& Text() const { return text_; }
    CheckBox& SetText(std::wstring_view text);

    State GetState() const { return state_; }
    CheckBox& SetState(State state);

    /// Three-state cycle: clicks cycle unchecked -> indeterminate -> checked (default is two-state toggle).
    CheckBox& SetThreeState(bool threeState) { threeState_ = threeState; return *this; }

    std::function<void(State)> OnChanged;

    void CollectText(std::wstring& out) const override { out += text_; out += L' '; }

protected:
    bool Focusable() const override { return true; }
    void OnKeydown(uint32_t vk) override;
    void OnPaint(Painter& p, const Theme& theme) override;
    Size DesiredSize() const override;
    void OnMouseMove(const Point& pos) override;
    void OnMouseLeave() override;
    void OnCaptureLost() override;
    void OnMouseDown(const Point& pos) override;
    void OnMouseUp(const Point& pos) override;

private:
    void Activate();   // shared by Space activation and mouse click
    std::wstring text_;
    State state_ = State::Unchecked;
    bool threeState_ = false;
    bool hover_ = false, pressed_ = false;
};

/// Radio button: mutually exclusive within the same group (same window).
class RadioButton : public Widget {
public:
    explicit RadioButton(std::wstring_view text = L"", int group = 0);

    const std::wstring& Text() const { return text_; }
    RadioButton& SetText(std::wstring_view text);

    bool Checked() const { return checked_; }
    RadioButton& SetChecked(bool checked);   // does not fire OnChecked

    std::function<void()> OnChecked;

    void CollectText(std::wstring& out) const override { out += text_; out += L' '; }

protected:
    bool Focusable() const override { return true; }
    void OnKeydown(uint32_t vk) override;
    void OnPaint(Painter& p, const Theme& theme) override;
    Size DesiredSize() const override;
    void OnMouseMove(const Point& pos) override;
    void OnMouseLeave() override;
    void OnCaptureLost() override;
    void OnMouseDown(const Point& pos) override;
    void OnMouseUp(const Point& pos) override;

private:
    friend struct detail::WindowImpl;   // iterates sibling buttons in the same group for mutual exclusion
    template <class W> friend struct detail::WindowFocusOps;   // radio group mutual exclusion / navigation (M-04)
    std::wstring text_;
    int group_ = 0;
    bool checked_ = false;
    bool hover_ = false, pressed_ = false;
};

/// Toggle switch: 40×20 rounded track + small round knob; on state fills with accent color.
class ToggleSwitch : public Widget {
public:
    bool Checked() const { return checked_; }
    ToggleSwitch& SetChecked(bool checked);   // does not fire OnChanged

    std::function<void()> OnChanged;

protected:
    bool Focusable() const override { return true; }
    void OnKeydown(uint32_t vk) override;
    void OnPaint(Painter& p, const Theme& theme) override;
    Size DesiredSize() const override { return { 40.0f, 20.0f }; }
    void OnCaptureLost() override;
    void OnMouseDown(const Point& pos) override;
    void OnMouseUp(const Point& pos) override;
    void OnAnimate() override;

private:
    void Activate();
    float KnobX() const;   // current knob x (includes movement animation)

    bool checked_ = false;
    float knobX_ = 4.0f;   // knob's animated current position (track-local; off-state target position)
    bool pressed_ = false;
};

// ---------------------------------------------------------------------------
// Slider
// ---------------------------------------------------------------------------

/// Slider: 4px rounded track (filled portion in accent color) + 18px round thumb, draggable.
class Slider : public Widget {
public:
    Slider(float minValue = 0.0f, float maxValue = 100.0f, float value = 0.0f);

    float Value() const { return value_; }
    Slider& SetValue(float value, bool notify = false);

    Slider& SetRange(float minValue, float maxValue);
    /// Snap step (0 = continuous).
    Slider& SetStep(float step) { step_ = step; return *this; }

    std::function<void(float)> OnChanged;

protected:
    bool Focusable() const override { return true; }
    void OnKeydown(uint32_t vk) override;
    void OnPaint(Painter& p, const Theme& theme) override;
    Size DesiredSize() const override { return { 120.0f, 20.0f }; }   // baseline .sld min-width:120px (C-7)
    void OnMouseMove(const Point& pos) override;
    void OnMouseDown(const Point& pos) override;
    void OnMouseUp(const Point& pos) override;
    void OnCaptureLost() override;

private:
    float ValueAt(float localX) const;
    void ApplyStep();

    float minValue_ = 0, maxValue_ = 100, value_ = 0, step_ = 0;
    bool dragging_ = false;
};

// ---------------------------------------------------------------------------
// ComboBox / SplitButton
// ---------------------------------------------------------------------------

/// Drop-down select: 32-high button-style trigger + pop-up option list; selected item filled with accent color.
class ComboBox : public Widget {
public:
    ComboBox();

    ComboBox& AddItem(std::wstring_view text);   // API39-01: Add* uniformly chainable return
    ComboBox& SetItems(const std::vector<std::wstring>& items);
    void ClearItems();

    int SelectedIndex() const { return selected_; }
    ComboBox& SetSelectedIndex(int index, bool notify = false);
    std::wstring SelectedText() const;

    /// Trigger minimum width (default 150).
    ComboBox& SetMinWidth(float width){ minWidth_ = width; Relayout(); 
        return *this;
    }

    /// Trigger text prefix: prepended only to the button display text; popup entries keep their original text
    /// (baseline: popup entries Small/Medium/Large/Extra-large, button shows "Font size - Medium", A-05).
    /// Desired size is measured from the "prefix + entry" display text; with a long prefix the visible width
    /// naturally grows from this measurement; SetMinWidth only raises the lower bound.
    ComboBox& SetTriggerPrefix(std::wstring_view prefix);

    std::function<void(int)> OnChanged;

    // trigger prefix + all entries (baseline popup entries live in the card's DOM, textContent can match them)
    void CollectText(std::wstring& out) const override {
        out += triggerPrefix_;
        out += L' ';
        for (const auto& it : items_) { out += it; out += L' '; }
    }

protected:
    bool Focusable() const override { return true; }
    void OnKeydown(uint32_t vk) override;
    void OnPaint(Painter& p, const Theme& theme) override;
    Size DesiredSize() const override;
    void OnMouseMove(const Point& pos) override;
    void OnMouseLeave() override;
    void OnCaptureLost() override;
    void OnMouseDown(const Point& pos) override;
    void OnMouseUp(const Point& pos) override;

private:
    friend struct detail::WindowImpl;
    void OpenPopup();
    bool PopupOpen() const;

    std::vector<std::wstring> items_;
    std::wstring triggerPrefix_;   // trigger display prefix (not into the popup, A-05)
    int selected_ = -1;
    float minWidth_ = 150.0f;
    bool hover_ = false, pressed_ = false;
};

/// Split button: main button area + drop-down arrow area; the arrow pops up a menu.
class SplitButton : public Widget {
public:
    explicit SplitButton(std::wstring_view text = L"");

    SplitButton& SetText(std::wstring_view text);
    SplitButton& SetIcon(const Icon& icon){ icon_ = icon; Relayout(); Invalidate(); 
        return *this;
    }   // DesiredSize includes icon 24 (R25-09)

    /// Menu builder for the drop-down portion (AddItem/AddSeparator/Disabled).
    Menu& Menu();

    std::function<void()> OnClick;   // main button area click

    void CollectText(std::wstring& out) const override {
        out += text_;
        out += L' ';
        if (menu_)
            for (const auto& it : menu_->items_)
                if (!it.separator) { out += it.text; out += L' '; }
    }

protected:
    bool Focusable() const override { return true; }
    void OnKeydown(uint32_t vk) override;
    void OnPaint(Painter& p, const Theme& theme) override;
    Size DesiredSize() const override;
    void OnMouseMove(const Point& pos) override;
    void OnMouseLeave() override;
    void OnCaptureLost() override;
    void OnMouseDown(const Point& pos) override;
    void OnMouseUp(const Point& pos) override;

private:
    friend struct detail::WindowImpl;
    void OpenMenu();

    std::wstring text_;
    Font font_{};
    Icon icon_;
    std::unique_ptr<class Menu> menu_;
    // arrow cell width: baseline .split arrow button contains a hidden .menu so :last-child misses => .btn padding 0 14px
    static constexpr float kChevW = 45.6f;
    bool hover_ = false, pressed_ = false, chevHover_ = false;
    bool chevPressed_ = false;
};

// ---------------------------------------------------------------------------
// Collections: ListView / TreeView
// ---------------------------------------------------------------------------

/// List view: avatar/icon + title + secondary text + time; single click selects (accent-colored left indicator bar).
class ListView : public Widget {
public:
    ListView();

    /// Adds a list item and returns its index. Neutral control background is used when no avatar gradient colors are passed.
    /// Default parameters use a fully transparent color to mean "unset" (Color{} defaults alpha to 1, unusable, L-02).
    int AddItem(const Icon& icon, std::wstring_view title,
                std::wstring_view subtitle, std::wstring_view time,
                Color avatarTop = Color{0, 0, 0, 0},
                Color avatarBottom = Color{0, 0, 0, 0});

    void Clear();
    int SelectedIndex() const { return selected_; }
    /// Programmatically sets the selected row (-1 = none). Default is **no notify** (API50-01; from 0.44.0
    /// aligned with ComboBox/Slider/RatingControl — previously defaulted to true, so the "restore selection"
    /// idiom also fired OnSelected, and changing the selection inside the callback re-entered; behavior change.
    /// User-interaction paths inside the library always pass true explicitly; unaffected).
    ListView& SetSelectedIndex(int index, bool notify = false);

    std::function<void(int)> OnSelected;

    void CollectText(std::wstring& out) const override {
        for (const auto& e : entries_) {
            out += e.title;    out += L' ';
            out += e.subtitle; out += L' ';
            out += e.time;     out += L' ';
        }
    }

protected:
    // baseline .li is a plain div (no tabindex/role); rows are not keyboard-focusable (round-19 U-02:
    // previously focusable + arrow keys, opposite of the baseline); selection is pointer-driven only
    void OnPaint(Painter& p, const Theme& theme) override;
    Size DesiredSize() const override;
    void OnMouseMove(const Point& pos) override;
    void OnMouseLeave() override;
    void OnMouseUp(const Point& pos) override;

private:
    struct Entry {
        Icon icon;
        std::wstring title, subtitle, time;
        // same L-02 convention as Avatar: optional carries "unset"
        std::optional<Color> avatarTop, avatarBottom;
    };
    float RowHeight(int index) const;   // per-row adaptive (baseline .li sized by row content, L-02)
    int RowAt(const Point& local) const;

    std::vector<Entry> entries_;
    int selected_ = -1;
    int hover_ = -1;
};

/// Tree view: expandable/collapsible hierarchical list (rotating arrow + indent guide line).
class TreeView : public Widget {
public:
    TreeView();
    ~TreeView() override;

    struct Node {
        std::wstring text;
        Icon icon;
        std::optional<Color> iconColor;   // if unset, color chosen by row kind: leaf textSecondary,
                                          // summary text (baseline .tree .leaf{color:var(--text2)}
                                          // via currentColor inheritance, R25-02; C-10 comment erratum)
        std::function<void()> onClick;

        /// Expansion state, read-only (API62-02, round-62): the field is private because a direct
        /// write never rebuilt rows_ — children silently failed to show/hide with no diagnostic.
        /// Writes go through TreeView::SetExpanded.
        bool IsExpanded() const { return expanded_; }

    private:
        friend class TreeView;
        bool expanded_ = false;
        std::vector<std::unique_ptr<Node>> children;
        Node* parent = nullptr;
    };

    Node& AddRoot(std::wstring_view text, const Icon& icon = {});
    Node& AddChild(Node& parent, std::wstring_view text, const Icon& icon = {});
    TreeView& SetExpanded(Node& node, bool expanded);

    /// Updates a node's label through the library (API62-02, round-62). Direct writes to
    /// Node::text remain possible (deliberately kept public) but neither invalidate layout nor
    /// repaint — this is the entry point that takes effect immediately.
    TreeView& SetNodeText(Node& node, std::wstring_view text);

    /// Programmatic row selection (R9, round-69). The row number is the flat visible-row
    /// index (0 = first visible row) — the same numbering OnMouseUp and the arrow keys
    /// write. -1 clears the selection (same contract as ComboBox/ListView/NavigationView::
    /// SetSelectedIndex). Out-of-range rows are rejected and the selection is left
    /// unchanged. Only the selection state changes: the keyboard focus is untouched, and
    /// no callback fires unless notify is true — then exactly one activation is replayed
    /// (node onClick, then OnClick); expansion is never toggled programmatically.
    TreeView& SetSelectedRow(int row, bool notify = false);

    /// Node-based selection (R9, round-69): collapsed ancestors of node are expanded so
    /// its row becomes visible, then the row is selected. A node that does not belong to
    /// this tree is rejected without touching anything (no expansion, no selection
    /// change). Focus and callback semantics match SetSelectedRow.
    TreeView& Select(Node& node, bool notify = false);

    /// The selected row as a flat visible-row index, -1 when nothing is selected. The
    /// selection follows its node across row-table rebuilds: rows added or removed above
    /// it shift the index, and once the selected node is no longer rendered (its ancestor
    /// was collapsed) the selection reads -1.
    int SelectedRow() const;

    /// Removes all roots (API62-01, round-62); hover and keyboard-focus row reset. Nodes are owned
    /// (unique_ptr), so their references must not be used after this call.
    void Clear();

    std::function<void(Node&)> OnClick;

    void CollectText(std::wstring& out) const override {
        std::function<void(const std::vector<std::unique_ptr<Node>>&)> walk =
            [&](const std::vector<std::unique_ptr<Node>>& nodes) {
                for (const auto& n : nodes) {
                    out += n->text;
                    out += L' ';
                    walk(n->children);
                }
            };
        walk(roots_);
    }

protected:
    // baseline <details>/<summary> is natively focusable (:focus-visible global rule); TreeView
    // matches it with keyboard accessibility (round-19 U-02)
    bool Focusable() const override { return true; }
    void OnPaint(Painter& p, const Theme& theme) override;
    Size DesiredSize() const override;
    void OnMouseMove(const Point& pos) override;
    void OnMouseLeave() override;
    void OnMouseUp(const Point& pos) override;
    void OnKeydown(uint32_t vk) override;

private:
    struct Row { Node* node = nullptr; int depth = 0; bool leaf = false; };
    void RebuildRows() const;
    int RowAt(const Point& local) const;

    std::vector<std::unique_ptr<Node>> roots_;
    mutable std::vector<Row> rows_;
    mutable bool rowsDirty_ = true;
    int hover_ = -1;
    mutable int keyRow_ = -1;   // selection/keyboard-focus row, an index into rows_ (U-02);
                                // mutable because RebuildRows() re-points it so the selection
                                // follows its node across rebuilds (-1 once the row is hidden)
};

// ---------------------------------------------------------------------------
// Status and info: InfoBar / ProgressBar / ProgressRing / Rating / badge
// ---------------------------------------------------------------------------

enum class InfoSeverity { Info, Success, Warning, Error };

/// Info bar: semantic color background + title and message + optional action link + close button.
class InfoBar : public Widget {
public:
    InfoBar(InfoSeverity severity = InfoSeverity::Info,
            std::wstring_view title = L"", std::wstring_view message = L"");

    InfoBar& SetSeverity(InfoSeverity severity){ severity_ = severity; Invalidate(); 
        return *this;
    }
    InfoBar& SetTitle(std::wstring_view title);
    InfoBar& SetMessage(std::wstring_view message);

    /// Action link on the right (e.g. "Check for updates").
    InfoBar& SetAction(std::wstring_view text, std::function<void()> onClick);

    /// Close button (clicking it auto-hides this control). Enabled by default.
    InfoBar& SetClosable(bool closable){ closable_ = closable; Relayout(); Invalidate(); 
        return *this;
    }   // DesiredSize depends on closable_ (R25-10)

    void CollectText(std::wstring& out) const override {
        out += title_;      out += L' ';
        out += message_;    out += L' ';
        out += actionText_; out += L' ';
    }

protected:
    void OnPaint(Painter& p, const Theme& theme) override;
    Size DesiredSize() const override;
    void SetBounds(const Rect& bounds) override;
    void OnMouseMove(const Point& pos) override;
    void OnMouseLeave() override;
    void OnMouseUp(const Point& pos) override;
    bool Focusable() const override {   // GAP38-04: focusable only with an action link or a close X
        return Enabled() && (!actionText_.empty() || closable_);
    }
    void OnKeydown(uint32_t vk) override;   // GAP38-04: left/right switch, Enter/Space activate

private:
    Rect ActionRect() const;   // control-local
    Rect CloseRect() const;    // control-local

    InfoSeverity severity_ = InfoSeverity::Info;
    std::wstring title_, message_, actionText_;
    std::function<void()> onAction_;
    bool closable_ = true;
    int hover_ = -1;           // -1 none / 0 action link / 1 close button
    int keyFocus_ = -1;        // keyboard focus (GAP38-04; 0 action link / 1 close X)
    float lastWidth_ = 0;      // last layout width (for measuring wrapped long-text height, M-03)
};

/// Progress bar: 3px rounded track; determinate fills by percentage; indeterminate is a looping light segment.
class ProgressBar : public Widget {
public:
    ProgressBar();

    ProgressBar& SetValue(float percent);            // 0~100
    float Value() const { return value_; }
    ProgressBar& SetIndeterminate(bool indeterminate);
    bool Indeterminate() const { return indeterminate_; }

    /// Small label above the track (e.g. "Downloading...").
    ProgressBar& SetLabel(std::wstring_view text);

    void CollectText(std::wstring& out) const override { out += label_; out += L' '; }

protected:
    void OnPaint(Painter& p, const Theme& theme) override;
    Size DesiredSize() const override;
    void OnAnimate() override;

private:
    float value_ = 0;
    float display_ = 0;  // displayed value smoothly animated toward (value-change animation)
    bool indeterminate_ = false;
    float phase_ = 0;    // indeterminate light-segment position (0~1)
    bool paintedSinceTick_ = false;   // PERF-03: in indeterminate mode, settles and stops the clock when unpainted outside the viewport
    std::wstring label_;
};

/// Progress ring: rotating arc (indeterminate).
class ProgressRing : public Widget {
public:
    explicit ProgressRing(float diameter = 20.0f);

    ProgressRing& SetDiameter(float diameter);

protected:
    void OnPaint(Painter& p, const Theme& theme) override;
    Size DesiredSize() const override { return { diameter_, diameter_ }; }
    void OnAnimate() override;

private:
    float diameter_ = 20.0f;
    float angle_ = 0;
    bool paintedSinceTick_ = false;   // PERF-03: settles and stops the clock when unpainted outside the viewport
};

/// Rating control: 5 stars, hover preview, single click sets the value, optional label text on the right.
class RatingControl : public Widget {
public:
    RatingControl();

    float Value() const { return value_; }
    RatingControl& SetValue(float value, bool notify = false);   // 0~5, rounded

    /// Label text to the right of the stars (e.g. "3.0 (214)").
    RatingControl& SetLabel(std::wstring_view text);

    std::function<void(float)> OnChanged;

    void CollectText(std::wstring& out) const override { out += label_; out += L' '; }

protected:
    bool Focusable() const override { return true; }
    void OnKeydown(uint32_t vk) override;
    void OnPaint(Painter& p, const Theme& theme) override;
    Size DesiredSize() const override;
    void OnMouseMove(const Point& pos) override;
    void OnMouseLeave() override;
    void OnMouseDown(const Point& pos) override;
    void OnMouseUp(const Point& pos) override;
    void OnCaptureLost() override;

private:
    float value_ = 0;
    int hover_ = -1;      // star hovered in preview (0-based, -1 none)
    bool pressed_ = false;   // pressed state: the held star scales to .88 (baseline :active)
    std::wstring label_;
};

/// Count badge: small accent-colored pill with a number (0 or negative renders as a dot).
class InfoBadge : public Widget {
public:
    explicit InfoBadge(int count = 0);

    InfoBadge& SetCount(int count);
    int Count() const { return count_; }

protected:
    void OnPaint(Painter& p, const Theme& theme) override;
    Size DesiredSize() const override;

private:
    int count_ = 0;
};

/// Avatar: circular gradient background + first character/initials; numeric badge attachable at top-right.
class Avatar : public Widget {
public:
    explicit Avatar(std::wstring_view initials = L"");

    Avatar& SetInitials(std::wstring_view initials);
    Avatar& SetBadge(int count);   // <=0 removes the badge
    Avatar& SetColors(Color top, Color bottom);

    void CollectText(std::wstring& out) const override { out += initials_; out += L' '; }

protected:
    void OnPaint(Painter& p, const Theme& theme) override;
    Size DesiredSize() const override { return { 44.0f, 44.0f }; }   // baseline .avatar 44 (B-10)

private:
    std::wstring initials_;
    // falls back to the baseline gray gradient when unset (#5a5d66->#3f424a); Color{} defaults alpha to 1,
    // the zero value cannot serve as the "unset" sentinel (L-02)
    std::optional<Color> top_, bottom_;
    int badge_ = 0;
};

// ---------------------------------------------------------------------------
// Layout controls: CommandBar / Expander / Tabs / ScrollViewer / side bar and top bar
// ---------------------------------------------------------------------------

/// Command bar: horizontal command buttons with icon on top and small label below, with separators and elastic spacers.
class CommandBar : public Widget {
public:
    CommandBar();

    CommandBar& AddButton(const Icon& icon, std::wstring_view label,
                          std::function<void()> onClick);
    CommandBar& AddSeparator();
    /// Elastic spacer: pushes items after it to the right side of the command bar.
    CommandBar& AddSpace();
    /// Right-side "More" button (icon + label); clicking pops up a menu.
    /// minW: per-item minimum width override (baseline "More" min-width:44px; default 58).
    CommandBar& AddMenuButton(const Icon& icon, std::wstring_view label, Menu& menu,
                              float minW = 0.0f);

    /// Updates an item's label (API62-03, round-62; separators/spacers ignore it — same
    /// contract as ToolBar::SetItemText).
    CommandBar& SetItemText(int index, std::wstring_view text);

    /// Enables/disables an item (API62-03, round-62 — same contract as ToolBar::EnableItem):
    /// disabled renders gray, shows no hover/press feedback, ignores clicks and keyboard
    /// activation, and is skipped by the left/right focus walk.
    CommandBar& EnableItem(int index, bool enabled);

    /// Removes all items (API62-01, round-62); hover/pressed/keyboard-focus reset. A flyout
    /// already opened by an item is window-owned and dismisses on its own.
    void Clear();
    void CollectText(std::wstring& out) const override {
        for (const auto& it : items_)
            if (!it.separator && !it.space) { out += it.label; out += L' '; }
    }

protected:
    float DockHeight() const override { return 0.0f; }
    void OnPaint(Painter& p, const Theme& theme) override;
    Size DesiredSize() const override;
    void OnMouseMove(const Point& pos) override;
    void OnMouseLeave() override;
    void OnMouseDown(const Point& pos) override;
    void OnMouseUp(const Point& pos) override;
    void OnCaptureLost() override;
    bool Focusable() const override { return true; }   // GAP38-03: reachable via Tab
    void OnKeydown(uint32_t vk) override;              // GAP38-03: left/right navigate, Enter/Space activate

private:
    friend struct detail::WindowImpl;
    struct Item {
        bool separator = false;
        bool space = false;
        bool menu = false;
        bool enabled = true;   // API62-03: same disabled semantics as ToolBar items
        Icon icon;
        std::wstring label;
        std::function<void()> onClick;
        Menu menuOwned;   // ARCH-06: CommandBar owns a copy of the item — previously it stored a non-owning
                          // raw pointer; passing a scope-local Menu compiled but dangled on click
        float minW = 0.0f;   // when >0 overrides the default min width 58 (baseline "More" 44)
        Rect rect{};   // control-local coordinates
    };
    int ItemAt(const Point& local) const;
    void LayoutItems() const;
    void OpenItemMenu(const Item& item);

    mutable std::vector<Item> items_;   // rect fields are layout cache
    mutable bool itemsDirty_ = true;
    mutable float lastLayoutW_ = -1.0f;   // last layout width (SetBounds is not called back, self-check invalidated, L-01)
    int hover_ = -1, pressed_ = -1;
    int keyFocus_ = -1;   // keyboard-focus item (GAP38-03; items_ index, -1 none)
    std::unique_ptr<detail::Flyout> popup_;
};

/// Expander: header row (arrow on the right) + expandable content container.
class Expander : public Container {
public:
    Expander();

    Expander& SetHeader(std::wstring_view text);
    const std::wstring& Header() const { return header_; }

    /// Content container (vertical layout), shown when expanded. If the content column is removed (ClearChildren etc.),
    /// it is lazily rebuilt on first access (ARCH39-03: cached child pointers must not dangle).
    Container& Content() {
        if (!content_) content_ = &Add<Column>();
        return *content_;
    }

    bool Expanded() const { return expanded_; }
    Expander& SetExpanded(bool expanded);

    void CollectText(std::wstring& out) const override {
        out += header_;
        out += L' ';
        Container::CollectText(out);   // collapsed content also included (textContent convention)
    }

protected:
    bool Focusable() const override { return true; }
    void OnKeydown(uint32_t vk) override;
    void OnChildRemoved(Widget* child) override;   // ARCH39-03: resets content_
    void AssignBounds(const Rect& area) override;
    Size DesiredSize() const override;
    void OnPaint(Painter& p, const Theme& theme) override;
    void OnMouseUp(const Point& pos) override;
    void OnMouseMove(const Point& pos) override;
    void OnMouseLeave() override;

private:
    float HeaderHeight() const { return 41.0f; }   // baseline .exp-h padding 10px ⇒ 21+20 (B-15)

    std::wstring header_;
    bool expanded_ = false;
    bool headerHover_ = false;
    Column* content_ = nullptr;   // created on Add, owned by children_
};

/// Tab page: vertical container carrying the tab title, created via Tabs::AddPage. The title travels with the page object
/// (GAP42-01/02 fixed at the root): Tabs no longer maintains a titles_ ⇄ children_ parallel array;
/// since round 43, with the notification surface completed (GAP43-01/02), a TabPage entering the tree via **any path** (including
/// Add<TabPage>() and Add via base-class upcast) is registered as a page by Tabs::OnChildAdded, and
/// BringToFront/SendToBack reordering marks the tab geometry cache dirty — the "tab ⇄ page"
/// correspondence is indistinguishable under construction history. Non-page children do not participate in page visibility semantics (SetSelected does
/// not touch their visibility), but they occupy the page area and are always visible (no defined slot; clarified in round-43 §6).
class TabPage : public Column {
public:
    /// Tab title (written by Tabs::AddPage).
    const std::wstring& Title() const { return title_; }
private:
    friend class Tabs;
    std::wstring title_;
};

/// Tabs: top tab strip + current page content; pages are vertical containers (other pages hidden).
class Tabs : public Container {
public:
    Tabs();

    /// Adds a page (returns the vertical container); the tab title shows at the top.
    /// Registered equivalently to Add<TabPage>() (GAP43-02: OnChildAdded is the unified registration path);
    /// this function only fills in the title; the injected page has an empty title and a full tab slot.
    Column& AddPage(std::wstring_view title);

    int Selected() const { return selected_; }
    Tabs& SetSelected(int index);

    void CollectText(std::wstring& out) const override {
        for (auto* pg : Pages()) { out += pg->Title(); out += L' '; }
        Container::CollectText(out);   // inactive pages also included (textContent convention)
    }

protected:
    bool Focusable() const override { return true; }
    void OnKeydown(uint32_t vk) override;
    void AssignBounds(const Rect& area) override;
    Size DesiredSize() const override;
    void OnPaint(Painter& p, const Theme& theme) override;
    void OnMouseMove(const Point& pos) override;
    void OnMouseLeave() override;
    void OnMouseUp(const Point& pos) override;
    void OnChildRemoved(Widget* child) override;   // ARCH39-08: page removed in sync with child removal
    void OnChildAdded(Widget* child) override;     // GAP43-02: TabPage entering the tree via any path is registered as a page
    void OnChildrenReordered() override;           // GAP43-01: reordering marks the tab geometry cache dirty

private:
    // GAP42-01/02 fixed at the root: page order is "the order of TabPage children within children_"; titles are stored
    // on TabPage itself. GAP41-01 once closed this out by hiding the base-class Add name with a private-section using declaration,
    // but M-66: access control is judged by the static type of the object expression, so an upcast Container& can still
    // reach it, and BringToFront/SendToBack swapping children_ breaks the parallel array the same way —
    // "name hiding" cannot serve as the invariant's guarantee mechanism, so the positional coupling was eliminated (the round-41 close-out
    // was removed along with it). Since round 43, Add/reorder paths are covered by the OnChildAdded/OnChildrenReordered notification
    // surface (GAP43-01/02). Non-page children are invisible to the page mechanism: Pages() collects only TabPage.
    //
    // PERF43-01: returns a reference to **member scratch** (each call clears and refills reusing capacity),
    // zero heap allocations in steady state — round 42 replaced titles_ with a by-value vector, which allocated on every call.
    // Constraint: calling Pages() again on the same object invalidates earlier references; do not hold across calls
    // (all current call sites use-and-discard within one function; re-entrant paths verified one by one).
    const std::vector<TabPage*>& Pages() const;
    int PageCount() const { return static_cast<int>(Pages().size()); }
    float BarHeight() const { return 35.0f; }   // baseline .tab padding 7px ⇒ 21+14 (B-15)
    void LayoutTabs() const;

    mutable std::vector<TabPage*> pagesScratch_;   // zero-allocation backing store for Pages() (PERF43-01)
    mutable std::vector<Rect> tabRects_;
    mutable bool tabsDirty_ = true;
    int selected_ = 0, hover_ = -1;
};

/// Scroll container: wheel-scrolls when content exceeds the viewport; draws a thin scrollbar.
class ScrollViewer : public Container {
public:
    ScrollViewer();

    friend struct detail::WindowImpl;   // scroll-key forwarding when no focus (R27-02)

    float ScrollOffset() const { return scrollY_; }
    ScrollViewer& SetScrollOffset(float offset);
    /// Scrolls so the target widget (should be a descendant) lands near the top of the viewport.
    ScrollViewer& ScrollToWidget(const Widget* target);
    float ContentHeight() const { return contentH_; }

    /// Notifies on scroll (for navigation-linked highlighting).
    std::function<void()> OnScrolled;

protected:
    void AssignBounds(const Rect& area) override;
    Size DesiredSize() const override;
    void OnPaint(Painter& p, const Theme& theme) override;
    void OnPostPaint(Painter& p, const Theme& theme) override;
    bool OnWheel(float delta) override;
    void OnMouseMove(const Point& pos) override;
    void OnMouseLeave() override;
    void OnMouseDown(const Point& pos) override;
    void OnMouseUp(const Point& pos) override;
    bool ChildHitClip(Rect* out) const override;
    bool ChildPaintClip(Rect* out) const override;
    bool Focusable() const override { return true; }   // reachable via Tab (R27-02)
    void OnKeydown(uint32_t vk) override;

private:
    float scrollY_ = 0;
    float contentH_ = 0;
    Rect viewport_{};
    Rect ThumbRect() const;          // scrollbar thumb (window coordinates)
    bool draggingThumb_ = false;
    float dragGrabOffset_ = 0;       // offset of the press point relative to the thumb top
    bool thumbHover_ = false;
};

/// Left navigation view (docked control): brand row + group labels + nav items + footer text.
/// The selected item shows an accent-colored left indicator bar; created by AddNavigationView.
class NavigationView : public Widget {
public:
    NavigationView();

    /// Brand row (badge square color follows the theme accent).
    NavigationView& SetBrand(std::wstring_view text);

    /// Adds a group label (not clickable).
    NavigationView& AddGroup(std::wstring_view text);
    /// Adds a nav item, returns the index.
    int AddItem(const Icon& icon, std::wstring_view text,
                std::function<void(size_t)> onClick = {});

    /// Removes all groups/items (API62-01, round-62); selection, hover, keyboard focus and
    /// scroll offset reset. Indices from later AddItem calls start over at 0.
    void Clear();

    int SelectedIndex() const { return selected_; }
    /// Programmatically selects a nav item. Default is no notify (same convention as Slider/ComboBox/ListView/RatingControl,
    /// API50-01); out-of-range is rejected (-1 = no selection, OBS53-01); with notify=true and a valid
    /// index, fires OnNavigate — scenarios like restoring the last position can reuse the linkage logic
    /// without redoing it manually (OBS52-02).
    NavigationView& SetSelectedIndex(int index, bool notify = false);
    NavigationView& SetFooter(std::wstring_view text);

    std::function<void(size_t)> OnNavigate;

protected:
    float DockWidth() const override;
    void OnPaint(Painter& p, const Theme& theme) override;
    void OnMouseMove(const Point& pos) override;
    void OnMouseLeave() override;
    void OnMouseDown(const Point& pos) override;
    void OnMouseUp(const Point& pos) override;
    void OnCaptureLost() override;
    bool OnWheel(float delta) override;
    bool Focusable() const override { return true; }   // GAP38-02: reachable via Tab
    void OnKeydown(uint32_t vk) override;              // GAP38-02: up/down navigate, Enter/Space activate

private:
    struct Entry {
        bool group = false;
        Icon icon;
        std::wstring text;
        Rect rect{};   // control-local coordinates (content coordinates, scroll offset not included)
    };
    void LayoutEntries() const;
    int EntryAt(const Point& local) const;
    float maxScroll() const;   // scrollable amount of the entries area (M-01)
    Rect ThumbRect() const;    // scrollbar thumb (control-local; empty = not scrollable)
    void DrawScrollbar(Painter& p, const Theme& theme);

    mutable std::vector<Entry> entries_;   // rect fields are layout cache
    std::vector<std::function<void(size_t)>> onClicks_;
    mutable bool entriesDirty_ = true;
    mutable float scrollOffset_ = 0;   // entries-area scroll offset (baseline .navlist overflow-y:auto)
    mutable float contentBottom_ = 0;  // bottom edge in content coordinates of the last entry
    int selected_ = -1, hover_ = -1;
    int keyFocus_ = -1;   // keyboard-focus entry (GAP38-02; itemIndex space, -1 none)
    bool thumbHover_ = false, thumbDrag_ = false;   // scrollbar thumb (M-01)
    float thumbGrab_ = 0;
    std::wstring brand_, footer_;
};

/// Top bar (docked control): breadcrumb title + elastic spacer + optional search box and icon button.
class TopBar : public Container {
public:
    TopBar();

    /// Breadcrumb title.
    TopBar& SetCrumb(std::wstring_view text);
    /// The three accessors lazily rebuild after a child is removed (ClearChildren etc.) (ARCH39-03:
    /// cached child pointers must not dangle); configuration and construction share the same one.
    Label& CrumbLabel() {
        if (!crumb_) {
            crumb_ = &Add<Label>();
            crumb_->SetFont(Font{ .size = 15.0f, .weight = FontWeight::SemiBold });
            crumb_->SetWeight(1.0f);
        }
        return *crumb_;
    }

    /// Embedded search box (hidden when unused: SearchBox().SetVisible(false)).
    TextBox& SearchBox() {
        if (!search_) {
            search_ = &Add<TextBox>();
            search_->SetFixedWidth(280.0f);
            search_->SetFixedHeight(32.0f);
            search_->SetSearchBoxStyle();   // baseline .searchbox: 22×22 button, spacing 8 after the icon, font size 13 (N-03)
            search_->SetCrossAlign(CrossAlign::Center);
        }
        return *search_;
    }

    /// Embedded theme-toggle icon button (hidden when unused).
    Button& ThemeButton() {
        if (!themeBtn_) {
            themeBtn_ = &Add<Button>(L"", nullptr);
            themeBtn_->SetStyle(ButtonStyle::IconOnly);
            themeBtn_->SetFixedHeight(32.0f);
            themeBtn_->SetCrossAlign(CrossAlign::Center);
        }
        return *themeBtn_;
    }

protected:
    float DockHeight() const override { return 48.0f; }
    void OnPaint(Painter& p, const Theme& theme) override;
    void OnChildRemoved(Widget* child) override;   // ARCH39-03: resets the three cached pointers

private:
    Label* crumb_ = nullptr;
    TextBox* search_ = nullptr;
    Button* themeBtn_ = nullptr;
};

// ---------------------------------------------------------------------------
// Window
// ---------------------------------------------------------------------------

/// Modal content dialog description. primary is the accent button (shown first), secondary is the normal button.
struct ContentDialogDesc {
    std::wstring title;
    std::wstring body;
    std::wstring primaryText = L"确定";
    std::function<void()> onPrimary;
    std::wstring secondaryText = L"取消";
    std::function<void()> onSecondary;
};

/// Top-level window. Widgets are owned by the window; references stay valid for the window's lifetime.
class Window {
public:
    Window();
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    Window& SetTitle(std::wstring_view title);
    Window& SetClientSize(float widthDip, float heightDip);
    Size ClientSize() const;

    /// Minimum window size (client-area DIP units; 0 = unrestricted, default). Constrains subsequent drag and
    /// programmatic resizing (WM_GETMINMAXINFO ptMinTrackSize); does not change an already-established size,
    /// nor affects minimization. R36-04: with no default lower bound, the window can shrink until the layout self-overlaps.
    Window& SetMinimumSize(float widthDip, float heightDip);

    /// Enables/disables window maximization as a whole (round-65 R2.3, default enabled). Disabled,
    /// every maximize path is suppressed: the title-bar maximize button (kept visible but inert —
    /// hide it via GetTitleBar().SetSystemButtons), Win+Up, caption double-click, Aero Snap drag to
    /// the screen top and the system-menu entry (WS_MAXIMIZEBOX removal + SC_MAXIMIZE swallowing).
    /// Resize, minimize and normal moving are unaffected. The state may be toggled any time.
    Window& SetMaximizable(bool enabled);
    bool Maximizable() const;

    const Theme& GetTheme() const;
    Window& SetTheme(const Theme& theme);

    /// Sets the window icon (taskbar / Alt+Tab / window list). No need to call by default: the library
    /// bakes the title's first character + theme accent gradient into an icon (same source as the owner-drawn badge) and
    /// refreshes it automatically on title or theme change. Passing a non-null handle switches to the user icon (the handle is owned by the caller,
    /// the library does not destroy it); when only one slot is given, the other falls back to the non-null handle (big←small, GAP52-02;
    /// small←big, R35-02); a single 0 does not silently clear the other slot; passing 0 for both restores
    /// the default icon.
    Window& SetIcon(HICON iconBig, HICON iconSmall = nullptr);

    /// Adds a widget, returns the reference (the window owns the widget).
    template <class T, class... Args>
    T& Add(Args&&... args);

    Label& AddLabel(std::wstring_view text, const Rect& bounds);
    Button& AddButton(std::wstring_view text, const Rect& bounds,
                      std::function<void()> onClick = {});

    /// Docked controls: menu bar / tool bar / top bar dock to the top (stacked in add order),
    /// navigation view docks to the left, status bar docks to the bottom.
    /// Outside the docked areas is the content area, updated automatically as the window size changes.
    MenuBar& AddMenuBar();
    ToolBar& AddToolBar();
    TopBar& AddTopBar();
    NavigationView& AddNavigationView();
    StatusBar& AddStatusBar();

    /// Access to the owner-drawn title bar (round-65 R1 control handover). The bar's mechanism (drag
    /// to move, DPI, hover/press visuals) stays with the library; content is the consumer's to
    /// configure: SetBadgeVisible, SetSystemButtons, AddButton/AddToggleButton (+ Toggled/SetToggled,
    /// SetButtonToolTip). Right-click fires the bar's inherited OnContextMenuCb (window DIP
    /// coordinates) — set it to pop the host's own menu; when unset the click is swallowed (the bar
    /// is owner-drawn, no system menu applies).
    TitleBar& GetTitleBar();

    /// Pops up a toast card at the window's bottom-right (auto-dismisses; display only, no interaction).
    void ShowToast(std::wstring_view title, std::wstring_view subtitle = L"");

    /// Modal content dialog: translucent overlay + full-width buttons at the bottom; closed by Esc, clicking the overlay, or a button.
    void ShowContentDialog(const ContentDialogDesc& desc);

    /// Pops up a context menu (right-click menu) at the given client-area position.
    /// Shows a context menu (clientPos is client-area DIP coordinates). minWidth is the popup
    /// minimum width (baseline context menu .menu.ctx 180, button menu 200).
    void ShowContextMenu(Menu& menu, const Point& clientPos, float minWidth = 180.0f);

    /// Periodic callback (e.g. demo value changes); intervalMs of 0 cancels.
    Window& SetTickHandler(std::function<void()> handler, uint32_t intervalMs);

    /// Current content area (the remainder outside the docked bars).
    Rect ContentArea() const;

    /// Layout batching: call in pairs (nestable). Within a batch, SetText/SetPanel/Relayout and
    /// similar calls no longer trigger a full-window layout immediately; one merged run executes at EndLayoutBatch —
    /// avoids O(N) full-tree reflows when one action changes multiple widgets consecutively.
    void BeginLayoutBatch();
    void EndLayoutBatch();

    void Show();
    void Close();

    /// Hides the window without destroying it (round-65 R2.1). The process keeps running (Run()
    /// exits by window count and a hidden window is still counted), the message loop keeps
    /// dispatching (a tray helper window keeps receiving messages), and position, size and state
    /// are preserved. Show() re-shows and activates. Pair with SetOnClosing to turn the close
    /// button into "hide to tray" (the close interception already covers Alt+F4 and SC_CLOSE).
    void Hide();

    /// True while the window is minimized (iconic) (round-66 R4). SW_SHOW does not restore a
    /// minimized window, so tray-click patterns branch on this: Restore() when minimized,
    /// Show() otherwise. (Named Minimized, not IsMinimized: windowsx.h defines IsMinimized
    /// as a function-like macro alias of IsIconic and would rewrite the declaration.)
    bool Minimized() const;

    /// SW_RESTORE (round-66 R4): a minimized window comes back to its previous size and position
    /// (a window that was maximized before minimizing comes back maximized) and is activated.
    /// On a visible window this is just an activation. Show() keeps its plain SW_SHOW semantics.
    void Restore();

    /// Always-on-top toggle (round-65 R2.2). Changes Z-order only: position, size and activation
    /// are untouched. The state persists across minimize/hide/show (OS-managed style bit).
    Window& SetTopmost(bool topmost);

    /// Reads back the topmost state from the OS (WS_EX_TOPMOST), not mirrored library state —
    /// external style changes by the host stay reflected truthfully.
    bool IsTopmost() const;

    /// Windows 11 system rounded corners for the main window (round-68 R8, default off = square,
    /// opt-in). Requests the DWM corner preference — the same attribute the popup host already
    /// sets (PopupWindow::Create) — so the radius, the 1 px system border and the drop shadow are
    /// the system's own (radius 8 DIP at 96 DPI, scaling with DPI; nothing is owner-drawn). On
    /// Windows 10, or whenever the call fails, the window silently keeps square corners — the
    /// same fallback the popup path has always had. The preference is a one-shot live window
    /// state: it persists across show/hide, minimize/restore, DPI changes and SWP_FRAMECHANGED
    /// style rewrites (SetMaximizable's mechanism), and is orthogonal to SetMaximizable and
    /// SetTopmost. Purely an outer visual clip: the window rect, the client rect and every
    /// WM_NCHITTEST answer (resize bands, caption, hit islands) are untouched — corner pixels
    /// are cut visually only, hit-testing at the corners keeps its original semantics.
    /// Maximizing or Aero-snapping squares the corners by OS policy and restores them on
    /// return to the floating state. Pass false to restore the default square corners.
    Window& SetRoundedCorners(bool rounded);

    /// Reads back the corner preference from the OS (DwmGetWindowAttribute), not mirrored
    /// library state — the same OS-truth contract as IsTopmost. True only while the window's
    /// live corner preference is actually DWMWCP_ROUND: on Windows 10 this reads false even
    /// immediately after SetRoundedCorners(true) (the request failed by design).
    bool RoundedCorners() const;

    /// Close-request interception (API-10): called before both clicking the close button / Alt+F4 (WM_CLOSE) and programmatic
    /// Close(); returning false cancels the close (unsaved-changes confirmation scenario). Default
    /// does not intercept.
    Window& SetOnClosing(std::function<bool()> handler);

    /// DPI change notification (API-12): the library already handles size/layout/icon refresh; this callback is only for
    /// consumers to make incidental adjustments (e.g. reselecting bitmap resources per DPI). The argument is the new DPI value.
    Window& SetDpiChangedHandler(std::function<void(float newDpi)> handler);

    /// System setting change notification (R37-03): currently triggered only by a system theme switch
    /// (WM_SETTINGCHANGE + ImmersiveColorSet). Whether to follow is the consumer's decision;
    /// re-resolve together with SystemPrefersDark() (the live semantics of the baseline prefers-color-scheme —
    /// browsers recompute and repaint live when the system theme switches).
    Window& SetSystemThemeChangedHandler(std::function<void()> handler);

    /// Client-size change notification (round-70 R13): fires when the client area's DIP size
    /// actually changed since the last notification, after the library has brought layout
    /// current — widget Bounds() read fresh at call time (the tooltip-on-truncation use:
    /// recompute MeasureText(text, font) vs Bounds().w here, then SetToolTip). Never fires
    /// while minimized (SIZE_MINIMIZED is skipped); a DPI-only rescale with an unchanged pixel
    /// size does not fire (use SetDpiChangedHandler for those); size changes driven by the
    /// host itself without a window resize (panel widths etc.) are the host's own actions —
    /// refresh at those points. Subscribing before Show() delivers the initial size on the
    /// first WM_SIZE (each real change fires exactly once, including the drag-resize stream).
    Window& SetSizeChangedHandler(std::function<void()> handler);

    /// Keyboard accelerators (API-11): matched at the WM_KEYDOWN/WM_SYSKEYDOWN stage before the focused widget;
    /// all of Ctrl/Shift/Alt must match to hit; on hit the callback fires and the key is swallowed.
    /// This is what makes shortcuts like Ctrl+N/F5 shown in menus work.
    Window& AddAccelerator(uint32_t vk, bool ctrl, bool shift = false,
                           bool alt = false, std::function<void()> onClick = {});

    /// Window screen position (API-09): frame origin, in DIP (converted to physical pixels using this window's DPI;
    /// with mixed-DPI multi-monitor, the target monitor's actual DPI governs; the library does not pick the screen).
    Window& SetPosition(float xDip, float yDip);
    Point Position() const;
    /// Centers within the current monitor's work area (keeps the current size).
    void CenterOnScreen();

    /// Layout-batch RAII guard (API-05): Begin on construction / End on destruction; exceptions or early
    /// return cannot wedge the batch state (explicit Begin/End pairing still available).
    class LayoutBatch {
    public:
        explicit LayoutBatch(Window& w) : w_(w) { w_.BeginLayoutBatch(); }
        ~LayoutBatch() { w_.EndLayoutBatch(); }
        LayoutBatch(const LayoutBatch&) = delete;
        LayoutBatch& operator=(const LayoutBatch&) = delete;
    private:
        Window& w_;
    };

    bool IsAlive() const;

private:
    friend class Widget;
    friend class Container;   // Remove/ClearChildren go through WindowImpl::DetachTree
    friend class TextBox;   // access native handles under impl_ (timer/clipboard)
    friend class MenuBar;
    friend class TitleBar;  // access impl_ (activation state/native handle)
    friend class ComboBox;  // access impl_ (popup lifetime management)
    friend class SplitButton;
    friend class CommandBar;
    friend class RadioButton;  // access impl_ (radio group mutual-exclusion traversal)
    friend struct detail::WindowImpl;
    friend void FlushPendingDestroyAll();   // ARCH38-07: unified destroy queue at end of frame
    std::unique_ptr<detail::WindowImpl> impl_;
};

/// ARCH38-07: flushes all windows' deferred destroy queues (called by Run/PumpOnce after each message round).
void FlushPendingDestroyAll();

// ---------------------------------------------------------------------------
// Global
// ---------------------------------------------------------------------------

/// Initializes the framework (D2D/DWrite factories + DPI awareness; does no COM/OLE initialization — the library
/// has no OLE dependency, the clipboard uses native APIs). Must be called once before creating windows.
bool Initialize();

/// Description of the most recent library failure (UTF-8; empty string when no failure recorded). Written by Initialize,
/// graphics device/popup render target creation, and other failure paths (API-14).
std::string LastError();

/// Releases global resources. Call after all windows are destroyed.
void Shutdown();

/// Measures single-line text size (DIP). For manual layout or estimation.
Size MeasureText(std::wstring_view text, const Font& font);

/// Whether the system prefers the dark app theme (reads registry AppsUseLightTheme; returns
/// false on read failure). With Window::SetSystemThemeChangedHandler implements R37-03's live
/// system theme following.
bool SystemPrefersDark();

/// Global message loop; returns the exit code after all windows close.
int Run();

/// Processes currently queued messages then returns immediately (non-blocking) (ARCH-16: for test/self-hosted
/// outer loops; regular apps still use Run()). Returns false on WM_QUIT.
bool PumpOnce();

} // namespace slender

#endif // SLENDER_H_INCLUDED

// ============================================================================
// Implementation
// ============================================================================

#ifdef SLENDER_IMPLEMENTATION
#ifndef SLENDER_IMPLEMENTATION_INCLUDED
#define SLENDER_IMPLEMENTATION_INCLUDED

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00 // Windows 10
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <initguid.h>   // must precede d2d1_1.h: its included d2d1effects.h uses DEFINE_GUID
                        // to declare effect CLSIDs; INITGUID makes those selectany definitions
#include <d2d1_1.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <imm.h>        // R35-03: IME composition window anchoring (ImmSetCompositionWindow)

// windows.h's DrawText macro would rewrite the Painter::DrawText method name; undefine it here.
#undef DrawText

#include <cmath>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <unordered_map>
#include <vector>

// ARCH38-08: linking-library injection is the default; consumers can turn it off via -DSLENDER_UI_NO_AUTO_LINK
// and link themselves
#if !defined(SLENDER_UI_NO_AUTO_LINK)
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "imm32.lib")
#pragma comment(lib, "advapi32.lib")   // RegGetValueW (SystemPrefersDark, R37-03)
#endif

namespace slender {

namespace detail {

// Windows 11 window corner-preference attributes: the corresponding dwmapi.h enums ship only in newer SDKs and the values are fixed,
// so call with raw numeric values (33 = DWMWA_WINDOW_CORNER_PREFERENCE, 2 = DWMWCP_ROUND);
// on Windows 10 the call fails and drawing falls back to square corners.
// ARCH38-03: moved into detail — previously in the global namespace, polluting consumer TUs
inline constexpr DWORD kDwmwaWindowCornerPreference = 33;
inline constexpr DWORD kDwmwcpDefault = 0;   // "let the system decide" — restores square corners on the borderless swap-chain window
inline constexpr DWORD kDwmwcpRound = 2;

template <class T>
class ComPtr {
public:
    ComPtr() = default;
    ~ComPtr() { Reset(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ComPtr(ComPtr&& other) noexcept : ptr_(other.ptr_) { other.ptr_ = nullptr; }
    ComPtr& operator=(ComPtr&& other) noexcept {
        if (this != &other) {
            Reset();
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }

    T* Get() const { return ptr_; }
    T* operator->() const { return ptr_; }
    explicit operator bool() const { return ptr_ != nullptr; }
    T** GetAddressOf() { Reset(); return &ptr_; }
    void Reset() {
        if (ptr_) { ptr_->Release(); ptr_ = nullptr; }
    }

private:
    T* ptr_ = nullptr;
};

inline D2D1_COLOR_F ToD2D(const Color& c) {
    return D2D1_COLOR_F{ c.r, c.g, c.b, c.a };
}

inline DXGI_RGBA ToDxgi(const Color& c) {
    return DXGI_RGBA{ c.r, c.g, c.b, c.a };
}

struct FontKey {
    std::wstring family;
    float size;
    UINT32 weight;
    float lineHeight = 0;
    bool tabular = false;
    bool operator==(const FontKey& o) const {
        return family == o.family && size == o.size && weight == o.weight &&
               lineHeight == o.lineHeight && tabular == o.tabular;
    }
};

/// Non-owning lookup key (PERF-05): the hit path avoids FontKey's wstring copy —
/// DrawText looks up the formats table once per text run drawn
struct FontKeyView {
    std::wstring_view family;
    float size;
    UINT32 weight;
    float lineHeight;
    bool tabular;
};

struct FontKeyHash {
    using is_transparent = void;   // C++20 heterogeneous lookup (PERF-05)
    static size_t Mix(std::wstring_view family, float size, UINT32 weight,
                      float lineHeight, bool tabular) {
        size_t h = std::hash<std::wstring_view>()(family);
        h ^= std::hash<float>()(size) << 1;
        h ^= weight * 0x9E3779B97F4A7C15ull;
        h ^= std::hash<float>()(lineHeight) << 3;
        h ^= tabular ? 0x5bf03635ull : 0;
        return h;
    }
    size_t operator()(const FontKey& k) const {
        return Mix(k.family, k.size, k.weight, k.lineHeight, k.tabular);
    }
    size_t operator()(const FontKeyView& v) const {
        return Mix(v.family, v.size, v.weight, v.lineHeight, v.tabular);
    }
};

struct FontKeyEqual {
    using is_transparent = void;   // C++20 heterogeneous lookup (PERF-05)
    bool operator()(const FontKey& a, const FontKey& b) const { return a == b; }
    bool operator()(const FontKeyView& v, const FontKey& k) const {
        return v.family == k.family && v.size == k.size && v.weight == k.weight &&
               v.lineHeight == k.lineHeight && v.tabular == k.tabular;
    }
    bool operator()(const FontKey& k, const FontKeyView& v) const {
        return (*this)(v, k);
    }
};

inline DWRITE_FONT_WEIGHT ToDWrite(FontWeight w) {
    switch (w) {
    case FontWeight::Bold:     return DWRITE_FONT_WEIGHT_BOLD;
    case FontWeight::SemiBold: return DWRITE_FONT_WEIGHT_SEMI_BOLD;
    default:                   return DWRITE_FONT_WEIGHT_NORMAL;
    }
}

} // namespace detail

// global state (defined at the end of the implementation section)
extern std::vector<Window*> g_windows;
extern detail::ComPtr<ID2D1Factory1> g_d2d;
extern detail::ComPtr<IDWriteFactory> g_dwrite;

namespace detail {
/// Library failure description channel (API-14): written by Initialize / device and render target creation and other failure paths,
/// read by the public LastError(). Process-wide, lock-free — safe under the UI single-thread convention.
void SetError(std::string s);
}

namespace detail {

/// Graphics devices shared across windows (D3D device / DXGI factory / D2D device).
/// The main window and menu popups share the same device, each holding its own swap chain and context.
struct GraphicsGlobals {
    ComPtr<ID3D11Device> d3d;
    ComPtr<IDXGIFactory2> factory;
    ComPtr<ID2D1Device> d2dDevice;
    bool attempted = false;
};

/// One renderable target (swap chain + context + brush).
struct RenderTarget {
    ComPtr<IDXGISwapChain1> swap;
    ComPtr<ID2D1DeviceContext> ctx;
    ComPtr<ID2D1SolidColorBrush> brush;
};

} // namespace detail

extern detail::GraphicsGlobals g_gfx;

namespace detail {

inline bool EnsureGraphics() {
    if (g_gfx.d2dDevice) return true;
    if (g_gfx.attempted) return false;
    g_gfx.attempted = true;
    // ARCH-01: the popup path (without the main window Render's g_d2d pre-guard) also reaches here;
    // with Initialize() missing or after Shutdown(), this must catch it, otherwise g_d2d->CreateDevice
    // null-pointer dereferences
    if (!g_d2d || !g_dwrite) {
        SetError("graphics not initialized (Initialize() missing or Shutdown() called)");
        return false;
    }

    // ARCH-05: when hardware D3D11 is unavailable (Basic Display Adapter / some VMs / session switch),
    // fall back to WARP software rendering — WARP is always available on the agreed Win10 floor, no longer
    // "one failure means a permanently blank window"
    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
        g_gfx.d3d.GetAddressOf(), nullptr, nullptr);
    if (FAILED(hr))
        hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
            g_gfx.d3d.GetAddressOf(), nullptr, nullptr);
    if (FAILED(hr)) {
        SetError("D3D11CreateDevice failed (hardware and WARP)");
        return false;
    }

    ComPtr<IDXGIDevice1> dxgiDevice1;
    if (SUCCEEDED(g_gfx.d3d->QueryInterface(IID_PPV_ARGS(dxgiDevice1.GetAddressOf()))))
        dxgiDevice1->SetMaximumFrameLatency(1); // queued frame cap 1: old frames do not linger in the present queue

    ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(g_gfx.d3d->QueryInterface(IID_PPV_ARGS(dxgiDevice.GetAddressOf())))) {
        SetError("IDXGIDevice QueryInterface failed");   // ARCH38-04: failures must be diagnosable
        return false;
    }
    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgiDevice->GetAdapter(adapter.GetAddressOf()))) {
        SetError("IDXGIDevice::GetAdapter failed");
        return false;
    }
    if (FAILED(adapter->GetParent(IID_PPV_ARGS(g_gfx.factory.GetAddressOf())))) {
        SetError("IDXGIAdapter::GetParent failed");
        return false;
    }
    if (FAILED(g_d2d->CreateDevice(dxgiDevice.Get(), g_gfx.d2dDevice.GetAddressOf()))) {
        SetError("ID2D1Factory::CreateDevice failed");
        return false;
    }
    SetError("");   // ARCH38-04: on successful device-chain rebuild, clear prior failure records (removes LastError stickiness)
    return true;
}

inline bool BindSwapChainTarget(RenderTarget& rt, float dpi) {
    ComPtr<IDXGISurface> surface;
    if (FAILED(rt.swap->GetBuffer(0, IID_PPV_ARGS(surface.GetAddressOf())))) {
        SetError("swap chain GetBuffer failed");   // ARCH38-04
        return false;
    }
    auto props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    ComPtr<ID2D1Bitmap1> bitmap;
    if (FAILED(rt.ctx->CreateBitmapFromDxgiSurface(surface.Get(), &props,
                                                   bitmap.GetAddressOf()))) {
        SetError("CreateBitmapFromDxgiSurface failed");
        return false;
    }
    rt.ctx->SetTarget(bitmap.Get());
    rt.ctx->SetDpi(dpi, dpi);
    return true;
}

inline bool CreateRenderTarget(HWND hwnd, float dpi, Color brushColor,
                               Color backgroundColor, RenderTarget& rt) {
    if (!EnsureGraphics()) return false;   // failure reason already written to the error channel (API-14)
    RECT rc;
    GetClientRect(hwnd, &rc);
    if (rc.right <= 0 || rc.bottom <= 0) {
        SetError("CreateRenderTarget: zero-size client area");
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width = static_cast<UINT>(rc.right);
    sd.Height = static_cast<UINT>(rc.bottom);
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    // on size mismatch (the gap during drag-resize) do not let DWM stretch the old frame: content anchors top-left as-is,
    // newly exposed areas fill with the background color. STRETCH's stretching is exactly the source of "content scaling with drag-resize".
    sd.Scaling = DXGI_SCALING_NONE;
    if (FAILED(g_gfx.factory->CreateSwapChainForHwnd(g_gfx.d3d.Get(), hwnd, &sd,
                                                     nullptr, nullptr,
                                                     rt.swap.GetAddressOf()))) {
        sd.Scaling = DXGI_SCALING_STRETCH; // some drivers/RDP do not support NONE; fall back to default
        if (FAILED(g_gfx.factory->CreateSwapChainForHwnd(g_gfx.d3d.Get(), hwnd, &sd,
                                                         nullptr, nullptr,
                                                         rt.swap.GetAddressOf()))) {
            SetError("CreateSwapChainForHwnd failed");
            return false;
        }
    }
    DXGI_RGBA bg = ToDxgi(backgroundColor);
    rt.swap->SetBackgroundColor(&bg);
    if (FAILED(g_gfx.d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                                                    rt.ctx.GetAddressOf()))) {
        SetError("CreateDeviceContext failed");
        return false;
    }
    if (FAILED(rt.ctx->CreateSolidColorBrush(ToD2D(brushColor),
                                             rt.brush.GetAddressOf()))) {
        SetError("CreateSolidColorBrush failed");
        return false;
    }
    if (!BindSwapChainTarget(rt, dpi)) return false;   // failure reason already written (ARCH38-04)
    SetError("");   // ARCH38-04: on successful target binding, clear prior failure records
    return true;
}

/// Resolves a font fallback stack: returns the first family actually present on the system; if all are missing, returns
/// the first item (left to DirectWrite system fallback). Results cached by input string (namespace-level
/// static, cleared at Shutdown via FontResolveCache — ARCH-17).
inline std::unordered_map<std::wstring, std::wstring>& FontResolveCache() {
    static std::unordered_map<std::wstring, std::wstring> cache;
    return cache;
}

/// Effective family stack (PERF43-01): empty Font::family = default family stack. The default stack body
/// lives in one static (allocated once per process); resolution results are still reused via FontResolveCache.
inline const std::wstring& EffectiveFontFamily(const std::wstring& family) {
    static const std::wstring def(kDefaultFontFamily);
    return family.empty() ? def : family;
}

inline const std::wstring& ResolveFontFamily(const std::wstring& spec) {
    auto& cache = FontResolveCache();
    auto it = cache.find(spec);
    if (it != cache.end()) return it->second;

    std::wstring resolved = spec;
    size_t sep = spec.find(L';');
    if (sep != std::wstring::npos && g_dwrite) {
        ComPtr<IDWriteFontCollection> coll;
        if (SUCCEEDED(g_dwrite->GetSystemFontCollection(coll.GetAddressOf()))) {
            size_t start = 0;
            while (start <= sep) {
                size_t end = spec.find(L';', start);
                if (end == std::wstring::npos) end = spec.size();
                std::wstring family = spec.substr(start, end - start);
                // trim leading/trailing whitespace
                size_t b = family.find_first_not_of(L" \t");
                size_t e = family.find_last_not_of(L" \t");
                if (b != std::wstring::npos) family = family.substr(b, e - b + 1);
                BOOL exists = FALSE;
                UINT32 index = 0;
                if (!family.empty() &&
                    SUCCEEDED(coll->FindFamilyName(family.c_str(), &index, &exists)) &&
                    exists) {
                    resolved = family;
                    break;
                }
                if (end == spec.size()) break;
                start = end + 1;
            }
        }
    }
    return cache.emplace(spec, std::move(resolved)).first->second;
}

/// Text format and metrics caches. During layout, DesiredSize/measurement is called extremely often (every
/// Relayout re-measures the whole tree); creating DWrite objects on the spot is the main cause of slow startup: cache
/// TextFormat by (family,size,weight), metrics by (text,font,width).
/// Lookups go through the MKeyView heterogeneous view (zero heap allocation on cache hit, P-01); only on a miss,
/// when inserting into the store, is an MKey copied out to own the text.
struct TextCache {
    std::unordered_map<FontKey, ComPtr<IDWriteTextFormat>, FontKeyHash,
                       FontKeyEqual>
        formats;

    struct MKey {
        std::wstring text;
        std::wstring family;
        float size;
        UINT32 weight;
        float maxWidth;   // 0 = no wrapping
        float lineHeight = 0;
        // GAP50-03: in metrics/baselines the lineHeight slot is exactly font.lineHeight;
        // tabLayouts reuses that slot for the draw-area height and stores font.lineHeight here instead —
        // otherwise drawing the same text in the same area with a different line spacing would reuse a layout with the wrong line spacing.
        float lineSpacing = 0;
    };
    /// Non-owning lookup view: fields must map one-to-one onto MKey.
    struct MKeyView {
        std::wstring_view text;
        std::wstring_view family;
        float size;
        UINT32 weight;
        float maxWidth;
        float lineHeight;
        float lineSpacing = 0;
    };
    struct MKeyHash {
        using is_transparent = void;
        static size_t Mix(std::wstring_view text, std::wstring_view family,
                          float size, UINT32 weight, float maxWidth,
                          float lineHeight, float lineSpacing) {
            size_t h = std::hash<std::wstring_view>()(text);
            h ^= std::hash<std::wstring_view>()(family) << 1;
            h ^= std::hash<float>()(size) << 2;
            h ^= weight * 0x9E3779B97F4A7C15ull;
            h ^= std::hash<float>()(maxWidth) << 3;
            h ^= std::hash<float>()(lineHeight) << 4;
            h ^= std::hash<float>()(lineSpacing) << 5;
            return h;
        }
        size_t operator()(const MKey& k) const {
            return Mix(k.text, k.family, k.size, k.weight, k.maxWidth,
                       k.lineHeight, k.lineSpacing);
        }
        size_t operator()(const MKeyView& v) const {
            return Mix(v.text, v.family, v.size, v.weight, v.maxWidth,
                       v.lineHeight, v.lineSpacing);
        }
    };
    struct MKeyEqual {
        using is_transparent = void;
        bool operator()(const MKey& a, const MKey& b) const {
            return a.maxWidth == b.maxWidth && a.size == b.size &&
                   a.weight == b.weight && a.lineHeight == b.lineHeight &&
                   a.lineSpacing == b.lineSpacing &&
                   a.family == b.family && a.text == b.text;
        }
        bool operator()(const MKey& a, const MKeyView& b) const {
            return a.maxWidth == b.maxWidth && a.size == b.size &&
                   a.weight == b.weight && a.lineHeight == b.lineHeight &&
                   a.lineSpacing == b.lineSpacing &&
                   std::wstring_view(a.family) == b.family &&
                   std::wstring_view(a.text) == b.text;
        }
        bool operator()(const MKeyView& a, const MKey& b) const {
            return (*this)(b, a);
        }
    };
    std::unordered_map<MKey, DWRITE_TEXT_METRICS, MKeyHash, MKeyEqual> metrics;
    std::unordered_map<MKey, float, MKeyHash, MKeyEqual> baselines;   // first-line baseline (R25-06)
    /// TextLayout cache for tabular numerals (tabularNumerals) (P-02): the key's
    /// maxWidth/lineHeight slots are reused as draw-area w/h; font.lineHeight is stored
    /// in the lineSpacing slot (GAP50-03: line spacing is baked into the format, so it must enter the key). On capacity
    /// overrun the whole cache is cleared (use sites are small sets like slider/progress values; normally far below the cap).
    std::unordered_map<MKey, ComPtr<IDWriteTextLayout>, MKeyHash, MKeyEqual>
        tabLayouts;
    static constexpr size_t kMetricsCap = 16384;

    /// Bulk clear (ARCH-17: Shutdown explicitly releases cached COM objects instead of
    /// relying on static destruction order — function-local statics destruct after g_dwrite.Reset)
    void ClearAll() {
        metrics.clear();
        baselines.clear();
        tabLayouts.clear();
        formats.clear();
    }

    void TrimMetrics() {
        // GAP50-02/OBS52-01: baselines and metrics are both keyed by text, and
        // MeasureTextBaseline writes into this table directly, bypassing metrics — both tables must be evicted in the same batch,
        // and the baseline store site must trigger this function itself (otherwise the cap is conditional).
        if (metrics.size() < kMetricsCap && baselines.size() < kMetricsCap)
            return;
        // At the cap, evict 1/4 per batch instead of clearing the whole table: a full clear forces
        // every text to be re-measured next frame (periodic spikes, P-03).
        size_t evict = kMetricsCap / 4;
        for (auto it = metrics.begin(); it != metrics.end() && evict > 0; ) {
            it = metrics.erase(it);
            --evict;
        }
        evict = kMetricsCap / 4;
        for (auto it = baselines.begin(); it != baselines.end() && evict > 0; ) {
            it = baselines.erase(it);
            --evict;
        }
    }
};

inline TextCache& TextCacheInstance() {
    static TextCache cache;   // UI is single-threaded
    return cache;
}

/// Get (or create) the shared IDWriteTextFormat for a font. Note: the returned format
/// is reused by DrawText/measure calls which mutate alignment and wrapping state; callers must set the state they need explicitly.
inline IDWriteTextFormat* GetCachedFormat(const Font& font) {
    if (!g_dwrite) return nullptr;   // ARCH39-06: out-of-contract drawing after Shutdown must not null-deref
    auto& cache = TextCacheInstance();
    const std::wstring& resolved = ResolveFontFamily(EffectiveFontFamily(font.family));
    // PERF-05: lookup on the hot path uses the non-owning FontKeyView, avoiding a wstring copy of FontKey
    FontKeyView view{ resolved, font.size,
                      static_cast<UINT32>(ToDWrite(font.weight)),
                      font.lineHeight, font.tabularNumerals };
    auto it = cache.formats.find(view);
    if (it != cache.formats.end()) return it->second.Get();
    ComPtr<IDWriteTextFormat> format;
    if (FAILED(g_dwrite->CreateTextFormat(
            resolved.c_str(), nullptr, ToDWrite(font.weight),
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            font.size, L"zh-CN", format.GetAddressOf()))) return nullptr;
    if (font.lineHeight > 0.0f)
        format->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM,
                               font.size * font.lineHeight,
                               font.size * font.lineHeight * 0.8f);
    IDWriteTextFormat* raw = format.Get();
    cache.formats.emplace(
        FontKey{ std::wstring(view.family), view.size, view.weight,
                 view.lineHeight, view.tabular },
        std::move(format));
    return raw;
}

/// Measure single-line text metrics (DIP). Used for menu auto-width and containers sizing to content.
inline DWRITE_TEXT_METRICS MeasureTextMetrics(std::wstring_view text, const Font& font) {
    DWRITE_TEXT_METRICS metrics{};
    if (!g_dwrite || text.empty()) return metrics;
    auto& cache = TextCacheInstance();
    const std::wstring& resolved = ResolveFontFamily(EffectiveFontFamily(font.family));
    TextCache::MKeyView key{
        text, resolved, font.size,
        static_cast<UINT32>(ToDWrite(font.weight)), 0.0f, font.lineHeight };
    auto it = cache.metrics.find(key);
    if (it != cache.metrics.end()) return it->second;
    IDWriteTextFormat* format = GetCachedFormat(font);
    if (!format) return metrics;
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(g_dwrite->CreateTextLayout(
            text.data(), static_cast<UINT32>(text.size()), format,
            10000.0f, font.size * 2.0f, layout.GetAddressOf()))) return metrics;
    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    layout->GetMetrics(&metrics);
    cache.TrimMetrics();
    cache.metrics.emplace(
        TextCache::MKey{ std::wstring(text), resolved, font.size,
                         static_cast<UINT32>(ToDWrite(font.weight)), 0.0f,
                         font.lineHeight, 0.0f },
        metrics);
    return metrics;
}

/// Measure single-line text width (DIP). Used where auto width is needed (menus etc.).
inline float MeasureTextWidth(std::wstring_view text, const Font& font) {
    return MeasureTextMetrics(text, font).widthIncludingTrailingWhitespace;
}

/// Measure the distance (DIP) from layout top to the first-line baseline of single-line text. Used by CrossAlign::Baseline
/// (R25-06); DWRITE_TEXT_METRICS carries no baseline, so DWRITE_LINE_METRICS is required.
inline float MeasureTextBaseline(std::wstring_view text, const Font& font) {
    if (!g_dwrite || text.empty()) return 0.0f;
    auto& cache = TextCacheInstance();
    const std::wstring& resolved = ResolveFontFamily(EffectiveFontFamily(font.family));
    TextCache::MKeyView key{
        text, resolved, font.size,
        static_cast<UINT32>(ToDWrite(font.weight)), 0.0f, font.lineHeight };
    if (auto it = cache.baselines.find(key); it != cache.baselines.end())
        return it->second;
    IDWriteTextFormat* format = GetCachedFormat(font);
    if (!format) return 0.0f;
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(g_dwrite->CreateTextLayout(
            text.data(), static_cast<UINT32>(text.size()), format,
            10000.0f, font.size * 2.0f, layout.GetAddressOf()))) return 0.0f;
    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    float baseline = 0.0f;
    // Even when the first query-style call returns E_NOT_SUFFICIENT_BUFFER it still writes the required line count
    // (observed hr=0x8007007A with a correct count), so only the count is taken and hr is not checked
    UINT32 count = 0;
    layout->GetLineMetrics(nullptr, 0, &count);
    if (count > 0 && count < 4096) {
        std::vector<DWRITE_LINE_METRICS> lms(count);
        UINT32 got = 0;
        if (SUCCEEDED(layout->GetLineMetrics(lms.data(), count, &got)) && got > 0)
            baseline = lms[0].baseline;
    }
    cache.TrimMetrics();   // OBS52-01: the baseline store site self-triggers; the cap is no longer conditional on the metrics path
    cache.baselines.emplace(
        TextCache::MKey{ std::wstring(text), resolved, font.size,
                         static_cast<UINT32>(ToDWrite(font.weight)), 0.0f,
                         font.lineHeight, 0.0f },
        baseline);
    return baseline;
}

/// Measure the height (DIP) of a text block wrapped at a given width.
inline float MeasureTextBlockHeight(std::wstring_view text, const Font& font,
                                    float width) {
    if (!g_dwrite || text.empty() || width <= 0) return 0.0f;
    auto& cache = TextCacheInstance();
    const std::wstring& resolved = ResolveFontFamily(EffectiveFontFamily(font.family));
    TextCache::MKeyView key{
        text, resolved, font.size,
        static_cast<UINT32>(ToDWrite(font.weight)), width, font.lineHeight };
    auto it = cache.metrics.find(key);
    if (it != cache.metrics.end()) return it->second.height;
    IDWriteTextFormat* format = GetCachedFormat(font);
    if (!format) return 0.0f;
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(g_dwrite->CreateTextLayout(
            text.data(), static_cast<UINT32>(text.size()), format,
            width, font.size * 4.0f, layout.GetAddressOf()))) return 0.0f;
    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    cache.TrimMetrics();
    cache.metrics.emplace(
        TextCache::MKey{ std::wstring(text), resolved, font.size,
                         static_cast<UINT32>(ToDWrite(font.weight)), width,
                         font.lineHeight, 0.0f },
        metrics);
    return metrics.height;
}

// ---------------------------------------------------------------------------
// SVG path parsing and icon geometry
// ---------------------------------------------------------------------------

struct IconPoint {
    float x = 0, y = 0;
};

struct IconData {
    struct Figure {
        IconPoint start{};
        std::vector<std::array<IconPoint, 3>> segments; // cubic bezier (c1,c2,end)
        bool closed = false;
    };
    std::vector<Figure> figures;

    bool tried = false;
    ComPtr<ID2D1PathGeometry> geometry;

    // ARCH38-02: live icon-data registry — function-local static Icons (title-bar buttons / tree arrows /
    // stars etc.) hold geometry created from the g_d2d factory; static destruction runs after Shutdown's g_d2d.Reset,
    // so they must register here and be released proactively by Shutdown. Weak refs: IconData dies with its last user
    static std::vector<std::weak_ptr<IconData>>& Live() {
        static std::vector<std::weak_ptr<IconData>> v;
        return v;
    }

    ID2D1PathGeometry* GetGeometry() {
        if (geometry || tried) return geometry.Get();
        if (!g_d2d) return nullptr;
        tried = true;
        if (FAILED(g_d2d->CreatePathGeometry(geometry.GetAddressOf()))) return nullptr;
        ID2D1GeometrySink* sink = nullptr;
        if (FAILED(geometry->Open(&sink))) { geometry.Reset(); return nullptr; }
        for (const auto& fig : figures) {
            sink->BeginFigure(D2D1_POINT_2F{ fig.start.x, fig.start.y },
                              D2D1_FIGURE_BEGIN_FILLED);
            for (const auto& seg : fig.segments) {
                sink->AddBezier(D2D1_BEZIER_SEGMENT{
                    D2D1_POINT_2F{ seg[0].x, seg[0].y },
                    D2D1_POINT_2F{ seg[1].x, seg[1].y },
                    D2D1_POINT_2F{ seg[2].x, seg[2].y } });
            }
            sink->EndFigure(fig.closed ? D2D1_FIGURE_END_CLOSED
                                       : D2D1_FIGURE_END_OPEN);
        }
        if (FAILED(sink->Close())) geometry.Reset();
        sink->Release();
        return geometry.Get();
    }
};

/// SVG path d-attribute parser: all commands normalized into cubic-bezier subpaths.
class SvgPathParser {
public:
    explicit SvgPathParser(std::string_view s) : buf_(s), p_(buf_.c_str()), end_(p_ + buf_.size()) {}

    bool Parse(IconData& out) {
        cx_ = cy_ = sx_ = sy_ = lx_ = ly_ = qx_ = qy_ = 0.0;
        hasCubic_ = hasQuad_ = false;
        cur_ = -1;
        bool any = false;
        while (p_ < end_) {
            SkipSeparators();
            if (p_ >= end_) break;
            char c = *p_;
            if (IsCmdStart(c)) {
                ++p_;
                cmd_ = c;
                rel_ = c >= 'a' && c <= 'z';
                if (!RunCommand(out)) return false;
                any = true;
            } else if (cmd_ != 0) {
                // implicit repeat of the previous command (parameter pairs after M are treated as L)
                if (!RunCommand(out)) return false;
                any = true;
            } else {
                return false;
            }
        }
        FlushCurrent(out);
        return any && !out.figures.empty();
    }

private:
    static bool IsCmdStart(char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    }

    void SkipWsp() {
        while (p_ < end_ && (*p_ == ' ' || *p_ == '\t' || *p_ == '\r' || *p_ == '\n')) ++p_;
    }

    void SkipSeparators() {
        SkipWsp();
        if (p_ < end_ && *p_ == ',') { ++p_; SkipWsp(); }
    }

    bool Num(double& v) {
        SkipSeparators();
        const char* start = p_;
        if (p_ < end_ && (*p_ == '+' || *p_ == '-')) ++p_;
        bool digits = false;
        while (p_ < end_ && *p_ >= '0' && *p_ <= '9') { ++p_; digits = true; }
        if (p_ < end_ && *p_ == '.') {
            ++p_;
            while (p_ < end_ && *p_ >= '0' && *p_ <= '9') { ++p_; digits = true; }
        }
        if (p_ < end_ && (*p_ == 'e' || *p_ == 'E')) {
            const char* save = p_;
            ++p_;
            if (p_ < end_ && (*p_ == '+' || *p_ == '-')) ++p_;
            bool ed = false;
            while (p_ < end_ && *p_ >= '0' && *p_ <= '9') { ++p_; ed = true; }
            if (!ed) p_ = save; // rewind so the remainder parses as a new token
        }
        if (!digits) { p_ = start; return false; }
        v = std::strtod(start, nullptr);
        return true;
    }

    bool Flag(int& f) {
        SkipSeparators();
        if (p_ < end_ && (*p_ == '0' || *p_ == '1')) {
            f = *p_ - '0';
            ++p_;
            return true;
        }
        return false;
    }

    IconPoint Cur() const { return { static_cast<float>(cx_), static_cast<float>(cy_) }; }

    void BeginFigure(const IconPoint& p) {
        IconData::Figure fig;
        fig.start = p;
        figures_.push_back(std::move(fig));
        cur_ = static_cast<int>(figures_.size()) - 1;
        sx_ = cx_ = p.x;
        sy_ = cy_ = p.y;
        hasCubic_ = hasQuad_ = false;
    }

    void AddCubic(const IconPoint& c1, const IconPoint& c2, const IconPoint& e) {
        if (cur_ < 0) BeginFigure(e); // bare command without M: defensive
        figures_[cur_].segments.push_back({ c1, c2, e });
        cx_ = e.x; cy_ = e.y;
    }

    void AddLine(const IconPoint& e) {
        IconPoint s = Cur();
        IconPoint c1{ s.x + (e.x - s.x) / 3.0f, s.y + (e.y - s.y) / 3.0f };
        IconPoint c2{ s.x + (e.x - s.x) * 2.0f / 3.0f, s.y + (e.y - s.y) * 2.0f / 3.0f };
        AddCubic(c1, c2, e);
    }

    bool RunCommand(IconData& out) {
        switch (cmd_ | 0x20) { // lowercase view; rel_ distinguishes absolute/relative
        case 'm': {
            double x, y;
            if (!Num(x) || !Num(y)) return false;
            double nx = rel_ ? cx_ + x : x;
            double ny = rel_ ? cy_ + y : y;
            FlushCurrent(out);
            BeginFigure({ static_cast<float>(nx), static_cast<float>(ny) });
            // parameter pairs after M are handled as L (delegated to the implicit-repeat logic)
            cmd_ = rel_ ? 'l' : 'L';
            return true;
        }
        case 'l': {
            double x, y;
            if (!Num(x) || !Num(y)) return false;
            IconPoint e{ static_cast<float>(rel_ ? cx_ + x : x),
                         static_cast<float>(rel_ ? cy_ + y : y) };
            AddLine(e);
            return true;
        }
        case 'h': {
            double x;
            if (!Num(x)) return false;
            AddLine({ static_cast<float>(rel_ ? cx_ + x : x), static_cast<float>(cy_) });
            return true;
        }
        case 'v': {
            double y;
            if (!Num(y)) return false;
            AddLine({ static_cast<float>(cx_), static_cast<float>(rel_ ? cy_ + y : y) });
            return true;
        }
        case 'c': {
            double x1, y1, x2, y2, x, y;
            if (!Num(x1) || !Num(y1) || !Num(x2) || !Num(y2) || !Num(x) || !Num(y)) return false;
            IconPoint s = Cur();
            IconPoint c1{ static_cast<float>(rel_ ? cx_ + x1 : x1),
                          static_cast<float>(rel_ ? cy_ + y1 : y1) };
            IconPoint c2{ static_cast<float>(rel_ ? cx_ + x2 : x2),
                          static_cast<float>(rel_ ? cy_ + y2 : y2) };
            IconPoint e{ static_cast<float>(rel_ ? cx_ + x : x),
                         static_cast<float>(rel_ ? cy_ + y : y) };
            AddCubic(c1, c2, e);
            lx_ = rel_ ? cx_ - x + x2 : x2; // previous control point (absolute coords)
            ly_ = rel_ ? cy_ - y + y2 : y2;
            hasCubic_ = true; hasQuad_ = false;
            return true;
        }
        case 's': {
            double x2, y2, x, y;
            if (!Num(x2) || !Num(y2) || !Num(x) || !Num(y)) return false;
            IconPoint s = Cur();
            IconPoint c1 = hasCubic_
                ? IconPoint{ static_cast<float>(2 * cx_ - lx_), static_cast<float>(2 * cy_ - ly_) }
                : s;
            IconPoint c2{ static_cast<float>(rel_ ? cx_ + x2 : x2),
                          static_cast<float>(rel_ ? cy_ + y2 : y2) };
            IconPoint e{ static_cast<float>(rel_ ? cx_ + x : x),
                         static_cast<float>(rel_ ? cy_ + y : y) };
            AddCubic(c1, c2, e);
            lx_ = rel_ ? cx_ - x + x2 : x2;
            ly_ = rel_ ? cy_ - y + y2 : y2;
            hasCubic_ = true; hasQuad_ = false;
            return true;
        }
        case 'q': {
            double qx, qy, x, y;
            if (!Num(qx) || !Num(qy) || !Num(x) || !Num(y)) return false;
            IconPoint s = Cur();
            IconPoint q{ static_cast<float>(rel_ ? cx_ + qx : qx),
                         static_cast<float>(rel_ ? cy_ + qy : qy) };
            IconPoint e{ static_cast<float>(rel_ ? cx_ + x : x),
                         static_cast<float>(rel_ ? cy_ + y : y) };
            IconPoint c1{ s.x + (q.x - s.x) * 2.0f / 3.0f, s.y + (q.y - s.y) * 2.0f / 3.0f };
            IconPoint c2{ e.x + (q.x - e.x) * 2.0f / 3.0f, e.y + (q.y - e.y) * 2.0f / 3.0f };
            AddCubic(c1, c2, e);
            qx_ = rel_ ? cx_ - x + qx : qx;
            qy_ = rel_ ? cy_ - y + qy : qy;
            hasQuad_ = true; hasCubic_ = false;
            return true;
        }
        case 't': {
            double x, y;
            if (!Num(x) || !Num(y)) return false;
            IconPoint s = Cur();
            IconPoint q = hasQuad_
                ? IconPoint{ static_cast<float>(2 * cx_ - qx_), static_cast<float>(2 * cy_ - qy_) }
                : s;
            IconPoint e{ static_cast<float>(rel_ ? cx_ + x : x),
                         static_cast<float>(rel_ ? cy_ + y : y) };
            IconPoint c1{ s.x + (q.x - s.x) * 2.0f / 3.0f, s.y + (q.y - s.y) * 2.0f / 3.0f };
            IconPoint c2{ e.x + (q.x - e.x) * 2.0f / 3.0f, e.y + (q.y - e.y) * 2.0f / 3.0f };
            AddCubic(c1, c2, e);
            qx_ = q.x; qy_ = q.y;
            hasQuad_ = true; hasCubic_ = false;
            return true;
        }
        case 'a': {
            double rx, ry, rot, x, y;
            int laf, sf;
            if (!Num(rx) || !Num(ry) || !Num(rot) || !Flag(laf) || !Flag(sf) ||
                !Num(x) || !Num(y)) return false;
            IconPoint from = Cur();
            IconPoint to{ static_cast<float>(rel_ ? cx_ + x : x),
                          static_cast<float>(rel_ ? cy_ + y : y) };
            ArcToCubics(from, to, rx, ry, rot, laf, sf);
            cx_ = to.x; cy_ = to.y;
            hasCubic_ = hasQuad_ = false;
            return true;
        }
        case 'z': {
            if (cur_ < 0) return false;
            figures_[cur_].closed = true;
            cx_ = sx_; cy_ = sy_; // close back to the subpath start
            cur_ = -1;
            hasCubic_ = hasQuad_ = false;
            return true;
        }
        default:
            return false;
        }
    }

    void ArcToCubics(IconPoint from, IconPoint to,
                     double rx, double ry, double rotDeg, int laf, int sf) {
        if (rx == 0 || ry == 0 || (from.x == to.x && from.y == to.y)) {
            AddLine(to);
            return;
        }
        rx = std::fabs(rx); ry = std::fabs(ry);
        double phi = rotDeg * 3.14159265358979323846 / 180.0;
        double cosP = std::cos(phi), sinP = std::sin(phi);
        double dx2 = (from.x - to.x) / 2.0, dy2 = (from.y - to.y) / 2.0;
        double x1p = cosP * dx2 + sinP * dy2;
        double y1p = -sinP * dx2 + cosP * dy2;

        double rx2 = rx * rx, ry2 = ry * ry;
        double lambda = x1p * x1p / rx2 + y1p * y1p / ry2;
        if (lambda > 1.0) {
            double s = std::sqrt(lambda);
            rx *= s; ry *= s;
            rx2 = rx * rx; ry2 = ry * ry;
        }

        double sign = (laf != sf) ? 1.0 : -1.0;
        double num = rx2 * ry2 - rx2 * y1p * y1p - ry2 * x1p * x1p;
        double den = rx2 * y1p * y1p + ry2 * x1p * x1p;
        double co = sign * std::sqrt(std::max(0.0, num / (den > 0 ? den : 1e-12)));
        double cxp = co * rx * y1p / ry;
        double cyp = -co * ry * x1p / rx;
        double cx = cosP * cxp - sinP * cyp + (from.x + to.x) / 2.0;
        double cy = sinP * cxp + cosP * cyp + (from.y + to.y) / 2.0;

        auto angle = [](double ux, double uy, double vx, double vy) {
            double dot = ux * vx + uy * vy;
            double len = std::sqrt((ux * ux + uy * uy) * (vx * vx + vy * vy));
            double a = std::acos(std::clamp(dot / (len > 0 ? len : 1e-12), -1.0, 1.0));
            if (ux * vy - uy * vx < 0) a = -a;
            return a;
        };
        double th1 = angle(1.0, 0.0, (x1p - cxp) / rx, (y1p - cyp) / ry);
        double dth = angle((x1p - cxp) / rx, (y1p - cyp) / ry,
                           (-x1p - cxp) / rx, (-y1p - cyp) / ry);
        if (!sf && dth > 0) dth -= 2 * 3.14159265358979323846;
        else if (sf && dth < 0) dth += 2 * 3.14159265358979323846;

        const double kPi = 3.14159265358979323846;
        int n = static_cast<int>(std::ceil(std::fabs(dth) / (kPi / 2.0)));
        if (n <= 0) n = 1;
        double delta = dth / n;
        double t = 4.0 / 3.0 * std::tan(delta / 4.0);
        double th = th1;
        IconPoint p0 = from;
        auto pt = [&](double a) {
            return IconPoint{ static_cast<float>(cx + rx * std::cos(a) * cosP - ry * std::sin(a) * sinP),
                              static_cast<float>(cy + rx * std::cos(a) * sinP + ry * std::sin(a) * cosP) };
        };
        auto dv = [&](double a) {
            return IconPoint{ static_cast<float>(-rx * std::sin(a) * cosP - ry * std::cos(a) * sinP),
                              static_cast<float>(-rx * std::sin(a) * sinP + ry * std::cos(a) * cosP) };
        };
        for (int i = 0; i < n; ++i) {
            double th2 = th + delta;
            IconPoint p1 = pt(th2);
            IconPoint d0 = dv(th), d1 = dv(th2);
            AddCubic({ p0.x + static_cast<float>(t) * d0.x, p0.y + static_cast<float>(t) * d0.y },
                     { p1.x - static_cast<float>(t) * d1.x, p1.y - static_cast<float>(t) * d1.y },
                     p1);
            p0 = p1;
            th = th2;
        }
    }

    void FlushCurrent(IconData& out) {
        for (auto& fig : figures_) {
            if (!fig.segments.empty() || fig.closed) out.figures.push_back(std::move(fig));
        }
        figures_.clear();
        cur_ = -1;
    }

    std::string buf_;
    const char* p_ = nullptr;
    const char* end_ = nullptr;
    char cmd_ = 0;
    bool rel_ = false;
    double cx_ = 0, cy_ = 0;   // current point
    double sx_ = 0, sy_ = 0;   // current subpath start
    double lx_ = 0, ly_ = 0;   // previous cubic control point
    double qx_ = 0, qy_ = 0;   // previous quadratic control point
    bool hasCubic_ = false, hasQuad_ = false;
    std::vector<IconData::Figure> figures_; // subpaths pending write-out
    int cur_ = -1;
};

} // namespace detail

inline Icon Icon::FromSvgPath(std::string_view pathData) {
    Icon icon;
    icon.data_ = std::make_shared<detail::IconData>();
    detail::SvgPathParser parser(pathData);
    if (!parser.Parse(*icon.data_)) icon.data_->figures.clear();
    // ARCH38-02: register live icon data so Shutdown can release device geometry; also prunes dead weak refs
    if (icon.data_) {
        auto& live = detail::IconData::Live();
        if (live.size() % 64 == 0)
            live.erase(std::remove_if(live.begin(), live.end(),
                                      [](const std::weak_ptr<detail::IconData>& w) {
                                          return w.expired();
                                      }),
                       live.end());
        live.emplace_back(icon.data_);
    }
    return icon;
}

Icon::Icon() = default;
Icon::~Icon() = default;
Icon::Icon(const Icon&) = default;
Icon& Icon::operator=(const Icon&) = default;
Icon::Icon(Icon&&) noexcept = default;
Icon& Icon::operator=(Icon&&) noexcept = default;
bool Icon::IsEmpty() const { return !data_ || data_->figures.empty(); }

// ---------------------------------------------------------------------------
// RenderPainter: D2D implementation of the Painter interface (framework-internal)
// ---------------------------------------------------------------------------

class RenderPainter final : public Painter {
public:
    void SetFrame(ID2D1DeviceContext* ctx, ID2D1SolidColorBrush* brush) {
        // After a device rebuild the ctx pointer object changes: device-dependent caches (gradients / shadow effects) are
        // invalidated accordingly (paired with DiscardDevice→OnDeviceLost as a double guard against address-reuse misses)
        if (ctx != ctx_) {
            gradients_.clear();
            shadowFx_.Reset();
            clipFx_.Reset();
            layers_.clear();
            layersCtx_ = nullptr;
        }
        ctx_ = ctx;
        brush_ = brush;
    }

    void FillRect(const Rect& rect, const Color& color) override {
        if (!ctx_ || !brush_) return;
        brush_->SetColor(detail::ToD2D(color));
        ctx_->FillRectangle(
            D2D1::RectF(rect.x, rect.y, rect.Right(), rect.Bottom()), brush_);
    }

    void StrokeRect(const Rect& rect, const Color& color, float width = 1.0f) override {
        if (!ctx_ || !brush_) return;
        brush_->SetColor(detail::ToD2D(color));
        ctx_->DrawRectangle(
            D2D1::RectF(rect.x, rect.y, rect.Right(), rect.Bottom()), brush_, width);
    }

    void StrokeRoundedRect(const Rect& rect, float radius, const Color& color,
                           float width = 1.0f) override {
        if (!ctx_ || !brush_) return;
        radius = std::min({ radius, rect.w * 0.5f, rect.h * 0.5f });
        brush_->SetColor(detail::ToD2D(color));
        ctx_->DrawRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(rect.x, rect.y, rect.Right(), rect.Bottom()),
                              radius, radius),
            brush_, width);
    }

    void StrokeRectDashed(const Rect& rect, const Color& color,
                          float width = 1.0f) override {
        if (!ctx_ || !brush_ || !EnsureDashStyle()) return;
        brush_->SetColor(detail::ToD2D(color));
        ctx_->DrawRectangle(
            D2D1::RectF(rect.x, rect.y, rect.Right(), rect.Bottom()),
            brush_, width, dashStyle_.Get());
    }

    void StrokeRoundedRectDashed(const Rect& rect, float radius,
                                 const Color& color,
                                 float width = 1.0f) override {
        if (!ctx_ || !brush_ || !EnsureDashStyle()) return;
        radius = std::min({ radius, rect.w * 0.5f, rect.h * 0.5f });
        brush_->SetColor(detail::ToD2D(color));
        ctx_->DrawRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(rect.x, rect.y, rect.Right(), rect.Bottom()),
                              radius, radius),
            brush_, width, dashStyle_.Get());
    }

    void FillRoundedRect(const Rect& rect, float radius, const Color& color) override {
        if (!ctx_ || !brush_) return;
        radius = std::min({ radius, rect.w * 0.5f, rect.h * 0.5f });
        brush_->SetColor(detail::ToD2D(color));
        ctx_->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(rect.x, rect.y, rect.Right(), rect.Bottom()),
                              radius, radius),
            brush_);
    }

    void DrawShadow(const Rect& rect, float radius, float offsetY,
                    float blur, const Color& color) override {
        if (!ctx_ || rect.w <= 0 || rect.h <= 0 || blur <= 0 || color.a <= 0) return;
        radius = std::min({ radius, rect.w * 0.5f, rect.h * 0.5f });

        // The shadow source shape is recorded as a command list: an opaque white rounded rect (the shadow effect takes its
        // alpha; color and opacity come from D2D1_SHADOW_PROP_COLOR); the offset is
        // folded into the source rect coordinates, avoiding reliance on DrawImage target positioning.
        detail::ComPtr<ID2D1CommandList> source;
        if (FAILED(ctx_->CreateCommandList(source.GetAddressOf()))) return;
        detail::ComPtr<ID2D1Image> previous;
        ctx_->GetTarget(previous.GetAddressOf());
        ctx_->SetTarget(source.Get());
        brush_->SetColor(D2D1_COLOR_F{ 1.0f, 1.0f, 1.0f, 1.0f });
        ctx_->FillRoundedRectangle(
            D2D1::RoundedRect(
                D2D1::RectF(rect.x, rect.y + offsetY, rect.Right(), rect.Bottom() + offsetY),
                radius, radius),
            brush_);
        ctx_->SetTarget(previous.Get());
        if (FAILED(source->Close())) return;

        // Lazy cache for shadow/composite effects (input and params are reset per call; the command list must be
        // re-recorded when the rect changes; effects are reused across calls — previously 2 CreateEffect calls each time,
        // hundreds of allocations per frame with dozens of cards on screen, round-15 §3.13)
        if (!shadowFx_ &&
            FAILED(ctx_->CreateEffect(CLSID_D2D1Shadow, shadowFx_.GetAddressOf())))
            return;
        shadowFx_->SetInput(0, source.Get());
        shadowFx_->SetValue(D2D1_SHADOW_PROP_BLUR_STANDARD_DEVIATION, blur * 0.5f);
        shadowFx_->SetValue(D2D1_SHADOW_PROP_COLOR,
                            D2D1_VECTOR_4F{ color.r, color.g, color.b, color.a });
        // CSS outer shadows are visible only outside the border box (the spec clips inside). Without clipping the shadow floods
        // the rect interior and shows through semi-transparent fills (--card has alpha) — DEST_OUT compositing punches
        // the border-box region (unoffset) out of the shadow.
        detail::ComPtr<ID2D1CommandList> mask;
        if (FAILED(ctx_->CreateCommandList(mask.GetAddressOf()))) {
            ctx_->DrawImage(shadowFx_.Get());
            return;
        }
        ctx_->SetTarget(mask.Get());
        brush_->SetColor(D2D1_COLOR_F{ 1.0f, 1.0f, 1.0f, 1.0f });
        ctx_->FillRoundedRectangle(
            D2D1::RoundedRect(
                D2D1::RectF(rect.x, rect.y, rect.Right(), rect.Bottom()),
                radius, radius),
            brush_);
        ctx_->SetTarget(previous.Get());
        if (FAILED(mask->Close())) return;
        if (!clipFx_ &&
            FAILED(ctx_->CreateEffect(CLSID_D2D1Composite, clipFx_.GetAddressOf()))) {
            ctx_->DrawImage(shadowFx_.Get());
            return;
        }
        clipFx_->SetInputEffect(0, shadowFx_.Get());
        clipFx_->SetInput(1, mask.Get());
        clipFx_->SetValue(D2D1_COMPOSITE_PROP_MODE, D2D1_COMPOSITE_MODE_DESTINATION_OUT);
        ctx_->DrawImage(clipFx_.Get());
    }

    void FillRoundedRectGradient(const Rect& rect, float radius,
                                 const Color& top, const Color& bottom) override {
        if (!ctx_ || rect.w <= 0 || rect.h <= 0) return;
        radius = std::min({ radius, rect.w * 0.5f, rect.h * 0.5f });

        // quantized gradient endpoint colors form the cache key (theme colors are constant, 100% hit rate)
        auto pack = [](const Color& c) {
            auto q = [](float v) {
                return static_cast<uint32_t>(v * 255.0f + 0.5f) & 0xFFu;
            };
            return q(c.r) << 24 | q(c.g) << 16 | q(c.b) << 8 | q(c.a);
        };
        uint64_t key = (static_cast<uint64_t>(pack(top)) << 32) | pack(bottom);
        auto it = gradients_.find(key);
        if (it == gradients_.end()) {
            D2D1_GRADIENT_STOP stops[2]{
                { 0.0f, detail::ToD2D(top) }, { 1.0f, detail::ToD2D(bottom) } };
            detail::ComPtr<ID2D1GradientStopCollection> collection;
            if (FAILED(ctx_->CreateGradientStopCollection(
                    stops, 2, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP,
                    collection.GetAddressOf()))) return;
            it = gradients_.emplace(key, std::move(collection)).first;
        }
        D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES props{
            D2D1::Point2F(rect.x, rect.y),
            D2D1::Point2F(rect.x, rect.Bottom()) };
        detail::ComPtr<ID2D1LinearGradientBrush> brush;
        if (FAILED(ctx_->CreateLinearGradientBrush(
                props, it->second.Get(), brush.GetAddressOf()))) return;
        ctx_->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(rect.x, rect.y, rect.Right(), rect.Bottom()),
                              radius, radius),
            brush.Get());
    }

    void FillRoundedRectGradientStops(const Rect& rect, float radius,
                                      const GradientStop* stops, size_t count,
                                      const Point& from, const Point& to) override {
        if (!ctx_ || rect.w <= 0 || rect.h <= 0 || !stops || count < 2) return;
        radius = std::min({ radius, rect.w * 0.5f, rect.h * 0.5f });
        ID2D1GradientStopCollection* collection = CachedStops(stops, count, true);
        if (!collection) return;
        D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES props{
            D2D1::Point2F(from.x, from.y), D2D1::Point2F(to.x, to.y) };
        detail::ComPtr<ID2D1LinearGradientBrush> brush;
        if (FAILED(ctx_->CreateLinearGradientBrush(
                props, collection, brush.GetAddressOf()))) return;
        ctx_->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(rect.x, rect.y, rect.Right(), rect.Bottom()),
                              radius, radius),
            brush.Get());
    }

    void FillEllipseGradientStops(const Rect& area, const GradientStop* stops,
                                  size_t count) override {
        if (!ctx_ || area.w <= 0 || area.h <= 0 || count < 2) return;
        Point c{ area.x + area.w * 0.5f, area.y + area.h * 0.5f };
        ID2D1GradientStopCollection* collection = CachedStops(stops, count, false);
        if (!collection) return;
        D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES props{
            D2D1::Point2F(c.x, c.y), D2D1_POINT_2F{ 0.0f, 0.0f },
            area.w * 0.5f, area.h * 0.5f };
        detail::ComPtr<ID2D1RadialGradientBrush> brush;
        if (FAILED(ctx_->CreateRadialGradientBrush(
                props, collection, brush.GetAddressOf()))) return;
        ctx_->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(c.x, c.y),
                          area.w * 0.5f, area.h * 0.5f), brush.Get());
    }

    void FillEllipse(const Rect& area, const Color& color) override {
        if (!ctx_ || !brush_ || area.w <= 0 || area.h <= 0) return;
        brush_->SetColor(detail::ToD2D(color));
        ctx_->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(area.x + area.w * 0.5f,
                                        area.y + area.h * 0.5f),
                          area.w * 0.5f, area.h * 0.5f), brush_);
    }

    void StrokeEllipse(const Rect& area, const Color& color,
                       float width = 1.0f) override {
        if (!ctx_ || !brush_ || area.w <= 0 || area.h <= 0) return;
        brush_->SetColor(detail::ToD2D(color));
        ctx_->DrawEllipse(
            D2D1::Ellipse(D2D1::Point2F(area.x + area.w * 0.5f,
                                        area.y + area.h * 0.5f),
                          area.w * 0.5f, area.h * 0.5f), brush_, width);
    }

    void DrawLine(const Point& from, const Point& to, const Color& color,
                  float width = 1.0f) override {
        if (!ctx_ || !brush_) return;
        brush_->SetColor(detail::ToD2D(color));
        ctx_->DrawLine(D2D1::Point2F(from.x, from.y), D2D1::Point2F(to.x, to.y),
                       brush_, width);
    }

    /// Build a round-capped arc (relative to origin; PERF-06: extracted from StrokeArc for shape caching).
    /// large = major-arc flag; cw = clockwise (sweep>0).
    detail::ComPtr<ID2D1PathGeometry> BuildArcShape(float a0, float a1,
                                                    float r0, float r1,
                                                    bool large, bool cw) {
        auto pt = [&](float a, float r) {
            return D2D1_POINT_2F{ r * std::cos(a), r * std::sin(a) };
        };
        D2D1_ARC_SIZE arcSize = large ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;
        D2D1_SWEEP_DIRECTION dir = cw ? D2D1_SWEEP_DIRECTION_CLOCKWISE
                                      : D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE;
        D2D1_SWEEP_DIRECTION back = cw ? D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE
                                       : D2D1_SWEEP_DIRECTION_CLOCKWISE;
        // Round caps on both ends (baseline stroke-linecap:round): half-circles centered at the radial midpoint
        float cr = (r0 - r1) * 0.5f;
        detail::ComPtr<ID2D1PathGeometry> path;
        if (FAILED(g_d2d->CreatePathGeometry(path.GetAddressOf()))) return path;
        ID2D1GeometrySink* sink = nullptr;
        if (FAILED(path->Open(&sink))) return detail::ComPtr<ID2D1PathGeometry>{};
        sink->BeginFigure(pt(a0, r0), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddArc(D2D1_ARC_SEGMENT{
            pt(a1, r0), D2D1::SizeF(r0, r0), 0.0f, dir, arcSize });
        sink->AddArc(D2D1_ARC_SEGMENT{
            pt(a1, r1), D2D1::SizeF(cr, cr), 0.0f, dir, D2D1_ARC_SIZE_SMALL });
        sink->AddArc(D2D1_ARC_SEGMENT{
            pt(a0, r1), D2D1::SizeF(r1, r1), 0.0f, back, arcSize });
        // The start cap must also wind along increasing θ (dir): the outer arc walks a→a1, so the start cap must
        // wind from the inner point (θ=a0+180°) back to the outer point (θ=a0), also increasing θ — written as "back",
        // the cap half-disc lands inside the stroke body and cancels it under FILL_MODE_ALTERNATE,
        // leaving a flat cut at the start (round-15 §3.11)
        sink->AddArc(D2D1_ARC_SEGMENT{
            pt(a0, r0), D2D1::SizeF(cr, cr), 0.0f, dir, D2D1_ARC_SIZE_SMALL });
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        bool ok = SUCCEEDED(sink->Close());
        sink->Release();
        if (!ok) return detail::ComPtr<ID2D1PathGeometry>{};
        return path;
    }

    void StrokeArc(const Point& center, float radius, float width,
                   float startDeg, float sweepDeg, const Color& color) override {
        if (!ctx_ || !brush_ || radius <= 0 || width <= 0 || sweepDeg == 0)
            return;
        constexpr float kPi = 3.14159265358979323846f;
        // 0° = 12 o'clock (-90° in screen angle); positive is clockwise (screen y points down).
        float a0 = (startDeg - 90.0f) * kPi / 180.0f;
        float a1 = a0 + sweepDeg * kPi / 180.0f;
        // Centered stroke (SVG stroke semantics): the arc band spans radius ± width/2, concentric with
        // the track drawn by StrokeEllipse (E-07; previously biased inward, arc hugging one side of the track)
        float r0 = radius + width * 0.5f, r1 = radius - width * 0.5f;
        if (r1 <= 0) { r0 = width; r1 = width * 0.5f; }
        bool large = std::abs(sweepDeg) > 180.0f;

        // PERF-06: arcs (relative to origin) are cached by quantized (start,sweep,r0,r1) — the only
        // caller, ProgressRing, rebuilds PathGeometry per ring per frame (angles step 8°,
        // so the real key space is only 45 entries). Drawing translates temporarily to the center
        auto q = [](float v) { return static_cast<uint16_t>(
            static_cast<int16_t>(std::lround(v * 4.0f))); };
        uint64_t key = static_cast<uint64_t>(q(startDeg));
        key |= static_cast<uint64_t>(q(sweepDeg)) << 16;
        key |= static_cast<uint64_t>(q(r0)) << 32;
        key |= static_cast<uint64_t>(q(r1)) << 48;
        ID2D1PathGeometry* path = nullptr;
        auto it = arcPaths_.find(key);
        if (it != arcPaths_.end()) {
            path = it->second.Get();
        } else {
            auto built = BuildArcShape(a0, a1, r0, r1, large, sweepDeg > 0);
            if (!built) return;
            if (arcPaths_.size() > 256) arcPaths_.clear();
            path = arcPaths_.emplace(key, std::move(built)).first->second.Get();
        }
        brush_->SetColor(detail::ToD2D(color));
        D2D1_MATRIX_3X2_F old;
        ctx_->GetTransform(&old);
        ctx_->SetTransform(D2D1::Matrix3x2F::Translation(center.x, center.y) * old);
        ctx_->FillGeometry(path, brush_);
        ctx_->SetTransform(old);
    }

    void DrawIcon(const Icon& icon, const Rect& area, const Color& color) override {
        if (!ctx_ || !brush_ || icon.IsEmpty()) return;
        ID2D1PathGeometry* geo = icon.data_->GetGeometry();
        if (!geo) return;
        float scale = std::min(area.w, area.h) / 24.0f;
        if (scale <= 0.0f) return;
        float ox = area.x + (area.w - 24.0f * scale) * 0.5f;
        float oy = area.y + (area.h - 24.0f * scale) * 0.5f;
        D2D1::Matrix3x2F m = D2D1::Matrix3x2F::Scale(D2D1::SizeF(scale, scale)) *
                             D2D1::Matrix3x2F::Translation(ox, oy);
        D2D1_MATRIX_3X2_F old;
        ctx_->GetTransform(&old);
        ctx_->SetTransform(m * old);
        brush_->SetColor(detail::ToD2D(color));
        ctx_->FillGeometry(geo, brush_);
        ctx_->SetTransform(old);
    }

    void PushClip(const Rect& rect) override {
        if (!ctx_) return;
        ctx_->PushAxisAlignedClip(
            D2D1::RectF(rect.x, rect.y, rect.Right(), rect.Bottom()),
            D2D1_ANTIALIAS_MODE_ALIASED);
    }

    void PopClip() override {
        if (!ctx_) return;
        ctx_->PopAxisAlignedClip();
    }

    // Rounded-rect clip layer: geometry mask + PushLayer, sharing the layers_ stack with PushOpacity
    // (so frame-start FlattenLayers / device-loss cleanup covers it too). Mask geometry is created per push —
    // call frequency is low (dialog footer), not worth caching.
    void PushClipRounded(const Rect& rect, float radius) override {
        if (!ctx_) { layers_.emplace_back(); return; }
        detail::ComPtr<ID2D1Factory> factory;
        ctx_->GetFactory(factory.GetAddressOf());
        detail::ComPtr<ID2D1RoundedRectangleGeometry> geo;
        if (!factory ||
            FAILED(factory->CreateRoundedRectangleGeometry(
                D2D1::RoundedRect(D2D1::RectF(rect.x, rect.y, rect.Right(), rect.Bottom()),
                                  radius, radius),
                geo.GetAddressOf()))) {
            layers_.emplace_back();   // failure: null placeholder, skipped at Pop (no clipping)
            return;
        }
        detail::ComPtr<ID2D1Layer> layer;
        if (FAILED(ctx_->CreateLayer(layer.GetAddressOf()))) {
            layers_.emplace_back();
            return;
        }
        D2D1_LAYER_PARAMETERS1 lp{};
        lp.contentBounds = D2D1::InfiniteRect();
        lp.geometricMask = geo.Get();
        lp.maskAntialiasMode = D2D1_ANTIALIAS_MODE_PER_PRIMITIVE;
        lp.maskTransform = D2D1::Matrix3x2F::Identity();
        lp.opacity = 1.0f;
        lp.opacityBrush = nullptr;
        lp.layerOptions = D2D1_LAYER_OPTIONS1_NONE;
        ctx_->PushLayer(&lp, layer.Get());
        layersCtx_ = ctx_;
        layers_.push_back(std::move(layer));
    }

    void PopClipRounded() override {
        if (layers_.empty()) return;
        // GAP50-07: same contract as FlattenLayers — after a device rebuild swaps the context, PopLayer must not run on
        // the new ctx_ (stack imbalance ⇒ every subsequent EndDraw fails)
        if (layers_.back() && ctx_ && ctx_ == layersCtx_) ctx_->PopLayer();
        layers_.pop_back();
    }

    // Group opacity (CSS opacity): PushLayer's constant opacity composites within the group first, then
    // blends with the background — different from per-element alpha multiply (e.g. a toggle knob sitting on track color).
    void PushOpacity(float opacity) override {
        if (!ctx_) { layers_.emplace_back(); return; }
        detail::ComPtr<ID2D1Layer> layer;
        if (FAILED(ctx_->CreateLayer(layer.GetAddressOf()))) {
            layers_.emplace_back();   // push failed: null placeholder, skipped at Pop
            return;
        }
        D2D1_LAYER_PARAMETERS1 lp{};
        lp.contentBounds = D2D1::InfiniteRect();
        lp.maskAntialiasMode = D2D1_ANTIALIAS_MODE_ALIASED;
        lp.maskTransform = D2D1::Matrix3x2F::Identity();
        lp.opacity = opacity;
        lp.opacityBrush = nullptr;
        lp.layerOptions = D2D1_LAYER_OPTIONS1_NONE;
        ctx_->PushLayer(&lp, layer.Get());
        layersCtx_ = ctx_;
        layers_.push_back(std::move(layer));
    }

    void PopOpacity() override {
        if (layers_.empty()) return;
        if (layers_.back() && ctx_ && ctx_ == layersCtx_) ctx_->PopLayer();
        layers_.pop_back();
    }

    void DrawText(std::wstring_view text, const Font& font, const Rect& area,
                  const Color& color, HAlign h = HAlign::Left,
                  VAlign v = VAlign::Center, bool wrap = false) override {
        if (!ctx_ || !brush_) return;
        IDWriteTextFormat* format = GetFormat(font);
        if (!format) return;
        DWRITE_TEXT_ALIGNMENT ta =
            h == HAlign::Center ? DWRITE_TEXT_ALIGNMENT_CENTER
            : h == HAlign::Right ? DWRITE_TEXT_ALIGNMENT_TRAILING
                                 : DWRITE_TEXT_ALIGNMENT_LEADING;
        DWRITE_PARAGRAPH_ALIGNMENT pa =
            v == VAlign::Top    ? DWRITE_PARAGRAPH_ALIGNMENT_NEAR
            : v == VAlign::Bottom ? DWRITE_PARAGRAPH_ALIGNMENT_FAR
                                  : DWRITE_PARAGRAPH_ALIGNMENT_CENTER;
        DWRITE_WORD_WRAPPING ww = wrap ? DWRITE_WORD_WRAPPING_WRAP
                                       : DWRITE_WORD_WRAPPING_NO_WRAP;
        brush_->SetColor(detail::ToD2D(color));
        if (font.tabularNumerals && g_dwrite && !text.empty()) {
            // Tabular numerals (baseline font-variant-numeric:tabular-nums): requires
            // enabling the OpenType feature per character via TextLayout + Typography. Layouts are cached by
            // (text,font,drawing area) for reuse (P-02: slider drags / progress animation would rebuild per frame);
            // alignment/wrapping state is cheap and re-set before each draw.
            auto& cache = detail::TextCacheInstance();
            const std::wstring& resolved = detail::ResolveFontFamily(detail::EffectiveFontFamily(font.family));
            detail::TextCache::MKeyView tkey{
                text, resolved, font.size,
                static_cast<UINT32>(detail::ToDWrite(font.weight)),
                std::max(0.0f, area.w), std::max(0.0f, area.h),
                font.lineHeight };
            IDWriteTextLayout* layout = nullptr;
            if (auto it = cache.tabLayouts.find(tkey);
                it != cache.tabLayouts.end()) {
                layout = it->second.Get();
            } else {
                detail::ComPtr<IDWriteTextLayout> fresh;
                if (SUCCEEDED(g_dwrite->CreateTextLayout(
                        text.data(), static_cast<UINT32>(text.size()), format,
                        std::max(0.0f, area.w), std::max(0.0f, area.h),
                        fresh.GetAddressOf()))) {
                    detail::ComPtr<IDWriteTypography> tp;
                    if (SUCCEEDED(g_dwrite->CreateTypography(tp.GetAddressOf()))) {
                        DWRITE_FONT_FEATURE feat{
                            DWRITE_FONT_FEATURE_TAG_TABULAR_FIGURES, 1 };
                        tp->AddFontFeature(feat);
                        fresh->SetTypography(
                            tp.Get(),
                            DWRITE_TEXT_RANGE{ 0, static_cast<UINT32>(text.size()) });
                    }
                    if (cache.tabLayouts.size() >= 256) cache.tabLayouts.clear();
                    auto ins = cache.tabLayouts.emplace(
                        detail::TextCache::MKey{
                            std::wstring(text), resolved, font.size,
                            static_cast<UINT32>(detail::ToDWrite(font.weight)),
                            std::max(0.0f, area.w), std::max(0.0f, area.h),
                            font.lineHeight },
                        std::move(fresh));
                    layout = ins.first->second.Get();
                }
            }
            if (layout) {
                layout->SetTextAlignment(ta);
                layout->SetParagraphAlignment(pa);
                layout->SetWordWrapping(ww);
                ctx_->DrawTextLayout(D2D1::Point2F(area.x, area.y),
                                     layout, brush_);
                return;
            }
        }
        format->SetTextAlignment(ta);
        format->SetParagraphAlignment(pa);
        format->SetWordWrapping(ww);
        ctx_->DrawTextW(text.data(), static_cast<UINT32>(text.size()), format,
                        D2D1::RectF(area.x, area.y, area.Right(), area.Bottom()),
                        brush_);
    }

private:
    /// Get (or create and cache) a multi-stop gradient collection (PERF-04). gamma22 selects sRGB
    /// perceptual (D2D1_GAMMA_2_2) or linear (D2D1_GAMMA_1_0) interpolation.
    ID2D1GradientStopCollection* CachedStops(const GradientStop* stops,
                                             size_t count, bool gamma22) {
        // round-48 P3: contract made explicit — both call sites guard up front; this is a backstop
        if (!stops || count < 2) return nullptr;
        auto packColor = [](const Color& c) {
            auto q = [](float v) {
                return static_cast<uint32_t>(v * 255.0f + 0.5f) & 0xFFu;
            };
            return (q(c.r) << 24) | (q(c.g) << 16) | (q(c.b) << 8) | q(c.a);
        };
        uint64_t key = 14695981039346656037ull;
        auto mix = [&key](uint64_t v) { key = (key ^ v) * 1099511628211ull; };
        mix(count);
        mix(gamma22 ? 1 : 0);
        for (size_t i = 0; i < count; ++i) {
            mix(static_cast<uint64_t>(
                static_cast<uint32_t>(stops[i].pos * 65535.0f + 0.5f)));
            mix(packColor(stops[i].color));
        }
        auto it = stopCollections_.find(key);
        if (it != stopCollections_.end()) return it->second.Get();
        std::vector<D2D1_GRADIENT_STOP> d2dStops(count);
        for (size_t i = 0; i < count; ++i)
            d2dStops[i] = { stops[i].pos, detail::ToD2D(stops[i].color) };
        detail::ComPtr<ID2D1GradientStopCollection> collection;
        if (FAILED(ctx_->CreateGradientStopCollection(
                d2dStops.data(), static_cast<UINT32>(count),
                gamma22 ? D2D1_GAMMA_2_2 : D2D1_GAMMA_1_0,
                D2D1_EXTEND_MODE_CLAMP, collection.GetAddressOf())))
            return nullptr;
        // PERF38-02: evict half instead of clearing the whole table — with >256 steady-state distinct entries, full clears
        // would thrash "fill→clear→rebuild" (one COM collection creation per rebuild), worse than no cache
        if (stopCollections_.size() > 256) {
            auto half = std::next(stopCollections_.begin(),
                                  static_cast<std::ptrdiff_t>(stopCollections_.size() / 2));
            stopCollections_.erase(stopCollections_.begin(), half);
        }
        return stopCollections_.emplace(key, std::move(collection))
            .first->second.Get();
    }

    IDWriteTextFormat* GetFormat(const Font& font) {
        return detail::GetCachedFormat(font);   // globally shared cache
    }

    /// Lazily create the dashed stroke style (shared by square/rounded dashed strokes).
    bool EnsureDashStyle() {
        if (!dashStyle_) {
            D2D1_STROKE_STYLE_PROPERTIES props{
                D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT,
                D2D1_LINE_JOIN_MITER, 10.0f,
                D2D1_DASH_STYLE_DASH, 0.0f };
            g_d2d->CreateStrokeStyle(props, nullptr, 0, dashStyle_.GetAddressOf());
        }
        return dashStyle_.Get() != nullptr;
    }

    ID2D1DeviceContext* ctx_ = nullptr;
    ID2D1SolidColorBrush* brush_ = nullptr;
    detail::ComPtr<ID2D1StrokeStyle> dashStyle_;
    std::unordered_map<uint64_t, detail::ComPtr<ID2D1GradientStopCollection>>
        gradients_;
    // Multi-stop gradient collection cache (PERF-04): content-hash cache over quantized stop positions/colors
    // — both Stops variants previously heap-allocated a vector + created a collection per call (per-frame call sites:
    // background blobs/Banner/Label/Avatar/NavigationView). Device-dependent; OnDeviceLost clears it
    std::unordered_map<uint64_t, detail::ComPtr<ID2D1GradientStopCollection>>
        stopCollections_;
    // Round-capped arc cache (PERF-06, device-independent — built from the g_d2d factory)
    std::unordered_map<uint64_t, detail::ComPtr<ID2D1PathGeometry>> arcPaths_;
    // PushOpacity active layers: null entry = push failed (placeholder; Pop skips PopLayer);
    // layersCtx_ records which ctx the layer stack lives on — after a device rebuild the old layers are void, never PopLayer them
    std::vector<detail::ComPtr<ID2D1Layer>> layers_;
    ID2D1DeviceContext* layersCtx_ = nullptr;
    // DrawShadow effect cache (device-dependent; released on SetFrame ctx change / OnDeviceLost;
    // previously 2 CommandLists + 2 Effects per call — hundreds of allocations per frame with dozens of cards,
    // round-15 §3.13)
    detail::ComPtr<ID2D1Effect> shadowFx_;
    detail::ComPtr<ID2D1Effect> clipFx_;

public:
    /// Frame-start/end backstop: flatten a leftover layer stack (if OnPaint returns early or throws between
    /// Push/Pop, the D2D layer stack stays unbalanced forever ⇒ EndDraw fails every frame and the window freezes on an old frame,
    /// round-15 §3.12). The window render loop calls this before BeginDraw.
    void FlattenLayers() {
        if (layers_.empty()) return;
        if (ctx_ && ctx_ == layersCtx_) {
            for (auto it = layers_.rbegin(); it != layers_.rend(); ++it)
                if (*it) ctx_->PopLayer();
        }
        layers_.clear();
        layersCtx_ = nullptr;
    }

    /// Device loss/rebuild: release all device-dependent caches (gradient stop collections are device-dependent;
    /// previously DiscardDevice only reset the render target without clearing caches ⇒ gradients silently broke after rebuild,
    /// round-15 §3.13). Called from DiscardDevice.
    void OnDeviceLost() {
        gradients_.clear();
        stopCollections_.clear();
        shadowFx_.Reset();
        clipFx_.Reset();
        layers_.clear();
        layersCtx_ = nullptr;
    }

private:
};

// ---------------------------------------------------------------------------
// Theme
// ---------------------------------------------------------------------------

const Theme& Theme::Light() {
    // Color values from docs/风格参考/Fluent示例库.html (WinUI 3 design token approximation).
    // Overlay-style tokens keep the baseline's semi-transparent values and are composited onto their backing surface
    // (card face / page background) via D2D source-over; one token renders correctly on both backings.
    static const Theme t{
        Color::Rgb(0xF3F3F3), // windowBackground
        Color::Rgb(0xF3F3F3), // titleBackground (same as the backing; depth is expressed by content cards)
        Color::Rgb(0xF3F3F3), // menuBackground
        Color::Rgb(0xF3F3F3), // toolbarBackground
        Color::Rgb(0xF3F3F3), // contentBackground
        Color::Rgb(0xFFFFFF, 0.72f), // cardBackground（--card rgba(255,255,255,.72)，I-11）
        Color::Rgb(0xFFFFFF, 0.45f), // cardSecondary (--card2 α.45 over the card face ⇒ ≈253, A-04)
        Color::Rgb(0xEBEBEB), // statusBackground
        Color::Rgb(0x000000, 0.896f), // text
        Color::Rgb(0x000000, 0.606f), // textSecondary
        Color::Rgb(0x000000, 0.361f), // textDisabled
        Color::Rgb(0x000000, 0.896f), // titleText
        Color::Rgb(0x000000, 0.606f), // statusText
        Color::Rgb(0xFFFFFF), // textOnAccent
        Color::Rgb(0xFFFFFF, 0.70f),  // control（--ctrl）
        Color::Rgb(0xFBFBFB, 0.88f),  // controlHover（--ctrl-hov）
        Color::Rgb(0xF9F9F9, 0.80f),  // controlPressed（--ctrl-act）
        Color::Rgb(0x000000, 0.896f), // controlText
        Color::Rgb(0xFFFFFF, 0.70f),  // inputBackground
        Color::Rgb(0x005FB8), // accent
        Color::Rgb(0x1A6AC7), // accentHover
        Color::Rgb(0x0053A2), // accentPressed
        Color::Rgb(0x005FB8, 0.10f), // accentSoft
        Color::Rgb(0x000000, 0.037f), // hoverSoft（subtle-hov）
        Color::Rgb(0xFFFFFF), // popupBackground (solid Layer)
        Color::Rgb(0xF6F6F6), // popupHover (baseline --subtle-hov light composite #F6F6F6)
        Color::Rgb(0x000000, 0.08f),  // divider
        Color::Rgb(0x000000, 0.057f), // selectedSoft（sel）
        Color::Rgb(0x000000, 0.446f), // textTertiary
        Color::Rgb(0x005FB8), // accentText
        Color::Rgb(0x000000, 0.13f),  // track
        Color::Rgb(0x000000, 0.058f), // controlStroke
        Color::Rgb(0x000000, 0.162f), // controlStrokeBottom
        Color::Rgb(0xF4F4F4), // disabledBackground (dis-bg solid)
        Color::Rgb(0xFFFFFF), // sliderThumb (white in both themes)
        Color::Rgb(0x0067C0, 0.09f),  // blob1
        Color::Rgb(0x00B7C3, 0.10f),  // blob2
        Color::Rgb(0x0F7B0F), // success
        Color::Rgb(0xE7F2E7), // successBackground
        Color::Rgb(0x9D5D00), // warning
        Color::Rgb(0xFCF4DA), // warningBackground
        Color::Rgb(0xC42B1C), // error
        Color::Rgb(0xFDF3F4), // errorBackground
        Color::Rgb(0x0067C0), // info
        Color::Rgb(0xE9F2FB), // infoBackground
        Color::Rgb(0xEBEBEB), // bg2 (= statusBackground, baseline --bg2)
        Color::Rgb(0xF9F9F9), // bg3 (baseline --bg3)
        Color::Rgb(0x000000, 0.024f), // subtleActive (baseline --subtle-act)
        Color::Rgb(0xF9F9F9), // tooltipBackground (baseline --tooltip-bg)
        Color::Rgb(0xC42B1C), // captionCloseHover (Win11 close-hover red, same in both themes)
        Color::Rgb(0xC74031), // captionClosePressed (Win11 light pressed, measured)
        Color::Rgb(0x000000, 0.025f), // captionPressed (Win11 dark measured equivalent: tint lighter than hover)
    };
    return t;
}

const Theme& Theme::Dark() {
    static const Theme t{
        Color::Rgb(0x202020), // windowBackground
        Color::Rgb(0x202020), // titleBackground
        Color::Rgb(0x202020), // menuBackground
        Color::Rgb(0x202020), // toolbarBackground
        Color::Rgb(0x202020), // contentBackground
        Color::Rgb(0xFFFFFF, 0.055f), // cardBackground（--card rgba(255,255,255,.055)，I-11）
        Color::Rgb(0xFFFFFF, 0.033f), // cardSecondary (--card2 α.033 over the card face ⇒ ≈#333, A-04)
        Color::Rgb(0x1C1C1C), // statusBackground
        Color::Rgb(0xFFFFFF), // text
        Color::Rgb(0xFFFFFF, 0.786f), // textSecondary
        Color::Rgb(0xFFFFFF, 0.365f), // textDisabled
        Color::Rgb(0xFFFFFF), // titleText
        Color::Rgb(0xFFFFFF, 0.786f), // statusText
        Color::Rgb(0x000000), // textOnAccent
        Color::Rgb(0xFFFFFF, 0.06f),  // control（--ctrl）
        Color::Rgb(0xFFFFFF, 0.085f), // controlHover
        Color::Rgb(0xFFFFFF, 0.048f), // controlPressed
        Color::Rgb(0xFFFFFF), // controlText
        Color::Rgb(0xFFFFFF, 0.06f),  // inputBackground
        Color::Rgb(0x4CC2FF), // accent
        Color::Rgb(0x44AFE6), // accentHover
        Color::Rgb(0x3D9BCC), // accentPressed
        Color::Rgb(0x4CC2FF, 0.09f), // accentSoft
        Color::Rgb(0xFFFFFF, 0.061f), // hoverSoft
        Color::Rgb(0x2B2B2B), // popupBackground（solid）
        Color::Rgb(0x383838), // popupHover (baseline --subtle-hov dark composite #383838)
        Color::Rgb(0xFFFFFF, 0.083f), // divider
        Color::Rgb(0xFFFFFF, 0.09f),  // selectedSoft（sel）
        Color::Rgb(0xFFFFFF, 0.544f), // textTertiary
        Color::Rgb(0x4CC2FF), // accentText
        Color::Rgb(0xFFFFFF, 0.16f),  // track
        Color::Rgb(0xFFFFFF, 0.07f),  // controlStroke
        Color::Rgb(0xFFFFFF, 0.094f), // controlStrokeBottom
        Color::Rgb(0xFFFFFF, 0.04f),  // disabledBackground（dis-bg）
        Color::Rgb(0xFFFFFF), // sliderThumb (white in both themes)
        Color::Rgb(0x4CC2FF, 0.07f),  // blob1
        Color::Rgb(0x8B5CF6, 0.08f),  // blob2
        Color::Rgb(0x6CCB5F), // success
        Color::Rgb(0x6CCB5F, 0.09f), // successBackground
        Color::Rgb(0xFCE100), // warning
        Color::Rgb(0xFCE100, 0.07f), // warningBackground
        Color::Rgb(0xFF99A4), // error
        Color::Rgb(0xFF6372, 0.09f), // errorBackground
        Color::Rgb(0x4CC2FF), // info
        Color::Rgb(0x4CC2FF, 0.08f), // infoBackground
        Color::Rgb(0x1C1C1C), // bg2 (= statusBackground, baseline --bg2)
        Color::Rgb(0x272727), // bg3 (baseline --bg3)
        Color::Rgb(0xFFFFFF, 0.042f), // subtleActive (baseline --subtle-act)
        Color::Rgb(0x2C2C2C), // tooltipBackground (baseline --tooltip-bg)
        Color::Rgb(0xC42B1C), // captionCloseHover (Win11 close-hover red, same in both themes)
        Color::Rgb(0xB22A1C), // captionClosePressed (Win11 dark pressed, measured)
        Color::Rgb(0xFFFFFF, 0.044f), // captionPressed (Win11 dark measured equivalent: white tint weaker than hover)
    };
    return t;
}

// ---------------------------------------------------------------------------
// MenuPopup: dropdown menu popup window (framework-internal)
// ---------------------------------------------------------------------------

namespace detail {

/// Popup positioning (R28-06: extracted from Flyout::CreateAt, reused by MenuPopup): prefer popping below the anchor
/// by dropBelow DIP (6 for button anchors, 5 otherwise, R28-09); if it does not fit below and there is more room above,
/// flip above (gap always 5, baseline .menu.up{bottom:calc(100% + 5px)}),
/// finally clamped into work area ∩ owner client rect (S-08). Zero-height anchors (right-click cursor) pop
/// at the cursor (B-12). anchor is in owner-client DIP coordinates; returns the popup top-left in screen pixels.
inline POINT ComputePopupPosition(HWND owner, float dpi, const Rect& anchor,
                                  float width, float height,
                                  float dropBelow) {
    POINT pos{ static_cast<LONG>(std::lround(anchor.x * dpi / 96.0f)),
               static_cast<LONG>(std::lround(anchor.y * dpi / 96.0f)) };
    ClientToScreen(owner, &pos);
    float sx = pos.x * 96.0f / dpi, sy = pos.y * 96.0f / dpi;

    // Clamp range = screen work area ∩ owner window client rect (S-08: a popup must not leave the main window)
    RECT wrc;
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &mi);
    wrc = mi.rcWork;
    RECT crc{ 0, 0, 0, 0 };
    GetClientRect(owner, &crc);
    MapWindowPoints(owner, nullptr, reinterpret_cast<POINT*>(&crc), 2);
    float boundT = std::max(wrc.top, crc.top) * 96.0f / dpi;
    float boundB = std::min(wrc.bottom, crc.bottom) * 96.0f / dpi;
    float boundR = std::min(wrc.right, crc.right) * 96.0f / dpi;

    float drop = anchor.h > 0.0f ? dropBelow : 0.0f;
    float below = boundB - (sy + anchor.h + drop);
    float above = (sy - 5.0f) - boundT;
    float y = sy + anchor.h + drop;
    if (y + height > boundB && above > below)
        y = sy - height - 5.0f;
    y = std::clamp(y, boundT + 2.0f,
                   std::max(boundT + 2.0f, boundB - height - 2.0f));
    float x = sx;
    if (sx + width > boundR - 4.0f) x = std::max(2.0f, boundR - width - 4.0f);
    return POINT{ static_cast<LONG>(std::lround(x * dpi / 96.0f)),
                  static_cast<LONG>(std::lround(y * dpi / 96.0f)) };
}

// ---------------------------------------------------------------------------
// PopupWindow: shared host for the four popups (MenuPopup / ToastPopup / Flyout / ToolTipPopup)
// (round-56 refactor: the four structurally identical copies totaling 1,174 lines were consolidated here). Owns window
// registration/creation, DWM rounding, and the render-target lifecycle (ARCH-02 / ARCH-03 / ARCH-04,
// ARCH38-01 device-loss recovery), close/alive semantics, and the message skeleton; subclasses provide background,
// drawing, interaction messages and destroy cleanup via hooks. Symmetric behavior (stroke contract R25-11, SyncInterval=0 present,
// scroll hint bar R37-06) is written once here — the structural breeding ground for
// "symmetric pair handled on one side only" defects (GAP52-01 / GAP53-01 family) is thereby removed.
// ---------------------------------------------------------------------------
class PopupWindow {
public:
    /// Sentinel return value of OnMessage: message unhandled, pass on to DefWindowProc.
    static constexpr LRESULT kUnhandled = -2;

    bool IsAlive() const { return hwnd_ != nullptr; }

    void Close() {
        if (hwnd_) DestroyWindow(hwnd_);
    }

protected:
    PopupWindow(HWND owner, float dpi, const Theme& theme)
        : owner_(owner), dpi_(dpi), theme_(theme) {}
    virtual ~PopupWindow() = default;   // popups are owned by WindowImpl members, never deleted via a base pointer
    PopupWindow(const PopupWindow&) = delete;
    PopupWindow& operator=(const PopupWindow&) = delete;

    /// Register the window class (duplicate registration is not a failure) and create the popup window + DWM rounding + render
    /// target (ARCH-02: a failed target creation destroys the window). Does not show it — the subclass
    /// does Show / SetCapture / SetTimer itself after Create succeeds.
    bool Create(const wchar_t* className, POINT pos, SIZE size,
                DWORD extraExStyle = 0) {
        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc = &PopupWindow::WndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = className;
        if (!RegisterClassExW(&wc) &&
            GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return false;
        hwnd_ = CreateWindowExW(
            WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | extraExStyle, className, L"",
            WS_POPUP, pos.x, pos.y, size.cx, size.cy, owner_, nullptr,
            GetModuleHandleW(nullptr), this);
        if (!hwnd_) return false;
        // Windows 11 requests system rounded corners (with system shadow and border as a bonus); on Windows 10 this
        // call fails and drawing falls back to square corners + owner-drawn border.
        DWORD pref = kDwmwcpRound;
        rounded_ = SUCCEEDED(DwmSetWindowAttribute(
            hwnd_, kDwmwaWindowCornerPreference, &pref, sizeof(pref)));
        if (!CreateRenderTarget(hwnd_, dpi_, theme_.text, BackgroundColor(), rt_)) {
            // ARCH-02: a failed render-target creation destroys the popup — previously the window showed as usual but
            // with completely empty content, no log and no fallback
            SetError("popup: CreateRenderTarget failed");
            DestroyWindow(hwnd_);   // WM_DESTROY clears hwnd_ and invokes OnDestroyed
            return false;
        }
        return true;
    }
    // OBS57-01 invariant: Create may be called from a derived-class constructor (all four popups do),
    // and its failure path synchronously invokes the derived-class virtual hooks via DestroyWindow→WM_DESTROY
    // OnDestroyed()/BackgroundColor() — before the derived constructor body has finished.
    // Therefore: (1) hook overrides may only touch members initialized before the constructor body
    // (member init list/NSDMI); (2) users of members initialized after Create() must tolerate
    // their uninitialized values on the Create-failure path, or switch to two-phase construction (explicit Init()).
    // Today the four popups' OnDestroyed only read early members such as captured_/hwnd_ — safe.

    void Show() { ShowWindow(hwnd_, SW_SHOWNA); }

    static Point ToDip(int px, int py, float dpi) {
        return { px * 96.0f / dpi, py * 96.0f / dpi };
    }

    /// Scroll limit and wheel step for scrollable popups (Menu/Flyout, R37-06).
    float MaxScroll() const { return std::max(0.0f, contentH_ - height_); }

    LRESULT WheelStep(float delta) {
        float maxScroll = MaxScroll();
        if (maxScroll <= 0) return 0;
        float old = scrollY_;
        scrollY_ = std::clamp(scrollY_ - delta * 0.5f, 0.0f, maxScroll);
        if (scrollY_ != old) InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    }

    /// Popup background: ToolTip uses tooltipBackground, the rest popupBackground.
    virtual Color BackgroundColor() const { return theme_.popupBackground; }

    /// WM_PAINT frame content (the owner-drawn part after the background Clear).
    virtual void OnDraw(RenderPainter& painter) = 0;

    /// Messages other than WM_PAINT/WM_DESTROY; return kUnhandled to pass on to DefWindowProc.
    virtual LRESULT OnMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
        (void)msg; (void)wParam; (void)lParam;
        return kUnhandled;
    }

    /// WM_DESTROY (hwnd_ still valid): release capture / timers and other window-bound resources.
    /// Popup window destroyed callback. OBS57-01: on the Create-failure path this fires synchronously
    /// inside the derived-class constructor — overrides may only touch members initialized before the constructor body (see the Create invariant note)
    virtual void OnDestroyed() {}

    /// Border stroke (baseline .menu/.tt border, R25-11 unified contract): when DWM rounding is active, draw along
    /// the 8px radius (inset 0.5 so the stroke's outer edge exactly meets the DWM clip circle); square fallback on failure.
    void DrawStroke(RenderPainter& painter) {
        if (rounded_)
            painter.StrokeRoundedRect({ 0.5f, 0.5f, width_ - 1, height_ - 1 },
                                      7.5f, theme_.controlStroke);
        else
            painter.StrokeRect({ 0.5f, 0.5f, width_ - 1, height_ - 1 },
                               theme_.controlStroke);
    }

    /// R37-06: when scrollable, draw the thin scroll hint bar (3px at the right edge).
    void DrawScrollHint(RenderPainter& painter) {
        float maxScroll = MaxScroll();
        if (maxScroll <= 0) return;
        float trackH = height_ - 2 * kPadV;
        float thumbH = std::max(28.0f, trackH * height_ / contentH_);
        float ty = kPadV + (scrollY_ / maxScroll) * (trackH - thumbH);
        painter.FillRoundedRect({ width_ - 4.0f, ty, 3, thumbH }, 1.5f,
                                theme_.divider);
    }

    HWND owner_ = nullptr;
    HWND hwnd_ = nullptr;
    float dpi_ = 96.0f;
    const Theme& theme_;
    detail::RenderTarget rt_;
    float width_ = 0, height_ = 0;
    float contentH_ = 0;   // R37-06: total item height (scrollable once height_ is clamped to the work area)
    float scrollY_ = 0;    // R37-06: scroll offset
    bool rounded_ = false;   // whether DWM rounding is active (decides rounded vs square stroke, R25-11)

    // Item geometry shared by Menu/Flyout (baseline .mi 34px rows, .msep 1+4×2, .menu
    // padding-top 4, icon slot 10+16+10)
    static constexpr float kItemH = 34.0f;
    static constexpr float kSepH = 9.0f;    // separator row height (baseline .msep 1 + margin 4×2)
    static constexpr float kPadV = 5.0f;    // first-row padding (baseline border 1 + .menu padding-top 4, R30-03)
    static constexpr float kGutter = 40.0f; // text starts after the icon area (10+16+10)

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                    LPARAM lParam) {
        auto* self =
            reinterpret_cast<PopupWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        switch (msg) {
        case WM_NCCREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            break;
        }
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            if (self) self->RenderFrame();
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DESTROY:
            if (self) {
                self->OnDestroyed();
                self->hwnd_ = nullptr;
            }
            break;
        default:
            if (self) {
                LRESULT r = self->OnMessage(msg, wParam, lParam);
                if (r != kUnhandled) return r;
            }
            break;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

private:
    /// Render skeleton (verbatim-identical across the former four copies): ARCH-04 rebuilds the target after device loss, clears the background
    /// then hands off to OnDraw; ARCH-03 checks EndDraw; ARCH38-01 folds Present into recovery.
    void RenderFrame() {
        if (!rt_.ctx && (!hwnd_ || !CreateRenderTarget(hwnd_, dpi_, theme_.text,
                                                       BackgroundColor(), rt_)))
            return;
        RenderPainter painter;
        painter.SetFrame(rt_.ctx.Get(), rt_.brush.Get());
        rt_.ctx->BeginDraw();
        rt_.ctx->Clear(ToD2D(BackgroundColor()));
        OnDraw(painter);
        // ARCH-03/04: check the EndDraw return value + SyncInterval=0 non-blocking present,
        // same as the main window (popup hover repaints ride the WM_MOUSEMOVE hot path and must not wait for vsync,
        // which would stall the main window's message pump); on device loss release the target, rebuilt next frame
        HRESULT hrDraw = rt_.ctx->EndDraw();
        // ARCH38-01: Present reporting device removal also enters recovery — if EndDraw succeeds but
        // Present fails, the popup would freeze on an old frame forever (main window :6003 has the same contract)
        HRESULT hrPresent = rt_.swap->Present(0, 0);
        if (hrDraw == (HRESULT)D2DERR_RECREATE_TARGET ||
            hrDraw == DXGI_ERROR_DEVICE_REMOVED || hrDraw == DXGI_ERROR_DEVICE_RESET ||
            hrPresent == DXGI_ERROR_DEVICE_REMOVED ||
            hrPresent == DXGI_ERROR_DEVICE_RESET) {
            SetError("device lost (EndDraw/Present)");   // ARCH38-04
            rt_.ctx.Reset(), rt_.swap.Reset();
        }
    }
};

class MenuPopup : public PopupWindow {
public:
    /// targetRect: the menu title's rect in window client coordinates (DIP); the popup shows directly below it.
    MenuPopup(MenuBar* bar, Menu* menu, HWND owner, float dpi,
              const Rect& targetRect, const Theme& theme)
        : PopupWindow(owner, dpi, theme), bar_(bar), menu_(menu) {
        // auto width from the widest item (icon + text + shortcut hint)
        const Font font{ .size = 14.0f };   // baseline .mi 14px
        const Font kShortcut{ .size = 12.0f };   // baseline .sc 12px
        float rowW = 0;
        for (const auto& item : menu_->items_) {
            if (item.separator) continue;
            // sum items per the baseline max-content: .menu padding 4 + .mi padding 0 10
            // + slot (icon 16+gap10=40; no icon and not checkable, indent 14, A-09) + text
            // + [.sc: gap 10 + padding-left 18 + shortcut] + right 14 (R28-07;
            // checkable items always occupy the icon slot, R28-01/R28-03 unified contract)
            float w = ((item.icon.IsEmpty() && !item.checkable) ? 14.0f : kGutter) +
                      detail::MeasureTextWidth(item.text, font) + 14.0f;
            if (!item.shortcut.empty())
                w += 28.0f + detail::MeasureTextWidth(item.shortcut, kShortcut);
            rowW = std::max(rowW, w);
        }
        // baseline border-box = max(max-content + 2, 120) (R30-03; +2 = the .menu
        // 1px left/right borders ride on the content item, so the 120 floor is a border-box measure); the 1px stroke
        // is drawn inside the window, so the size must include the border item
        width_ = std::max(rowW + 2.0f, 120.0f);
        height_ = kPadV * 2.0f;
        for (const auto& item : menu_->items_)
            height_ += item.separator ? kSepH : kItemH;
        contentH_ = height_;
        // R37-06: when taller than the work area, clamp the window height and enable wheel scrolling — a popup is a separate
        // window; anything beyond the work area is cut off with no way to scroll
        {
            MONITORINFO mi{ sizeof(mi) };
            if (GetMonitorInfoW(
                    MonitorFromWindow(owner_, MONITOR_DEFAULTTONEAREST), &mi)) {
                float workH =
                    (mi.rcWork.bottom - mi.rcWork.top) * 96.0f / dpi_;
                float maxH = std::max(kItemH, workH - 16.0f);   // 8 DIP margin top and bottom
                if (contentH_ > maxH) height_ = maxH;
            }
        }

        // Pop position: snug under the title (MenuBar contract, no gap; flip above when it does not fit and
        // clamp into the work area — same contract as Flyout, Project Conventions A.4, R28-06)
        POINT pos = ComputePopupPosition(owner_, dpi_, targetRect, width_,
                                         height_, 0.0f);
        SIZE size{
            static_cast<LONG>(std::lround(width_ * dpi_ / 96.0f)),
            static_cast<LONG>(std::lround(height_ * dpi_ / 96.0f)),
        };
        if (!Create(L"SlenderMenuPopup", pos, size)) return;
        Show();
        SetCapture(hwnd_);
        captured_ = true;
    }

    ~MenuPopup() override { Close(); }

private:

    bool InsideBounds(const Point& p) const {
        return p.x >= 0 && p.x < width_ && p.y >= 0 && p.y < height_;
    }

    /// Returns the hit item index; separators, disabled areas and outside the popup return -1.
    int ItemIndexAt(const Point& p) const {
        if (!InsideBounds(p)) return -1;
        if (p.y < kPadV || p.y >= height_ - kPadV) return -1;
        float y = p.y - kPadV + scrollY_;   // R37-06: hit-testing converts by scroll offset
        for (int i = 0; i < static_cast<int>(menu_->items_.size()); ++i) {
            float h = menu_->items_[i].separator ? kSepH : kItemH;
            if (y < h) return i;
            y -= h;
        }
        return -1;
    }

    int HoverIndexAt(const Point& p) const {
        int index = ItemIndexAt(p);
        if (index >= 0 && (menu_->items_[index].separator ||
                           menu_->items_[index].text.empty() ||
                           menu_->items_[index].disabled)) {
            return -1; // separators, empty text and disabled items are not hoverable
        }
        return index;
    }

    /// Item top y: separator rows and item rows differ in height, so accumulate row by row.
    float ItemTop(int index) const {
        float y = kPadV;
        for (int i = 0; i < index; ++i)
            y += menu_->items_[i].separator ? kSepH : kItemH;
        return y;
    }

    Rect ItemRect(int index) const {
        float h = menu_->items_[index].separator ? kSepH : kItemH;
        return { 0.0f, ItemTop(index), width_, h };
    }

    LRESULT OnMouseMove(int px, int py) {
        Point p = ToDip(px, py, dpi_);
        int index = HoverIndexAt(p);
        if (index >= 0) {
            if (hover_ != index) {
                hover_ = index;
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return 0;
        }
        // outside the popup: hovering another menu title switches menus
        int title = TitleOnBar(px, py);
        if (title >= 0 && title != bar_->open_) {
            bar_->SwitchMenu(title);
            return 0; // this has been destroyed; do not touch members again
        }
        if (hover_ != -1) {
            hover_ = -1;
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return 0;
    }

    LRESULT OnButtonDown(int px, int py) {
        Point p = ToDip(px, py, dpi_);
        int index = ItemIndexAt(p);
        if (index >= 0) {
            auto& item = menu_->items_[index];
            bool enabled = !item.separator && !item.text.empty() && !item.disabled;
            if (enabled) {
                pressed_ = index;   // baseline .mi:active pressed state; committed on release
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return 0; // blank or disabled item: do not close
        }
        pressed_ = -1;
        bar_->CloseMenu(); // click outside the popup: close
        return 0;
    }

    LRESULT OnButtonUp(int px, int py) {
        if (pressed_ < 0) return 0;
        int index = pressed_;
        pressed_ = -1;
        if (index >= static_cast<int>(menu_->items_.size()) ||
            ItemIndexAt(ToDip(px, py, dpi_)) != index) {
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;   // released after moving off the item: treat as cancel
        }
        auto& item = menu_->items_[index];
        bool enabled = !item.separator && !item.text.empty() && !item.disabled;
        if (!enabled) return 0;
        if (item.checkable) item.checked = !item.checked;
        std::function<void()> cb = item.onClick;
        bar_->CloseMenu(); // destroys this; afterwards only local copies are used
        cb();
        return 0;
    }

    /// Popup client pixel coordinates → menu-bar title index (-1 on miss).
    int TitleOnBar(int px, int py) {
        if (!owner_ || !bar_->window_) return -1;
        POINT p{ px, py };
        ClientToScreen(hwnd_, &p);
        POINT origin{ 0, 0 };
        ClientToScreen(owner_, &origin);
        const Rect& b = bar_->bounds_;
        Point local{ (p.x - origin.x) * 96.0f / dpi_ - b.x,
                     (p.y - origin.y) * 96.0f / dpi_ - b.y };
        if (local.y < 0 || local.y >= b.h) return -1;
        return bar_->TitleAt(local);
    }

    void OnDraw(RenderPainter& painter) override {
        const Font font{ .size = 14.0f };   // baseline .mi 14px
        const Font kShortcut{ .size = 12.0f };   // baseline .sc 12px
        painter.PushClip({ 0, 0, width_, height_ });   // R37-06: scroll clipping
        for (int i = 0; i < static_cast<int>(menu_->items_.size()); ++i) {
            const auto& item = menu_->items_[i];
            Rect r = ItemRect(i);
            r.y -= scrollY_;   // R37-06: scroll offset
            if (r.y + r.h <= 0 || r.y >= height_) continue;   // whole row outside the viewport: skip
            if (item.separator) {
                // short-row line center takes integer 4: ItemTop only accumulates integers (4/34/9), r.y is always an integer,
                // +4.5 half-integer would straddle two pixel rows and get diluted (R28-08)
                float y = r.y + 4.0f;
                painter.FillRect({ 10, y, width_ - 20, 1 }, theme_.divider);
                continue;
            }
            bool enabled = !item.text.empty() && !item.disabled;
            bool hovered = i == hover_ && enabled;
            // selected item (.mi.on): accent full-row fill; hover pill: inset 4 from the popup edge (baseline .menu padding 4)
            Rect pill{ 4, r.y, width_ - 8, r.h };
            if (item.selected)
                painter.FillRoundedRect(pill, kRadiusControl,
                                        hovered ? theme_.accentHover : theme_.accent);
            else if (i == pressed_ && enabled)
                painter.FillRoundedRect(pill, kRadiusControl, theme_.subtleActive);
            else if (hovered)
                painter.FillRoundedRect(pill, kRadiusControl, theme_.popupHover);
            float cy = r.y + kItemH / 2.0f;
            if (item.checkable) {
                // checkable items always occupy the icon slot: unchecked draws an empty box, checked a filled box
                // (R28-01/R28-03 unified contract, consistent with Flyout)
                Rect check{ 14, cy - 6, 12, 12 };
                if (item.checked)
                    painter.FillRoundedRect(check, 3, item.selected ? theme_.textOnAccent
                                                                    : theme_.accent);
                else painter.StrokeRect(check, item.selected ? theme_.textOnAccent
                                                             : theme_.divider);
            } else if (!item.icon.IsEmpty()) {
                // baseline .mi .i is always --text2 (no recolor on hover); .mi.dis .i becomes --text4
                // (round-17 E-02); the selected item inverts
                painter.DrawIcon(item.icon, { 14, cy - 8, 16, 16 },
                                 item.disabled ? theme_.textDisabled
                                 : item.selected ? theme_.textOnAccent
                                                 : theme_.textSecondary);
            }
            // checkable items share the 40 slot with icon items (R28-01); no icon and not checkable, indent 14 (A-09)
            float gutter = (item.icon.IsEmpty() && !item.checkable) ? 14.0f : kGutter;
            float textW = width_ - gutter - 14;
            if (!item.shortcut.empty()) {
                float sw = detail::MeasureTextWidth(item.shortcut, kShortcut);
                painter.DrawText(item.shortcut, kShortcut,
                                 { width_ - 14 - sw, r.y, sw + 2, r.h },
                                 item.selected ? theme_.textOnAccent : theme_.textTertiary,
                                 HAlign::Left, VAlign::Center);
                textW -= sw + 18.0f;   // baseline .sc padding-left 18
            }
            painter.DrawText(item.text, font,
                             { gutter, r.y, std::max(0.0f, textW), r.h },
                             !enabled ? theme_.textDisabled
                             : item.selected ? theme_.textOnAccent : theme_.text,
                             HAlign::Left, VAlign::Center);
        }
        painter.PopClip();   // R37-06
        DrawScrollHint(painter);   // R37-06: 3px scroll hint at the right edge when scrollable
        // Border stroke (baseline .menu{border:1px solid var(--stroke)}, R25-11 unified contract):
        // when DWM rounding is active draw along the 8px radius (inset 0.5 so the stroke's outer edge meets the DWM clip circle),
        // square fallback on failure — previously nothing was drawn when rounding was active, leaving menus without a border
        DrawStroke(painter);
    }

    LRESULT OnMessage(UINT msg, WPARAM wParam, LPARAM lParam) override {
        switch (msg) {
        case WM_MOUSEMOVE:
            return OnMouseMove(static_cast<short>(LOWORD(lParam)),
                               static_cast<short>(HIWORD(lParam)));
        case WM_LBUTTONDOWN:
            return OnButtonDown(static_cast<short>(LOWORD(lParam)),
                                static_cast<short>(HIWORD(lParam)));
        case WM_LBUTTONUP:
            return OnButtonUp(static_cast<short>(LOWORD(lParam)),
                              static_cast<short>(HIWORD(lParam)));
        case WM_MOUSEWHEEL:
            // R37-06: wheel-scroll items once the menu height is clamped to the work area
            return WheelStep(static_cast<float>(static_cast<short>(HIWORD(wParam))));
        }
        return kUnhandled;
    }

    void OnDestroyed() override {
        if (captured_) ReleaseCapture();
    }

    MenuBar* bar_ = nullptr;
    Menu* menu_ = nullptr;
    int hover_ = -1;
    int pressed_ = -1;   // pressed item (.mi:active)
    bool captured_ = false;
};

// ---------------------------------------------------------------------------
// ToastPopup: bottom-right toast card (framework-internal, no interaction, auto-dismisses)
// ---------------------------------------------------------------------------

class ToastPopup : public PopupWindow {
public:
    ToastPopup(HWND owner, float dpi, const Theme& theme,
               std::wstring_view title, std::wstring_view subtitle)
        : PopupWindow(owner, dpi, theme) {
        title_.assign(title.begin(), title.end());
        subtitle_.assign(subtitle.begin(), subtitle.end());
        const Font kTitleFont{ .size = 12.5f };
        const Font kSubFont{ .size = 11.0f };
        float textW = detail::MeasureTextWidth(title_, kTitleFont);
        titleH_ = detail::MeasureTextMetrics(title_, kTitleFont).height;
        float subH = 0;
        if (!subtitle_.empty()) {
            textW = std::max(textW, detail::MeasureTextWidth(subtitle_, kSubFont));
            subH = 3.0f + detail::MeasureTextMetrics(subtitle_, kSubFont).height;
        }
        width_ = 10 + 16 + 10 + textW + 16 + 10;
        height_ = std::max(38.0f, titleH_ + subH + 20);

        // near the owner window's bottom-right corner (20/48 DIP from the edges)
        RECT rc;
        GetClientRect(owner_, &rc);
        POINT origin{ 0, 0 };
        ClientToScreen(owner_, &origin);
        float cw = static_cast<float>(rc.right) * 96.0f / dpi_;
        float ch = static_cast<float>(rc.bottom) * 96.0f / dpi_;
        POINT pos{
            origin.x + static_cast<LONG>(std::lround((cw - width_ - 20.0f) * dpi_ / 96.0f)),
            origin.y + static_cast<LONG>(std::lround((ch - height_ - 48.0f) * dpi_ / 96.0f)),
        };
        SIZE size{
            static_cast<LONG>(std::lround(width_ * dpi_ / 96.0f)),
            static_cast<LONG>(std::lround(height_ * dpi_ / 96.0f)),
        };
        // OBS46-02: when the toast is wider than the owner's client area (or the owner sits at a screen edge), anchoring its bottom-right
        // point would push the left/top edge off-screen; clamp to the current monitor's work area so it stays fully visible.
        // Does not share the "work area ∩ client rect" contract with ComputePopupPosition — the toast hugging
        // the owner's bottom-right is its own semantics; when wider than the owner, the work area is the last visibility bound.
        MONITORINFO mi{ sizeof(mi) };
        if (GetMonitorInfoW(MonitorFromWindow(owner_, MONITOR_DEFAULTTONEAREST),
                            &mi)) {
            pos.x = std::clamp(pos.x, mi.rcWork.left,
                               std::max(mi.rcWork.left, mi.rcWork.right - size.cx));
            pos.y = std::clamp(pos.y, mi.rcWork.top,
                               std::max(mi.rcWork.top, mi.rcWork.bottom - size.cy));
        }
        if (!Create(L"SlenderToast", pos, size, WS_EX_TOPMOST)) return;
        SetTimer(hwnd_, kTimerId, 2600, nullptr);
        Show();
    }

    ~ToastPopup() override { Close(); }

private:
    static constexpr UINT kTimerId = 1;

    void OnDraw(RenderPainter& painter) override {
        static const Icon kCheck = Icon::FromSvgPath(
            "M12 2a10 10 0 1 1 0 20 10 10 0 0 1 0-20Zm0 1.5a8.5 8.5 0 1 0 0 17 "
            "8.5 8.5 0 0 0 0-17ZM10.55 14.45 8.1 12l-1.2 1.2 3.65 3.65 7.55-7.55-1.2-1.2Z");
        painter.DrawIcon(kCheck, { 10, (height_ - 16.0f) / 2.0f, 16, 16 }, theme_.accent);
        painter.DrawText(title_, Font{ .size = 12.5f },
                         { 36, 10, std::max(0.0f, width_ - 46), height_ - 20 },
                         theme_.text, HAlign::Left, VAlign::Top);
        if (!subtitle_.empty())
            painter.DrawText(subtitle_, Font{ .size = 11.0f },
                             { 36, 10 + titleH_ + 3, std::max(0.0f, width_ - 46),
                               std::max(0.0f, height_ - 23 - titleH_) },
                             theme_.textDisabled, HAlign::Left, VAlign::Top);
    }

    LRESULT OnMessage(UINT msg, WPARAM wParam, LPARAM lParam) override {
        (void)lParam;
        if (msg == WM_NCHITTEST) return HTTRANSPARENT;   // display-only: mouse pass-through
        if (msg == WM_TIMER && wParam == kTimerId) {
            Close();
            return 0;
        }
        return kUnhandled;
    }

    void OnDestroyed() override {
        KillTimer(hwnd_, kTimerId);
    }

    std::wstring title_, subtitle_;
    float titleH_ = 0;
};

// ---------------------------------------------------------------------------
// Flyout: generic dropdown popup (ComboBox / SplitButton / AutoSuggest / context menus)
// ---------------------------------------------------------------------------

class Flyout : public PopupWindow {
public:
    struct Item {
        bool separator = false;
        bool disabled = false;
        bool checkable = false;   // checkable item: always occupies the icon slot (empty box when unchecked, R28-03)
        bool checked = false;
        bool accent = false;   // selected item: accent fill + inverted text
        Icon icon;
        std::wstring text;
        std::wstring shortcut;
        std::function<void()> onClick;
    };

    /// anchor is an anchor rect within the owner window's client coordinates (DIP); shown below it by default
    /// (above when it does not fit). Width is at least minWidth and adapts to content.
    /// owner is the creating widget (nullable), so the trigger can query popup ownership and aliveness.
    Flyout(HWND owner, float dpi, const Theme& theme, const Rect& anchor,
           std::vector<Item> items, float minWidth = 120.0f,
           const void* ownerWidget = nullptr, float dropBelow = 5.0f)
        : PopupWindow(owner, dpi, theme), items_(std::move(items)),
          ownerWidget_(ownerWidget), dropBelow_(dropBelow) {
        ComputeSize(minWidth);
        CreateAt(anchor);
        if (!hwnd_) return;
        Show();
        SetCapture(hwnd_);
        captured_ = true;
    }

    ~Flyout() override { Close(); }

    /// Replace items at runtime (AutoSuggest filtering): recompute height and resize the window.
    void SetItems(std::vector<Item> items, float minWidth) {
        items_ = std::move(items);
        ComputeSize(minWidth);
        scrollY_ = 0;   // R37-06: reset scrolling after the item set changes
        if (hwnd_) {
            SIZE size{ static_cast<LONG>(std::lround(width_ * dpi_ / 96.0f)),
                       static_cast<LONG>(std::lround(height_ * dpi_ / 96.0f)) };
            SetWindowPos(hwnd_, nullptr, 0, 0, size.cx, size.cy,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }

    /// The creating widget (nullptr if unrecorded), so the trigger can query popup ownership and aliveness.
    const void* OwnerWidget() const { return ownerWidget_; }

private:
    friend struct WindowImpl;

    void ComputeSize(float minWidth) {
        const Font font{ .size = 14.0f };   // baseline .mi 14px
        const Font kShortcut{ .size = 12.0f };
        float rowW = 0;
        for (const auto& item : items_) {
            if (item.separator) continue;
            // sum items per the baseline max-content (same formula as MenuPopup, R28-07):
            // .menu padding 4 + .mi padding 0 10 + slot (icon 40 / indent 14,
            // A-09; checkable items always occupy the icon slot, R28-03) + text + right 14
            // + [.sc: gap 10 + padding-left 18 + shortcut]
            float w = ((item.icon.IsEmpty() && !item.checkable) ? 14.0f : kGutter) +
                      detail::MeasureTextWidth(item.text, font) + 14.0f;
            if (!item.shortcut.empty())
                w += 28.0f + detail::MeasureTextWidth(item.shortcut, kShortcut);
            rowW = std::max(rowW, w);
        }
        // baseline border-box = max(max-content + 2, minWidth, 120): +2 = the .menu
        // 1px left/right borders (R30-03) ride on the content item; min-width itself is a border-box measure
        // (box-sizing:border-box), so no extra border item is added
        width_ = std::max({ rowW + 2.0f, minWidth, 120.0f });
        height_ = kPadV * 2.0f;
        for (const auto& item : items_)
            height_ += item.separator ? kSepH : kItemH;
        contentH_ = height_;
        // R37-06: when taller than the work area, clamp the height and enable wheel scrolling (same contract as MenuPopup)
        if (owner_) {
            MONITORINFO mi{ sizeof(mi) };
            if (GetMonitorInfoW(
                    MonitorFromWindow(owner_, MONITOR_DEFAULTTONEAREST), &mi)) {
                float workH =
                    (mi.rcWork.bottom - mi.rcWork.top) * 96.0f / dpi_;
                float maxH = std::max(kItemH, workH - 16.0f);
                if (contentH_ > maxH) height_ = maxH;
            }
        }
    }

    void CreateAt(const Rect& anchor) {
        POINT finalPos = ComputePopupPosition(owner_, dpi_, anchor, width_,
                                              height_, dropBelow_);
        SIZE size{ static_cast<LONG>(std::lround(width_ * dpi_ / 96.0f)),
                   static_cast<LONG>(std::lround(height_ * dpi_ / 96.0f)) };
        Create(L"SlenderFlyout", finalPos, size);
    }

    float ItemTop(int index) const {
        float y = kPadV;
        for (int i = 0; i < index; ++i)
            y += items_[i].separator ? kSepH : kItemH;
        return y;
    }

    Rect ItemRect(int index) const {
        float h = items_[index].separator ? kSepH : kItemH;
        return { 0.0f, ItemTop(index), width_, h };
    }

    int ItemIndexAt(const Point& p) const {
        if (p.x < 0 || p.x >= width_ || p.y < kPadV || p.y >= height_ - kPadV)
            return -1;
        float y = p.y - kPadV + scrollY_;   // R37-06: hit-testing converts by scroll offset
        for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
            float h = items_[i].separator ? kSepH : kItemH;
            if (y < h) return i;
            y -= h;
        }
        return -1;
    }

    /// Keyboard navigation scrolls the highlighted item into view (R37-06).
    void EnsureIndexVisible(int index) {
        float maxScroll = MaxScroll();
        if (maxScroll <= 0) return;
        float top = ItemTop(index);
        float bottom = top + (items_[index].separator ? kSepH : kItemH);
        if (top - scrollY_ < kPadV)
            scrollY_ = std::max(0.0f, top - kPadV);
        else if (bottom - scrollY_ > height_ - kPadV)
            scrollY_ = std::min(maxScroll, bottom - height_ + kPadV);
    }

    int HoverIndexAt(const Point& p) const {
        int index = ItemIndexAt(p);
        if (index >= 0 && (items_[index].separator || items_[index].disabled))
            return -1;
        return index;
    }

    LRESULT OnMouseMove(int px, int py) {
        Point p = ToDip(px, py, dpi_);
        int index = HoverIndexAt(p);
        if (index != hover_) {
            hover_ = index;
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return 0;
    }

    LRESULT OnButtonDown(int px, int py) {
        Point p = ToDip(px, py, dpi_);
        int index = ItemIndexAt(p);
        if (index >= 0 && !items_[index].separator && !items_[index].disabled) {
            pressed_ = index;   // baseline .mi:active pressed state; committed on release
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        pressed_ = -1;
        if (index < 0) Close();   // click outside the popup: close
        return 0;
    }

    LRESULT OnButtonUp(int px, int py) {
        if (pressed_ < 0) return 0;
        int index = pressed_;
        pressed_ = -1;
        if (index >= static_cast<int>(items_.size()) ||
            ItemIndexAt(ToDip(px, py, dpi_)) != index) {
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;   // released after moving off the item: treat as cancel
        }
        if (items_[index].separator || items_[index].disabled) return 0;
        std::function<void()> cb = items_[index].onClick;
        Close();   // destroys this; afterwards only local copies are used
        if (cb) cb();
        return 0;
    }

    void MoveHighlight(int delta) {
        if (items_.empty()) return;
        int n = static_cast<int>(items_.size());
        int i = hover_ < 0 ? (delta > 0 ? -1 : n) : hover_;
        for (int step = 0; step < n; ++step) {
            i += delta;
            if (i < 0) i = n - 1;
            if (i >= n) i = 0;
            if (!items_[i].separator && !items_[i].disabled) {
                hover_ = i;
                EnsureIndexVisible(i);   // R37-06: scroll the highlight into view
                InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
        }
    }

    /// Arrow keys/Tab move the highlight; returns whether it was handled.
    bool KeyNav(uint32_t vk) {
        switch (vk) {
        case VK_DOWN: MoveHighlight(1); return true;
        case VK_UP:   MoveHighlight(-1); return true;
        case VK_TAB:  MoveHighlight(GetKeyState(VK_SHIFT) & 0x8000 ? -1 : 1); return true;
        case VK_HOME:
            if (!items_.empty()) { hover_ = -1; MoveHighlight(1); }
            return true;
        case VK_END:
            if (!items_.empty()) { hover_ = -1; MoveHighlight(-1); }
            return true;
        case VK_RETURN: case VK_SPACE: return InvokeHighlighted();
        default: return false;
        }
    }

    /// Enter: trigger the current highlighted item and close the popup.
    bool InvokeHighlighted() {
        if (hover_ < 0 || hover_ >= static_cast<int>(items_.size())) return false;
        Item& it = items_[hover_];
        if (it.separator || it.disabled) return false;
        std::function<void()> cb = it.onClick;
        Close();   // destroys this; afterwards only local copies are used
        if (cb) cb();
        return true;
    }

    void OnDraw(RenderPainter& painter) override {
        const Font font{ .size = 14.0f };   // baseline .mi 14px
        const Font kShortcut{ .size = 12.0f };
        painter.PushClip({ 0, 0, width_, height_ });   // R37-06: scroll clipping
        for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
            const Item& item = items_[i];
            Rect r = ItemRect(i);
            r.y -= scrollY_;   // R37-06: scroll offset
            if (r.y + r.h <= 0 || r.y >= height_) continue;   // whole row outside the viewport: skip
            if (item.separator) {
                // line center takes integer 4 (r.y is always an integer; 4.5 half-integer would straddle two pixel rows and get diluted, R28-08)
                float y = r.y + 4.0f;
                painter.FillRect({ 10, y, width_ - 20, 1 }, theme_.divider);
                continue;
            }
            bool hovered = i == hover_;
            // selected item: accent full-row fill; hover pill: inset 4 from the popup edge (baseline .menu padding 4)
            if (item.accent) {
                painter.FillRoundedRect({ 4, r.y, width_ - 8, r.h }, 4,
                                        hovered ? theme_.accentHover
                                                : theme_.accent);
            } else if (i == pressed_ && !item.disabled) {
                painter.FillRoundedRect({ 4, r.y, width_ - 8, r.h }, 4,
                                        theme_.subtleActive);
            } else if (hovered) {
                painter.FillRoundedRect({ 4, r.y, width_ - 8, r.h }, 4,
                                        theme_.popupHover);
            }
            float cy = r.y + kItemH / 2.0f;
            Color fg = item.accent ? theme_.textOnAccent
                     : item.disabled ? theme_.textDisabled
                                     : theme_.text;
            if (item.checkable) {
                // checkable items always occupy the icon slot: empty box when unchecked, filled box when checked, colors
                // follow the selected item's inversion (R28-03: previously unchecked drew nothing and the text start jumped)
                Rect check{ 14, cy - 6, 12, 12 };
                if (item.checked)
                    painter.FillRoundedRect(check, 3, item.accent ? theme_.textOnAccent
                                                                  : theme_.accent);
                else painter.StrokeRect(check, item.accent ? theme_.textOnAccent
                                                           : theme_.divider);
            } else if (!item.icon.IsEmpty()) {
                // baseline .mi.dis .i becomes --text4, same priority as the text-disabled branch (E-02)
                painter.DrawIcon(item.icon, { 14, cy - 8, 16, 16 },
                                 item.disabled ? theme_.textDisabled
                                 : item.accent ? theme_.textOnAccent
                                               : theme_.textSecondary);
            }
            // checkable items share the 40 slot with icon items (R28-03); no icon and not checkable, indent 14 (A-09)
            float gutter = (item.icon.IsEmpty() && !item.checkable) ? 14.0f : kGutter;
            float textW = width_ - gutter - 14;
            if (!item.shortcut.empty()) {
                float sw = detail::MeasureTextWidth(item.shortcut, kShortcut);
                painter.DrawText(item.shortcut, kShortcut,
                                 { width_ - 14 - sw, r.y, sw + 2, r.h },
                                 item.accent ? theme_.textOnAccent
                                             : theme_.textTertiary,
                                 HAlign::Left, VAlign::Center);
                textW -= sw + 18.0f;   // baseline .sc padding-left 18
            }
            painter.DrawText(item.text, font,
                             { gutter, r.y, std::max(0.0f, textW), r.h },
                             fg, HAlign::Left, VAlign::Center);
        }
        painter.PopClip();   // R37-06
        DrawScrollHint(painter);   // R37-06: 3px scroll hint at the right edge when scrollable
        // Border stroke, same contract as MenuPopup (baseline .menu border, R25-11): when DWM rounding is
        // active draw along the 8px radius, square fallback on failure
        DrawStroke(painter);
    }

    LRESULT OnMessage(UINT msg, WPARAM wParam, LPARAM lParam) override {
        switch (msg) {
        case WM_MOUSEMOVE:
            return OnMouseMove(static_cast<short>(LOWORD(lParam)),
                               static_cast<short>(HIWORD(lParam)));
        case WM_LBUTTONDOWN:
            return OnButtonDown(static_cast<short>(LOWORD(lParam)),
                                static_cast<short>(HIWORD(lParam)));
        case WM_LBUTTONUP:
            return OnButtonUp(static_cast<short>(LOWORD(lParam)),
                              static_cast<short>(HIWORD(lParam)));
        case WM_MOUSEWHEEL:
            // R37-06: wheel-scroll items once item height is clamped to the work area (AutoSuggest long lists etc.)
            return WheelStep(static_cast<float>(static_cast<short>(HIWORD(wParam))));
        }
        return kUnhandled;
    }

    void OnDestroyed() override {
        if (captured_) ReleaseCapture();
    }

    std::vector<Item> items_;
    const void* ownerWidget_ = nullptr;
    float dropBelow_ = 5.0f;   // gap below the anchor: 6 for button anchors, 5 otherwise (R28-09)
    int hover_ = -1;
    int pressed_ = -1;   // pressed item (.mi:active)
    bool captured_ = false;
};

} // namespace detail

// ---------------------------------------------------------------------------
// Window implementation (internal)
// ---------------------------------------------------------------------------

namespace detail {

constexpr UINT kCaretTimerId = 1;   // focused-widget caret blink timer
constexpr UINT kAnimTimerId = 2;    // animation frame timer (~30fps)
constexpr UINT kTipTimerId = 3;     // tooltip delay timer
constexpr UINT kTickTimerId = 4;    // app tick callback (demo value changes etc.)
constexpr UINT kCapTimerId = 5;     // title-bar hover safety-check timer (R32-02)

// ---------------------------------------------------------------------------

class ToolTipPopup : public PopupWindow {
public:
    /// widgetRect: the target widget's rect in window client coordinates (DIP).
    ToolTipPopup(HWND owner, float dpi, const Theme& theme,
                 const Rect& widgetRect, std::wstring_view text)
        : PopupWindow(owner, dpi, theme) {
        text_.assign(text.begin(), text.end());

        const Font font{ .size = 12.0f };
        float textW = detail::MeasureTextWidth(text_, font);
        width_ = textW + 22.0f;   // baseline padding 5px 10px + border 1px×2 (B-16)
        height_ = 28.8f;   // baseline 12px×1.4 line box 16.8 + padding 5×2 + border 2 = 28.8 (C-13)

        // shown 8px above the widget (aligned with the baseline); flipped below when it would leave the screen upward.
        const Rect& b = widgetRect;
        POINT origin{ 0, 0 };
        ClientToScreen(owner_, &origin);
        float originX = origin.x * 96.0f / dpi_;
        float originY = origin.y * 96.0f / dpi_;
        RECT wrc;
        MONITORINFO mi{ sizeof(mi) };
        GetMonitorInfoW(MonitorFromWindow(owner_, MONITOR_DEFAULTTONEAREST), &mi);
        wrc = mi.rcWork;
        float workR = wrc.right * 96.0f / dpi_;
        float workB = wrc.bottom * 96.0f / dpi_;
        float workT = wrc.top * 96.0f / dpi_;
        float cx = originX + b.x + b.w * 0.5f;
        float x = cx - width_ * 0.5f;
        x = std::clamp(x, 2.0f, std::max(2.0f, workR - width_ - 2.0f));
        float y = originY + b.y - height_ - 8.0f;
        if (y < workT + 2.0f) y = originY + b.y + b.h + 8.0f;
        if (y + height_ > workB) y = std::max(2.0f, workT + 2.0f);

        POINT pos{ static_cast<LONG>(std::lround(x * dpi_ / 96.0f)),
                   static_cast<LONG>(std::lround(y * dpi_ / 96.0f)) };
        SIZE size{ static_cast<LONG>(std::lround(width_ * dpi_ / 96.0f)),
                   static_cast<LONG>(std::lround(height_ * dpi_ / 96.0f)) };
        if (!Create(L"SlenderToolTip", pos, size, WS_EX_TOPMOST)) return;
        Show();
    }

    ~ToolTipPopup() override { Close(); }

protected:
    Color BackgroundColor() const override { return theme_.tooltipBackground; }

private:
    void OnDraw(RenderPainter& painter) override {
        painter.DrawText(text_, Font{ .size = 12.0f },
                         { 11, 0, std::max(0.0f, width_ - 22), height_ },
                         theme_.text, HAlign::Left, VAlign::Center);
        // Border stroke, same contract as MenuPopup/Flyout (baseline .tt border, R25-11): along
        // the DWM 8px rounding — previously the square stroke got clipped by the rounding, leaving notched corners
        DrawStroke(painter);
    }

    LRESULT OnMessage(UINT msg, WPARAM wParam, LPARAM lParam) override {
        (void)wParam; (void)lParam;
        if (msg == WM_NCHITTEST) return HTTRANSPARENT;   // mouse pass-through
        return kUnhandled;
    }

    std::wstring text_;
};

/// Whether focus came from the keyboard (set on WM_KEYDOWN, cleared on mouse-down): only keyboard navigation draws the focus ring.
inline bool g_focusFromKeyboard = false;

/// Bakes a "gradient rounded square + title first letter" badge into an HICON of the given pixel size (R34-02).
/// Same source as TitleBar::OnPaint's owner-drawn badge: 20 DIP box / radius 6 / 12 DIP bold letter.
/// The D2D DC render target's premultiplied-α output is exactly the 32bpp icon format; no COM/WIC anywhere.
/// letter takes the full code point (1–2 UTF-16 code units, R36-05b).
inline HICON MakeBadgeIcon(std::wstring_view letter, int px, const Theme& theme) {
    if (px <= 0 || !g_d2d || !g_dwrite) return nullptr;
    ComPtr<ID2D1DCRenderTarget> rt;
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    if (FAILED(g_d2d->CreateDCRenderTarget(&props, rt.GetAddressOf()))) return nullptr;
    rt->SetDpi(96.0f, 96.0f);   // draw in raw pixel space, no system DPI scaling

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = px;
    bi.bmiHeader.biHeight = -px;    // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib || !bits) {
        if (dib) DeleteObject(dib);
        ReleaseDC(nullptr, screen);
        return nullptr;
    }
    HDC mem = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    HBITMAP old = static_cast<HBITMAP>(SelectObject(mem, dib));
    RECT rc{ 0, 0, px, px };
    HRESULT hr = rt->BindDC(mem, &rc);
    if (SUCCEEDED(hr)) {
        rt->BeginDraw();
        float f = static_cast<float>(px);
        D2D1_GRADIENT_STOP stops[2]{
            { 0.0f, ToD2D(theme.accentHover) }, { 1.0f, ToD2D(theme.accent) } };
        ComPtr<ID2D1GradientStopCollection> col;
        ComPtr<ID2D1LinearGradientBrush> brush;
        if (SUCCEEDED(rt->CreateGradientStopCollection(
                stops, 2, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP,
                col.GetAddressOf())) &&
            SUCCEEDED(rt->CreateLinearGradientBrush(
                D2D1::LinearGradientBrushProperties(
                    D2D1::Point2F(0, 0), D2D1::Point2F(0, f)),
                col.Get(), brush.GetAddressOf()))) {
            float radius = f * 6.0f / 20.0f;
            rt->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(0, 0, f, f), radius, radius),
                brush.Get());
            // Title first letter (first available family of the badge's font stack + bold). The family name must go through
            // EffectiveFontFamily/ResolveFontFamily: since round-43 Font{}.family
            // is always empty; reading it directly hands an empty family to DWrite's layout-time fallback — the icon
            // still gets glyphs but in a different font, no longer matching the owner-drawn badge (PERF44-01, round-44
            // A/B measured 3.3% pixel delta at 32px). Library reads of family must not bypass this resolution.
            std::wstring family =
                ResolveFontFamily(EffectiveFontFamily(std::wstring{}));
            size_t semi = family.find(L';');
            if (semi != std::wstring::npos) family.resize(semi);
            ComPtr<IDWriteTextFormat> format;
            ComPtr<IDWriteTextLayout> layout;
            // ARCH39-06: when g_dwrite has been Reset (out-of-contract call after Shutdown), skip the glyph
            if (g_dwrite && SUCCEEDED(g_dwrite->CreateTextFormat(
                    family.c_str(), nullptr, DWRITE_FONT_WEIGHT_BOLD,
                    DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                    f * 12.0f / 20.0f, L"en-us", format.GetAddressOf())) &&
                SUCCEEDED(g_dwrite->CreateTextLayout(
                    letter.data(), static_cast<UINT32>(letter.size()),
                    format.Get(), f, f, layout.GetAddressOf()))) {
                DWRITE_TEXT_METRICS tm{};
                layout->GetMetrics(&tm);
                ComPtr<ID2D1SolidColorBrush> ink;
                if (SUCCEEDED(rt->CreateSolidColorBrush(ToD2D(theme.textOnAccent),
                                                        ink.GetAddressOf())))
                    rt->DrawTextLayout(
                        D2D1::Point2F((f - tm.width) * 0.5f, (f - tm.height) * 0.5f),
                        layout.Get(), ink.Get());
            }
        }
        // R35-01: the EndDraw return value must be consumed — any drawing error inside a D2D frame ⇒ drop the whole frame;
        // without checking this HRESULT an all-zero DIB still gets baked into a fully transparent icon by CreateIconIndirect
        hr = rt->EndDraw();
    }
    SelectObject(mem, old);
    DeleteDC(mem);
    if (FAILED(hr)) { DeleteObject(dib); return nullptr; }

    // 32bpp α icons still need a monochrome mask (all 0 = whole rows opaque; the α channel takes over)
    std::vector<BYTE> zeros(static_cast<size_t>((px + 7) / 8) * px, 0);
    HBITMAP mask = CreateBitmap(px, px, 1, 1, zeros.data());
    if (!mask) { DeleteObject(dib); return nullptr; }
    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmMask = mask;
    ii.hbmColor = dib;
    HICON icon = CreateIconIndirect(&ii);   // the system copies the bitmaps; the originals remain ours to release
    DeleteObject(dib);
    DeleteObject(mask);
    return icon;
}

    // ======================================================================
    // ARCH58-01 stage 2: class-level split of WindowImpl by concern (CRTP ops bases).
    // Each base holds one concern's members and methods and reaches the core state through the impl() back-reference
    // (owner/hwnd/dpi/theme/widgets/...). WindowImpl publicly inherits all bases:
    // externally (Window/Widget/static handlers) member and method names are unchanged, call sites untouched;
    // inside base methods only core-state references are rewritten as impl()->x (the compiler is the missed-edit checker),
    // everything else is preserved verbatim (M-111 discipline).
    // ======================================================================

/// Modal content dialog (ARCH58-01 stage 2 split: state + ShowDialog/CloseDialog/DialogButtonRect/RenderDialog).
template <class W>
struct WindowDialogOps {
protected:
    W* impl() { return static_cast<W*>(this); }
public:

    bool dialogActive = false;
    Rect dialogRect{};
    int dialogHover = -1;             // dialog button hover (0=primary 1=secondary)
    int dialogPressed = -1;           // dialog button pressed (0=primary 1=secondary; committed on release)
    int dialogFocus = -1;             // dialog keyboard focus (GAP38-01; 0=primary 1=secondary)
    std::wstring dlgTitle, dlgBody, dlgPrimary, dlgSecondary;
    std::function<void()> dlgOnPrimary, dlgOnSecondary;

    void ShowDialog(const ContentDialogDesc& desc) {
        impl()->CloseMenus();
        dlgTitle = desc.title;
        dlgBody = desc.body;
        dlgPrimary = desc.primaryText;
        dlgSecondary = desc.secondaryText;
        dlgOnPrimary = desc.onPrimary;
        dlgOnSecondary = desc.onSecondary;
        dialogHover = -1;
        dialogPressed = -1;
        dialogFocus = desc.primaryText.empty() ? 1 : 0;   // GAP38-01: initial keyboard focus on the primary button

        // baseline .dlg width 440 + max-width calc(100vw - 48px) (M-02)
        const float kWidth = std::max(200.0f, std::min(440.0f, impl()->clientW - 48.0f));
        const Font kTitleFont{ .size = 20.0f, .weight = FontWeight::SemiBold };
        const Font kBodyFont{ .size = 13.5f, .lineHeight = 1.6f };   // baseline .dlg-b
        float titleH = dlgTitle.empty()
            ? 0.0f : detail::MeasureTextMetrics(dlgTitle, kTitleFont).height;
        float bodyH = dlgBody.empty()
            ? 0.0f : detail::MeasureTextBlockHeight(dlgBody, kBodyFont,
                                                    kWidth - 48.0f);
        // .dlg-h padding 22px 24px 0 and .dlg-b padding 12px 24px 24px apply unconditionally in the
        // baseline (padding even for empty title/body, R28-05); no conditionalizing here
        // footer 49 = .dlg-f border-top 1px + button row 48 (R30-02)
        float h = 22 + titleH + 12 + bodyH + 24 + 49.0f;
        dialogRect = { std::round((impl()->clientW - kWidth) * 0.5f),
                       std::round((impl()->clientH - h) * 0.5f), kWidth, h };
        dialogActive = true;
        impl()->Invalidate();
    }

    void CloseDialog(bool primary) {
        if (!dialogActive) return;
        dialogActive = false;
        dialogHover = -1;
        dialogPressed = -1;
        dialogFocus = -1;
        auto cb = primary ? dlgOnPrimary : dlgOnSecondary;
        dlgOnPrimary = nullptr;
        dlgOnSecondary = nullptr;
        impl()->Invalidate();
        if (cb) cb();
    }

    /// Dialog bottom button rects (window coordinates): 0=primary 1=secondary.
    /// Baseline .dlg-f button{flex:1} (HTML:317): allocated by the number of non-empty buttons — a lone button spans
    /// the full width (GAP53-01), two buttons split evenly; the vertical divider comes from the button+button adjacent selector
    /// ⇒ only with two buttons (render side checks separately). A missing button returns an empty rect (unhittable)
    Rect DialogButtonRect(int index) const {
        const bool hasP = !dlgPrimary.empty(), hasS = !dlgSecondary.empty();
        const int n = (hasP ? 1 : 0) + (hasS ? 1 : 0);
        float y = dialogRect.Bottom() - 48.0f;
        if (n < 2) {
            if (n == 0 || (hasP ? index != 0 : index != 1)) return {};
            return { dialogRect.x, y, dialogRect.w, 48.0f };
        }
        float half = dialogRect.w / 2.0f;
        return index == 0 ? Rect{ dialogRect.x, y, half, 48.0f }
                          : Rect{ dialogRect.x + half, y, half, 48.0f };
    }

    void RenderDialog() {
        // scrim
        impl()->painter.FillRect(Rect{ 0, 0, impl()->clientW, impl()->clientH },
                         Color{ 0.0f, 0.0f, 0.0f, 0.3f });
        // dialog body: solid fill + two-layer large shadow (baseline --sh-dlg)
        impl()->painter.DrawShadow(dialogRect, kRadiusPanel, kShadowDialog.offsetY,
                           kShadowDialog.blur, Color::Rgb(0x000000, ShadowAlpha(kShadowDialog, impl()->theme)));
        impl()->painter.DrawShadow(dialogRect, kRadiusPanel, kShadowDialogNear.offsetY,
                           kShadowDialogNear.blur, Color::Rgb(0x000000, ShadowAlpha(kShadowDialogNear, impl()->theme)));
        impl()->painter.FillRoundedRect(dialogRect, kRadiusPanel, impl()->theme.popupBackground);
        const Font kTitleFont{ .size = 20.0f, .weight = FontWeight::SemiBold };
        const Font kBodyFont{ .size = 13.5f, .lineHeight = 1.6f };   // baseline .dlg-b
        float y = dialogRect.y + 22;
        if (!dlgTitle.empty()) {
            // title box height shares the same source as ShowDialog layout (measured titleH, not the literal 28 —
            // the two sources once made the dialog 6.4 DIP short, round-26 C-6)
            float titleH = detail::MeasureTextMetrics(dlgTitle, kTitleFont).height;
            impl()->painter.DrawText(dlgTitle, kTitleFont,
                             { dialogRect.x + 24, y, dialogRect.w - 48, titleH },
                             impl()->theme.text, HAlign::Left, VAlign::Top);
            y += titleH + 12;
        }
        y += dlgTitle.empty() ? 12.0f : 0.0f;   // .dlg-b padding-top 12 unconditional (R28-05)
        if (!dlgBody.empty()) {
            // body box height shares the same source as ShowDialog layout (measured bodyH, not the literal 200 —
            // the two sources once clipped long bodies, R28-04; same as the round-26 C-6 title box)
            float bodyH = detail::MeasureTextBlockHeight(dlgBody, kBodyFont,
                                                         dialogRect.w - 48.0f);
            impl()->painter.DrawText(dlgBody, kBodyFont,
                             { dialogRect.x + 24, y, dialogRect.w - 48, bodyH },
                             impl()->theme.textSecondary, HAlign::Left, VAlign::Top, true);
        }
        // Bottom full-bleed buttons: divider + primary button accent fill. The button area is clipped by the panel-radius
        // geometry mask — the baseline .dlg{overflow:hidden} trims the full-bleed buttons into 8px arcs at both ends
        // (previously a square bottom-left corner, round-17 E-01); pressed colors, and commit only on release still inside the button
        // (baseline click semantics, round-17 U-02, see WM_LBUTTONUP)
        // The divider is the .dlg-f border-top, a separate row drawn above the button row (baseline
        // full-bleed 1px solid; it once landed in the button row's first line and got covered on the left by the primary fill, R30-01)
        const float fy = dialogRect.Bottom() - 49.0f;
        const float by = dialogRect.Bottom() - 48.0f;
        impl()->painter.PushClipRounded(dialogRect, kRadiusPanel);
        impl()->painter.FillRect({ dialogRect.x, fy, dialogRect.w, 1 }, impl()->theme.divider);
        for (int i = 0; i < 2; ++i) {
            Rect r = DialogButtonRect(i);
            const std::wstring& text = i == 0 ? dlgPrimary : dlgSecondary;
            if (text.empty()) continue;
            bool hov = dialogHover == i;
            bool prs = dialogPressed == i;
            if (i == 0) {
                impl()->painter.FillRect(r, prs ? impl()->theme.accentPressed
                                        : hov ? impl()->theme.accentHover : impl()->theme.accent);
                impl()->painter.DrawText(text, Font{ .size = 14.0f, .weight = FontWeight::SemiBold },
                                 r, impl()->theme.textOnAccent, HAlign::Center, VAlign::Center);
            } else {
                if (prs) impl()->painter.FillRect(r, impl()->theme.subtleActive);
                else if (hov) impl()->painter.FillRect(r, impl()->theme.hoverSoft);
                impl()->painter.DrawText(text, Font{ .size = 14.0f }, r,
                                 impl()->theme.text, HAlign::Center, VAlign::Center);
            }
            // baseline .dlg-f button + button (HTML:320): the vertical divider is drawn only between two buttons —
            // a lone full-width button has none, and a lone secondary has no dangling line (GAP53-01)
            if (i == 1 && !dlgPrimary.empty())
                impl()->painter.FillRect({ r.x, by, 1, 48 }, impl()->theme.divider);
            // GAP38-01: keyboard focus ring (buttons are full-bleed to the panel edge; the ring is inset 2px inside the button;
            // inside the PushClipRounded mask, the bottom arcs are clipped with the panel)
            if (i == dialogFocus && g_focusFromKeyboard)
                impl()->painter.StrokeRoundedRect({ r.x + 2, r.y + 2, r.w - 4, r.h - 4 },
                                          5.0f, impl()->theme.accentText, 2.0f);
        }
        impl()->painter.PopClipRounded();
    }
};

/// Keyboard focus / radio-group exclusivity / accelerators (ARCH58-01 stage 2 split).
template <class W>
struct WindowFocusOps {
protected:
    W* impl() { return static_cast<W*>(this); }
public:

    Widget* focused = nullptr;   // keyboard-focused widget
    struct Accelerator {
        uint32_t vk;
        bool ctrl, shift, alt;
        std::function<void()> fn;
    };
    std::vector<Accelerator> accelerators;

    /// Trigger on an accelerator hit and return true (API-11): called from WM_KEYDOWN/WM_SYSKEYDOWN
    /// before routing to the focused widget
    bool CheckAccelerators(uint32_t vk) {
        if (accelerators.empty()) return false;
        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
        for (auto& a : accelerators) {
            if (a.vk == vk && a.ctrl == ctrl && a.shift == shift && a.alt == alt) {
                if (a.fn) a.fn();
                return true;
            }
        }
        return false;
    }

    /// Switch keyboard focus (nullptr clears), notifying old and new widgets and repainting.
    void SetFocused(Widget* widget) {
        if (focused == widget) return;
        if (focused) {
            focused->OnUnfocused();
            // R12: public focus-lost notification — genuine transfers only. DetachTree clears
            // focus silently (no user callbacks mid-detach, ARCH39-02), so removing the focused
            // box cannot double-fire a commit-and-rebuild flow.
            if (auto* tb = dynamic_cast<TextBox*>(focused))
                if (tb->OnFocusLost) tb->OnFocusLost();
        }
        // R14: the callbacks above may legitimately mutate the tree (blur-commit-rebuild:
        // the whole list is rebuilt from inside TextBox::OnFocusLost), and the new target
        // may have gone down with it. DetachTree already cleared `focused` for everything
        // it detached, so re-writing the pre-callback target here would resurrect a
        // detached pointer — and FlushPendingDestroy only heals bare pointers that EQUAL
        // a queued subtree root, so a target nested inside a rebuilt row is never healed
        // (the next input dereferences freed memory). Same window_ guard as the click
        // path (ARCH39-01): a detached widget must not become focused and must not
        // receive OnFocused; focus simply lands nowhere.
        if (widget && !widget->window_) widget = nullptr;
        focused = widget;
        if (widget) widget->OnFocused();
        impl()->Invalidate();
    }

    /// Tab / Shift+Tab cycle keyboard focus (in render order).
    /// A radio group occupies a single Tab stop (focus lands on the checked item, or the first if none; M-04).
    void CycleFocus(bool backward) {
        std::vector<Widget*> order;
        std::vector<std::pair<int, RadioButton*>> radioRep;   // group → Tab representative
        auto walk = [&](auto&& self, Widget* w) -> void {
            if (!w->Visible()) return;
            if (auto* rb = dynamic_cast<RadioButton*>(w)) {
                bool seen = false;
                for (auto& [g, rep] : radioRep) {
                    if (g == rb->group_) {
                        seen = true;
                        if (rb->checked_ && !rep->checked_) rep = rb;   // prefer the checked item
                    }
                }
                if (!seen)
                    radioRep.push_back({ rb->group_, rb });
            } else if (w->Focusable() && w->Enabled()) {
                order.push_back(w);
            }
            if (auto* c = dynamic_cast<Container*>(w))
                for (auto& child : c->children_) self(self, child.get());
        };
        for (auto& w : impl()->widgets) walk(walk, w.get());
        for (auto& [g, rep] : radioRep)
            if (rep->Enabled()) order.push_back(rep);
        if (order.empty()) return;
        size_t idx = 0;
        bool found = false;
        for (size_t i = 0; i < order.size(); ++i)
            if (order[i] == focused) { idx = i; found = true; break; }
        size_t next = found
            ? (backward ? (idx + order.size() - 1) % order.size()
                        : (idx + 1) % order.size())
            : (backward ? order.size() - 1 : 0);
        SetFocused(order[next]);
    }

    /// Radio buttons of the same group in one window are mutually exclusive: uncheck all but except.
    void UncheckRadioGroup(int group, Widget* except) {
        for (auto& w : impl()->widgets) UncheckRadioWalk(w.get(), group, except);
    }

    void UncheckRadioWalk(Widget* w, int group, Widget* except) {
        if (!w) return;
        if (w != except) {
            if (auto* rb = dynamic_cast<RadioButton*>(w);
                rb && rb->group_ == group && rb->checked_) {
                rb->checked_ = false;
            }
        }
        if (auto* c = dynamic_cast<Container*>(w)) {
            for (auto& child : c->children_) UncheckRadioWalk(child.get(), group, except);
        }
    }

    void CollectRadioWalk(Widget* w, int group, std::vector<RadioButton*>& out) {
        if (!w || !w->Visible()) return;
        if (auto* rb = dynamic_cast<RadioButton*>(w);
            rb && rb->group_ == group && rb->Enabled())
            out.push_back(rb);
        if (auto* c = dynamic_cast<Container*>(w))
            for (auto& child : c->children_) CollectRadioWalk(child.get(), group, out);
    }

    /// Arrow-key navigation target within a radio group (cyclic in render order, skipping disabled; M-04).
    RadioButton* NextRadioInGroup(RadioButton* from, bool backward) {
        std::vector<RadioButton*> g;
        for (auto& w : impl()->widgets) CollectRadioWalk(w.get(), from->group_, g);
        if (g.size() < 2) return nullptr;
        for (size_t i = 0; i < g.size(); ++i)
            if (g[i] == from)
                return g[(i + (backward ? g.size() - 1 : 1)) % g.size()];
        return nullptr;
    }
};

/// Hit-testing / container hover highlight / multi-click detection / outermost scroll view (ARCH58-01 stage 2 split).
template <class W>
struct WindowHitOps {
protected:
    W* impl() { return static_cast<W*>(this); }
public:

    // Multi-click detection (double-click word-select / triple-click select-all, R27-03): the window class needs CS_DBLCLKS,
    // and consecutive clicks must land within the system double-click time and rect (SM_CX/YDOUBLECLK) to accumulate
    DWORD lastClickTime_ = 0;
    Point lastClickPos_{};
    int clickCount_ = 0;

    /// Accumulate the click streak and return this click's ordinal (1=single, 2=double, ≥3=multi).
    int BumpClickCount(const Point& pos) {
        DWORD now = GetMessageTime();
        float tolX = GetSystemMetrics(SM_CXDOUBLECLK) * 96.0f / impl()->dpi;
        float tolY = GetSystemMetrics(SM_CYDOUBLECLK) * 96.0f / impl()->dpi;
        if (now - lastClickTime_ <= GetDoubleClickTime() &&
            std::fabs(pos.x - lastClickPos_.x) <= tolX &&
            std::fabs(pos.y - lastClickPos_.y) <= tolY)
            ++clickCount_;
        else
            clickCount_ = 1;
        lastClickTime_ = now;
        lastClickPos_ = pos;
        return clickCount_;
    }

    Widget* HitTest(Point pos) {
        for (auto it = impl()->widgets.rbegin(); it != impl()->widgets.rend(); ++it) {
            if (Widget* hit = HitTestAt(it->get(), pos)) return hit;
        }
        return nullptr;
    }

    /// Depth-first reverse-order hit test: children draw above their parent container, so they are hit before it;
    /// the final hit is decided by the widget's own HitTarget (composite widgets may defer to inner widgets).
    Widget* HitTestAt(Widget* widget, Point pos) {
        if (!widget->Visible() || !widget->Enabled()) return nullptr;
        Rect childClip;
        if (widget->ChildHitClip(&childClip) && !childClip.Contains(pos.x, pos.y))
            return nullptr;   // hit point outside the child's clip region (viewport): the whole subtree defers
        if (auto* c = dynamic_cast<Container*>(widget)) {
            for (auto it = c->children_.rbegin(); it != c->children_.rend(); ++it) {
                if (Widget* hit = HitTestAt(it->get(), pos)) return hit;
            }
        }
        return widget->HitTarget(pos);
    }

    /// Hover-chain state (round-70 R10; was "container hover highlight" before the walk went
    /// unfiltered): the widgets on the chain of the hovered (or drag-captured) widget light up,
    /// those left go dark. target == nullptr means everything leaves. Before R10 the collect
    /// filter kept only containers with hoverHighlight_ — the only reader was Container::OnPaint.
    /// The walk is now unfiltered so any widget can read Hovered() / subscribe OnHoverChanged;
    /// highlight containers get exactly the same transitions as before (identical lit set, so
    /// rendering is unchanged), and only they Invalidate — lit has no rendering effect
    /// elsewhere, so unsubscribed trees change nothing observable.
    void UpdateContainerHover(Widget* from, Widget* to) {
        auto collect = [](Widget* w, Widget** buf, int& n) {
            for (; w; w = w->parent_) {
                if (n >= 64) return;   // GAP50-09: fixed-size stack array; on overflow, truncate rather than write out of bounds (the chain top is dropped)
                buf[n++] = w;
            }
        };
        Widget* a[64]; int na = 0;
        Widget* b[64]; int nb = 0;
        collect(from, a, na);
        collect(to, b, nb);
        for (int i = 0; i < na; ++i) {
            bool still = false;
            for (int j = 0; j < nb; ++j) still = still || b[j] == a[i];
            if (!still && a[i]->hoverLit_) {
                a[i]->hoverLit_ = false;
                if (a[i]->hoverHighlight_) a[i]->Invalidate();   // R10: lit is render-visible only for highlight containers
                if (a[i]->OnHoverChanged) a[i]->OnHoverChanged(false);
            }
        }
        for (int j = 0; j < nb; ++j) {
            if (!b[j]->hoverLit_) {
                b[j]->hoverLit_ = true;
                if (b[j]->hoverHighlight_) b[j]->Invalidate();
                if (b[j]->OnHoverChanged) b[j]->OnHoverChanged(true);
            }
        }
    }

    /// Shared teardown of the client hover chain (round-70 R10): WM_MOUSELEAVE and the
    /// WM_ACTIVATE WA_INACTIVE branch (R32-02's "no state leaks across activations" extended
    /// from the caption to the client area) both close the tooltip, unlight the whole chain
    /// (leave callbacks included) and drop the deep target with its OnMouseLeave.
    void ClearHoverChain() {
        impl()->CloseTooltip();
        UpdateContainerHover(impl()->hovered, nullptr);
        if (impl()->hovered) {
            impl()->hovered->OnMouseLeave();
            impl()->hovered = nullptr;
        }
    }

    /// Outermost ScrollViewer (the landing spot for scroll keys when no widget has focus, R27-02).
    ScrollViewer* FindScroller(Widget* w) {
        if (!w || !w->Visible()) return nullptr;
        if (auto* sv = dynamic_cast<ScrollViewer*>(w)) return sv;
        if (auto* c = dynamic_cast<Container*>(w))
            for (auto& child : c->children_)
                if (auto* sv = FindScroller(child.get())) return sv;
        return nullptr;
    }

    ScrollViewer* RootScroller() {
        for (auto& w : impl()->widgets)
            if (auto* sv = FindScroller(w.get())) return sv;
        return nullptr;
    }
};

/// Popup and tip (toast/tooltip/flyout) lifecycle (ARCH58-01 stage 2 split).
template <class W>
struct WindowPopupOps {
protected:
    W* impl() { return static_cast<W*>(this); }
public:

    std::unique_ptr<detail::ToastPopup> toast;
    DWORD hoverTick = 0;              // when the currently hovered widget was entered
    std::unique_ptr<detail::ToolTipPopup> tooltip;
    std::unique_ptr<detail::Flyout> flyout;   // active dropdown/context-menu popup

    /// Popup liveness: a popup that closed itself (item click / outside click) only destroys its window and leaves the
    /// flyout pointer; this lazily clears the residue, otherwise WM_CHAR swallows keys forever and global input dies (round-7 F-02).
    bool FlyoutAlive() {
        if (flyout && !flyout->IsAlive()) flyout.reset();
        return flyout != nullptr;
    }

    /// Close all open menus and popups (called on window move/resize/deactivation).
    void CloseMenus() {
        for (auto& w : impl()->widgets) {
            if (auto* bar = dynamic_cast<MenuBar*>(w.get())) bar->CloseMenu();
        }
        if (flyout) {
            std::unique_ptr<detail::Flyout> f = std::move(flyout);
            flyout = nullptr;
            f->Close();
        }
        CloseTooltip();
    }

    void CloseTooltip() {
        if (tooltip) {
            std::unique_ptr<detail::ToolTipPopup> t = std::move(tooltip);
            tooltip = nullptr;
            t->Close();
        }
        if (impl()->hwnd) KillTimer(impl()->hwnd, kTipTimerId);
    }
};

/// Window icon bake cache (GAP48-01/PERF48-01; ARCH58-01 stage 2 split).
template <class W>
struct WindowIconOps {
protected:
    W* impl() { return static_cast<W*>(this); }
public:

    HICON generatedIconBig = nullptr;
    HICON generatedIconSmall = nullptr;
    std::wstring iconLetter;   // first letter (full code point, R36-05b)
    // Cache keys over all badge colors (R36-05a): gradient ends accentHover/accent plus the letter color
    // textOnAccent — comparing only accent would miss themes where just the other two change
    uint32_t iconKeyAccent = 0;
    uint32_t iconKeyHover = 0;
    uint32_t iconKeyOnAccent = 0;
    bool iconUser = false;
    // GAP48-01/PERF48-01: baked-icon cache. Once a library-generated icon is installed via WM_SETICON,
    // its handle is owned by this cache until window destruction; replacement installs without destroying — the old
    // "DestroyIcon first, then WM_SETICON" left the window briefly holding a freed handle; re-entry with the same key
    // (SetTitle/SetTheme toggling) hits the cache directly, skipping the ≈2.8 ms DC render-target
    // rebuild. generatedIconBig/Small are thereby demoted to aliases of "currently installed".
    // Capacity eviction only removes uninstalled entries (see RefreshIcon).
    struct IconCacheEntry {
        std::wstring letter;
        UINT px;
        uint32_t keyAccent, keyHover, keyOnAccent;
        HICON icon;
    };
    std::vector<IconCacheEntry> iconCache_;
    UINT iconPxBig = 0;   // the short-circuit key includes size: after a DPI change, icons of the old size must not be reused

    /// GAP48-01/PERF48-01: look up the bake cache by full key (letter+size+three colors); on miss
    /// run MakeBadgeIcon (the ≈2.8 ms DC render-target rebuild happens only for first-seen keys).
    /// Entries are reclaimed together at window destruction; capacity eviction runs in RefreshIcon after
    /// installing the new icon (the replaced old icon is then off the window).
    HICON FindOrBakeIcon(const std::wstring& letter, UINT px,
                         uint32_t keyAccent, uint32_t keyHover,
                         uint32_t keyOnAccent) {
        for (const auto& e : iconCache_)
            if (e.px == px && e.keyAccent == keyAccent &&
                e.keyHover == keyHover && e.keyOnAccent == keyOnAccent &&
                e.letter == letter)
                return e.icon;
        HICON icon = MakeBadgeIcon(letter, static_cast<int>(px), impl()->theme);
        if (!icon) return nullptr;
        iconCache_.push_back(
            { letter, px, keyAccent, keyHover, keyOnAccent, icon });
        return icon;
    }

    /// (Re)generate the default window icon and WM_SETICON; called after title/theme changes, idempotent.
    void RefreshIcon() {
        if (!impl()->hwnd || iconUser) return;
        // First letter takes the full code point (R36-05b): taking only the first UTF-16 code unit leaves an orphan
        // surrogate for non-BMP first letters (emoji etc.), rendering as blank/replacement glyphs
        std::wstring letter = L"S";
        if (impl()->titlebar && !impl()->titlebar->Text().empty()) {
            const std::wstring& t = impl()->titlebar->Text();
            size_t units = (t[0] >= 0xD800 && t[0] < 0xDC00 && t.size() >= 2 &&
                            t[1] >= 0xDC00 && t[1] < 0xE000) ? 2 : 1;
            letter = t.substr(0, units);
        }
        auto pack = [this](const Color& c) {
            auto q = [](float v) {
                return static_cast<uint32_t>(v * 255.0f + 0.5f) & 0xFFu;
            };
            // OBS52-03: alpha joins the key — the three colors MakeBadgeIcon consumes composite with alpha,
            // so themes differing only in alpha must not wrongly hit the same cache entry
            return q(c.a) << 24 | q(c.r) << 16 | q(c.g) << 8 | q(c.b);
        };
        uint32_t keyAccent = pack(impl()->theme.accent);
        uint32_t keyHover = pack(impl()->theme.accentHover);
        uint32_t keyOnAccent = pack(impl()->theme.textOnAccent);
        UINT iconDpi = GetDpiForWindow(impl()->hwnd);
        UINT pxBig = static_cast<UINT>(GetSystemMetricsForDpi(SM_CXICON, iconDpi));
        UINT pxSmall = static_cast<UINT>(GetSystemMetricsForDpi(SM_CXSMICON, iconDpi));
        // the short-circuit key includes size (iconPxBig): after a DPI change, icons of the old size must not be reused
        if (letter == iconLetter && pxBig == iconPxBig &&
            keyAccent == iconKeyAccent && keyHover == iconKeyHover &&
            keyOnAccent == iconKeyOnAccent && generatedIconBig) return;
        HICON bigIcon = FindOrBakeIcon(letter, pxBig, keyAccent, keyHover,
                                       keyOnAccent);
        HICON smallIcon = FindOrBakeIcon(letter, pxSmall, keyAccent, keyHover,
                                         keyOnAccent);   // do not name it "small": rpcndr.h macro
        if (!bigIcon && !smallIcon) return;       // bake failed: keep the current icons
        // GAP48-01: install without destroying — the old icons stay owned by iconCache_; the window holds valid
        // handles at any moment (the old way DestroyIcon'd first then WM_SETICON, briefly holding freed
        // handles; taskbar/title bar could pick up empty icons)
        SendMessageW(impl()->hwnd, WM_SETICON, ICON_BIG,
                     (LPARAM)(bigIcon ? bigIcon : smallIcon));
        SendMessageW(impl()->hwnd, WM_SETICON, ICON_SMALL,
                     (LPARAM)(smallIcon ? smallIcon
                                        : (bigIcon ? bigIcon : smallIcon)));
        generatedIconBig = bigIcon ? bigIcon : smallIcon;
        generatedIconSmall = smallIcon ? smallIcon : generatedIconBig;
        iconLetter = letter;
        iconPxBig = pxBig;
        iconKeyAccent = keyAccent;
        iconKeyHover = keyHover;
        iconKeyOnAccent = keyOnAccent;
        // After installation, evict over-capacity entries: the replaced old icons are off the window by now and can be
        // safely destroyed; newly installed entries are protected by the generatedIcon* aliases
        while (iconCache_.size() > 8) {
            bool evicted = false;
            for (auto it = iconCache_.begin(); it != iconCache_.end(); ++it) {
                if (it->icon != generatedIconBig &&
                    it->icon != generatedIconSmall) {
                    DestroyIcon(it->icon);
                    iconCache_.erase(it);
                    evicted = true;
                    break;
                }
            }
            if (!evicted) break;
        }
    }
};

/// Graphics device and frame canvas (rt/painter + the device trio; ARCH58-01 stage 2 split).
template <class W>
struct WindowGfxOps {
protected:
    W* impl() { return static_cast<W*>(this); }
public:

    RenderPainter painter;
    detail::RenderTarget rt;

    void EnsureDevice() {
        if (rt.ctx) return;
        if (!impl()->hwnd || !g_d2d) return;
        detail::CreateRenderTarget(impl()->hwnd, impl()->dpi, impl()->theme.text, impl()->theme.windowBackground, rt);
    }

    void ResizeSwapChain(UINT width, UINT height) {
        if (!rt.ctx || !rt.swap) return;
        rt.ctx->SetTarget(nullptr);
        if (SUCCEEDED(rt.swap->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0))) {
            detail::BindSwapChainTarget(rt, impl()->dpi);
        } else {
            detail::SetError("ResizeBuffers failed");   // ARCH38-04
            DiscardDevice();
        }
    }

    void DiscardDevice() {
        painter.OnDeviceLost();   // releases device-dependent caches (gradients/effects/layer stack) together
        rt = detail::RenderTarget{};
    }
};

/// Animation frame subscription (ARCH58-01 stage 2 split).
template <class W>
struct WindowAnimOps {
protected:
    W* impl() { return static_cast<W*>(this); }
public:

    std::vector<Widget*> animators;

    void AddAnimator(Widget* widget) {
        for (Widget* w : animators)
            if (w == widget) return;
        animators.push_back(widget);
        if (impl()->hwnd) SetTimer(impl()->hwnd, kAnimTimerId, 33, nullptr);
    }

    void RemoveAnimator(Widget* widget) {
        for (size_t i = 0; i < animators.size(); ++i) {
            if (animators[i] == widget) {
                animators.erase(animators.begin() + i);
                break;
            }
        }
        if (animators.empty() && impl()->hwnd) KillTimer(impl()->hwnd, kAnimTimerId);
    }
};

/// Deferred destroy queue (ARCH38-07; ARCH58-01 stage 2 split).
template <class W>
struct WindowDestroyOps {
protected:
    W* impl() { return static_cast<W*>(this); }
public:

    // ARCH38-07: deferred destroy — Container::Remove/ClearChildren parks the detached subtree in a
    // queue; destruction happens in bulk after this frame's messages. Synchronous destruction would make "Remove self
    // inside a callback" (a card's × closing itself, a common pattern) dereference a dangling pointer once control
    // returns to WindowImpl (WM_LBUTTONDOWN still reads widget->Focusable() after OnMouseDown).
    // Only destruction is deferred: children_.erase stays synchronous; iterator invalidation from erase-during-iteration
    // is unhandled (unreachable today — no user callbacks inside children_ iteration, ARCH39-02)
    std::vector<std::unique_ptr<Widget>> pendingDestroy_;
    bool flushingDestroy_ = false;

    void QueueDestroy(std::unique_ptr<Widget> w) {
        pendingDestroy_.push_back(std::move(w));
    }

    void FlushPendingDestroy() {
        if (pendingDestroy_.empty() || flushingDestroy_) return;
        flushingDestroy_ = true;   // Remove inside a destroy callback only enqueues, re-entry guarded
        while (!pendingDestroy_.empty()) {
            std::vector<std::unique_ptr<Widget>> batch;
            batch.swap(pendingDestroy_);
            for (auto& w : batch) {
                // ARCH39-01: clear bare references before flushing — detached widgets may still be dirtied (the focus
                // write sites got window_ guards; this backstops the same risk for hovered/captured)
                if (impl()->focused == w.get()) impl()->focused = nullptr;
                if (impl()->hovered == w.get()) impl()->hovered = nullptr;
                if (impl()->captured == w.get()) {
                    impl()->captured = nullptr;
                    if (impl()->hwnd) ReleaseCapture();
                }
                w.reset();
            }
        }
        flushingDestroy_ = false;
    }
};

/// Owner-drawn title-bar state and IME anchoring (R32/R35-03; ARCH58-01 stage 2 split).
template <class W>
struct WindowChromeOps {
protected:
    W* impl() { return static_cast<W*>(this); }
public:

    TitleBar* titlebar = nullptr;   // owner-drawn title bar (created automatically with the window)
    bool active = true;             // tracks WM_NCACTIVATE; the title bar grays out when inactive
    int captionPressed = 0;         // title-bar button pressed state (HTMINBUTTON/HTMAXBUTTON/
                                    // HTCLOSE; 0=none. While captured, release arrives as WM_LBUTTONUP)

    /// IME composition window anchoring (R35-03): writes the focused widget's insertion point into the default IME context
    /// so the composition/candidate window hugs the owner-drawn caret. No-op without focus or an anchor point.
    void UpdateImeAnchor() {
        if (!impl()->hwnd || !impl()->focused) return;
        Point a;
        if (!impl()->focused->ImeAnchorPoint(a)) return;
        HIMC himc = ImmGetContext(impl()->hwnd);
        if (!himc) return;
        COMPOSITIONFORM cf{};
        cf.dwStyle = CFS_POINT;   // ptCurrentPos = client pixel coordinates
        cf.ptCurrentPos.x = static_cast<LONG>(a.x * impl()->dpi / 96.0f + 0.5f);
        cf.ptCurrentPos.y = static_cast<LONG>(a.y * impl()->dpi / 96.0f + 0.5f);
        ImmSetCompositionWindow(himc, &cf);
        ImmReleaseContext(impl()->hwnd, himc);
    }

    /// Title-bar hover write + hover safety timer (R32-02): WM_NCMOUSELEAVE may be missing for a whole
    /// round; while hover is non-zero the cursor is checked periodically and cleared when out of bounds, guaranteeing the highlight fades.
    void SetCaptionHover(int ht) {
        if (titlebar) titlebar->SetCaptionHover(ht);
        if (!impl()->hwnd) return;
        if (ht != 0) SetTimer(impl()->hwnd, kCapTimerId, 50, nullptr);
        else KillTimer(impl()->hwnd, kCapTimerId);
    }

    /// Title-bar button hit code → system command (R32-01 commit on release).
    void PostCaptionCommand(int code) {
        switch (code) {
        case HTMINBUTTON:
            PostMessageW(impl()->hwnd, WM_SYSCOMMAND, SC_MINIMIZE, 0);
            break;
        case HTMAXBUTTON:
            PostMessageW(impl()->hwnd, WM_SYSCOMMAND,
                         IsZoomed(impl()->hwnd) ? SC_RESTORE : SC_MAXIMIZE, 0);
            break;
        case HTCLOSE:
            PostMessageW(impl()->hwnd, WM_SYSCOMMAND, SC_CLOSE, 0);
            break;
        default:
            break;
        }
    }
};

struct WindowImpl : WindowDialogOps<WindowImpl>, WindowFocusOps<WindowImpl>,
           WindowHitOps<WindowImpl>, WindowPopupOps<WindowImpl>,
           WindowIconOps<WindowImpl>, WindowGfxOps<WindowImpl>,
           WindowAnimOps<WindowImpl>, WindowDestroyOps<WindowImpl>,
           WindowChromeOps<WindowImpl> {
    // ARCH60-02 (destruction-order invariant, registered round-61): after the CRTP split the widget tree (widgets,
    // this derived class's member) destructs before the nine base subobjects (popups/graphics device/icon cache) —
    // the reverse of 0.46.3's declaration order. The safety this relies on: widget destructors must not touch the window's
    // popups/device/icon cache (today the only widget with a real destructor, MenuBar::~MenuBar, only collapses its own
    // menu — satisfied); popup destruction→WM_DESTROY→OnDestroyed only reads the popup's own state;
    // no reverse dependency (a base destructor touching the widget tree) exists. New widgets' destructors are bound by this too.
    Window* owner = nullptr;
    HWND hwnd = nullptr;
    float dpi = 96.0f;
    float clientW = 960.0f;
    float clientH = 600.0f;
    Theme theme = Theme::Dark();
    std::vector<std::unique_ptr<Widget>> widgets;
    Widget* hovered = nullptr;
    Widget* captured = nullptr;
    bool trackingMouse = false;
    // Minimum window size (R36-04): client-area DIP; 0 = unconstrained (default). Written by SetMinimumSize,
    // converted to ptMinTrackSize by WM_GETMINMAXINFO
    float minWidthDip = 0;
    float minHeightDip = 0;
    // Maximize enabled (round-65 R2.3, default): written by SetMaximizable, which also mirrors it
    // into the WS_MAXIMIZEBOX style bit; WndProc consults it for SC_MAXIMIZE / caption double-click
    bool maximizable = true;

    // External-interface state (API-10/11/12)
    std::function<bool()> onClosing;             // close interception: false = cancel
    std::function<void(float)> onDpiChanged;     // DPI change notification
    std::function<void()> onSystemThemeChanged;  // system theme switch (R37-03)
    std::function<void()> onSizeChanged;         // R13: client DIP size actually changed
    // R13: last notified client size (DIP); sentinel -1 makes the first real WM_SIZE notify
    float lastNotifiedW = -1;
    float lastNotifiedH = -1;


    /// Cleanup when a subtree is detached from the window (API-06: Container::Remove/ClearChildren):
    /// animation subscription, keyboard focus, mouse capture and hover are reset if they fall inside the subtree
    void DetachTree(Widget* w) {
        if (!w) return;
        // R10: the detached subtree's own lit flag clears silently — the subtree (and every
        // subscription inside it) is going away, and no user callback may run mid-detach
        // (DetachTree executes inside Container::Remove's children_ iteration, ARCH39-02).
        // Every recursive call below passes through here, so the whole subtree resets.
        w->hoverLit_ = false;
        // ARCH38-06: before detaching, close popups owned by the subtree (their OwnerWidget is about to dangle);
        // at this point the subtree's parent chain is still intact, so ownership can be walked upward
        if (flyout && flyout->OwnerWidget() && InSubtree(flyout->OwnerWidget(), w))
            CloseMenus();
        for (Widget* a = w->parent_; a; a = a->parent_)
            if (a->hoverLit_) {
                a->hoverLit_ = false;
                a->Invalidate();
            }
        if (auto* c = dynamic_cast<Container*>(w))
            for (auto& child : c->children_) DetachTree(child.get());
        RemoveAnimator(w);
        if (focused && InSubtree(focused, w)) {
            // R12: protected cleanup only, deliberately not SetFocused — the public
            // TextBox::OnFocusLost must not fire while a commit callback could re-enter
            // Remove (ARCH39-02). API-06's documented intent ("keyboard focus ... reset if
            // they fall inside the subtree") covers the whole subtree, not just its root;
            // clearing only focused == w left focus pointing into the detached subtree.
            focused->OnUnfocused();
            focused = nullptr;
        }
        if (captured && InSubtree(captured, w)) {
            // R14: subtree-wide, same contract as focus (R12) and hover (R10) — the rebuild
            // flows that detach the focused widget equally detach the captured one, and a
            // nested captured widget is not healed by FlushPendingDestroy (only queued
            // subtree roots are), leaving WM_LBUTTONUP / WM_CAPTURECHANGED to dereference
            // freed memory. Capture is dropped without OnCaptureLost: the widget is going
            // away, and this mirrors the root-detach case that already behaved this way.
            captured = nullptr;
            if (hwnd) ReleaseCapture();
        }
        if (hovered && InSubtree(hovered, w)) {
            // R10: same subtree-wide contract as the focus clear above (was hovered == w only).
            // Ancestor lit flags clear silently above; the next mouse move rebuilds the chain.
            hovered = nullptr;
            CloseTooltip();   // ARCH38-06: the tooltip is pinned to the removed widget; close it to avoid an orphan
        }
        w->window_ = nullptr;
        w->parent_ = nullptr;
    }

    /// Whether q (a bare pointer such as a popup's OwnerWidget) falls inside root's subtree (parent chain must be intact).
    static bool InSubtree(const void* q, const Widget* root) {
        for (const Widget* a = static_cast<const Widget*>(q); a; a = a->parent_)
            if (a == root) return true;
        return false;
    }

    // ARCH38-02: out-of-contract use (windows still alive at Shutdown) releases their device resources
    // so windows do not keep holding a Reset swap chain/context. The contract path (windows destroyed first) is a no-op
    static void DiscardAllDevices() {
        for (Window* w : g_windows)
            if (w->impl_) {
                // ARCH39-06: popups are separate HWNDs, not in g_windows — out of contract
                // (windows still alive at Shutdown), close all popups first so their next-frame WM_PAINT
                // does not null-deref after g_dwrite has been Reset
                w->impl_->CloseMenus();
                w->impl_->DiscardDevice();
            }
    }

    // Batched layout (P-04): inside a batch, RequestLayout only records; EndLayoutBatch/Render
    // merges and runs one whole-window layout, avoiding O(N) full-tree relayouts from one action
    int layoutBatchDepth_ = 0;
    bool layoutPending_ = false;

    void RequestLayout() {
        if (layoutBatchDepth_ > 0) {
            layoutPending_ = true;
            return;
        }
        Layout();
    }
    void FlushLayout() {
        if (layoutPending_) {
            layoutPending_ = false;
            Layout();
        }
    }





    Rect contentRect{}; // docked content area

    // Window icon (R34-02): by default baked from the title's first letter + theme accent (MakeBadgeIcon,
    // same source as the owner-drawn badge); a user icon injected via SetIcon wins (handle owned by the user,
    // the library does not destroy it). iconLetter/iconKey* are regeneration cache keys; skip when title and theme are unchanged.






    std::function<void()> onTick;     // app tick callback

    // ---- window ----

    void Attach(std::unique_ptr<Widget> widget) {
        widget->window_ = owner;
        widget->parent_ = nullptr;
        SyncOwnership(widget.get());
        widgets.push_back(std::move(widget));
        Layout();
        Invalidate();
    }

    /// Recursively set the owning window over a container subtree.
    void SyncOwnership(Widget* widget) {
        if (auto* c = dynamic_cast<Container*>(widget)) {
            for (auto& child : c->children_) {
                child->window_ = owner;
                SyncOwnership(child.get());
            }
        }
    }


    void Invalidate() {
        if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
    }

    /// Layout: dock bands are allocated interleaved in insertion order (TitleBar enqueues first → always full width).
    /// Top/Bottom bands span between the left/right cursors at their time: Left/Right docked earlier
    /// narrow the later Top/Bottom — when the navigation view docks first, the top bar only spans the content column (S-03).
    void Layout() {
        float top = 0, bottom = clientH, left = 0, right = clientW;
        for (auto& w : widgets) {
            if (!w->Visible()) continue;
            switch (w->dock_) {
            case Dock::Top:
                w->bounds_ = { left, top, std::max(0.0f, right - left),
                               w->DockHeight() };
                top += w->DockHeight();
                break;
            case Dock::Bottom: {
                float h = w->DockHeight();
                bottom -= h;
                w->bounds_ = { left, bottom, std::max(0.0f, right - left), h };
                break;
            }
            case Dock::Left: {
                float wd = w->DockWidth();
                w->bounds_ = { left, top, wd, std::max(0.0f, bottom - top) };
                left += wd;
                break;
            }
            case Dock::Right: {
                float wd = w->DockWidth();
                right -= wd;
                w->bounds_ = { right, top, wd, std::max(0.0f, bottom - top) };
                break;
            }
            default:
                break;
            }
        }
        contentRect = { left, top, std::max(0.0f, right - left),
                        std::max(0.0f, bottom - top) };
        // Docked composite containers (TopBar etc.) must also lay out their own children;
        // simple docked widgets (menu bar/toolbar/status bar) have no children and are unaffected.
        for (auto& w : widgets) {
            if (w->dock_ == Dock::None) {
                if (auto* c = dynamic_cast<Container*>(w.get()); c && c->Visible())
                    c->AssignBounds(contentRect);
            } else if (auto* c = dynamic_cast<Container*>(w.get()); c && c->Visible()) {
                c->AssignBounds(w->bounds_);
            }
        }
    }

    // ---- animation frames ----














    // ---- device resources ----




    // ---- rendering ----

    void Render() {
        if (!rt.ctx || !rt.swap) return;
        FlushLayout();   // pending layouts left in the batch are merged and run before rendering (P-04)
        painter.SetFrame(rt.ctx.Get(), rt.brush.Get());
        painter.FlattenLayers();   // frame-start backstop: flatten any layer stack left over from the last frame (§3.12)
        rt.ctx->BeginDraw();
        rt.ctx->Clear(detail::ToD2D(theme.windowBackground));
        // Content area: one flat content-color band; floating cards (Card) express elevation on top
        painter.FillRect(contentRect, theme.contentBackground);
        // Background decorative blobs (body::before twin radial gradients): viewport-percent anchored + fixed pixel radii,
        // transparent 70% ⇒ 0.7 stop cut-off (fully transparent beyond 70%, round-9 B-03)
        float vw = clientW, vh = clientH;
        const GradientStop blob1Stops[3]{
            { 0.0f, theme.blob1 }, { 0.7f, Color::Rgb(0, 0.0f) },
            { 1.0f, Color::Rgb(0, 0.0f) } };
        const GradientStop blob2Stops[3]{
            { 0.0f, theme.blob2 }, { 0.7f, Color::Rgb(0, 0.0f) },
            { 1.0f, Color::Rgb(0, 0.0f) } };
        painter.FillEllipseGradientStops({ vw * 0.06f - 640.0f,
                                           -vh * 0.08f - 480.0f, 1280.0f, 960.0f },
                                         blob1Stops, 3);
        painter.FillEllipseGradientStops({ vw * 1.04f - 760.0f,
                                           vh * 1.08f - 560.0f, 1520.0f, 1120.0f },
                                         blob2Stops, 3);
        for (auto& widget : widgets) PaintTree(widget.get());
        if (dialogActive) RenderDialog();
        HRESULT hr = rt.ctx->EndDraw();
        // SyncInterval=0 non-blocking present: the modal resize loop must not wait for vsync,
        // otherwise the window size keeps advancing during present and a new frame composites at an even newer size
        HRESULT hrPresent = rt.swap->Present(0, 0);
        if (hr == (HRESULT)D2DERR_PUSH_POP_UNBALANCED) {
            // flatten a layer-stack imbalance on the spot (drawing recovers next frame); no device rebuild needed
            painter.FlattenLayers();
            Invalidate();
        } else if (hr == (HRESULT)D2DERR_RECREATE_TARGET ||
            hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET ||
            hrPresent == DXGI_ERROR_DEVICE_REMOVED ||
            hrPresent == DXGI_ERROR_DEVICE_RESET) {
            // ARCH-04: Present reporting device removal also enters recovery — if EndDraw succeeds but
            // Present fails, the window would freeze on its last frame forever
            detail::SetError("device lost (EndDraw/Present)");
            DiscardDevice();
            Invalidate();
        }
    }

    /// Depth-first draw: parents before children (children layer above their parent container).
    void PaintTree(Widget* widget) {
        if (!widget->Visible()) return;
        Rect paintClip;
        const bool clipped = widget->ChildPaintClip(&paintClip);
        if (clipped && (paintClip.w <= 0 || paintClip.h <= 0)) return;
        widget->OnPaint(painter, theme);
        if (auto* c = dynamic_cast<Container*>(widget)) {
            if (clipped) {
                painter.PushClip(paintClip);
                for (auto& child : c->children_) {
                    // subtrees fully outside the viewport are skipped whole (PERF-02, symmetric with ChildHitClip);
                    // partially visible subtrees are clipped by PushClip at the D2D layer
                    if (!child->Visible() || !paintClip.Intersects(child->bounds_))
                        continue;
                    PaintTree(child.get());
                }
                painter.PopClip();
            } else {
                for (auto& child : c->children_) PaintTree(child.get());
            }
        }
        widget->OnPostPaint(painter, theme);
    }

    // ---- modal content dialog ----





    // ---- message handling ----

    static Point ToDip(LPARAM lParam, float dpi) {
        float x = static_cast<short>(LOWORD(lParam));
        float y = static_cast<short>(HIWORD(lParam));
        return { x * 96.0f / dpi, y * 96.0f / dpi };
    }

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        auto* impl = reinterpret_cast<WindowImpl*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        // ARCH58-01 (stage 1): the original single switch's 35 message cases (678 lines) were split by concern
        // into the 14 handlers below. Each handler switches again over its own message subset and returns
        // std::optional<LRESULT> — nullopt = unhandled, uniformly falls to DefWindowProc.
        // case bodies are preserved verbatim (comments included); break semantics are unchanged inside the handlers' inner switches.
        if (auto r = OnNcCreate(hwnd, impl, msg, wParam, lParam)) return *r;
        if (auto r = OnFrameMetric(hwnd, impl, msg, wParam, lParam)) return *r;
        if (auto r = OnCaptionNc(hwnd, impl, msg, wParam, lParam)) return *r;
        if (auto r = OnWindowState(hwnd, impl, msg, wParam, lParam)) return *r;
        if (auto r = OnTimerDispatch(hwnd, impl, msg, wParam, lParam)) return *r;
        if (auto r = OnCharIme(hwnd, impl, msg, wParam, lParam)) return *r;
        if (auto r = OnKeydown(hwnd, impl, msg, wParam, lParam)) return *r;
        if (auto r = OnWheel(hwnd, impl, msg, wParam, lParam)) return *r;
        if (auto r = OnSetCursor(hwnd, impl, msg, wParam, lParam)) return *r;
        if (auto r = OnContextMenu(hwnd, impl, msg, wParam, lParam)) return *r;
        if (auto r = OnPaintDpi(hwnd, impl, msg, wParam, lParam)) return *r;
        if (auto r = OnMouseMove(hwnd, impl, msg, wParam, lParam)) return *r;
        if (auto r = OnMouseButton(hwnd, impl, msg, wParam, lParam)) return *r;
        if (auto r = OnLifecycle(hwnd, impl, msg, wParam, lParam)) return *r;
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }


    // ---- concern 1/14: window creation: GWLP_USERDATA binding and hwnd registration (ARCH58-01) ----
    static std::optional<LRESULT> OnNcCreate(HWND hwnd, WindowImpl* impl,
                                        UINT msg, WPARAM wParam, LPARAM lParam) {
        (void)impl;   // this concern does not consume this parameter
        (void)wParam;   // this concern does not consume this parameter
        switch (msg) {

        case WM_NCCREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            auto* wnd = static_cast<Window*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(wnd->impl_.get()));
            wnd->impl_->hwnd = hwnd;
            break;
        }
        }
        return std::nullopt;
    }


    // ---- concern 2/14: frame metrics and hit-testing: background erase / owner-drawn client / min size / hit test / activation (ARCH58-01) ----
    static std::optional<LRESULT> OnFrameMetric(HWND hwnd, WindowImpl* impl,
                                        UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {

        case WM_ERASEBKGND:
            return 1;
        case WM_NCCALCSIZE: {
            // owner-drawn title bar: the whole window rect is the client area; when maximized, inset by the border width
            // so content is not pushed off-screen
            if (wParam && impl) {
                if (IsZoomed(hwnd)) {
                    UINT dpi = GetDpiForWindow(hwnd);
                    // SM_CXPADDEDBORDER is the only padding metric (winuser.h has no
                    // SM_CYPADDEDBORDER); using x/y together is the SDK contract, not a typo
                    int x = static_cast<int>(GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi)) +
                            GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
                    int y = static_cast<int>(GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi)) +
                            GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
                    auto* r = reinterpret_cast<RECT*>(lParam);
                    r->left += x; r->top += y; r->right -= x; r->bottom -= y;
                }
                return 0;
            }
            break;
        }
        case WM_GETMINMAXINFO: {
            // R36-04: minimum window size. With an owner-drawn frame (WM_NCCALCSIZE whole window = client area)
            // ptMinTrackSize is already client pixels; no non-client frame is added. During creation
            // GWLP_USERDATA is unset (impl null) and the system default applies; minWidth/Height
            // of 0 mean no floor, likewise allowed through
            if (impl && (impl->minWidthDip > 0 || impl->minHeightDip > 0)) {
                auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
                if (impl->minWidthDip > 0)
                    mmi->ptMinTrackSize.x = static_cast<LONG>(
                        std::lround(impl->minWidthDip * impl->dpi / 96.0f));
                if (impl->minHeightDip > 0)
                    mmi->ptMinTrackSize.y = static_cast<LONG>(
                        std::lround(impl->minHeightDip * impl->dpi / 96.0f));
                return 0;
            }
            break;
        }
        case WM_NCHITTEST: {
            if (!impl) break;
            POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            RECT rc;
            GetWindowRect(hwnd, &rc);
            UINT dpi = GetDpiForWindow(hwnd);
            int b = static_cast<int>(GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi)) +
                    GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
            if (!IsZoomed(hwnd)) {
                bool l = pt.x < rc.left + b, r = pt.x >= rc.right - b;
                bool t = pt.y < rc.top + b, btm = pt.y >= rc.bottom - b;
                if (t && l) return HTTOPLEFT;
                if (t && r) return HTTOPRIGHT;
                if (btm && l) return HTBOTTOMLEFT;
                if (btm && r) return HTBOTTOMRIGHT;
            }
            int caption = 0;
            if (impl->titlebar) {
                // hit-testing shares client coordinates with layout/draw (R33-01): normally the client origin equals
                // the window origin; when maximized WM_NCCALCSIZE insets the client origin by one border width, and
                // converting window-relative anyway would shift the whole title-bar hit band by
                // 7.2 DIP (measured HTCLOSE hits 3781..3830 vs draw [3790,3840))
                POINT org{ 0, 0 };
                MapWindowPoints(hwnd, nullptr, &org, 1);
                Point dip{ (pt.x - org.x) * 96.0f / impl->dpi,
                           (pt.y - org.y) * 96.0f / impl->dpi };
                // custom caption buttons (round-65 R1) are client islands inside the band: HTCLIENT
                // routes their clicks/moves through the widget tree (hover/press/tooltip machinery)
                if (impl->titlebar->CustomButtonAt(dip) >= 0) return HTCLIENT;
                int ht = impl->titlebar->HitTest(dip);
                // caption buttons and the badge own their draw box even where the top resize band
                // overlaps it (round-66 R6: with a 32 DIP band the buttons start at y=0, and the
                // system frame behaves the same — measured on Explorer, only the topmost strip of
                // the close button still reads HTTOP). HTCAPTION keeps deferring to the borders.
                if (ht != 0 && ht != HTCAPTION) return ht;
                caption = ht;
            }
            if (!IsZoomed(hwnd)) {
                bool l = pt.x < rc.left + b, r = pt.x >= rc.right - b;
                bool t = pt.y < rc.top + b, btm = pt.y >= rc.bottom - b;
                if (l) return HTLEFT;
                if (r) return HTRIGHT;
                if (t) return HTTOP;
                if (btm) return HTBOTTOM;
            }
            if (caption) return caption;
            return HTCLIENT;
        }
        case WM_NCACTIVATE: {
            if (impl) {
                impl->active = LOWORD(wParam) != 0;
                impl->Invalidate();
            }
            break;
        }
        }
        return std::nullopt;
    }


    // ---- concern 3/14: owner-drawn title-bar NC interaction (R32-01/02/05): hover/press/commit on release (ARCH58-01) ----
    static std::optional<LRESULT> OnCaptionNc(HWND hwnd, WindowImpl* impl,
                                        UINT msg, WPARAM wParam, LPARAM lParam) {
        (void)lParam;   // this concern does not consume this parameter
        switch (msg) {

        case WM_NCMOUSEMOVE: {
            if (impl && impl->titlebar) {
                impl->SetCaptionHover(static_cast<int>(wParam));
                // re-arm on every entry (R32-02): TME_NONCLIENT is a one-shot arm,
                // WM_NCMOUSELEAVE may be missing for a whole round; re-arm each time
                TRACKMOUSEEVENT tme{ sizeof(tme), TME_NONCLIENT, hwnd, 0 };
                TrackMouseEvent(&tme);
                return 0;
            }
            break;
        }
        case WM_NCMOUSELEAVE: {
            if (impl) {
                impl->SetCaptionHover(0);
            }
            break;
        }
        case WM_NCRBUTTONUP: {
            // round-66 R5: the caption band is HTCAPTION, so a physical right-click arrives as
            // this NC message — and DefWindowProc turns it into nothing on this OS (r107ctx trace:
            // no WM_CONTEXTMENU synthesis, no system menu), which left the round-65 takeover hook
            // unreachable from real input. Hand the caption release to the same context-menu exit
            // the pointer path uses, so TitleBar::OnContextMenuCb fires with window DIP
            // coordinates. With no callback wired the dispatch is a no-op and the release stays
            // swallowed (unchanged status quo: never a system menu). Other hit codes (HTSYSMENU
            // badge) keep default handling.
            if (impl && impl->titlebar && wParam == HTCAPTION) {
                DispatchContextMenu(impl, hwnd,
                                    POINT{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });
                return 0;
            }
            break;
        }
        case WM_NCLBUTTONDOWN: {
            // R32-01: take over pressing on the three owner-drawn title-bar buttons. Previously this fell to DefWindowProc,
            // whose NC button state machine sets its own capture and consumes the release — close got committed by it
            // (SC_CLOSE) while minimize/maximize had no response at all. Here the press state is self-managed with
            // mouse capture; the release arrives as WM_LBUTTONUP and commits (same as dialog buttons: commit only
            // when released over the same button, dragging out cancels — U-02 / R32-05). Other hit codes
            // such as HTSYSMENU still fall to the default handling (clicking the badge pops the system menu, R32-09)
            if (impl && impl->titlebar &&
                (wParam == HTMINBUTTON || wParam == HTMAXBUTTON ||
                 wParam == HTCLOSE)) {
                impl->captionPressed = static_cast<int>(wParam);
                impl->titlebar->SetCaptionPressed(impl->captionPressed);
                impl->SetCaptionHover(impl->captionPressed);   // show the pressed state while held
                SetCapture(hwnd);
                return 0;
            }
            break;
        }
        case WM_NCLBUTTONUP: {
            // fallback path: when SetCapture fails the release still arrives as an NC message (the main path is
            // WM_LBUTTONUP). Commit only for self-managed presses; the rest goes to default handling
            if (impl && impl->titlebar &&
                impl->captionPressed == static_cast<int>(wParam)) {
                int code = impl->captionPressed;
                impl->captionPressed = 0;
                impl->titlebar->SetCaptionPressed(0);
                impl->SetCaptionHover(0);
                impl->PostCaptionCommand(code);
                return 0;
            }
            break;
        }
        case WM_NCLBUTTONDBLCLK: {
            // round-65 R2.3: DefWindowProc toggles maximize/restore on caption double-click; suppressed
            // when maximization is disabled (WS_MAXIMIZEBOX removal already gates it — belt-and-braces
            // so the toggle stays dead even on the direct NC path). Other hit codes keep default
            // handling (HTSYSMENU badge double-click = close, R32-09/A13).
            if (impl && !impl->maximizable && wParam == HTCAPTION) return 0;
            break;
        }
        }
        return std::nullopt;
    }


    // ---- concern 4/14: size/position/activation: DPI write-back, menu collapse, swap-chain resize (ARCH58-01) ----
    static std::optional<LRESULT> OnWindowState(HWND hwnd, WindowImpl* impl,
                                        UINT msg, WPARAM wParam, LPARAM lParam) {
        (void)lParam;   // this concern does not consume this parameter
        switch (msg) {

        case WM_SIZE: {
            if (!impl || wParam == SIZE_MINIMIZED) break;
            impl->dpi = static_cast<float>(GetDpiForWindow(hwnd));
            impl->CloseMenus();
            RECT rc;
            GetClientRect(hwnd, &rc);
            if (rc.right > 0 && rc.bottom > 0) {
                // write the real client size back as DIP so layout sizes are correct while drag-resizing the border
                float wDip = static_cast<float>(rc.right) * 96.0f / impl->dpi;
                float hDip = static_cast<float>(rc.bottom) * 96.0f / impl->dpi;
                // R13: notify only on an actual DIP change since the last notification
                // (idempotent WM_SIZE repeats stay silent; the -1 sentinel makes the first
                // real size notify; SIZE_MINIMIZED never updates the sentinel)
                bool notifySize = impl->onSizeChanged &&
                                  (wDip != impl->lastNotifiedW ||
                                   hDip != impl->lastNotifiedH);
                impl->clientW = wDip;
                impl->clientH = hDip;
                if (impl->rt.ctx) {
                    impl->ResizeSwapChain(static_cast<UINT>(rc.right),
                                          static_cast<UINT>(rc.bottom));
                } else {
                    impl->EnsureDevice();
                }
                impl->Layout();
                // input arriving continuously during the modal resize loop starves WM_PAINT: render directly here with
                // non-blocking present so frames at the new size hit the screen with minimal latency; with the swap chain's
                // DXGI_SCALING_NONE, DWM no longer stretches the old frame in misaligned-size gaps
                impl->Render();
                if (!impl->rt.ctx) impl->Invalidate(); // fall back to invalidate-repaint when the device is down
                if (notifySize) {
                    impl->lastNotifiedW = wDip;
                    impl->lastNotifiedH = hDip;
                    impl->onSizeChanged();   // R13: layout is current here — Bounds() read fresh
                }
            }
            return 0;
        }
        case WM_MOVE: {
            if (impl) impl->CloseMenus();
            break;
        }
        case WM_ACTIVATE: {
            if (impl && LOWORD(wParam) == WA_INACTIVE) {
                impl->CloseMenus();
                // R10: the client hover chain joins the no-leak rule — the caption hover
                // clear below is R32-02; the mouse may still be over the window, state
                // rebuilds from the next WM_MOUSEMOVE (leave callbacks fire exactly once)
                impl->ClearHoverChain();
                impl->SetFocused(nullptr);
                // R32-02: deactivation clears title-bar hover/press; no state leaks across activations
                impl->SetCaptionHover(0);
                if (impl->captionPressed) {
                    impl->captionPressed = 0;
                    if (impl->titlebar) impl->titlebar->SetCaptionPressed(0);
                }
            }
            break;
        }
        }
        return std::nullopt;
    }


    // ---- concern 5/14: timers: caret / animation (PERF-07) / title-bar safety net / tooltip / tick (ARCH58-01) ----
    static std::optional<LRESULT> OnTimerDispatch(HWND hwnd, WindowImpl* impl,
                                        UINT msg, WPARAM wParam, LPARAM lParam) {
        (void)lParam;   // this concern does not consume this parameter
        switch (msg) {

        case WM_TIMER: {
            if (!impl) break;
            if (wParam == kCaretTimerId) {
                if (impl->focused) impl->focused->OnTimer();
                return 0;
            }
            if (wParam == kAnimTimerId) {
                // PERF-07: reverse in-place walk replaces a whole-list copy per frame. OnAnimate may
                // Start (append at tail, not dispatched this round) / Stop (erase; reverse order never skips elements);
                // in reverse order another's erase shifts at most one already-visited element left; missing one dispatch frame is harmless
                for (size_t i = impl->animators.size(); i-- > 0;) {
                    Widget* w = impl->animators[i];
                    if (w->window_ && w->window_->impl_ && w->window_->impl_->hwnd)
                        w->OnAnimate();
                }
                return 0;
            }
            if (wParam == kCapTimerId) {
                // R32-02 safety net: while hover is non-zero, verify the cursor and clear it when off the button
                // (the leave notification may be missing for a whole round, see WM_NCMOUSEMOVE)
                if (impl->titlebar) {
                    int hover = impl->titlebar->CaptionHover();
                    if (hover != 0) {
                        POINT sp;
                        if (GetCursorPos(&sp)) {
                            MapWindowPoints(nullptr, hwnd, &sp, 1);
                            Point dip{ sp.x * 96.0f / impl->dpi,
                                       sp.y * 96.0f / impl->dpi };
                            if (impl->titlebar->HitTest(dip) != hover)
                                impl->SetCaptionHover(0);
                        }
                    } else {
                        KillTimer(hwnd, kCapTimerId);
                    }
                }
                return 0;
            }
            if (wParam == kTipTimerId) {
                KillTimer(hwnd, kTipTimerId);
                if (impl->hovered && !impl->hovered->ToolTip().empty() &&
                    !impl->tooltip) {
                    impl->tooltip = std::make_unique<detail::ToolTipPopup>(
                        hwnd, impl->dpi, impl->theme, impl->hovered->Bounds(),
                        impl->hovered->ToolTip());
                }
                return 0;
            }
            if (wParam == kTickTimerId) {
                if (impl->onTick) impl->onTick();
                return 0;
            }
            break;
        }
        }
        return std::nullopt;
    }


    // ---- concern 6/14: character input and IME anchoring (R35-03) (ARCH58-01) ----
    static std::optional<LRESULT> OnCharIme(HWND hwnd, WindowImpl* impl,
                                        UINT msg, WPARAM wParam, LPARAM lParam) {
        (void)hwnd;   // this concern does not consume this parameter
        (void)lParam;   // this concern does not consume this parameter
        switch (msg) {

        case WM_CHAR: {
            if (impl) {
                if (impl->dialogActive) return 0;
                if (impl->FlyoutAlive()) {
                    // input popups (AutoSuggest): character keys pass through when owned by the current focused widget,
                    // otherwise keep "popup open swallows characters" (round-7 F-01)
                    if (!impl->focused || impl->flyout->OwnerWidget() != impl->focused)
                        return 0;
                }
                if (impl->focused) {
                    impl->focused->OnChar(static_cast<wchar_t>(wParam));
                    return 0;
                }
            }
            break;
        }
        case WM_IME_SETCONTEXT: {
            // R35-03: anchor the composition window on gaining focus. Must still call DefWindowProc —
            // swallowing this message keeps the composition window from showing
            if (impl) impl->UpdateImeAnchor();
            break;
        }
        case WM_IME_STARTCOMPOSITION:
        case WM_IME_COMPOSITION: {
            // during composition characters are uncommitted and the insertion point does not move; re-anchor on every
            // composition start to cover IMEs that re-query position per key. Must also reach DefWindowProc to continue the GCS_* → WM_CHAR chain
            if (impl) impl->UpdateImeAnchor();
            break;
        }
        }
        return std::nullopt;
    }


    // ---- concern 7/14: keyboard navigation: dialog footer (GAP38-01) / accelerators / popups / focus / scroll forwarding (ARCH58-01) ----
    static std::optional<LRESULT> OnKeydown(HWND hwnd, WindowImpl* impl,
                                        UINT msg, WPARAM wParam, LPARAM lParam) {
        (void)lParam;   // this concern does not consume this parameter
        switch (msg) {

        case WM_KEYDOWN: {
            if (impl) {
                // keys during IME composition arrive as VK_PROCESSKEY(0xE5) (R35-03): characters
                // are handled by the WM_IME_* chain and must not reach OnKeyDown
                if (wParam == VK_PROCESSKEY) return 0;
                g_focusFromKeyboard = true;   // only keyboard navigation draws the focus ring (:focus-visible)
                if (impl->dialogActive) {
                    // GAP38-01: footer button keyboard path — Tab/←→ move focus between the two buttons,
                    // Enter/Space commit the focused button, Esc cancels. The baseline footer uses native
                    // <button>s; previously everything but Esc was swallowed, leaving dialogs without a keyboard commit path
                    if (wParam == VK_ESCAPE) {
                        impl->CloseDialog(false);
                    } else if (wParam == VK_TAB || wParam == VK_LEFT ||
                               wParam == VK_RIGHT) {
                        g_focusFromKeyboard = true;
                        // GAP50-08: when primary is empty the primary button is not drawn or visible, and focus
                        // stays on the secondary (same intent as the :6248 focus initial value and the render skip)
                        impl->dialogFocus =
                            impl->dlgPrimary.empty() ? 1
                            : (impl->dialogFocus == 0 &&
                               !impl->dlgSecondary.empty()) ? 1 : 0;
                        impl->Invalidate();
                    } else if (wParam == VK_RETURN || wParam == VK_SPACE) {
                        impl->CloseDialog(impl->dialogFocus == 0);
                    }
                    return 0;
                }
                // API-11: accelerators route before the focused widget
                if (impl->CheckAccelerators(static_cast<uint32_t>(wParam))) return 0;
                if (impl->FlyoutAlive()) {
                    if (wParam == VK_ESCAPE) {
                        impl->CloseMenus();
                        return 0;
                    }
                    // input popups (AutoSuggest): list navigation keys go to the popup, editing keys go to the text box
                    if (impl->focused && impl->flyout->OwnerWidget() == impl->focused) {
                        if (wParam == VK_DOWN || wParam == VK_UP || wParam == VK_RETURN) {
                            impl->flyout->KeyNav(static_cast<uint32_t>(wParam));
                            return 0;
                        }
                        if (wParam == VK_TAB) {
                            impl->CloseMenus();
                            impl->CycleFocus((GetKeyState(VK_SHIFT) & 0x8000) != 0);
                            return 0;
                        }
                        impl->focused->OnKeydown(static_cast<uint32_t>(wParam));
                        return 0;
                    }
                    // ordinary popups: arrows/Tab/Enter/Space navigate, the rest are swallowed
                    impl->flyout->KeyNav(static_cast<uint32_t>(wParam));
                    return 0;
                }
                // R36-03: keyboard context-menu gestures (Apps key / Shift+F10). The mouse
                // path already fires from WM_RBUTTONUP and DefWindowProc no longer synthesizes
                // WM_CONTEXTMENU; synthesize explicitly here through the unified exit (anchored at the focused widget)
                if (wParam == VK_APPS ||
                    (wParam == VK_F10 && (GetKeyState(VK_SHIFT) & 0x8000) != 0)) {
                    SendMessageW(hwnd, WM_CONTEXTMENU, (WPARAM)hwnd, (LPARAM)-1);
                    return 0;
                }
                if (wParam == VK_TAB) {
                    impl->CycleFocus((GetKeyState(VK_SHIFT) & 0x8000) != 0);
                    return 0;
                }
                if (impl->focused) {
                    impl->focused->OnKeydown(static_cast<uint32_t>(wParam));
                    return 0;
                }
                // no focused widget (initial state / after clicking the content area clears focus): scroll keys forward to the outermost
                // ScrollViewer (R27-02; the baseline keeps routing keys to .content scrolling when activeElement=BODY).
                // With a focused widget, no forwarding — ScrollViewer's
                // OnKeydown is already in the Tab order and other widgets handle their own keys
                switch (wParam) {
                case VK_PRIOR: case VK_NEXT:
                case VK_UP: case VK_DOWN: case VK_HOME: case VK_END:
                    if (ScrollViewer* sv = impl->RootScroller())
                        sv->OnKeydown(static_cast<uint32_t>(wParam));
                    return 0;
                }
            }
            break;
        }
        }
        return std::nullopt;
    }


    // ---- concern 8/14: wheel: bubble along the parent chain (ARCH58-01) ----
    static std::optional<LRESULT> OnWheel(HWND hwnd, WindowImpl* impl,
                                        UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {

        case WM_MOUSEWHEEL: {
            if (!impl) break;
            float delta = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) /
                          static_cast<float>(WHEEL_DELTA);
            POINT spt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &spt);
            Point pos{ spt.x * 96.0f / impl->dpi, spt.y * 96.0f / impl->dpi };
            Widget* target = impl->captured ? impl->captured : impl->HitTest(pos);
            // bubble up the parent chain until some layer handles it (e.g. ScrollViewer)
            for (Widget* w = target; w; w = w->parent_) {
                if (w->OnWheel(delta)) return 0;
            }
            return 0;
        }
        }
        return std::nullopt;
    }


    // ---- concern 9/14: client-area cursor self-report (R36-01) (ARCH58-01) ----
    static std::optional<LRESULT> OnSetCursor(HWND hwnd, WindowImpl* impl,
                                        UINT msg, WPARAM wParam, LPARAM lParam) {
        (void)wParam;   // this concern does not consume this parameter
        switch (msg) {

        case WM_SETCURSOR: {
            // R36-01: the client cursor is self-reported by the hit widget (text input = I-beam), the rest use
            // the default arrow; the non-client area goes to DefWindowProc — title drag / resize borders /
            // HTSYSMENU keep their system cursor semantics. IDC_* expansion follows UNICODE, in the same
            // family as LoadCursorW; M-23's LoadCursorA applies only to the ctypes path
            if (impl && LOWORD(lParam) == HTCLIENT) {
                POINT pt{};
                GetCursorPos(&pt);
                ScreenToClient(hwnd, &pt);
                Point pos{ pt.x * 96.0f / impl->dpi, pt.y * 96.0f / impl->dpi };
                Widget* w = impl->HitTest(pos);
                const wchar_t* cur = w ? w->CursorForClient() : nullptr;
                SetCursor(LoadCursorW(nullptr, cur ? cur : IDC_ARROW));
                return TRUE;
            }
            break;
        }
        }
        return std::nullopt;
    }


    // ---- concern 10/14: right-click and context-menu unified exit (R36-03) (ARCH58-01) ----

    // Shared dispatch for the WM_CONTEXTMENU pointer branch and the WM_NCRBUTTONUP caption branch
    // (round-66 R5): convert a screen point to window DIP, hit-test the widget tree and dispatch.
    // Returns true when a widget took the menu (caller returns "handled"); false = default handling.
    static bool DispatchContextMenu(WindowImpl* impl, HWND hwnd, POINT spt) {
        if (!impl) return false;
        ScreenToClient(hwnd, &spt);
        Point pos{ spt.x * 96.0f / impl->dpi, spt.y * 96.0f / impl->dpi };
        if (Widget* widget = impl->HitTest(pos)) {
            widget->OnContextMenu(pos);
            return true;
        }
        return false;
    }

    static std::optional<LRESULT> OnContextMenu(HWND hwnd, WindowImpl* impl,
                                        UINT msg, WPARAM wParam, LPARAM lParam) {
        (void)wParam;   // this concern does not consume this parameter
        switch (msg) {

        case WM_RBUTTONUP: {
            if (!impl) break;
            Point pos = ToDip(lParam, impl->dpi);
            if (Widget* widget = impl->HitTest(pos)) widget->OnContextMenu(pos);
            return 0;
        }
        case WM_CONTEXTMENU: {
            // R36-03: unified context-menu exit. The pointer path already fires from WM_RBUTTONUP
            // (not via DefWindowProc, so no double synthesis); this branch serves the keyboard gesture —
            // Apps key / Shift+F10 arrive at (-1,-1) and fall back to anchoring at the focused widget
            // (text-input widgets anchor at the caret via ImeAnchorPoint).
            if (!impl) break;
            if (lParam == -1) {
                if (impl->focused) {
                    Point a;
                    if (!impl->focused->ImeAnchorPoint(a))
                        a = { impl->focused->bounds_.x,
                              impl->focused->bounds_.y + impl->focused->bounds_.h };
                    impl->focused->OnContextMenu(a);
                    return 0;
                }
                // round-66 R5 (the consumer's preferred option): with nothing focused the keyboard
                // gesture still reaches the tree, anchored at the cursor — this also makes the
                // title-bar takeover reachable without any focusable widget. No widget under the
                // cursor: default handling, as before.
                POINT pt{};
                if (GetCursorPos(&pt) && DispatchContextMenu(impl, hwnd, pt)) return 0;
                break;
            }
            if (DispatchContextMenu(impl, hwnd,
                                    POINT{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) }))
                return 0;
            break;   // nothing to take over (e.g. a system-generated NC right-click): default handling
        }
        }
        return std::nullopt;
    }


    // ---- concern 11/14: painting and DPI change (ARCH39-07 / round-35 todo ②) (ARCH58-01) ----
    static std::optional<LRESULT> OnPaintDpi(HWND hwnd, WindowImpl* impl,
                                        UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            (void)hdc;
            if (impl) {
                impl->EnsureDevice(); // after device loss (including EndDraw error recovery), rebuild here
                impl->Render();
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DPICHANGED: {
            if (!impl) break;
            impl->dpi = static_cast<float>(HIWORD(wParam));
            impl->CloseMenus();
            auto* suggested = reinterpret_cast<RECT*>(lParam);
            // ARCH39-07: the success branch also clears the error channel — after one failure rt.ctx is ready,
            // EnsureDevice early-outs and never reaches CreateRenderTarget's success cleanup ⇒ sticky error
            if (SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                             suggested->right - suggested->left,
                             suggested->bottom - suggested->top,
                             SWP_NOZORDER | SWP_NOACTIVATE))
                detail::SetError("");
            else
                detail::SetError("WM_DPICHANGED SetWindowPos failed");   // ARCH38-04
            // round-35 todo ②: the cache key (iconLetter/iconKey*) excludes DPI; after cross-DPI drags the keys must be
            // cleared first or RefreshIcon early-outs because letter+accent are unchanged;
            // user icons (iconUser) are protected by RefreshIcon's own early-out
            impl->iconLetter.clear();
            impl->RefreshIcon();
            if (impl->rt.ctx) {
                RECT rc;
                GetClientRect(hwnd, &rc);
                impl->clientW = static_cast<float>(rc.right) * 96.0f / impl->dpi;
                impl->clientH = static_cast<float>(rc.bottom) * 96.0f / impl->dpi;
                // DPI change where the pixel size did not change: rebind the target to refresh the context SetDpi;
                // if SetWindowPos already triggered WM_SIZE, this is just an idempotent repeat
                impl->ResizeSwapChain(static_cast<UINT>(rc.right),
                                      static_cast<UINT>(rc.bottom));
                impl->Layout();
                impl->Invalidate();
            }
            if (impl->onDpiChanged) impl->onDpiChanged(impl->dpi);   // API-12
            return 0;
        }
        }
        return std::nullopt;
    }


    // ---- concern 12/14: pointer move: hover chain / container highlight / dialog hover / tooltip (ARCH58-01) ----
    static std::optional<LRESULT> OnMouseMove(HWND hwnd, WindowImpl* impl,
                                        UINT msg, WPARAM wParam, LPARAM lParam) {
        (void)wParam;   // this concern does not consume this parameter
        switch (msg) {

        case WM_MOUSEMOVE: {
            if (!impl) break;
            Point pos = ToDip(lParam, impl->dpi);
            if (impl->captionPressed && impl->titlebar) {
                // title-bar button press-drag (R32-01/05): moves during capture arrive as client messages;
                // show the pressed state only inside the held button, cancel the visuals when dragged out
                int ht = impl->titlebar->HitTest(pos);
                impl->SetCaptionHover(ht == impl->captionPressed ? ht : 0);
                return 0;
            }
            if (impl->titlebar && impl->titlebar->CaptionHover() != 0)
                impl->SetCaptionHover(0);   // entering the client area clears title-bar hover (R32-02)
            Widget* target = impl->captured ? impl->captured : impl->HitTest(pos);
            Widget* prevHovered = impl->hovered;
            if (impl->hovered != target) {
                if (impl->hovered) impl->hovered->OnMouseLeave();
                impl->hovered = target;
                impl->CloseTooltip();
                if (target && !target->ToolTip().empty()) {
                    // baseline [data-tip]:hover appears immediately (L-08: no more 600ms delay)
                    PostMessageW(hwnd, WM_TIMER, kTipTimerId, 0);
                }
            }
            if (target) target->OnMouseMove(pos);
            impl->UpdateContainerHover(prevHovered, target);   // container hover highlight
            if (impl->dialogActive) {
                int hover = -1;
                for (int i = 0; i < 2; ++i) {
                    // GAP52-01: an empty-text button is not drawn and not visible; hover must not point at it
                    // (same predicate as the press path, symmetric for primary/secondary, M-98)
                    if ((i == 0 ? impl->dlgPrimary : impl->dlgSecondary).empty())
                        continue;
                    if (impl->DialogButtonRect(i).Contains(pos.x, pos.y))
                        hover = i;
                }
                if (impl->dialogHover != hover) {
                    impl->dialogHover = hover;
                    impl->Invalidate();
                }
            }
            if (!impl->trackingMouse) {
                TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
                TrackMouseEvent(&tme);
                impl->trackingMouse = true;
            }
            return 0;
        }
        case WM_MOUSELEAVE: {
            if (!impl) break;
            impl->trackingMouse = false;
            impl->ClearHoverChain();   // R10: shared with the deactivation path
            return 0;
        }
        }
        return std::nullopt;
    }


    // ---- concern 13/14: press / double-click / release / capture lost (U-02, ARCH39-01, round-15 §3.15) (ARCH58-01) ----
    static std::optional<LRESULT> OnMouseButton(HWND hwnd, WindowImpl* impl,
                                        UINT msg, WPARAM wParam, LPARAM lParam) {
        (void)wParam;   // this concern does not consume this parameter
        switch (msg) {

        case WM_LBUTTONDOWN: {
            if (!impl) break;
            g_focusFromKeyboard = false;   // mouse clicks setting focus do not draw the focus ring
            Point pos = ToDip(lParam, impl->dpi);
            if (impl->dialogActive) {
                // modal: a button press only records pressed (commit on release over the same button —
                // baseline click semantics: dragging out and releasing cancels, round-17 U-02);
                // scrim clicks count as cancel, the rest are swallowed
                // GAP50-08/GAP52-01: empty-text buttons are not drawn and not visible (the render loop
                // continues over empty text); clicks must not fire their callbacks — symmetric for primary/secondary (M-98)
                if (!impl->dlgPrimary.empty() &&
                    impl->DialogButtonRect(0).Contains(pos.x, pos.y)) {
                    impl->dialogPressed = 0;
                    SetCapture(hwnd);
                } else if (!impl->dlgSecondary.empty() &&
                           impl->DialogButtonRect(1).Contains(pos.x, pos.y)) {
                    impl->dialogPressed = 1;
                    SetCapture(hwnd);
                } else if (!impl->dialogRect.Contains(pos.x, pos.y)) {
                    impl->CloseDialog(false);
                }
                impl->Invalidate();
                return 0;
            }
            if (Widget* widget = impl->HitTest(pos)) {
                impl->captured = widget;
                SetCapture(hwnd);
                int cc = impl->BumpClickCount(pos);
                // R11: the multi-click callback may legitimately move focus (begin-edit
                // flows); the click-focus defaults below apply only when it left focus alone
                Widget* focusBefore = impl->focused;
                widget->OnMouseDown(pos);
                // from the 3rd click on, the system may stop sending WM_LBUTTONDBLCLK (plain DOWN instead);
                // own counting synthesizes the multi-click event (triple-click select-all etc.)
                if (cc >= 3) {
                    widget->OnDoubleClick(pos, cc);
                    if (widget->OnDoubleClickCb) widget->OnDoubleClickCb(pos, cc);   // R11
                }
                // custom caption buttons (round-65 R1) are caption-like: pressing them changes no
                // keyboard focus at all (native caption semantics — no focus steal, no focus clear)
                bool captionButton = widget == impl->titlebar &&
                                     impl->titlebar->CustomButtonAt(pos) >= 0;
                // focus follows the click: clicking a non-focused widget clears focus. window_ guard (ARCH39-01):
                // the OnMouseDown callback may already have Removed itself — DetachTree has run,
                // window_ is nulled; a detached widget must not become focused, otherwise after end-of-frame destruction
                // focused dangles (next frame's OnKeydown/OnUnfocused dereference freed memory)
                if (!captionButton && impl->focused == focusBefore) {   // R11: focus moved inside the callback is respected
                    if (widget->window_ && widget->Focusable()) impl->SetFocused(widget);
                    else if (impl->focused) impl->SetFocused(nullptr);
                }
            } else if (impl->focused) {
                // ARCH-08: clicking empty space also clears keyboard focus — otherwise the caret
                // keeps blinking in the blank area and keys keep routing to the widget that was clicked away from
                impl->SetFocused(nullptr);
            }
            return 0;
        }
        case WM_LBUTTONDBLCLK: {
            // double-click routing (R27-03): same hit/capture/focus path as single click, plus the multi-click event
            if (!impl) break;
            g_focusFromKeyboard = false;   // mouse clicks setting focus do not draw the focus ring
            Point pos = ToDip(lParam, impl->dpi);
            if (impl->dialogActive) return 0;   // no extra semantics for double clicks while modal
            if (Widget* widget = impl->HitTest(pos)) {
                impl->captured = widget;
                SetCapture(hwnd);
                int cc = impl->BumpClickCount(pos);
                // R11: a double-click callback may legitimately move focus (begin-edit flows —
                // hide the label, show and focus a text box); the click-focus defaults below
                // apply only when the callback left focus alone
                Widget* focusBefore = impl->focused;
                widget->OnMouseDown(pos);
                widget->OnDoubleClick(pos, cc);
                if (widget->OnDoubleClickCb) widget->OnDoubleClickCb(pos, cc);   // R11
                // round-65 R1: custom caption buttons change no keyboard focus (same as the
                // single-click path above)
                if (!(widget == impl->titlebar &&
                      impl->titlebar->CustomButtonAt(pos) >= 0) &&
                    impl->focused == focusBefore) {
                    // ARCH39-01: same as the single-click path — after a callback Removes itself, focus must not be set
                    if (widget->window_ && widget->Focusable()) impl->SetFocused(widget);
                    else if (impl->focused) impl->SetFocused(nullptr);
                }
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            if (!impl) break;
            Point pos = ToDip(lParam, impl->dpi);
            if (impl->captionPressed) {
                // title-bar button commits on release (R32-01): only when still over the same button,
                // dragging out cancels (same semantics as dialog buttons, U-02)
                int pressed = impl->captionPressed;
                impl->captionPressed = 0;
                if (impl->titlebar) impl->titlebar->SetCaptionPressed(0);
                int ht = impl->titlebar ? impl->titlebar->HitTest(pos) : 0;
                bool commit = ht == pressed;
                ReleaseCapture();   // already cleared in WM_CAPTURECHANGED; do not loop back
                impl->SetCaptionHover(commit ? 0 : ht);
                if (commit) impl->PostCaptionCommand(pressed);
                impl->Invalidate();
                return 0;
            }
            if (impl->dialogPressed >= 0) {
                // commit only when released still inside the same button (baseline click; dragging out cancels)
                int pressed = impl->dialogPressed;
                impl->dialogPressed = -1;
                ReleaseCapture();   // dialogPressed already cleared in WM_CAPTURECHANGED; do not loop back
                if (impl->dialogActive &&
                    impl->DialogButtonRect(pressed).Contains(pos.x, pos.y))
                    impl->CloseDialog(pressed == 0);
                impl->Invalidate();
                return 0;
            }
            if (impl->captured) {
                Widget* widget = impl->captured;
                impl->captured = nullptr;
                ReleaseCapture();
                widget->OnMouseUp(pos);
            }
            return 0;
        }
        case WM_CAPTURECHANGED: {
            // Alt+Tab / system popups steal capture: this window never sees WM_LBUTTONUP,
            // so notify the original widget to cancel its pressed state, else pressed_ sticks (round-15 §3.15)
            if (impl && impl->captured) {
                Widget* widget = impl->captured;
                impl->captured = nullptr;
                widget->OnCaptureLost();
            }
            // dialog button pressed states are cleared too (U-02, same reasoning as §3.15; no commit fires)
            if (impl && impl->dialogPressed >= 0) {
                impl->dialogPressed = -1;
                impl->Invalidate();
            }
            // title-bar button pressed state likewise (R32-01: Alt+Tab etc. stealing capture cancels)
            if (impl && impl->captionPressed) {
                impl->captionPressed = 0;
                if (impl->titlebar) impl->titlebar->SetCaptionPressed(0);
                impl->Invalidate();
            }
            return 0;
        }
        }
        return std::nullopt;
    }


    // ---- concern 14/14: close interception (API-10) / system theme (R37-03) / SYS accelerators / destroy reclaim (GAP48-01) (ARCH58-01) ----
    static std::optional<LRESULT> OnLifecycle(HWND hwnd, WindowImpl* impl,
                                        UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {

        case WM_CLOSE: {
            // API-10: close interception — the system close button / Alt+F4 / SC_CLOSE all converge on
            // WM_CLOSE; returning false cancels the close
            if (impl && impl->onClosing && !impl->onClosing()) return 0;
            DestroyWindow(hwnd);
            return 0;
        }
        case WM_SYSCOMMAND: {
            // round-65 R2.3: with maximization disabled every SC_MAXIMIZE delivery path is swallowed
            // (title-bar command, Win+Up hotkey, system-menu entry, programmatic posts); all other
            // system commands (SC_MINIMIZE / SC_RESTORE / SC_CLOSE / SC_KEYMENU...) fall through to
            // default handling unchanged. wParam's low 4 bits are internal (SDK contract)
            if (impl && !impl->maximizable &&
                (static_cast<unsigned long>(wParam) & 0xFFF0) == SC_MAXIMIZE)
                return 0;
            break;
        }
        case WM_SETTINGCHANGE: {
            // R37-03: system theme switches (ImmersiveColorSet) are handed to the user via callback;
            // whether/how to follow and re-resolve is the user's decision; other settings changes go to default handling
            if (impl && lParam &&
                CompareStringOrdinal(reinterpret_cast<LPCWCH>(lParam), -1,
                                     L"ImmersiveColorSet", -1, TRUE) == CSTR_EQUAL &&
                impl->onSystemThemeChanged)
                impl->onSystemThemeChanged();
            break;
        }
        case WM_SYSKEYDOWN: {
            // API-11: accelerators involving Alt take the SYS path, swallowed on hit; the rest go to default
            if (impl && impl->CheckAccelerators(static_cast<uint32_t>(wParam)))
                return 0;
            break;
        }
        case WM_DESTROY: {
            if (impl) {
                impl->hwnd = nullptr;
                // library-generated default icons die with the window (user icons belong to the user, R34-02).
                // Since GAP48-01/PERF48-01 all baked icons are owned by iconCache_ and reclaimed here
                // in one place; generatedIcon* are just aliases and no longer destroyed separately.
                for (auto& e : impl->iconCache_) DestroyIcon(e.icon);
                impl->iconCache_.clear();
                impl->generatedIconBig = impl->generatedIconSmall = nullptr;
                for (size_t i = 0; i < g_windows.size(); ++i) {
                    if (g_windows[i] == impl->owner) {
                        g_windows.erase(g_windows.begin() + i);
                        break;
                    }
                }
                // ARCH-09: do not post WM_QUIT here — posting during destruction leaves a stale
                // WM_QUIT; a window created afterwards whose Run() starts would exit instantly. Exit is decided
                // by Run() counting windows instead
            }
            break;
        }
        }
        return std::nullopt;
    }

};

} // namespace detail

// ---------------------------------------------------------------------------
// widget implementations
// ---------------------------------------------------------------------------

/// Keyboard focus indicator: a 2px stroke around the widget's outer edge (baseline :focus-visible outline 2px +
/// outline-offset 1px; drawn outside the widget so it stays visible over accent fills). The input mode
/// (g_focusFromKeyboard) is maintained by WindowImpl on WM_KEYDOWN / mouse-down.
/// Composite widgets (Tabs/Expander) use DrawFocusRingRect to place the ring on the truly focused
/// sub-region (active tab / header) instead of the whole widget (B-06).
inline void DrawFocusRingRect(const Widget* w, const Rect& area, Painter& p,
                              const Theme& t) {
    if (w->Focused() && w->Enabled() && detail::g_focusFromKeyboard)
        p.StrokeRoundedRect({ area.x - 2, area.y - 2, area.w + 4, area.h + 4 },
                            5.0f, t.accentText, 2.0f);
}

inline void DrawFocusRing(const Widget* w, Painter& p, const Theme& t) {
    DrawFocusRingRect(w, w->Bounds(), p, t);
}

void Widget::SetBounds(const Rect& bounds) {
    if (bounds.x == bounds_.x && bounds.y == bounds_.y &&
        bounds.w == bounds_.w && bounds.h == bounds_.h) return;
    bounds_ = bounds;
    Invalidate();
}

Widget& Widget::SetVisible(bool visible) {
    if (visible_ == visible) return *this;
    visible_ = visible;
    // docked bars and layout containers both depend on visibility; relayout uniformly
    Relayout();
    Invalidate();

    return *this;
}

Widget& Widget::SetEnabled(bool enabled) {
    if (enabled_ == enabled) return *this;
    enabled_ = enabled;
    Relayout();   // content-based sizing may change with the disabled style
    Invalidate();
    return *this;
}

Widget& Widget::SetToolTip(std::wstring_view text) {
    tooltip_.assign(text.begin(), text.end());
    Invalidate();
    return *this;
}

void Widget::StartAnimation() {
    if (window_ && window_->impl_) window_->impl_->AddAnimator(this);
}

void Widget::StopAnimation() {
    if (window_ && window_->impl_) window_->impl_->RemoveAnimator(this);
}

void Widget::Relayout() {
    if (window_ && window_->impl_) window_->impl_->RequestLayout();
}

Widget& Widget::SetFocus() {
    if (window_ && window_->impl_ && Focusable()) window_->impl_->SetFocused(this);

    return *this;
}

bool Widget::Focused() const {
    return window_ && window_->impl_ && window_->impl_->focused == this;
}

// ---------------------------------------------------------------------------
// title bar implementation
// ---------------------------------------------------------------------------

float TitleBar::DockHeight() const { return height_; }

TitleBar& TitleBar::SetHeight(float height) {
    // buttons keep their 32 DIP box: a band shorter than that would clip them
    height_ = std::max(kCapH, height);
    Relayout();   // the dock band, button rects, badge and title all derive from the height
    return *this;
}

TitleBar& TitleBar::SetToggledFillVisible(bool visible) {
    if (toggledFill_ != visible) {
        toggledFill_ = visible;
        Invalidate();
    }

    return *this;
}

TitleBar& TitleBar::SetCaptionInset(float insetDip) {
    if (captionInsetOverride_ != insetDip) {
        captionInsetOverride_ = insetDip;
        // the whole row shifts: stale hover/press codes may point at geometry that moved
        // (same clearing SetSystemButtons does for vanished buttons)
        if (window_ && window_->impl_) window_->impl_->captionPressed = 0;
        captionPressed_ = 0;
        customHover_ = -1;
        customPressed_ = -1;
        SetCaptionHover(0);
        Invalidate();
    }

    return *this;
}

TitleBar& TitleBar::SetText(std::wstring_view text) {
    text_.assign(text.begin(), text.end());
    Invalidate();

    return *this;
}

bool TitleBar::WindowActive() const {
    return window_ && window_->impl_ && window_->impl_->active;
}

TitleBar& TitleBar::SetCaptionHover(int ht) {
    if (captionHover_ != ht) {
        captionHover_ = ht;
        Invalidate();
    }

    return *this;
}

TitleBar& TitleBar::SetCaptionPressed(int ht) {
    if (captionPressed_ != ht) {
        captionPressed_ = ht;
        Invalidate();
    }

    return *this;
}

bool TitleBar::WindowMaximizable() const {
    return !window_ || !window_->impl_ || window_->impl_->maximizable;
}

TitleBar& TitleBar::SetBadgeVisible(bool visible) {
    if (badgeVisible_ != visible) {
        badgeVisible_ = visible;
        Invalidate();
    }

    return *this;
}

TitleBar& TitleBar::SetSystemButtons(WindowButtons buttons) {
    if (systemButtons_ != buttons) {
        systemButtons_ = buttons;
        // a button that vanishes must not keep stale hover/press visuals (same clearing the
        // WM_CAPTURECHANGED path does for stolen presses)
        if (window_ && window_->impl_) window_->impl_->captionPressed = 0;
        captionPressed_ = 0;
        SetCaptionHover(0);
        Invalidate();
    }

    return *this;
}

int TitleBar::AddButton(const Icon& icon, std::function<void()> onClick) {
    CaptionButton b;
    b.icon = icon;
    b.onClick = std::move(onClick);
    customButtons_.push_back(std::move(b));
    Invalidate();

    return static_cast<int>(customButtons_.size()) - 1;
}

int TitleBar::AddToggleButton(const Icon& icon, std::function<void(bool)> onToggled) {
    CaptionButton b;
    b.icon = icon;
    b.toggle = true;
    b.onToggled = std::move(onToggled);
    customButtons_.push_back(std::move(b));
    Invalidate();

    return static_cast<int>(customButtons_.size()) - 1;
}

bool TitleBar::Toggled(int index) const {
    return index >= 0 && index < static_cast<int>(customButtons_.size()) &&
           customButtons_[index].toggle && customButtons_[index].on;
}

TitleBar& TitleBar::SetToggled(int index, bool on) {
    if (index >= 0 && index < static_cast<int>(customButtons_.size()) &&
        customButtons_[index].toggle && customButtons_[index].on != on) {
        customButtons_[index].on = on;
        Invalidate();
    }

    return *this;
}

TitleBar& TitleBar::SetButtonToolTip(int index, std::wstring_view text) {
    if (index >= 0 && index < static_cast<int>(customButtons_.size())) {
        customButtons_[index].tooltip.assign(text.begin(), text.end());
        // a live hover on the edited button must follow the new text immediately (the popup
        // snapshots the widget's tooltip text when created)
        if (customHover_ == index && customPressed_ < 0 && window_ && window_->impl_) {
            SetToolTip(text);
            if (window_->impl_->tooltip) window_->impl_->CloseTooltip();
            if (!customButtons_[index].tooltip.empty() && window_->impl_->hwnd)
                PostMessageW(window_->impl_->hwnd, WM_TIMER, detail::kTipTimerId, 0);
        }
    }

    return *this;
}

const std::wstring& TitleBar::ButtonToolTip(int index) const {
    static const std::wstring kEmpty;
    if (index >= 0 && index < static_cast<int>(customButtons_.size()))
        return customButtons_[index].tooltip;
    return kEmpty;
}

/// Effective right-edge inset of the caption button row (DIP): an explicit value set via
/// SetCaptionInset wins in every window state; otherwise the automatic value — normally one
/// border width (R32-07) so the rightmost b DIP of the band stay the HTRIGHT resize zone and
/// the buttons' hit box and draw box coincide, and 0 while maximized (the row hugs the
/// client's right edge, consistent with the system) or before the window exists.
float TitleBar::CaptionInset() const {
    if (captionInsetOverride_ >= 0.0f) return captionInsetOverride_;
    if (window_ && window_->impl_ && window_->impl_->hwnd &&
        !IsZoomed(window_->impl_->hwnd)) {
        UINT dpi = GetDpiForWindow(window_->impl_->hwnd);
        int b = static_cast<int>(GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi)) +
                GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
        return b * 96.0f / window_->impl_->dpi;
    }
    return 0.0f;
}

/// Number of visible system buttons (bit population of the Minimize/Maximize/Close flags).
int TitleBar::SystemSlotCount() const {
    unsigned v = static_cast<unsigned>(systemButtons_);
    return static_cast<int>((v & 1u) + ((v >> 1) & 1u) + ((v >> 2) & 1u));
}

/// Caption button rect (window DIP coordinates). System buttons: index = slot of the visible ones,
/// left-to-right in Minimize/Maximize/Close order, rightmost slot hugging the border inset.
Rect TitleBar::ButtonRect(int index) const {
    float y = bounds_.y + (DockHeight() - kCapH) / 2.0f;
    int count = SystemSlotCount();
    return { bounds_.x + bounds_.w - CaptionInset() - (count - index) * kCapW, y, kCapW, kCapH };
}

/// Custom caption button rect: continues leftward from the system block (or from the border inset
/// when no system button is visible); insertion order = left-to-right.
Rect TitleBar::CustomButtonRect(int index) const {
    float y = bounds_.y + (DockHeight() - kCapH) / 2.0f;
    float blockLeft = bounds_.x + bounds_.w - CaptionInset() - SystemSlotCount() * kCapW;
    int n = static_cast<int>(customButtons_.size());
    return { blockLeft - (n - index) * kCapW, y, kCapW, kCapH };
}

/// Custom button under the point (window DIP coordinates), or -1.
int TitleBar::CustomButtonAt(const Point& dip) const {
    if (dip.y < bounds_.y || dip.y >= bounds_.y + bounds_.h) return -1;
    for (int i = static_cast<int>(customButtons_.size()) - 1; i >= 0; --i)
        if (CustomButtonRect(i).Contains(dip.x, dip.y)) return i;
    return -1;
}

Rect TitleBar::BadgeRect() const {
    // vertically centered on the band (11 DIP at the default 42 — identical pre-0.49 geometry)
    return { bounds_.x + 14, bounds_.y + (DockHeight() - 20.0f) / 2.0f, 20, 20 };
}

int TitleBar::HitTest(const Point& dip) const {
    if (dip.y < bounds_.y || dip.y >= bounds_.y + bounds_.h ||
        dip.x < bounds_.x || dip.x >= bounds_.x + bounds_.w) return 0;
    const int kHts[3] = { HTMINBUTTON, HTMAXBUTTON, HTCLOSE };   // WindowButtons bit order
    unsigned v = static_cast<unsigned>(systemButtons_);
    int slot = -1;
    for (int i = 0; i < 3; ++i) {   // visible buttons left-to-right: Minimize, Maximize, Close
        if (!((v >> i) & 1u)) continue;
        ++slot;
        if (ButtonRect(slot).Contains(dip.x, dip.y)) {
            // disabled maximize (R2.3): the button rect is caption drag space
            if (kHts[i] == HTMAXBUTTON && !WindowMaximizable()) return HTCAPTION;
            return kHts[i];
        }
    }
    if (badgeVisible_ && BadgeRect().Contains(dip.x, dip.y)) return HTSYSMENU;   // R32-09
    return HTCAPTION;
}

void TitleBar::OnPaint(Painter& p, const Theme& theme) {
    p.FillRect(bounds_, theme.titleBackground);
    bool active = WindowActive();
    auto dim = [&](const Color& c) {
        return Color{ c.r, c.g, c.b, c.a * (active ? 1.0f : 0.55f) };
    };

    // app badge: gradient rounded square + title first letter (dims with the whole title bar when inactive, R34-01)
    Rect badge = BadgeRect();
    if (badgeVisible_) {
        p.FillRoundedRectGradient(badge, 6, dim(theme.accentHover), dim(theme.accent));
        wchar_t letter = text_.empty() ? L'S' : text_.front();
        wchar_t letterText[2] = { letter, L'\0' };
        p.DrawText(std::wstring_view(letterText, 1),
                   Font{ .size = 12.0f, .weight = FontWeight::Bold },
                   badge, dim(theme.textOnAccent), HAlign::Center, VAlign::Center);
    }

    // window title (right side reserved for the visible caption buttons: system + custom)
    float textX = badgeVisible_ ? badge.Right() + 10 : bounds_.x + 14;
    float reserve =
        static_cast<float>(SystemSlotCount() + static_cast<int>(customButtons_.size())) * kCapW;
    if (!text_.empty())
        p.DrawText(text_, Font{ .size = 13.0f },
                   { textX, bounds_.y,
                     std::max(0.0f, bounds_.x + bounds_.w - reserve - textX),
                     DockHeight() },
                   dim(theme.titleText), HAlign::Left, VAlign::Center);

    // window buttons (non-client interaction; icons drawn here)
    static const Icon kCapMin = Icon::FromSvgPath("M4.5 11.25H19.5V12.75H4.5Z");
    static const Icon kCapMax = Icon::FromSvgPath(
        "M5.5 5.5H18.5V18.5H5.5ZM7 7V17H17V7Z");
    static const Icon kCapClose = Icon::FromSvgPath(
        "M6.36 4.93L4.95 6.34L10.6 12L4.95 17.66L6.36 19.07L12 13.42"
        "L17.64 19.07L19.05 17.66L13.4 12L19.05 6.34L17.64 4.93L12 10.59Z");
    const int kHts[3] = { HTMINBUTTON, HTMAXBUTTON, HTCLOSE };
    const Icon* kIcons[3] = { &kCapMin, &kCapMax, &kCapClose };
    bool zoomed = window_ && window_->impl_ && window_->impl_->hwnd &&
                  IsZoomed(window_->impl_->hwnd);
    unsigned v = static_cast<unsigned>(systemButtons_);
    int slot = -1;
    for (int i = 0; i < 3; ++i) {
        if (!((v >> i) & 1u)) continue;
        ++slot;
        Rect r = ButtonRect(slot);
        int ht = kHts[i];
        bool close = ht == HTCLOSE;
        bool pressed = active && captionPressed_ == ht && captionHover_ == ht;
        bool hovered = active && captionHover_ == ht && !pressed;
        Color fill = theme.titleBackground;
        if (pressed)      fill = close ? theme.captionClosePressed
                                       : theme.captionPressed;
        else if (hovered) fill = close ? theme.captionCloseHover
                                       : theme.hoverSoft;
        // hover/press fill extends from the window's top edge down to the hit box bottom (round-64):
        // the real Win11 highlight box is taller than its hit box and its top edge touches the window
        // top (R32-06 measurement, M-05) -- filling only the hit box reads as a floating box with a
        // strip of bare title background above it. Hit geometry stays as registered in the trade-off
        // table (docs/项目约定.md, round-32 R32-06 registration).
        if (pressed || hovered)
            p.FillRect({ r.x, bounds_.y, r.w, r.Bottom() - bounds_.y }, fill);
        // disabled maximize (R2.3): kept visible but dimmed, like the system's grayed-out button
        bool maxDisabled = ht == HTMAXBUTTON && !WindowMaximizable();
        Color iconColor = (pressed || hovered) && close
                              ? theme.textOnAccent : dim(theme.titleText);
        if (maxDisabled)
            iconColor = Color{ iconColor.r, iconColor.g, iconColor.b, iconColor.a * 0.4f };
        // glyph drawing area 16 DIP (R32-08: the original 11 DIP left ink at ~2/3 of the system's;
        // Fluent System Icons use a 24-unit view box: square ink 13/24, × ink 14.1/24)
        Rect area{ r.x + (r.w - 16.0f) / 2.0f, r.y + (r.h - 16.0f) / 2.0f,
                   16.0f, 16.0f };
        if (i == 1 && zoomed) {
            // restore state (R32-03): rear block top-right, front block bottom-left (matching the system orientation);
            // the front block is first filled with the current button background over its ink box (erasing the rear block's crossing lines) then stroked.
            // measured from the system: block outer edge 6.0 DIP, offset 2.0 DIP, total ink span 8.0 DIP
            Rect back{ area.x + 3.0f, area.y + 1.0f, 11.0f, 11.0f };
            Rect front{ area.x + 1.0f, area.y + 3.0f, 11.0f, 11.0f };
            p.DrawIcon(kCapMax, back, iconColor);
            p.FillRect({ front.x + 2.5f, front.y + 2.5f, 6.0f, 6.0f }, fill);
            p.DrawIcon(kCapMax, front, iconColor);
        } else {
            p.DrawIcon(*kIcons[i], area, iconColor);
        }
    }

    // custom caption buttons (round-65 R1): same fills as the system buttons; a toggled button keeps
    // a subtle active fill — also extending up to the band top, since the round-64 reasoning (a fill
    // that stops at the hit box reads as a floating box) applies to persistent fills alike — and
    // tints its icon with the accent color
    for (int i = 0; i < static_cast<int>(customButtons_.size()); ++i) {
        const CaptionButton& b = customButtons_[i];
        Rect r = CustomButtonRect(i);
        bool pressed = active && customPressed_ == i && customHover_ == i;
        bool hovered = active && customHover_ == i && !pressed;
        bool on = b.toggle && b.on;
        bool fillOn = on && toggledFill_;   // R6: the persistent fill is optional, the tint is not
        Color fill = theme.titleBackground;
        if (pressed)      fill = theme.captionPressed;
        else if (hovered) fill = theme.hoverSoft;
        else if (fillOn)  fill = theme.subtleActive;
        if (pressed || hovered || fillOn)
            p.FillRect({ r.x, bounds_.y, r.w, r.Bottom() - bounds_.y }, fill);
        Color iconColor = on ? dim(theme.accent) : dim(theme.titleText);
        Rect area{ r.x + (r.w - 16.0f) / 2.0f, r.y + (r.h - 16.0f) / 2.0f, 16.0f, 16.0f };
        p.DrawIcon(b.icon, area, iconColor);
    }
}

// Custom caption buttons live on client islands: the window machinery dispatches client mouse
// messages here (hover/press/tooltip), while release-inside commits mirror the dialog-button
// semantics (U-02: dragging out cancels, re-entry resumes).

void TitleBar::OnMouseMove(const Point& pos) {
    int idx = CustomButtonAt(pos);
    // while a press is held (window capture), only the held button shows the pressed fill; hovering
    // others shows nothing (same semantics as the NC system buttons, R32-01/05)
    int hover = customPressed_ >= 0 ? (idx == customPressed_ ? idx : -1) : idx;
    if (hover == customHover_) return;
    customHover_ = hover;
    Invalidate();
    // per-button tooltip: the popup snapshots the widget's tooltip text when created, and the
    // hovered-change block checks ToolTip() BEFORE this handler runs — so every rect change rewrites
    // the text and (re)arms the standard tip timer; the kTipTimerId path re-derives everything
    // (hovered + non-empty text + no popup yet), making redundant posts and stale popups self-healing
    SetToolTip(hover >= 0 ? std::wstring_view(customButtons_[hover].tooltip)
                          : std::wstring_view{});
    if (window_ && window_->impl_) {
        if (window_->impl_->tooltip) window_->impl_->CloseTooltip();
        if (hover >= 0 && !customButtons_[hover].tooltip.empty() && window_->impl_->hwnd)
            PostMessageW(window_->impl_->hwnd, WM_TIMER, detail::kTipTimerId, 0);
    }
}

void TitleBar::OnMouseLeave() {
    if (customHover_ != -1) {
        customHover_ = -1;
        SetToolTip(std::wstring_view{});
        Invalidate();
    }
    // the pressed state survives a leave while captured (release inside still commits, U-02)
}

void TitleBar::OnMouseDown(const Point& pos) {
    customPressed_ = CustomButtonAt(pos);
    if (customPressed_ >= 0) {
        customHover_ = customPressed_;   // show the pressed state while held
        Invalidate();
    }
}

void TitleBar::OnMouseUp(const Point& pos) {
    int pressed = customPressed_;
    customPressed_ = -1;
    if (pressed < 0) return;
    // commit only when released over the same button (U-02); the callback may reconfigure the bar,
    // so the pressed state is cleared first
    int idx = CustomButtonAt(pos);
    customHover_ = idx;
    Invalidate();
    if (idx == pressed) CommitCustom(pressed);
}

void TitleBar::OnCaptureLost() {
    // capture stolen (Alt+Tab etc.): cancel the pressed state, else it sticks (round-15 §3.15)
    if (customPressed_ != -1) {
        customPressed_ = -1;
        Invalidate();
    }
}

void TitleBar::CommitCustom(int index) {
    if (index < 0 || index >= static_cast<int>(customButtons_.size())) return;
    CaptionButton& b = customButtons_[index];
    if (b.toggle) {
        b.on = !b.on;
        Invalidate();
        if (b.onToggled) b.onToggled(b.on);
    } else if (b.onClick) {
        b.onClick();
    }
}

// ---------------------------------------------------------------------------
// layout container implementation
// ---------------------------------------------------------------------------

void Container::OnPaint(Painter& p, const Theme& theme) {
    // hover highlight background (drawn before children, under the content)
    if (hoverHighlight_ && hoverLit_ && enabled_)
        p.FillRoundedRect(bounds_, hoverRadius_, theme.hoverSoft);
}

void Container::AssignBounds(const Rect& area) {
    bounds_ = area;
    if (children_.empty()) return;
    Rect content{ area.x + padL_, area.y + padT_,
                  std::max(0.0f, area.w - padL_ - padR_),
                  std::max(0.0f, area.h - padT_ - padB_) };
    LayoutChildren(content);
}

void Container::LayoutChildren(const Rect& area) {
    // PERF57-02: plan snapshots now take slots from the member pool via AcquirePlanBuf, closing out
    // "1 heap allocation per container per relayout". The pool hands out slots by acquisition order (≈ nesting/re-entry
    // depth): recursive child containers and re-entry of the same container each take a slot without overwrite (deque keeps
    // outer slot references valid as the pool grows). Snapshot semantics unchanged — still an independent copy per pass, so ARCH39-02's
    // iterator-safety argument for "removing siblings during iteration" continues to hold
    std::vector<Plan>& plans = AcquirePlanBuf();
    struct PlanBufGuard {
        Container& c;
        ~PlanBufGuard() { c.ReleasePlanBuf(); }
    } planBufGuard{*this};
    plans.clear();
    plans.reserve(children_.size());
    for (auto& c : children_) {
        if (!c->Visible()) continue;
        Size d = c->DesiredSize();
        float fixedMain = vertical_ ? c->fixedH_ : c->fixedW_;
        Plan p;
        p.widget = c.get();
        p.main = fixedMain > 0 ? fixedMain : (vertical_ ? d.h : d.w);
        p.fixedCross = vertical_ ? c->fixedW_ : c->fixedH_;
        p.desiredCross = vertical_ ? d.w : d.h;
        p.weight = c->weight_;
        p.weighted = fixedMain <= 0 && c->weight_ > 0;
        plans.push_back(p);
    }
    if (plans.empty()) return;

    float availMain = vertical_ ? area.h : area.w;
    float availCross = vertical_ ? area.w : area.h;
    float used = 0, totalWeight = 0;
    for (const auto& p : plans) {
        if (p.weighted) totalWeight += p.weight;
        else used += p.main;
    }
    float flex = std::max(0.0f,
                          availMain - used - spacing_ * float(plans.size() - 1));
    float lineBaseline = 0;   // Baseline group row baseline (horizontal containers only)

    float pos = vertical_ ? area.y : area.x;
    // Baseline groups (horizontal containers only, R25-06): row baseline = max of the Baseline children's baselines,
    // children sink by "row baseline − own baseline" (CSS align-items:baseline).
    // Baseline children do not participate in stretch (CSS contract); cross sizes take desired/fixed values
    if (!vertical_) {
        bool anyBaseline = false;
        for (const auto& p : plans)
            if (p.widget->EffectiveCrossAlign(childCrossAlignSet_, childCrossAlign_)
                    == CrossAlign::Baseline) { anyBaseline = true; break; }
        if (anyBaseline)
            for (auto& p : plans) {
                if (p.widget->EffectiveCrossAlign(childCrossAlignSet_, childCrossAlign_)
                        != CrossAlign::Baseline) continue;
                float cross = p.fixedCross > 0 ? p.fixedCross : p.desiredCross;
                p.baseline = p.widget->BaselineOffset(cross);
                lineBaseline = std::max(lineBaseline, p.baseline);
            }
    }
    for (const auto& p : plans) {
        float main = p.weighted ? flex * p.weight / totalWeight : p.main;
        float crossSize, offset = 0;
        // cross-axis alignment resolved uniformly (§3.14): explicit align-self wins, then the container's explicit
        // SetChildCrossAlign, otherwise keep the child's own semantics (CSS default stretch)
        CrossAlign ca = p.widget->EffectiveCrossAlign(childCrossAlignSet_,
                                                      childCrossAlign_);
        if (ca == CrossAlign::Stretch) {
            crossSize = p.fixedCross > 0 ? p.fixedCross : availCross;
        } else {
            crossSize = p.fixedCross > 0 ? p.fixedCross : p.desiredCross;
        }
        // max-width constraint (e.g. the sections' 1080px cap): width is the cross axis in a vertical container (Column)
        // and the main axis in a horizontal one (Row) — it used to be clamped on the cross axis always, wrongly clamping height inside Rows
        // (round-15 §3.14). Center/End recompute their start after clamping (L-04)
        if (p.widget->maxWidth_ > 0) {
            if (vertical_) {
                if (crossSize > p.widget->maxWidth_) crossSize = p.widget->maxWidth_;
            } else if (main > p.widget->maxWidth_) {
                main = p.widget->maxWidth_;
            }
        }
        if (ca == CrossAlign::Center)
            offset = (availCross - crossSize) / 2;
        else if (ca == CrossAlign::End)
            offset = availCross - crossSize;
        else if (ca == CrossAlign::Baseline && !vertical_)
            offset = lineBaseline - p.baseline;
        Rect childRect = vertical_
            ? Rect{ area.x + offset, pos, crossSize, main }
            : Rect{ pos, area.y + offset, main, crossSize };
        // child containers need to keep laying out downward; plain widgets only record bounds
        if (auto* nested = dynamic_cast<Container*>(p.widget))
            nested->AssignBounds(childRect);
        else
            p.widget->SetBounds(childRect);
        pos += main + spacing_;
    }
}

Size Container::DesiredSize() const {
    float main = 0, cross = 0;
    int count = 0;
    // Row's Baseline group (R25-06): the row's cross size must accommodate the deepest descent after baseline alignment
    struct BItem { float b, h; };
    std::vector<BItem> baselineItems;
    for (const auto& c : children_) {
        if (!c->Visible()) continue;
        Size d = c->DesiredSize();
        float fixedMain = vertical_ ? c->fixedH_ : c->fixedW_;
        float fixedCross = vertical_ ? c->fixedW_ : c->fixedH_;
        main += fixedMain > 0 ? fixedMain : (vertical_ ? d.h : d.w);
        float ch = fixedCross > 0 ? fixedCross : (vertical_ ? d.w : d.h);
        cross = std::max(cross, ch);
        if (!vertical_ && c->EffectiveCrossAlign(childCrossAlignSet_, childCrossAlign_)
                == CrossAlign::Baseline)
            baselineItems.push_back({ c->BaselineOffset(ch), ch });
        ++count;
    }
    if (!baselineItems.empty()) {
        float lb = 0;
        for (const auto& bi : baselineItems) lb = std::max(lb, bi.b);
        for (const auto& bi : baselineItems) cross = std::max(cross, lb - bi.b + bi.h);
    }
    if (count > 0) main += spacing_ * float(count - 1);
    // padding is assigned per axis: main-axis padding on the main axis, cross-axis padding on the cross axis.
    // The two axes used to be swapped for horizontal containers (Row) — vertical padding leaked out of the height, horizontal padding was added into it (L-01)
    main  += vertical_ ? (padT_ + padB_) : (padL_ + padR_);
    cross += vertical_ ? (padL_ + padR_) : (padT_ + padB_);
    return vertical_ ? Size{ cross, main } : Size{ main, cross };
}

template <class T, class... Args>
T& Container::Add(Args&&... args) {
    auto widget = std::make_unique<T>(std::forward<Args>(args)...);
    T& ref = *widget;
    widget->window_ = window_;
    widget->parent_ = this;
    children_.push_back(std::move(widget));
    // ARCH38-05: recursively re-own the whole subtree with the same contract as Attach — a detached container
    // (subtree already built) added back to the tree used to leave descendants' window_ nullptr, silently breaking their Invalidate/Relayout
    if (window_ && window_->impl_) window_->impl_->SyncOwnership(&ref);
    // GAP43-02: added-to-tree notification (virtual, dispatched on the actual object — upcast Add paths covered too).
    // No window_ precondition: a detached container's Add must also register (Tabs::AddPage takes this path).
    OnChildAdded(&ref);
    if (window_) Relayout();
    return ref;
}

bool Container::Remove(Widget& child) {
    for (auto it = children_.begin(); it != children_.end(); ++it) {
        if (it->get() == &child) {
            // ARCH40-01: the callback must run before QueueDestroy — QueueDestroy moves the unique_ptr out by value,
            // emptying the slot; a callback afterwards that "compares by pointer / asks for a positional index"
            // (Tabs::OnChildRemoved) would never match (attached path: tabs never shrink →
            // ghost tabs, blank content area). At this moment child is still fully in the tree; queries inside the callback are safe
            OnChildRemoved(&child);
            // animation subscription / focus / capture / hover are cleaned up with the subtree (API-06)
            if (window_ && window_->impl_) {
                window_->impl_->DetachTree(it->get());
                // ARCH38-07: no synchronous destruction here — callback stacks (OnMouseDown etc.) may
                // still hold this widget's pointer; destruction is deferred to after this frame's messages
                window_->impl_->QueueDestroy(std::move(*it));
            }
            children_.erase(it);
            Relayout();
            Invalidate();
            return true;
        }
    }
    return false;
}

void Container::ClearChildren() {
    if (children_.empty()) return;
    // ARCH40-04: same as Remove — deferred destroy only for attached containers; detached (window_ null)
    // does not enqueue; children_.clear() below destroys all children synchronously
    if (window_ && window_->impl_) {
        for (auto& c : children_) window_->impl_->DetachTree(c.get());
        for (auto& c : children_) window_->impl_->QueueDestroy(std::move(c));
    }
    OnChildRemoved(nullptr);   // ARCH39-03: bulk-clear notification (child=nullptr)
    children_.clear();
    Relayout();
    Invalidate();
}

bool Container::BringToFront(Widget& child) {
    for (auto it = children_.begin(); it != children_.end(); ++it) {
        if (it->get() == &child) {
            if (it + 1 == children_.end()) return false;   // already topmost
            auto owned = std::move(*it);
            children_.erase(it);
            children_.push_back(std::move(owned));
            OnChildrenReordered();   // GAP43-01: reorder notification (tab geometry cache etc. marked dirty)
            Relayout();
            Invalidate();
            return true;
        }
    }
    return false;
}

bool Container::SendToBack(Widget& child) {
    for (auto it = children_.begin(); it != children_.end(); ++it) {
        if (it->get() == &child) {
            if (it == children_.begin()) return false;   // already bottommost
            auto owned = std::move(*it);
            children_.erase(it);
            children_.insert(children_.begin(), std::move(owned));
            OnChildrenReordered();   // GAP43-01: reorder notification (tab geometry cache etc. marked dirty)
            Relayout();
            Invalidate();
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// flow container / gradient banner implementation
// ---------------------------------------------------------------------------

void Flow::AssignBounds(const Rect& area) {
    bounds_ = area;
    // when minItemW>0, auto-fill grid: column count = the fewest that fit, equal widths
    int gridCols = 0;
    float itemW = 0;
    if (minItemW_ > 0 && area.w > 0) {
        gridCols = std::max(1, static_cast<int>((area.w + spacing_) /
                                                (minItemW_ + spacing_)));
        itemW = (area.w - spacing_ * (gridCols - 1)) / gridCols;
    }
    // pass 1: only x/widths and wrapping; items are archived by row (equal heights across a row must wait for the row height
    // before y and heights can be settled, A-01)
    struct RowItem { Widget* c; float x, w, h; bool fixedH; float baseline = 0; };
    std::vector<std::vector<RowItem>> rows;
    float x = area.x, avail = area.w;
    int col = 0;
    for (auto& c : children_) {
        if (!c->Visible()) continue;
        bool fullRow = ChildFlowFullRow(c.get());
        if (fullRow && (x > area.x || col > 0)) {   // finish the half row before a full-row item
            x = area.x;
            rows.emplace_back();
            col = 0;
        }
        float w, h;
        bool fixedH = ChildFixedH(c.get()) > 0;
        if (fullRow) {                     // .ex-card.wide
            w = area.w;
            if (float mw = ChildMaxWidth(c.get()); mw > 0) w = std::min(w, mw);
            h = fixedH ? ChildFixedH(c.get()) : ChildDesired(c.get()).h;
        } else if (gridCols > 0) {
            w = ChildFixedW(c.get()) > 0 ? ChildFixedW(c.get()) : itemW;
            h = fixedH ? ChildFixedH(c.get()) : ChildDesired(c.get()).h;
        } else {
            w = ChildFixedW(c.get()) > 0 ? ChildFixedW(c.get())
                : std::clamp(ChildDesired(c.get()).w, 0.0f, avail);
            h = fixedH ? ChildFixedH(c.get()) : ChildDesired(c.get()).h;
            if (x > area.x && x + w > area.x + avail) {   // wrap
                x = area.x;
                rows.emplace_back();
                col = 0;
            }
        }
        if (rows.empty()) rows.emplace_back();
        rows.back().push_back({ c.get(), x, w, h, fixedH });
        if (fullRow) {   // a full-row item takes the whole row, straight to the next
            x = area.x;
            rows.emplace_back();
            col = 0;
        } else if (gridCols > 0) {
            x += itemW + spacing_;
            if (++col >= gridCols) {   // grid row full: wrap
                x = area.x;
                rows.emplace_back();
                col = 0;
            }
        } else {
            x += w + spacing_;
        }
    }
    // pass 2: CSS Grid/Flex children default to align-items:stretch — everything in a row stretches to the
    // tallest; fixed-height items keep their own height (A-01)
    float rowY = area.y;
    float bottom = area.y;   // the deepest point content actually reached (full-row items do not interrupt the tally)
    for (auto& r : rows) {
        // GAP50-10: if a row eagerly created in pass 1 ends up empty (last item is a full-row item / grid row full),
        // rowY already includes all prior row heights and gaps — counting it would inflate desiredH_
        // by one spacing_ (CSS flex-wrap/grid has no trailing gap)
        if (r.empty()) continue;
        // row height and the Baseline group's row baseline settle first (R25-06): baseline items do not stretch,
        // the row height must accommodate the deepest descent after baseline alignment before stretch items get the full row height
        float rowH = 0, rowBaseline = 0;
        bool anyBaseline = false;
        for (auto& it : r) {
            rowH = std::max(rowH, it.h);
            if (it.c->EffectiveCrossAlign(childCrossAlignSet_, childCrossAlign_)
                    == CrossAlign::Baseline) {
                it.baseline = it.c->BaselineOffset(it.h);
                rowBaseline = std::max(rowBaseline, it.baseline);
                anyBaseline = true;
            }
        }
        if (anyBaseline)
            for (auto& it : r)
                if (it.c->EffectiveCrossAlign(childCrossAlignSet_, childCrossAlign_)
                        == CrossAlign::Baseline)
                    rowH = std::max(rowH, rowBaseline - it.baseline + it.h);
        for (auto& it : r) {
            // cross-axis alignment, same semantics as LayoutChildren (round-15 §3.14: Flow previously
            // never read SetChildCrossAlign): stretch fills the row height, Start/Center/
            // End align by desired height within the row, Baseline sinks by the row baseline (R25-06)
            CrossAlign ca = it.c->EffectiveCrossAlign(childCrossAlignSet_,
                                                      childCrossAlign_);
            if (ca == CrossAlign::Stretch) {
                AssignChild(it.c, { it.x, rowY, it.w, it.fixedH ? it.h : rowH });
            } else if (ca == CrossAlign::Baseline) {
                AssignChild(it.c, { it.x, rowY + rowBaseline - it.baseline, it.w, it.h });
            } else {
                float yy = rowY +
                           (ca == CrossAlign::Center ? (rowH - it.h) * 0.5f
                          : ca == CrossAlign::End    ? rowH - it.h
                                                     : 0.0f);
                AssignChild(it.c, { it.x, yy, it.w, it.h });
            }
        }
        bottom = std::max(bottom, rowY + rowH);
        rowY += rowH + spacing_;
    }
    float totalH = children_.empty() ? 0.0f : (bottom - area.y);
    // only updates the cache; no relayout here (layout re-entry would fight the outer LayoutChildren over
    // bounds); consumers like ScrollViewer re-read DesiredSize after layout for a second pass
    desiredH_ = totalH;
}

Size Flow::DesiredSize() const {
    // flow layout height depends on available width; use the wrapped total height of the last actual layout
    return { 200.0f, desiredH_ };
}

const Overlay::Insets& Overlay::InsetOf(const Widget* w) const {
    static const Insets kZero{};
    // last match wins (later write wins, GAP44-01): EraseInset already guarantees at most one entry per child, so
    // forward vs reverse is equivalent; reverse traversal is defensive — should duplicate entries ever appear, the last
    // setting wins, instead of a later write silently failing as under the old first-match (proven by round-44 C3).
    for (auto it = insets_.rbegin(); it != insets_.rend(); ++it)
        if (it->first == w) return it->second;
    return kZero;
}

void Overlay::AssignBounds(const Rect& area) {
    bounds_ = area;
    for (auto& c : children_) {
        if (!c->Visible()) continue;
        const Insets& in = InsetOf(c.get());
        Size d = ChildDesired(c.get());
        // constrained on both sides = stretch (CSS full inset); one side = dock by desired size;
        // a fully unconstrained side is placed by cross-axis alignment (default Start = left/top dock, matching
        // historical behavior; Overlay previously never read SetChildCrossAlign, round-15 §3.14)
        CrossAlign ca = c->EffectiveCrossAlign(childCrossAlignSet_, childCrossAlign_);
        float x, w;
        if (in.l >= 0 && in.r >= 0) {
            x = area.x + in.l;
            w = std::max(0.0f, area.w - in.l - in.r);
        } else if (in.l >= 0) {
            w = d.w; x = area.x + in.l;
        } else if (in.r >= 0) {
            w = d.w; x = area.x + area.w - in.r - d.w;
        } else {
            w = d.w;
            x = ca == CrossAlign::Center ? area.x + (area.w - d.w) * 0.5f
              : ca == CrossAlign::End    ? area.x + area.w - d.w
                                         : area.x;
        }
        float yy, h;
        if (in.t >= 0 && in.b >= 0) {
            yy = area.y + in.t;
            h = std::max(0.0f, area.h - in.t - in.b);
        } else if (in.t >= 0) {
            h = d.h; yy = area.y + in.t;
        } else if (in.b >= 0) {
            h = d.h; yy = area.y + area.h - in.b - d.h;
        } else {
            h = d.h;
            yy = ca == CrossAlign::Center ? area.y + (area.h - d.h) * 0.5f
               : ca == CrossAlign::End    ? area.y + area.h - d.h
                                          : area.y;
        }
        AssignChild(c.get(), { x, yy, w, h });
    }
}

Size Overlay::DesiredSize() const {
    float w = 0, h = 0;
    for (auto& c : children_) {
        if (!c->Visible()) continue;
        Size d = ChildDesired(c.get());
        const Insets& in = InsetOf(c.get());
        // unconstrained side (-1) does not count into hug
        w = std::max(w, (in.l >= 0 ? in.l : 0) + d.w + (in.r >= 0 ? in.r : 0));
        h = std::max(h, (in.t >= 0 ? in.t : 0) + d.h + (in.b >= 0 ? in.b : 0));
    }
    return { w, h };
}

namespace detail {
/// CSS linear-gradient endpoint conversion (B-01): deg is a CSS angle (0° up, 90° right),
/// direction vector (sinθ, −cosθ), gradient line length = |W·sinθ| + |H·cosθ|, symmetric about the box center.
/// The previous "top-left → below bottom-right" approximation was flatter and shorter than the baseline, shifting every stop position.
inline void CssGradientEndpoints(const Rect& r, float deg, Point& from, Point& to) {
    float rad = deg * 3.14159265f / 180.0f;
    float dx = std::sin(rad), dy = -std::cos(rad);
    float len = std::abs(r.w * dx) + std::abs(r.h * dy);
    Point c{ r.x + r.w * 0.5f, r.y + r.h * 0.5f };
    from = Point{ c.x - dx * len * 0.5f, c.y - dy * len * 0.5f };
    to   = Point{ c.x + dx * len * 0.5f, c.y + dy * len * 0.5f };
}
} // namespace detail

void Banner::AssignBounds(const Rect& area) {
    bounds_ = area;
    // the single child docks to the bottom-right corner (26 margin), at its desired size
    for (auto& c : children_) {
        if (!c->Visible()) continue;
        Size d = ChildDesired(c.get());
        float w = ChildFixedW(c.get()) > 0 ? ChildFixedW(c.get())
                                           : std::min(d.w, area.w - 52.0f);
        float h = ChildFixedH(c.get()) > 0 ? ChildFixedH(c.get()) : d.h;
        Rect r{ area.x + area.w - 26.0f - w, area.y + area.h - 26.0f - h, w, h };
        AssignChild(c.get(), r);
    }
}

Size Banner::DesiredSize() const {
    return { 320.0f, 230.0f };
}

void Banner::OnPaint(Painter& p, const Theme& theme) {
    if (stops_.size() < 2) {
        // OBS46-01: the fallback fill follows the theme (previously hardcoded 0x0F6CBD, still bright blue in dark theme).
        // The baseline hero is fixed blue anyway ⇒ the demo sets explicit stops and never takes this branch.
        p.FillRoundedRect(bounds_, kRadiusPanel, theme.accent);
        return;
    }
    // CSS linear-gradient(<angle>,…): endpoints from the direction vector and gradient line length (B-01)
    Point from, to;
    detail::CssGradientEndpoints(bounds_, angleDeg_, from, to);
    p.FillRoundedRectGradientStops(bounds_, kRadiusPanel, stops_.data(),
                                   stops_.size(), from, to);
    // blobs: the baseline is a solid circle + filter:blur(50px) — a near-full-strength plateau inside, decaying only within
    // 1~2σ of the edge; a linear radial gradient is down to 55% at 0.45R (B-03). g1/g3 use a
    // "0.55 plateau" approximation; g2 (dark blue) sits right at the geometric edge at the measured point, where the baseline blur gives ≈half strength,
    // so the rect expands 50px with the plateau at 0.6 falling linearly to 0 (≈ half strength at the edge). No real blur
    // (Project Conventions)
    p.PushClip(bounds_);
    const GradientStop g1[3] = { { 0.0f, Color::Rgb(0xFFFFFF, 0.28f) },
                                 { 0.55f, Color::Rgb(0xFFFFFF, 0.28f) },
                                 { 1.0f, Color::Rgb(0xFFFFFF, 0.0f) } };
    p.FillEllipseGradientStops({ bounds_.x - 60, bounds_.y - 120, 340, 340 },
                               g1, 3);
    const GradientStop g2[3] = { { 0.0f, Color::Rgb(0x0C2250, 0.35f) },
                                 { 0.6f, Color::Rgb(0x0C2250, 0.35f) },
                                 { 1.0f, Color::Rgb(0x0C2250, 0.0f) } };
    p.FillEllipseGradientStops({ bounds_.Right() - 370, bounds_.Bottom() - 290, 500, 500 },
                               g2, 3);
    // third blob: baseline .g3{width:220px;right:26%} ⇒ solid circle left = 0.74W - 220
    const GradientStop g3[3] = { { 0.0f, Color::Rgb(0xFFD666, 0.35f) },
                                 { 0.55f, Color::Rgb(0xFFD666, 0.35f) },
                                 { 1.0f, Color::Rgb(0xFFD666, 0.0f) } };
    p.FillEllipseGradientStops({ bounds_.x + bounds_.w * 0.74f - 220.0f, bounds_.y - 90, 220, 220 },
                               g3, 3);
    p.PopClip();
}

// ---------------------------------------------------------------------------
// floating card implementation
// ---------------------------------------------------------------------------

Card& Card::SetTitle(std::wstring_view title) {
    title_.assign(title.begin(), title.end());
    titleDirty_ = true;
    Relayout();
    Invalidate();

    return *this;
}

Card& Card::SetDescription(std::wstring_view desc) {
    desc_.assign(desc.begin(), desc.end());
    titleDirty_ = true;
    Relayout();
    Invalidate();

    return *this;
}

/// Total height of the title + description rows (including the gap to the content).
/// Baseline .ex-card: h3 margin 0 + .desc margin 1px 0 12px ⇒ "description→content" 12px;
/// no 12px padding before title/description (all extra spacing lands after the description, A-01).
float Card::TitleHeight() const {
    if (titleDirty_) {
        titleH_ = 0.0f;
        if (!title_.empty()) {
            titleH_ += detail::MeasureTextMetrics(
                title_,
                Font{ .size = titleSize_, .weight = FontWeight::SemiBold }).height;
        }
        if (!desc_.empty()) {
            if (!title_.empty()) titleH_ += 2.0f;
            titleH_ += detail::MeasureTextMetrics(desc_, Font{ .size = 12.0f }).height;
        }
        if (!title_.empty() || !desc_.empty()) titleH_ += 12.0f;   // gap to the content
        titleDirty_ = false;
    }
    return titleH_;
}

void Card::AssignBounds(const Rect& area) {
    bounds_ = area;
    float th = TitleHeight();
    Rect content{ area.x + padSide_, area.y + padTop_ + th,
                  std::max(0.0f, area.w - padSide_ * 2.0f),
                  std::max(0.0f, area.h - padTop_ - padBottom_ - th) };
    LayoutChildren(content);
}

Size Card::DesiredSize() const {
    Size s = Container::DesiredSize();   // padding_ stays 0; margins are added here
    float th = TitleHeight();
    return { s.w + padSide_ * 2.0f, s.h + padTop_ + padBottom_ + th };
}

const ShadowSpec& Card::Shadow() const {
    switch (shadowTier_) {
    case 1: return kShadowFly;
    case 2: return kShadowDialog;
    default: return kShadowCard;
    }
}

void Card::OnPaint(Painter& p, const Theme& theme) {
    // shadow tiers (kShadowCard/Fly/Dialog), opacity picked by theme light/dark;
    // the dialog tier is a double shadow (--sh-dlg far + near, I-10)
    if (shadowTier_ >= kShadowTierCard) {
        const ShadowSpec& sh = Shadow();
        p.DrawShadow(bounds_, kRadiusPanel, sh.offsetY, sh.blur,
                     Color::Rgb(0x000000, ShadowAlpha(sh, theme)));
        if (shadowTier_ == kShadowTierDialog)
            p.DrawShadow(bounds_, kRadiusPanel, kShadowDialogNear.offsetY,
                         kShadowDialogNear.blur,
                         Color::Rgb(0x000000, ShadowAlpha(kShadowDialogNear, theme)));
    }
    p.FillRoundedRect(bounds_, kRadiusPanel,
                      background_.value_or(theme.cardBackground));
    // GAP42-04: title/description draw as single lines with wrap=false; overlong text used to overflow the card and
    // overprint the neighbor card (visible once round-41 lengthened demo copy, with the overflow region drifting across
    // sessions and breaking pixel-regression determinism) — clip to the card bounds, matching the baseline .desc's
    // in-card wrap-and-truncate appearance
    p.PushClip(bounds_);
    float y = bounds_.y + padTop_;
    if (!title_.empty()) {
        p.DrawText(title_, Font{ .size = titleSize_, .weight = FontWeight::SemiBold },
                   { bounds_.x + padSide_, y,
                     std::max(0.0f, bounds_.w - padSide_ * 2.0f),
                     std::max(0.0f, TitleHeight() - 12) },
                   theme.text, HAlign::Left, VAlign::Top);
    }
    if (!desc_.empty()) {
        float titleH = title_.empty()
            ? 0.0f
            : detail::MeasureTextMetrics(
                  title_,
                  Font{ .size = titleSize_, .weight = FontWeight::SemiBold })
                  .height + 2.0f;
        p.DrawText(desc_, Font{ .size = 12.0f },
                   { bounds_.x + padSide_, y + titleH,
                     std::max(0.0f, bounds_.w - padSide_ * 2.0f),
                     std::max(0.0f, TitleHeight() - 12 - titleH) },
                   theme.textSecondary, HAlign::Left, VAlign::Top);
    }
    p.PopClip();
}

void Widget::Invalidate() {
    if (window_ && window_->impl_) window_->impl_->Invalidate();
}

Label& Label::SetText(std::wstring_view text) {
    text_.assign(text.begin(), text.end());
    Relayout();  // the content-based width may change
    Invalidate();

    return *this;
}

Label& Label::SetFont(const Font& font) {
    font_ = font;
    Relayout();
    Invalidate();

    return *this;
}

Size Label::DesiredSize() const {
    // a leading icon takes iconSize_ + 6 (same contract as OnPaint shifting text right, C-11);
    // empty text + icon = bare icon (icon grid cell), measured by the icon itself.
    // Without an icon, height/width match the old contract value-for-value (iconW/iconH = 0).
    float iconW = icon_.IsEmpty() ? 0.0f : iconSize_ + 6.0f;
    float iconH = icon_.IsEmpty() ? 0.0f : iconSize_;
    if (text_.empty()) return icon_.IsEmpty() ? Size{} : Size{ iconSize_, iconSize_ };
    DWRITE_TEXT_METRICS m = detail::MeasureTextMetrics(text_, font_);
    // the chip label keeps padding around its text
    return panel_ ? Size{ m.widthIncludingTrailingWhitespace + 24.0f + iconW,
                          std::max(m.height, iconH) + 16.0f }
                  : Size{ m.widthIncludingTrailingWhitespace + iconW,
                          std::max(m.height, iconH) };
}

float Label::BaselineOffset(float crossSize) const {
    if (text_.empty()) return crossSize;   // no text: size from the box composite (CSS contract)
    DWRITE_TEXT_METRICS m = detail::MeasureTextMetrics(text_, font_);
    float baseline = detail::MeasureTextBaseline(text_, font_);
    bool padded = panel_ || !gradientStops_.empty();
    float boxTop = padded ? 8.0f : 0.0f;
    float boxH = std::max(0.0f, crossSize - (padded ? 16.0f : 0.0f));
    // consistent with OnPaint's DrawText vertical placement: the text box (m.height) is placed by vAlign
    float vOff = vAlign_ == VAlign::Top    ? 0.0f
               : vAlign_ == VAlign::Bottom ? std::max(0.0f, boxH - m.height)
                                           : std::max(0.0f, boxH - m.height) * 0.5f;
    return boxTop + vOff + baseline;
}

void Label::OnPaint(Painter& p, const Theme& theme) {
    auto roleColor = [&]() -> Color {
        switch (role_) {
        case TextRole::Secondary: return theme.textSecondary;
        case TextRole::Tertiary:  return theme.textTertiary;
        case TextRole::Disabled:  return theme.textDisabled;
        default:                  return theme.text;
        }
    };
    Color color = textColor_.value_or(roleColor());
    if (shadowTier_ >= kShadowTierCard) {
        const ShadowSpec& sh = customShadow_ ? *customShadow_
                             : shadowTier_ == kShadowTierDialog ? kShadowDialog
                             : shadowTier_ == kShadowTierFlyout ? kShadowFly : kShadowCard;
        // shadow color defaults to black (tiered by theme light/dark); the elevation stack diagram's top block overrides it via SetShadowColor
        // with a colored large shadow (baseline 0 16px 32px rgba(0,103,192,.35)).
        // the punch-hole radius takes the widget's own corner radius (previously hardcoded 8; once decoupled from
        // SetCornerRadius the shadow peeked out of the corners, round-15 §3.13 rider)
        p.DrawShadow(bounds_, cornerRadius_, sh.offsetY, sh.blur,
                     shadowColor_.value_or(Color::Rgb(0x000000, ShadowAlpha(sh, theme))));
        // dialog-tier double shadow: --sh-dlg far 0 32px 64px + near 0 2px 21px (I-10)
        if (!customShadow_ && shadowTier_ == kShadowTierDialog)
            p.DrawShadow(bounds_, cornerRadius_, kShadowDialogNear.offsetY, kShadowDialogNear.blur,
                         Color::Rgb(0x000000, ShadowAlpha(kShadowDialogNear, theme)));
    }
    if (!gradientStops_.empty()) {
        Point from, to;
        detail::CssGradientEndpoints(bounds_, gradientAngle_, from, to);
        p.FillRoundedRectGradientStops(bounds_, cornerRadius_, gradientStops_.data(),
                                       gradientStops_.size(), from, to);
    } else if (panel_) {
        p.FillRoundedRect(bounds_, cornerRadius_, *panel_);
    }
    Rect content = (panel_ || !gradientStops_.empty())
        ? Rect{ bounds_.x + 12, bounds_.y + 8,
                std::max(0.0f, bounds_.w - 24), std::max(0.0f, bounds_.h - 16) }
        : bounds_;
    if (!icon_.IsEmpty()) {
        bool centered = text_.empty();
        Rect ic = centered
            ? Rect{ content.x + (content.w - iconSize_) * 0.5f,
                    content.y + (content.h - iconSize_) * 0.5f, iconSize_, iconSize_ }
            : Rect{ content.x, content.y + (content.h - 16.0f) * 0.5f, iconSize_, iconSize_ };
        p.DrawIcon(icon_, ic, color);
        if (!centered) content = { content.x + iconSize_ + 6.0f, content.y,
                                   std::max(0.0f, content.w - iconSize_ - 6.0f), content.h };
    }
    // ellipsis truncation: when text exceeds the content area, binary-search the longest fitting
    // prefix (text-overflow:ellipsis approximation). The prefix advance width is monotone
    // non-decreasing in length, so the largest passing n found here is identical to the linear
    // scan this replaces — but with O(log N) instead of O(N) cache lookups, and measuring through
    // a wstring_view instead of substr removes the per-step string copy entirely (PERF62-01:
    // the old loop was a per-frame O(N²)-copy / O(N)-allocation path on any overflowing label).
    std::wstring& draw = ellipsisScratch_;
    bool clipped = false;
    if (ellipsis_ && !wrap_ && content.w > 0 &&
        detail::MeasureTextWidth(text_, font_) > content.w) {
        clipped = true;
        const float dotsW = detail::MeasureTextWidth(L"…", font_);
        size_t lo = 0, hi = text_.size();   // invariant: P(lo) assumed, P(hi+1..) rejected
        while (lo < hi) {
            const size_t mid = lo + (hi - lo + 1) / 2;
            if (detail::MeasureTextWidth(std::wstring_view(text_.data(), mid), font_) + dotsW <=
                content.w) lo = mid;
            else hi = mid - 1;
        }
        draw.assign(text_.data(), lo);   // assign (not resize+copy) also clears stale scratch
        if (lo > 0) draw += L"…";
        // lo == 0 (the ellipsis alone does not fit) draws nothing, same as the old n == 0 branch
    }
    if (clipped) p.DrawText(draw, font_, content, color, hAlign_, vAlign_, wrap_);
    else p.DrawText(text_, font_, content, color, hAlign_, vAlign_, wrap_);
    if (borderColor_) {
        // dashes follow the rounded corners (baseline #ctxArea border-radius:4px + dashed, round-19 F-02;
        // previously a square dashed frame sat on the rounded fill, bulging at all four corners)
        if (borderDashed_) p.StrokeRoundedRectDashed(bounds_, cornerRadius_, *borderColor_);
        else p.StrokeRoundedRect(bounds_, cornerRadius_, *borderColor_);
    }
}

Button& Button::SetText(std::wstring_view text) {
    text_.assign(text.begin(), text.end());
    Relayout();
    Invalidate();

    return *this;
}

Button& Button::SetFont(const Font& font) {
    font_ = font;
    Relayout();
    Invalidate();

    return *this;
}

Size Button::DesiredSize() const {
    if (style_ == ButtonStyle::IconOnly) return { 32.0f, 32.0f };
    if (style_ == ButtonStyle::Hyperlink)
        return { detail::MeasureTextWidth(text_, font_) + 8.0f, 28.0f };
    float w = detail::MeasureTextWidth(text_, font_);
    if (!icon_.IsEmpty()) w += 16.0f + 8.0f;
    if (badge_ > 0) {
        std::wstring bt = std::to_wstring(badge_);
        // baseline HTML:761 inline style has only min-width:17px, no padding (.badge-n's
        // 0 5px belongs to the .avatar .badge-n descendant selector; button instances do not match); +2 keeps ink
        // off the edges (round-15 §3.5 — previously the avatar contract text+10 was wrongly applied, button width +3)
        w += 8.0f + std::max(17.0f, detail::MeasureTextWidth(
                 bt, Font{ .size = 11.0f, .weight = FontWeight::SemiBold }) + 2.0f);
    }
    // baseline .btn: padding 0 14px + 1px borders both sides = text + 30, no min-width
    // (previously max(64, w+28) lifted 2-char buttons to 64 and undercounted borders by 2px, I-01)
    return { w + 30.0f, 32.0f };
}

float Button::BaselineOffset(float crossSize) const {
    if (style_ == ButtonStyle::IconOnly || text_.empty()) return crossSize;
    DWRITE_TEXT_METRICS m = detail::MeasureTextMetrics(text_, font_);
    // the text area is vertically centered in the whole box (OnPaint textArea VAlign::Center)
    return std::max(0.0f, crossSize - m.height) * 0.5f +
           detail::MeasureTextBaseline(text_, font_);
}

void Button::Activate() {
    if (!Enabled()) return;
    if (toggleable_) toggled_ = !toggled_;
    Invalidate();
    if (OnClick) OnClick();
}

void Button::OnKeydown(uint32_t vk) {
    if (vk == VK_SPACE || vk == VK_RETURN) Activate();
}

/// Icon-button corner dot badge rect (widget-local): 8px dot + 2px background-color ring,
/// center 9px from the corner, ring's outer edge 3px from the edge (baseline .dotb::after top/right 3 + content-box, B-11).
static Rect ButtonDotRect(const Rect& b) {
    return { b.Right() - 13.0f, b.y + 5.0f, 8.0f, 8.0f };
}

void Button::OnPaint(Painter& p, const Theme& theme) {
    bool disabled = !Enabled();
    bool active = toggleable_ && toggled_;
    Rect iconArea{};
    Rect textArea = bounds_;

    if (style_ == ButtonStyle::IconOnly) {
        if (!disabled && (hover_ || pressed_))
            p.FillRoundedRect(bounds_, kRadiusControl,
                              pressed_ ? theme.subtleActive : theme.hoverSoft);
        if (!icon_.IsEmpty()) {
            Color fg = disabled ? theme.textDisabled : theme.text;
            // icon centered at the widget size (36px+ uses a 20px icon, otherwise 16px;
            // SetIconSize overrides the auto choice, I-04)
            float s = iconSize_ > 0.0f ? iconSize_
                    : (bounds_.w >= 36.0f || bounds_.h >= 36.0f) ? 20.0f : 16.0f;
            p.DrawIcon(icon_, { bounds_.x + (bounds_.w - s) * 0.5f,
                                bounds_.y + (bounds_.h - s) * 0.5f, s, s }, fg);
        }
        if (dot_) {
            Rect d = ButtonDotRect(bounds_);
            p.FillEllipse({ d.x - 2, d.y - 2, d.w + 4, d.h + 4 },
                          theme.windowBackground);   // 2px background-color stroke ring
            p.FillEllipse(d, theme.accent);
        }
        DrawFocusRing(this, p, theme);
        return;
    }

    // icon layout: icon 16 + gap 8, text takes the rest
    float textW = detail::MeasureTextWidth(text_, font_);
    // count badge (baseline .badge-n position:static: an inline pill after the text, not a corner badge; round-9 A-02 re-measured)
    Rect badgeRect{};
    if (badge_ > 0) {
        std::wstring bt = std::to_wstring(badge_);
        float bw = std::max(17.0f, detail::MeasureTextWidth(
                       bt, Font{ .size = 11.0f, .weight = FontWeight::SemiBold }) + 2.0f);
        float groupW = textW + (icon_.IsEmpty() ? 0.0f : 24.0f) + 8.0f + bw;
        float start = bounds_.x + (bounds_.w - groupW) * 0.5f;
        if (!icon_.IsEmpty()) {
            iconArea = { start, bounds_.y + (bounds_.h - 16.0f) / 2.0f, 16, 16 };
            start += 24.0f;
        }
        textArea = { start, bounds_.y, textW, bounds_.h };
        badgeRect = { start + textW + 8.0f, bounds_.y + (bounds_.h - 17.0f) / 2.0f, bw, 17 };
    } else if (!icon_.IsEmpty()) {
        float group = 16.0f + 8.0f + textW;
        float start = bounds_.x + (bounds_.w - group) * 0.5f;
        iconArea = { start, bounds_.y + (bounds_.h - 16.0f) / 2.0f, 16, 16 };
        textArea = { start + 24.0f, bounds_.y, textW, bounds_.h };
    }

    Color fill{}, stroke{}, strokeBottom{}, textColor{};
    switch (style_) {
    case ButtonStyle::Accent:
        if (disabled) {
            fill = theme.disabledBackground;
            stroke = theme.controlStroke;
            strokeBottom = theme.controlStroke;
            textColor = theme.textDisabled;
        } else {
            fill = pressed_ ? theme.accentPressed
                 : hover_   ? theme.accentHover
                            : theme.accent;
            stroke = Color{ 0, 0, 0, 0 };
            strokeBottom = Color::Rgb(0x000000, 0.18f);
            textColor = theme.textOnAccent;
        }
        break;
    case ButtonStyle::Subtle:
        // baseline .btn.tbtn[aria-pressed=true]{background:var(--sel)} (0,3,0) beats
        // .btn:hover/.btn:active (0,2,0): the selected background is always --sel; hover/press
        // no longer override it (round-19 I-02). Disabled goes through .btn:disabled (source order after .btn.subtle,
        // same specificity): --dis-bg fill + --stroke border on all sides (R25-03)
        fill = disabled ? theme.disabledBackground
             : active   ? theme.selectedSoft
             : pressed_ ? theme.subtleActive
             : hover_   ? theme.hoverSoft
                        : Color{ 0, 0, 0, 0 };
        stroke = disabled ? theme.controlStroke : Color{ 0, 0, 0, 0 };
        strokeBottom = disabled ? theme.controlStroke : Color{ 0, 0, 0, 0 };
        // baseline .btn:active{color:var(--text2)} applies to subtle too (.btn.subtle
        // sets no color, nothing overrides it later) — pressed text darkens (round-15 §3.6);
        // tbtn selected sets no color, so a static selection stays --text1
        textColor = disabled ? theme.textDisabled
                  : pressed_ ? theme.textSecondary
                             : theme.text;
        break;
    case ButtonStyle::Outline:
        // baseline cascade: .btn.outline{background:transparent} is declared after .btn:hover/
        // :active at the same specificity ⇒ outline buttons keep a transparent background on hover/press
        // (the author only added :hover/:active for .btn.subtle, round-15 §3.7). Disabled goes through
        // .btn:disabled (last in source order, R25-03): --dis-bg fill, --stroke border
        // (α 0.058, not .outline's own --stroke-bot 0.162)
        fill = disabled ? theme.disabledBackground : Color{ 0, 0, 0, 0 };
        stroke = disabled ? theme.controlStroke : theme.controlStrokeBottom;
        strokeBottom = disabled ? theme.controlStroke : theme.controlStrokeBottom;
        textColor = disabled ? theme.textDisabled
                  : pressed_ ? theme.textSecondary
                             : theme.text;
        break;
    case ButtonStyle::Hyperlink:
        textColor = disabled ? theme.textDisabled : theme.accentText;
        break;
    default:
        // selected (tbtn aria-pressed, 0,3,0) outranks hover/press (0,2,0, round-19
        // I-02) and also outranks disabled (R25-03b: Project Conventions :70, CheckBox same-class inference)
        // — disabled+selected keeps the --sel background, drops the border, text stays --text4
        fill = active   ? theme.selectedSoft
             : disabled ? theme.disabledBackground
             : pressed_ ? theme.controlPressed
             : hover_   ? theme.controlHover
                        : theme.control;
        // baseline .btn.tbtn[aria-pressed=true]: the pressed (selected) state drops the border, weight 600
        stroke = active ? Color{ 0, 0, 0, 0 } : theme.controlStroke;
        strokeBottom = active   ? Color{ 0, 0, 0, 0 }
                     : disabled ? theme.controlStroke
                                : theme.controlStrokeBottom;
        // baseline .btn:active{color:var(--text2)} (standard buttons darken text on press, §3.6;
        // accent buttons take the Accent branch; the baseline ships :active{color:var(--on-accent)} for them)
        textColor = disabled ? theme.textDisabled
                  : pressed_ ? theme.textSecondary
                             : theme.controlText;
        break;
    }
    if (textOverride_) textColor = *textOverride_;

    if (style_ == ButtonStyle::Hyperlink) {
        if (disabled)   // .btn:disabled fill still applies; .btn.hyper{border:0} has no border (R25-03)
            p.FillRoundedRect(bounds_, kRadiusControl, theme.disabledBackground);
        p.DrawText(text_, font_, textArea, textColor, HAlign::Center, VAlign::Center);
        if (!disabled && hover_) {   // underline
            float w = detail::MeasureTextWidth(text_, font_);
            float cx = bounds_.x + bounds_.w * 0.5f;
            p.FillRect({ cx - w / 2, bounds_.Bottom() - 4, w, 1 }, textColor);
        }
        return;
    }

    p.FillRoundedRect(bounds_, kRadiusControl, fill);
    // border: 1px overall + darkened 1px bottom edge, along the 4px radius (baseline .btn
    // border-radius:4px — previously fill was rounded but stroke square, bulging 1–2 px at corners, round-19 F-01).
    // the bottom-edge row is cut out of the full ring and drawn once (baseline border-bottom-color is a single value,
    // no α compounding — previously ring+bottom overdrawn, disabled bottom edge 1.87×, E-04)
    Rect strokeRect{ bounds_.x + 0.5f, bounds_.y + 0.5f, bounds_.w - 1, bounds_.h - 1 };
    if (stroke.a > 0) {
        p.PushClip({ bounds_.x, bounds_.y, bounds_.w, bounds_.h - 1 });
        p.StrokeRoundedRect(strokeRect, kRadiusControl, stroke);
        p.PopClip();
    }
    if (strokeBottom.a > 0) {
        p.PushClip({ bounds_.x, bounds_.Bottom() - 1, bounds_.w, 1 });
        p.StrokeRoundedRect(strokeRect, kRadiusControl, strokeBottom);
        p.PopClip();
    }
    if (!icon_.IsEmpty())
        p.DrawIcon(icon_, iconArea, textColor);
    if (badge_ > 0) {   // baseline .badge-n has no base style ⇒ the count is plain small text after the label (not a pill/corner badge)
        std::wstring bt = std::to_wstring(badge_);
        p.DrawText(bt, Font{ .size = 11.0f }, badgeRect, textColor,
                   HAlign::Center, VAlign::Center);
    }
    Font textFont = font_;
    if (active) textFont.weight = FontWeight::SemiBold;   // baseline :disabled does not declare
                                    // font-weight ⇒ selected 600 is not overridden by disabled (C-5)
    p.DrawText(text_, textFont, textArea, textColor, HAlign::Center, VAlign::Center);
    DrawFocusRing(this, p, theme);
}

void Button::OnMouseMove(const Point& pos) {
    bool inside = bounds_.Contains(pos.x, pos.y);
    if (hover_ != inside) {
        hover_ = inside;
        Invalidate();
    }
}

void Button::OnMouseLeave() {
    if (hover_) {
        hover_ = false;
        Invalidate();
    }
}

void Button::OnCaptureLost() {
    if (pressed_ || hover_) {
        pressed_ = hover_ = false;
        Invalidate();
    }
}

void Button::OnMouseDown(const Point&) {
    pressed_ = true;
    hover_ = true;
    Invalidate();
}

void Button::OnMouseUp(const Point& pos) {
    bool wasPressed = pressed_;
    pressed_ = false;
    hover_ = bounds_.Contains(pos.x, pos.y);
    Invalidate();
    if (wasPressed && hover_) Activate();
}

// ---------------------------------------------------------------------------
// text box implementation
// ---------------------------------------------------------------------------

TextBox& TextBox::SetText(std::wstring_view text) {
    text_.assign(text.begin(), text.end());
    caret_ = anchor_ = text_.size();
    scrollX_ = 0;
    Relayout();              // DesiredSize depends on text_ (all SetText family call this, R25-08)
    EnsureCaretVisible();    // caret at the end; long text must scroll into view
    Invalidate();
    // programmatic SetText resets history (same contract as native SetWindowText; AutoSuggest
    // suggestion clicks also come through here — a suggestion replacing text is a new context, not merged with typed history)
    undoStack_.clear();
    redoStack_.clear();
    typingGroup_ = false;

    return *this;
}

TextBox& TextBox::SetFont(const Font& font) {
    font_ = font;
    Relayout();              // DesiredSize depends on font_ (R25-08)
    EnsureCaretVisible();
    Invalidate();

    return *this;
}

TextBox& TextBox::SetPassword(bool revealable) {
    password_ = true;
    revealed_ = false;
    revealable_ = revealable;
    EnsureCaretVisible();   // GAP50-06: masked mode measures by the dot string; re-clamp scrolling
    Invalidate();

    return *this;
}

TextBox& TextBox::SetSuggestions(std::vector<std::wstring> items) {
    suggestions_ = std::move(items);
    UpdateSuggestPopup();

    return *this;
}

Size TextBox::DesiredSize() const {
    // +18 = ContentLeft 11 + ContentRight 7 (same "border-inclusive" contract, C-8 —
    // previously +24 overcounted by 6 DIP, contradicting its own Content*())
    return { std::max(200.0f, detail::MeasureTextWidth(text_, font_) + 18.0f),
             32.0f };
}

float TextBox::BaselineOffset(float crossSize) const {
    // text is vertically centered in the fixed-height box (OnPaint VAlign::Center); the baseline follows
    const std::wstring& t = text_.empty() ? placeholder_ : text_;
    if (t.empty()) return crossSize;
    DWRITE_TEXT_METRICS m = detail::MeasureTextMetrics(t, font_);
    return std::max(0.0f, crossSize - m.height) * 0.5f +
           detail::MeasureTextBaseline(t, font_);
}

float TextBox::ContentRight() const {
    // the text area's right edge is inset: 1px stroke + 6px padding, embedded buttons each take (box + 6 gap)
    // (baseline .tb padding-right 6 + gap 6; searchbox-variant buttons are 22 ⇒ each takes 28)
    float b = searchStyle_ ? 22.0f : 24.0f;
    float w = 7.0f;
    if (password_ && revealable_) w += b + 6.0f;
    if (clearButton_ && !text_.empty()) w += b + 6.0f;
    return w;
}

/// Embedded button rects (widget-local): 0=clear 1=eye (clear sits left of the eye).
/// Baseline .tb .icon-btn: 24×24 vertically centered, 7 from the right edge (1px stroke + 6px padding),
/// 6px between buttons; the searchbox variant .searchbox .icon-btn is 22×22 (G-1).
Rect TextBox::ButtonRect(int index) const {
    bool eye = password_ && revealable_;
    bool clear = clearButton_ && !text_.empty();
    float b = searchStyle_ ? 22.0f : 24.0f;
    float y = (bounds_.h - b) * 0.5f;
    if (index == 1) {
        if (!eye) return {};
        return { bounds_.w - (7.0f + b), y, b, b };
    }
    if (!clear) return {};
    return { bounds_.w - (7.0f + b + (eye ? b + 6.0f : 0.0f)), y, b, b };
}

float TextBox::ContentLeft() const {
    // 1px stroke + 10px padding (baseline .tb padding-left 10 + border 1);
    // uses the same "border-inclusive" contract as ContentRight; previously the left side was 1 px short (round-17 I-08).
    // searchbox variant: baseline .searchbox flex gap:8px ⇒ icon 16 + 8 = 24 (G-2)
    return 11.0f + (leadingIcon_.IsEmpty() ? 0.0f : (searchStyle_ ? 24.0f : 22.0f));
}

void TextBox::OnPaint(Painter& p, const Theme& theme) {
    // baseline style: widget fill + 1px stroke (darker bottom edge); focused = accent bottom edge (fill unchanged;
    // .tb has no pure hover brightening, B-05)
    bool disabled = !Enabled();
    Color fill = disabled ? theme.disabledBackground : theme.inputBackground;
    p.FillRoundedRect(bounds_, kRadiusControl, fill);
    Color bottomStroke = disabled ? theme.controlStroke
                       : Focused() ? theme.accent
                                   : theme.controlStrokeBottom;
    // the full ring's bottom 1px row is cut out and the bottom row strokes bottomStroke separately — each drawn once
    // (previously the bottom row was overdrawn by ring+band, α compounding 1.35×, E-04)
    Rect strokeRect{ bounds_.x + 0.5f, bounds_.y + 0.5f,
                     bounds_.w - 1, bounds_.h - 1 };
    p.PushClip({ bounds_.x, bounds_.y, bounds_.w, bounds_.h - 1 });
    p.StrokeRoundedRect(strokeRect, kRadiusControl, theme.controlStroke);
    p.PopClip();
    // recolor the entire bottom edge (including the lowest row of both corner arcs)
    p.PushClip({ bounds_.x, bounds_.Bottom() - 1, bounds_.w, 1 });
    p.StrokeRoundedRect(strokeRect, kRadiusControl, bottomStroke);
    p.PopClip();

    if (!leadingIcon_.IsEmpty())
        p.DrawIcon(leadingIcon_, { bounds_.x + 11, bounds_.y + (bounds_.h - 16.0f) / 2.0f,
                                   16, 16 }, theme.textTertiary);

    float cr = ContentRight();
    Rect content{ bounds_.x + ContentLeft(), bounds_.y,
                  std::max(0.0f, bounds_.w - ContentLeft() - cr), bounds_.h };
    p.PushClip(bounds_);
    Color textColor = disabled ? theme.textDisabled : theme.controlText;
    if (caret_ != anchor_ && !disabled) {
        size_t lo = std::min(caret_, anchor_), hi = std::max(caret_, anchor_);
        float x1 = CaretX(lo) - scrollX_, x2 = CaretX(hi) - scrollX_;
        p.FillRect({ content.x + x1, bounds_.y, x2 - x1, bounds_.h },
                   theme.selectedSoft);
    }
    if (text_.empty()) {
        // placeholder text: shown when empty (kept when focused too, matching native behavior)
        if (!placeholder_.empty())
            p.DrawText(placeholder_, font_, content, theme.textTertiary,
                       HAlign::Left, VAlign::Center);
    } else if (password_ && !revealed_) {
        // native password mask U+2022 (B-02). PERF57-01: the mask string became a member scratch
        // buffer (MaskedText); ≥8 code units exceed SSO, previously 1 heap allocation per repaint
        p.DrawText(MaskedText(text_.size()), font_,
                   { content.x - scrollX_, bounds_.y, bounds_.w + scrollX_, bounds_.h },
                   textColor, HAlign::Left, VAlign::Center);
    } else {
        // the text area shifts left by scrollX_, tracking the caret horizontally
        p.DrawText(text_, font_,
                   { content.x - scrollX_, bounds_.y, bounds_.w + scrollX_, bounds_.h },
                   textColor, HAlign::Left, VAlign::Center);
    }
    if (Focused() && !disabled && caretVisible_) {
        // caret takes the widget's text color: the baseline never sets caret-color ⇒ currentColor = --text1
        // (previously wrongly accent, round-17 I-06)
        p.FillRect({ content.x + CaretX(caret_) - scrollX_, bounds_.y + 7,
                     1, bounds_.h - 14 }, theme.controlText);
    }
    p.PopClip();

    // embedded buttons: clear (×) and password reveal (eye) — 24×24 vertically centered, 7 from the right edge
    // (baseline .tb .icon-btn 24×24 / glyph 14×14, colored --text2; .searchbox .icon-btn uses
    // --text3 — color by box kind, N-03b: the old leadingIcon_ heuristic miscolored .tb boxes with a leading icon)
    if (clearButton_ && !text_.empty() && !disabled) {
        Rect r = ButtonRect(0).Translated(bounds_.x, bounds_.y);
        if (pressedBtn_ == 0 || hoverBtn_ == 0)
            p.FillRoundedRect(r, kRadiusControl,
                              pressedBtn_ == 0 ? theme.subtleActive : theme.hoverSoft);
        Color glyph = searchStyle_ ? theme.textTertiary : theme.textSecondary;
        // ink 8px (baseline .i 14 box, x path 12 units × 14/24 ≈ 8.1, B-01);
        // the searchbox's 22 box also takes 8px centered ink (baseline .searchbox .icon-btn has no .i
        // override ⇒ glyph 16 box, ink ≈ 8.7, converging to 8 anyway)
        float o = searchStyle_ ? 7.0f : 8.0f;
        p.DrawLine({ r.x + o, r.y + o }, { r.x + o + 8, r.y + o + 8 }, glyph, 1.1f);
        p.DrawLine({ r.x + o + 8, r.y + o }, { r.x + o, r.y + o + 8 }, glyph, 1.1f);
    }
    if (password_ && revealable_ && !disabled) {
        Rect r = ButtonRect(1).Translated(bounds_.x, bounds_.y);
        if (pressedBtn_ == 1 || hoverBtn_ == 1)
            p.FillRoundedRect(r, kRadiusControl,
                              pressedBtn_ == 1 ? theme.subtleActive : theme.hoverSoft);
        // ink 12×8 including stroke (baseline .tb .icon-btn .i 14 box, I-04 — previously 16×12)
        Rect eye{ r.x + 6.6f, r.y + 8.6f, 10.8f, 6.8f };
        p.StrokeEllipse(eye, theme.textSecondary, 1.2f);
        p.FillEllipse({ eye.x + 3.4f, eye.y + 1.4f, 4, 4 }, theme.textSecondary);
        if (revealed_)   // revealed state adds a slash
            p.DrawLine({ eye.x - 3, eye.Bottom() + 3 },
                       { eye.Right() + 3, eye.y - 3 }, theme.textSecondary, 1.2f);
    }
}

void TextBox::OnMouseDown(const Point& pos) {
    if (!Enabled()) return;
    Point local{ pos.x - bounds_.x, pos.y - bounds_.y };
    if (clearButton_ && !text_.empty() &&
        ButtonRect(0).Contains(local.x, local.y)) {
        pressedBtn_ = 0;   // pressed state, commit on release (baseline .icon-btn:active, M-05)
        Invalidate();
        return;
    }
    if (password_ && revealable_ && ButtonRect(1).Contains(local.x, local.y)) {
        pressedBtn_ = 1;
        Invalidate();
        return;
    }
    SetFocus();
    typingGroup_ = false;   // click-relocating the caret ends the consecutive-typing group
    caret_ = IndexAt(local.x);
    anchor_ = caret_;
    dragging_ = true;
    caretVisible_ = true;
    Invalidate();
}

void TextBox::OnMouseMove(const Point& pos) {
    if (!Enabled()) return;
    bool inside = bounds_.Contains(pos.x, pos.y);
    if (hover_ != inside) {
        hover_ = inside;
        Invalidate();
    }
    Point local{ pos.x - bounds_.x, pos.y - bounds_.y };
    int btn = -1;
    if (clearButton_ && !text_.empty() && ButtonRect(0).Contains(local.x, local.y))
        btn = 0;
    else if (password_ && revealable_ && ButtonRect(1).Contains(local.x, local.y))
        btn = 1;
    if (hoverBtn_ != btn) {
        hoverBtn_ = btn;
        Invalidate();
    }
    if (pressedBtn_ >= 0 && !ButtonRect(pressedBtn_).Contains(local.x, local.y)) {
        pressedBtn_ = -1;   // dragged off the button: cancel the pressed state
        Invalidate();
    }
    if (dragging_) MoveCaret(IndexAt(local.x), true);
}

void TextBox::OnMouseLeave() {
    if (hover_) {
        hover_ = false;
        Invalidate();
    }
    if (pressedBtn_ >= 0) {
        pressedBtn_ = -1;
        Invalidate();
    }
}

void TextBox::OnCaptureLost() {
    // after Alt+Tab etc. steal capture no WM_LBUTTONUP arrives: selection dragging and the clear/reveal
    // buttons' pressed states are cancelled together (round-19 F-03, same contract as round-15's 7 widgets)
    if (pressedBtn_ >= 0) {
        pressedBtn_ = -1;
        Invalidate();
    }
    if (dragging_) {
        dragging_ = false;
        Invalidate();
    }
}

void TextBox::OnMouseUp(const Point& pos) {
    if (pressedBtn_ >= 0) {
        int btn = pressedBtn_;
        pressedBtn_ = -1;
        Point local{ pos.x - bounds_.x, pos.y - bounds_.y };
        if (btn == 0 && ButtonRect(0).Contains(local.x, local.y)) {
            SaveUndo(false);   // clear is one undoable edit (R36-02)
            text_.clear();
            caret_ = anchor_ = 0;
            scrollX_ = 0;
            hoverBtn_ = -1;
            Invalidate();
            NotifyChanged();
            return;
        }
        if (btn == 1 && ButtonRect(1).Contains(local.x, local.y)) {
            revealed_ = !revealed_;
            // GAP50-06: mask↔plain switching discontinuously changes CaretX's measurement basis (dot string vs
            // real text, R37-02); scrollX_ must be re-clamped on the new basis
            EnsureCaretVisible();
            Invalidate();
            return;
        }
        Invalidate();
        return;
    }
    dragging_ = false;
}

void TextBox::OnFocused() {
    caretVisible_ = true;
    if (window_ && window_->impl_ && window_->impl_->hwnd)
        SetTimer(window_->impl_->hwnd, detail::kCaretTimerId, 500, nullptr);
    UpdateSuggestPopup();
    Invalidate();
}

void TextBox::OnUnfocused() {
    if (window_ && window_->impl_ && window_->impl_->hwnd)
        KillTimer(window_->impl_->hwnd, detail::kCaretTimerId);
    dragging_ = false;
    anchor_ = caret_;   // collapse the selection
    CloseSuggestPopup();
    Invalidate();
}

void TextBox::OnTimer() {
    caretVisible_ = !caretVisible_;
    Invalidate();
}

// ---- word-editing helpers (R27-03) ----------------------------------------------
// semantics = measured ground truth of the baseline's native <input> (r28wordnav.py, CDP-trusted keys):
// a word = a run of ASCII alphanumerics/underscores; breaks at Latin↔CJK transitions (measured at the CJK↔foo_bar and
// CJK|测试 boundaries); a CJK run is one word (Ctrl+→ steps from 测 to before 。 in one move);
// punctuation runs form their own segment; whitespace after a word skips with it (Ctrl+→ from hello| skips the space).
inline bool TbxIsSpace(wchar_t c) { return iswspace(c) != 0; }
inline bool TbxIsWord(wchar_t c) {
    if (c < 0x80) return iswalnum(c) != 0 || c == L'_';
    if (TbxIsSpace(c)) return false;
    // fullwidth punctuation/symbols are separators (。，、！ and the fullwidth-space half region)
    return !((c >= 0x3000 && c <= 0x303F) || (c >= 0xFF00 && c <= 0xFF65));
}
inline bool TbxIsAsciiWord(wchar_t c) {
    return c < 0x80 && (iswalnum(c) != 0 || c == L'_');
}

void TextBox::OnChar(wchar_t ch) {
    if (ch == L'\b') {
        if (caret_ != anchor_ || caret_ > 0) SaveUndo(false);
        if (caret_ != anchor_) {
            EraseSelection();
        } else if (caret_ > 0) {
            size_t start;
            if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
                start = WordStart(caret_);   // Ctrl+Backspace deletes to the word start (R27-03)
            else
                start = PrevCodePoint(caret_);   // R37-01: step by code point, never splitting surrogate pairs
            text_.erase(start, caret_ - start);
            MoveCaret(start, false);
        }
        caretVisible_ = true;
        Invalidate();
        NotifyChanged();
        return;
    }
    if (ch >= 0x20) {   // filter control characters (\r\n\t etc.; Ctrl+Z/Y's control codes 0x1A/0x19 are filtered here too)
        InsertText(std::wstring(1, ch), /*coalesce=*/true);
    }
}

void TextBox::OnKeydown(uint32_t vk) {
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool handled = true;
    switch (vk) {
    case 'A': if (ctrl) MoveCaret(text_.size(), false), anchor_ = 0; break;
    case 'C': if (ctrl) CopyToClipboard(false); break;
    case 'X': if (ctrl) CopyToClipboard(true); break;
    case 'V': if (ctrl) PasteFromClipboard(); break;
    case 'Z': if (ctrl) (shift ? Redo() : Undo()); break;   // Ctrl+Shift+Z = redo
    case 'Y': if (ctrl) Redo(); break;
    case VK_RETURN:
        // R12: Enter commits when subscribed; unsubscribed keeps the exact previous
        // fall-through (handled = false — no caret nudge, zero observable change)
        if (OnCommitted) {
            OnCommitted(text_);
            break;
        }
        handled = false;
        break;
    case VK_LEFT: case VK_RIGHT: {
        int step = vk == VK_LEFT ? -1 : 1;
        if (ctrl) {   // move/select by word (R27-03; boundaries = measured native baseline input)
            MoveCaret(step < 0 ? WordStart(caret_) : WordEnd(caret_), shift);
            break;
        }
        // surrogate pairs (emoji etc.) skip as a whole — the extracted code-point stepping is shared with backspace/delete (R37-01)
        size_t next = step < 0 ? PrevCodePoint(caret_) : NextCodePoint(caret_);
        MoveCaret(next, shift);
        break;
    }
    case VK_HOME: MoveCaret(0, shift); break;
    case VK_END:  MoveCaret(text_.size(), shift); break;
    case VK_DELETE:
        if (caret_ != anchor_ || caret_ < text_.size()) SaveUndo(false);
        if (caret_ != anchor_) {
            EraseSelection();
            NotifyChanged();
        } else if (ctrl && caret_ < text_.size()) {
            size_t end = WordEnd(caret_);   // Ctrl+Delete deletes to the word end (R27-03)
            text_.erase(caret_, end - caret_);
            EnsureCaretVisible();
            Invalidate();
            NotifyChanged();
        } else if (caret_ < text_.size()) {
            size_t next = NextCodePoint(caret_);   // R37-01: step by code point
            text_.erase(caret_, next - caret_);
            EnsureCaretVisible();
            Invalidate();
            NotifyChanged();
        }
        break;
    default: handled = false; break;
    }
    if (handled) caretVisible_ = true;
}

float TextBox::CaretX(size_t index) const {
    // R37-02: masked mode measures by the mask string (dots) — the actually drawn glyphs are U+2022; measuring
    // by the real text misplaces caret/selection/horizontal scrolling (measured up to 11.45 DIP).
    // the mask string is the same length as text_, index-for-index. PERF57-01: shares the drawing path's
    // MaskedText member scratch; previously every call built a fresh string (≥8 code units = a heap allocation)
    if (password_ && !revealed_)
        return detail::MeasureTextWidth(MaskedText(std::min(index, text_.size())), font_);
    return detail::MeasureTextWidth(std::wstring_view(text_).substr(0, index), font_);
}

bool TextBox::ImeAnchorPoint(Point& out) const {
    // caret bottom (same formula as rendering: top+7 / bottom-7); the composition and candidate windows sit below it
    out.x = bounds_.x + ContentLeft() + CaretX(caret_) - scrollX_;
    out.y = bounds_.y + bounds_.h - 7.0f;
    return true;
}

size_t TextBox::IndexAt(float localX) const {
    float target = localX - ContentLeft() + scrollX_;
    if (target <= 0) return 0;
    for (size_t i = 1; i <= text_.size(); ++i) {
        if (CaretX(i) > target) return i - 1;
    }
    return text_.size();
}

// word-segment start: skip whitespace first, then take the adjacent segment's start (words break by ASCII/CJK class;
// punctuation runs are their own segment, landing at their start — baseline measured 27→26 (。segment), 13→11 (", "segment))
size_t TextBox::PrevCodePoint(size_t pos) const {
    if (pos == 0) return 0;
    size_t p = pos - 1;
    if (p > 0 && text_[p] >= 0xDC00 && text_[p] < 0xE000 &&
        text_[p - 1] >= 0xD800 && text_[p - 1] < 0xDC00) p -= 1;
    return p;
}

size_t TextBox::NextCodePoint(size_t pos) const {
    if (pos >= text_.size()) return pos;
    size_t n = pos + 1;
    if (text_[pos] >= 0xD800 && text_[pos] < 0xDC00 &&
        n < text_.size() && text_[n] >= 0xDC00 && text_[n] < 0xE000) n += 1;
    return n;
}

size_t TextBox::WordStart(size_t pos) const {
    while (pos > 0 && TbxIsSpace(text_[pos - 1])) pos--;
    if (pos == 0) return 0;
    if (TbxIsWord(text_[pos - 1])) {
        bool ascii = TbxIsAsciiWord(text_[pos - 1]);
        while (pos > 0 && TbxIsWord(text_[pos - 1]) &&
               TbxIsAsciiWord(text_[pos - 1]) == ascii) pos--;
    } else {
        while (pos > 0 && !TbxIsWord(text_[pos - 1]) && !TbxIsSpace(text_[pos - 1])) pos--;
    }
    // surrogate pairs form word boundaries whole (R37-01): scanning by code unit can cut into a pair
    if (pos > 0 && pos < text_.size() &&
        text_[pos - 1] >= 0xD800 && text_[pos - 1] < 0xDC00 &&
        text_[pos] >= 0xDC00 && text_[pos] < 0xE000) pos--;
    return pos;
}

// word-segment end: inside a word, walk to its end and carry trailing whitespace (baseline measured 0→6 crossing "hello ");
// on whitespace/punctuation, cross to the next segment's start (26→27)
size_t TextBox::WordEnd(size_t pos) const {
    size_t n = text_.size();
    // surrogate pairs form word boundaries whole (R37-01): when cut into, carry over the trailing code unit
    auto snap = [this, &pos, n]() {
        if (pos > 0 && pos < n &&
            text_[pos - 1] >= 0xD800 && text_[pos - 1] < 0xDC00 &&
            text_[pos] >= 0xDC00 && text_[pos] < 0xE000) pos++;
    };
    if (pos < n && TbxIsWord(text_[pos])) {
        bool ascii = TbxIsAsciiWord(text_[pos]);
        while (pos < n && TbxIsWord(text_[pos]) &&
               TbxIsAsciiWord(text_[pos]) == ascii) pos++;
        while (pos < n && TbxIsSpace(text_[pos])) pos++;
        snap();
        return pos;
    }
    while (pos < n && !TbxIsWord(text_[pos])) pos++;
    snap();
    return pos;
}

void TextBox::OnDoubleClick(const Point& pos, int clickCount) {
    if (!Enabled()) return;
    SetFocus();
    if (clickCount >= 3) {   // triple-click selects all (baseline measured [0, len])
        anchor_ = 0;
        MoveCaret(text_.size(), true);
    } else {   // double-click selects a word: word + trailing whitespace (baseline measured [0,6] = "hello ")
        Point local{ pos.x - bounds_.x, pos.y - bounds_.y };
        size_t hit = std::min(IndexAt(local.x), text_.size());
        size_t start = hit;
        if (start < text_.size() && TbxIsWord(text_[start])) {
            bool ascii = TbxIsAsciiWord(text_[start]);
            while (start > 0 && TbxIsWord(text_[start - 1]) &&
                   TbxIsAsciiWord(text_[start - 1]) == ascii) start--;
        } else {   // punctuation segment: take the start of the punctuation run it sits in (a click on whitespace selects from there)
            while (start > 0 && !TbxIsWord(text_[start - 1]) &&
                   !TbxIsSpace(text_[start - 1])) start--;
        }
        anchor_ = start;
        MoveCaret(WordEnd(hit), true);
    }
    caretVisible_ = true;
    dragging_ = false;
    Invalidate();
}

void TextBox::MoveCaret(size_t index, bool extendSelection) {
    caret_ = std::min(index, text_.size());
    if (!extendSelection) anchor_ = caret_;
    EnsureCaretVisible();
    Invalidate();
}

void TextBox::EraseSelection() {
    if (caret_ == anchor_) return;
    size_t lo = std::min(caret_, anchor_), hi = std::max(caret_, anchor_);
    text_.erase(lo, hi - lo);
    caret_ = anchor_ = lo;
    EnsureCaretVisible();
    Invalidate();
}

void TextBox::InsertText(std::wstring_view text, bool coalesceUndo) {
    SaveUndo(coalesceUndo);   // typing (true) merges into the consecutive-input step; paste etc. (false) starts a new step
    EraseSelection();
    text_.insert(caret_, std::wstring(text));
    caret_ += text.size();
    anchor_ = caret_;
    EnsureCaretVisible();
    Invalidate();
    NotifyChanged();
}

void TextBox::CopyToClipboard(bool cut) {
    if (caret_ == anchor_ || !window_ || !window_->impl_) return;
    size_t lo = std::min(caret_, anchor_), hi = std::max(caret_, anchor_);
    std::wstring sel = text_.substr(lo, hi - lo);
    HWND hwnd = window_->impl_->hwnd;
    if (!OpenClipboard(hwnd)) return;
    {
        EmptyClipboard();
        HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (sel.size() + 1) * sizeof(wchar_t));
        if (mem) {
            auto* data = static_cast<wchar_t*>(GlobalLock(mem));
            if (data) {
                memcpy(data, sel.c_str(), (sel.size() + 1) * sizeof(wchar_t));
                GlobalUnlock(mem);
                SetClipboardData(CF_UNICODETEXT, mem);
            }
        }
    }
    CloseClipboard();
    if (cut) {
        SaveUndo(false);   // cut = one undoable edit (reaching here implies a selection; already early-returned above)
        EraseSelection();
        NotifyChanged();
    }
}

void TextBox::PasteFromClipboard() {
    if (!window_ || !window_->impl_ || !OpenClipboard(window_->impl_->hwnd)) return;
    std::wstring paste;
    if (HANDLE mem = GetClipboardData(CF_UNICODETEXT)) {
        if (auto* data = static_cast<const wchar_t*>(GlobalLock(mem))) {
            size_t maxLen = GlobalSize(mem) / sizeof(wchar_t), len = 0;
            while (len < maxLen && data[len]) ++len;
            paste.assign(data, len);
            GlobalUnlock(mem);
        }
    }
    CloseClipboard();
    if (!paste.empty()) {
        // multi-line paste keeps only the single line (newlines replaced with spaces)
        for (auto& ch : paste)
            if (ch == L'\r' || ch == L'\n' || ch == L'\t') ch = L' ';
        InsertText(paste);
    }
}

// ---- undo/redo (R36-02) ----------------------------------------------------
// snapshot stack contract: each "edit action" pushes a step; consecutive character input (typing) merges into one step,
// any non-typing edit (backspace/delete/cut/paste/clear/click-relocate) terminates the current typing group.
// restored state includes caret and selection (native same contract).

void TextBox::SaveUndo(bool coalesce) {
    if (coalesce && typingGroup_) return;   // consecutive input merges into the previous step
    if (undoStack_.size() >= kUndoLimit) undoStack_.erase(undoStack_.begin());
    undoStack_.push_back({text_, caret_, anchor_});
    typingGroup_ = coalesce;
    redoStack_.clear();   // a new edit forks: drop the redo chain
}

void TextBox::RestoreUndoState(UndoState&& st) {
    text_ = std::move(st.text);
    caret_ = std::min(st.caret, text_.size());
    anchor_ = std::min(st.anchor, text_.size());
    typingGroup_ = false;
    scrollX_ = 0;   // snapshots exclude scrolling; EnsureCaretVisible recomputes after reset
    Relayout();     // DesiredSize depends on text_ (SetText family contract)
    EnsureCaretVisible();
    Invalidate();
    NotifyChanged();
}

void TextBox::Undo() {
    if (undoStack_.empty()) return;
    redoStack_.push_back({text_, caret_, anchor_});
    UndoState st = std::move(undoStack_.back());
    undoStack_.pop_back();
    RestoreUndoState(std::move(st));
}

void TextBox::Redo() {
    if (redoStack_.empty()) return;
    undoStack_.push_back({text_, caret_, anchor_});
    UndoState st = std::move(redoStack_.back());
    redoStack_.pop_back();
    RestoreUndoState(std::move(st));
}

const wchar_t* TextBox::CursorForClient() const {
    return Enabled() ? IDC_IBEAM : nullptr;   // disabled keeps the default arrow
}

void TextBox::OnContextMenu(const Point& pos) {
    if (OnContextMenuCb) {   // the user wired it up: take over, no default menu stacked
        OnContextMenuCb(pos);
        return;
    }
    if (!window_ || !window_->impl_ || !Enabled()) return;
    CloseSuggestPopup();   // both popup kinds share impl_->flyout: close the suggestion list first
    bool hasSel = caret_ != anchor_;
    bool canPaste = IsClipboardFormatAvailable(CF_UNICODETEXT) != 0;
    Menu menu;
    menu.AddItem(Icon{}, L"剪切", L"Ctrl+X", [this] { CopyToClipboard(true); });
    if (!hasSel) menu.Disabled();   // Disabled() disables the previous item
    menu.AddItem(Icon{}, L"复制", L"Ctrl+C", [this] { CopyToClipboard(false); });
    if (!hasSel) menu.Disabled();
    menu.AddItem(Icon{}, L"粘贴", L"Ctrl+V", [this] { PasteFromClipboard(); });
    if (!canPaste) menu.Disabled();
    menu.AddItem(Icon{}, L"全选", L"Ctrl+A", [this] {
        MoveCaret(text_.size(), false);
        anchor_ = 0;
    });
    if (text_.empty()) menu.Disabled();
    window_->ShowContextMenu(menu, pos, 180.0f);   // baseline .menu.ctx width
}

void TextBox::EnsureCaretVisible() {
    // R38-01: when layout is unset (SetText inside a LayoutBatch after tree build etc.) bounds_.w==0;
    // extrapolating from zero width would push scrollX_ up to the full text width — text/dots scroll entirely out of view;
    // there is nothing to scroll by, so reset to 0 and re-clamp once SetBounds assigns bounds
    if (bounds_.w <= 0) {
        scrollX_ = 0;
        return;
    }
    float view = std::max(0.0f, bounds_.w - ContentLeft() - ContentRight() - 4);
    float x = CaretX(caret_);
    if (x - scrollX_ > view) scrollX_ = x - view;
    if (x - scrollX_ < 0) scrollX_ = x;
    if (scrollX_ < 0) scrollX_ = 0;
}

void TextBox::SetBounds(const Rect& bounds) {
    Widget::SetBounds(bounds);
    // R38-01: recompute scrolling after layout assigns bounds — FlushLayout only assigns bounds_ without
    // re-clamping scrollX_; a stale in-batch value would be frozen in permanently
    EnsureCaretVisible();
}

TextBox& TextBox::SetRevealed(bool revealed) {
    revealed_ = revealed;
    EnsureCaretVisible();   // GAP50-06: mask↔plain discontinuously changes the measurement basis; re-clamp scrolling
    Invalidate();

    return *this;
}

void TextBox::NotifyChanged() {
    UpdateSuggestPopup();
    if (OnChanged) OnChanged();
}

void TextBox::CloseSuggestPopup() {
    if (!suggestActive_) return;
    suggestActive_ = false;
    if (window_ && window_->impl_ && window_->impl_->flyout) {
        std::unique_ptr<detail::Flyout> f = std::move(window_->impl_->flyout);
        window_->impl_->flyout = nullptr;
        f->Close();
    }
}

void TextBox::UpdateSuggestPopup() {
    if (suggestions_.empty() || !Enabled() || !window_ || !window_->impl_ ||
        !Focused()) {
        CloseSuggestPopup();
        return;
    }
    // case-insensitive substring filter; input is trimmed first (baseline filterAsg's
    // value.trim().toLowerCase(), G-3 — "segoe " must match as "segoe")
    std::wstring t = text_;
    size_t tb = 0, te = t.size();
    while (tb < te && iswspace(t[tb])) ++tb;
    while (te > tb && iswspace(t[te - 1])) --te;
    t = t.substr(tb, te - tb);
    for (auto& c : t) c = static_cast<wchar_t>(towlower(c));
    std::vector<detail::Flyout::Item> items;
    for (const auto& s : suggestions_) {
        std::wstring low = s;
        for (auto& c : low) c = static_cast<wchar_t>(towlower(c));
        if (!t.empty() && low.find(t) == std::wstring::npos) continue;
        detail::Flyout::Item it;
        it.text = s;
        it.onClick = [this, s]() {
            if (!window_ || !window_->impl_) return;
            SetText(s);
            if (OnSuggestion) OnSuggestion(s);
        };
        items.push_back(std::move(it));
    }
    if (items.empty()) {
        CloseSuggestPopup();
        return;
    }
    if (suggestActive_ && window_->impl_->flyout &&
        window_->impl_->flyout->IsAlive()) {
        window_->impl_->flyout->SetItems(std::move(items), bounds_.w);
        return;
    }
    CloseSuggestPopup();
    window_->impl_->CloseMenus();
    suggestActive_ = true;
    window_->impl_->flyout = std::make_unique<detail::Flyout>(
        window_->impl_->hwnd, window_->impl_->dpi, window_->GetTheme(),
        bounds_, std::move(items), bounds_.w, this);   // owner=this: input popup (F-01)
}

// ---------------------------------------------------------------------------
// selection control implementations
// ---------------------------------------------------------------------------

Menu& Menu::Disabled() {
    if (!items_.empty()) items_.back().disabled = true;
    return *this;
}

Menu& Menu::Selected() {
    if (!items_.empty()) items_.back().selected = true;
    return *this;
}

CheckBox::CheckBox(std::wstring_view text) {
    text_.assign(text.begin(), text.end());
}

CheckBox& CheckBox::SetText(std::wstring_view text) {
    text_.assign(text.begin(), text.end());
    Relayout();
    Invalidate();

    return *this;
}

CheckBox& CheckBox::SetState(State state) {
    if (state_ != state) {
        state_ = state;
        Invalidate();
    }
    return *this;
}

Size CheckBox::DesiredSize() const {
    return { 18.0f + 9.0f + detail::MeasureTextWidth(text_, Font{}), 32.0f };
}

void CheckBox::OnPaint(Painter& p, const Theme& theme) {
    bool disabled = !Enabled();
    bool on = state_ != State::Unchecked;
    Rect box{ bounds_.x, bounds_.y + (bounds_.h - 18.0f) / 2.0f, 18, 18 };

    bool indet = state_ == State::Indeterminate;
    // baseline .chk input:indeterminate + .box (0,3,1) beats .chk.off .box{--dis-bg}
    // (0,3,0): disabled + indeterminate still shows the accent block + on-accent dash (round-19 I-05;
    // the baseline page has no rendered instance of that combination, inferred from cascade rules)
    Color fill = disabled ? (indet ? theme.accent : theme.disabledBackground)
               : on       ? theme.accent
               : hover_   ? theme.controlHover
                          : theme.control;
    p.FillRoundedRect(box, 4, fill);
    p.StrokeRoundedRect({ box.x + 0.5f, box.y + 0.5f, 17, 17 }, 4,
                        disabled ? (indet ? theme.accent : theme.controlStroke)
                                 : on ? theme.accent : theme.controlStrokeBottom);
    Color mark = indet ? theme.textOnAccent
               : disabled ? theme.textDisabled
                          : theme.textOnAccent;
    if (state_ == State::Checked) {
        // checkmark: the baseline .box::after's 5×9 border box rotated 45°, white core 11×8 (I-03,
        // measured with the same threshold as the baseline) — three center-line points, 2px stroke
        p.DrawLine({ box.x + 14.0f, box.y + 4.8f }, { box.x + 7.0f, box.y + 13.0f },
                   mark, 2.0f);
        p.DrawLine({ box.x + 7.0f, box.y + 13.0f }, { box.x + 3.0f, box.y + 8.8f },
                   mark, 2.0f);
    } else if (state_ == State::Indeterminate) {
        p.FillRoundedRect({ box.x + 4, box.y + 8, 10, 2 }, 1, mark);
    }
    p.DrawText(text_, Font{},
               { box.Right() + 9, bounds_.y,
                 std::max(0.0f, bounds_.w - 27), bounds_.h },
               disabled ? theme.textDisabled : theme.text,
               HAlign::Left, VAlign::Center);
    DrawFocusRing(this, p, theme);
}

void CheckBox::Activate() {
    if (!Enabled()) return;
    if (threeState_) {
        state_ = state_ == State::Unchecked ? State::Indeterminate
               : state_ == State::Indeterminate ? State::Checked
                                                : State::Unchecked;
    } else {
        state_ = state_ == State::Checked ? State::Unchecked : State::Checked;
    }
    Invalidate();
    if (OnChanged) OnChanged(state_);
}

void CheckBox::OnKeydown(uint32_t vk) {
    if (vk == VK_SPACE) Activate();
}

void CheckBox::OnMouseMove(const Point& pos) {
    bool inside = bounds_.Contains(pos.x, pos.y);
    if (hover_ != inside) { hover_ = inside; Invalidate(); }
}

void CheckBox::OnMouseLeave() {
    if (hover_) { hover_ = false; Invalidate(); }
}

void CheckBox::OnCaptureLost() {
    if (pressed_) { pressed_ = false; Invalidate(); }
}

void CheckBox::OnMouseDown(const Point&) {
    if (!Enabled()) return;
    pressed_ = true;
    Invalidate();
}

void CheckBox::OnMouseUp(const Point& pos) {
    bool fire = pressed_ && bounds_.Contains(pos.x, pos.y);
    pressed_ = false;
    Invalidate();
    if (fire) Activate();
}

RadioButton::RadioButton(std::wstring_view text, int group) : group_(group) {
    text_.assign(text.begin(), text.end());
}

RadioButton& RadioButton::SetText(std::wstring_view text) {
    text_.assign(text.begin(), text.end());
    Relayout();
    Invalidate();

    return *this;
}

RadioButton& RadioButton::SetChecked(bool checked) {
    checked_ = checked;
    Invalidate();
    return *this;
}

Size RadioButton::DesiredSize() const {
    return { 18.0f + 9.0f + detail::MeasureTextWidth(text_, Font{}), 32.0f };
}

void RadioButton::OnPaint(Painter& p, const Theme& theme) {
    bool disabled = !Enabled();
    Rect ring{ bounds_.x, bounds_.y + (bounds_.h - 18.0f) / 2.0f, 18, 18 };
    Color fill = disabled ? theme.disabledBackground
               : hover_   ? theme.controlHover
                          : theme.control;
    p.FillEllipse(ring, fill);
    p.StrokeEllipse({ ring.x + 0.5f, ring.y + 0.5f, 17, 17 },
                    disabled ? (checked_ ? theme.textDisabled : theme.controlStroke)
                             : checked_ ? theme.accent : theme.controlStrokeBottom,
                    1.0f);
    if (checked_) {
        Color dot = disabled ? theme.textDisabled : theme.accent;
        p.FillEllipse({ ring.x + 5, ring.y + 5, 8, 8 }, dot);   // baseline inset 4 + border 1 ⇒ 8×8 (B-03)
    }
    p.DrawText(text_, Font{},
               { ring.Right() + 9, bounds_.y,
                 std::max(0.0f, bounds_.w - 27), bounds_.h },
               disabled ? theme.textDisabled : theme.text,
               HAlign::Left, VAlign::Center);
    DrawFocusRing(this, p, theme);
}

void RadioButton::OnMouseMove(const Point& pos) {
    bool inside = bounds_.Contains(pos.x, pos.y);
    if (hover_ != inside) { hover_ = inside; Invalidate(); }
}

void RadioButton::OnMouseLeave() {
    if (hover_) { hover_ = false; Invalidate(); }
}

void RadioButton::OnCaptureLost() {
    if (pressed_) { pressed_ = false; Invalidate(); }
}

void RadioButton::OnMouseDown(const Point&) {
    if (!Enabled()) return;
    pressed_ = true;
    Invalidate();
}

void RadioButton::OnMouseUp(const Point& pos) {
    bool fire = pressed_ && bounds_.Contains(pos.x, pos.y);
    pressed_ = false;
    Invalidate();
    if (!fire || !Enabled() || checked_) return;
    checked_ = true;
    if (window_ && window_->impl_)
        window_->impl_->UncheckRadioGroup(group_, this);
    Invalidate();
    if (OnChecked) OnChecked();
}

void RadioButton::OnKeydown(uint32_t vk) {
    if (!Enabled()) return;
    // arrow keys move and select within the group (native radio behavior, M-04)
    if (vk == VK_UP || vk == VK_LEFT || vk == VK_DOWN || vk == VK_RIGHT) {
        if (window_ && window_->impl_) {
            if (auto* next = window_->impl_->NextRadioInGroup(
                    this, vk == VK_UP || vk == VK_LEFT)) {
                next->checked_ = true;
                window_->impl_->UncheckRadioGroup(group_, next);
                window_->impl_->SetFocused(next);
                next->Invalidate();
                if (next->OnChecked) next->OnChecked();
            }
        }
        return;
    }
    if (vk != VK_SPACE || checked_) return;
    checked_ = true;
    if (window_ && window_->impl_)
        window_->impl_->UncheckRadioGroup(group_, this);
    Invalidate();
    if (OnChecked) OnChecked();
}

ToggleSwitch& ToggleSwitch::SetChecked(bool checked) {
    checked_ = checked;
    // a preset checked state has no animation to push the knob: snap straight to the target position (E-01, otherwise the knob
    // sits at off on the first frame; user clicks still take Activate's motion animation)
    knobX_ = checked_ ? 24.0f : 4.0f;
    Invalidate();
    return *this;
}

/// The toggle's current x (track-local): target 4 (off) or 24 (on, baseline border box +24), animated approach.
float ToggleSwitch::KnobX() const {
    float target = checked_ ? 24.0f : 4.0f;
    float x = knobX_;
    if (std::abs(x - target) < 0.5f) return target;
    return x;   // OnAnimate has already pushed knobX_ toward the target
}

void ToggleSwitch::Activate() {
    if (!Enabled()) return;
    checked_ = !checked_;
    StartAnimation();   // knob motion animation (state visibility, an allowed M-24 exception)
    Invalidate();
    if (OnChanged) OnChanged();
}

void ToggleSwitch::OnKeydown(uint32_t vk) {
    if (vk == VK_SPACE) Activate();
}

void ToggleSwitch::OnAnimate() {
    float target = checked_ ? 24.0f : 4.0f;
    knobX_ += (target - knobX_) * 0.35f;
    if (std::abs(knobX_ - target) < 0.4f) {
        knobX_ = target;
        StopAnimation();
    }
    Invalidate();
}

void ToggleSwitch::OnPaint(Painter& p, const Theme& theme) {
    bool disabled = !Enabled();
    // disabled = baseline .sw.off{opacity:.42} whole-group opacity: composite within the group first (knob pressed onto
    // track color) then blend into the background — different from per-element α multiply (E-05)
    if (disabled) p.PushOpacity(0.42f);
    Rect track{ bounds_.x, bounds_.y + (bounds_.h - 20.0f) / 2.0f,
                std::min(40.0f, bounds_.w), 20.0f };
    // baseline .sw/.tr have no :hover/:active recoloring (B-04)
    Color fill = checked_ ? theme.accent : theme.control;
    p.FillRoundedRect(track, 10, fill);
    // 1px full stroke (around the rounded track)
    p.StrokeRoundedRect({ track.x + 0.5f, track.y + 0.5f, track.w - 1, track.h - 1 },
                        10, checked_ ? theme.accent : theme.controlStrokeBottom);
    float kx = track.x + KnobX();
    if (checked_) {
        // on state: 12px solid knob (white in light / black in dark)
        p.FillEllipse({ kx, track.y + 4, 12, 12 }, theme.textOnAccent);
    } else {
        // off state: 12px knob + a small dot at the track's right end
        p.FillEllipse({ kx, track.y + 4, 12, 12 }, theme.textSecondary);
        p.FillEllipse({ track.x + 30, track.y + 8, 4, 4 }, theme.textTertiary);
    }
    DrawFocusRing(this, p, theme);
    if (disabled) p.PopOpacity();
}

void ToggleSwitch::OnCaptureLost() {
    if (pressed_) { pressed_ = false; Invalidate(); }
}

void ToggleSwitch::OnMouseDown(const Point&) {
    if (!Enabled()) return;
    pressed_ = true;
    Invalidate();
}

void ToggleSwitch::OnMouseUp(const Point& pos) {
    bool fire = pressed_ && bounds_.Contains(pos.x, pos.y);
    pressed_ = false;
    Invalidate();
    if (fire) Activate();
}

// ---------------------------------------------------------------------------
// Slider implementation
// ---------------------------------------------------------------------------

Slider::Slider(float minValue, float maxValue, float value)
    : minValue_(minValue), maxValue_(maxValue), value_(value) {
    value_ = std::clamp(value_, minValue_, maxValue_);
    crossAlign_ = CrossAlign::Start;
}

Slider& Slider::SetValue(float value, bool notify) {
    value_ = std::clamp(value, minValue_, maxValue_);
    Invalidate();
    if (notify && OnChanged) OnChanged(value_);

    return *this;
}

Slider& Slider::SetRange(float minValue, float maxValue) {
    minValue_ = minValue;
    maxValue_ = maxValue;
    value_ = std::clamp(value_, minValue_, maxValue_);
    Invalidate();

    return *this;
}

float Slider::ValueAt(float localX) const {
    float t = std::clamp((localX - 9.0f) / std::max(1.0f, bounds_.w - 18.0f),
                         0.0f, 1.0f);
    return minValue_ + t * (maxValue_ - minValue_);
}

void Slider::ApplyStep() {
    if (step_ > 0) value_ = std::round(value_ / step_) * step_;
    value_ = std::clamp(value_, minValue_, maxValue_);
}

void Slider::OnPaint(Painter& p, const Theme& theme) {
    bool disabled = !Enabled();
    // baseline :disabled{opacity:.4} applies to the whole input (E-05 same root cause: per-element α
    // multiply miscalculates "thumb over an already-dimmed track", round-15 §3.1) — the whole group at 40%,
    // elements inside keep original colors, no accent fill
    if (disabled) p.PushOpacity(0.4f);
    float cy = bounds_.y + bounds_.h * 0.5f;
    // track and accent fill span the full widget width (baseline gradient starts at x=0, A-11); only the thumb insets by half width
    float t = (value_ - minValue_) / std::max(0.0001f, maxValue_ - minValue_);
    float tx = bounds_.x + 9.0f + t * std::max(0.0f, bounds_.w - 18.0f);
    p.FillRoundedRect({ bounds_.x, cy - 2, bounds_.w, 4 }, 2, theme.track);
    if (!disabled)
        p.FillRoundedRect({ bounds_.x, cy - 2, std::max(0.0f, t * bounds_.w), 4 }, 2,
                          theme.accent);
    // baseline box-shadow blur 3px ⇒ σ=1.5 (in-library σ=blur×0.5; passing 1.5 used to give σ=0.75,
    // half-tight shadow, §3.9); disabled does not multiply α manually, the group opacity carries it
    p.DrawShadow({ tx - 9, cy - 9, 18, 18 }, 9, 1, 3.0f,
                 Color::Rgb(0x000000, 0.22f));
    // thumb uses the sliderThumb token (white in both themes, visible in dark)
    p.FillEllipse({ tx - 9, cy - 9, 18, 18 }, theme.sliderThumb);
    p.StrokeEllipse({ tx - 8.5f, cy - 8.5f, 17, 17 }, theme.controlStrokeBottom);
    if (disabled) p.PopOpacity();
    DrawFocusRing(this, p, theme);
}

void Slider::OnKeydown(uint32_t vk) {
    if (!Enabled()) return;
    float step = step_ > 0 ? step_ : (maxValue_ - minValue_) / 20.0f;
    switch (vk) {
    case VK_LEFT: case VK_DOWN: SetValue(value_ - step, true); break;
    case VK_RIGHT: case VK_UP:  SetValue(value_ + step, true); break;
    case VK_HOME: SetValue(minValue_, true); break;
    case VK_END:  SetValue(maxValue_, true); break;
    case VK_PRIOR: SetValue(value_ + (maxValue_ - minValue_) / 10.0f, true); break;
    case VK_NEXT:  SetValue(value_ - (maxValue_ - minValue_) / 10.0f, true); break;
    default: return;
    }
}

void Slider::OnMouseDown(const Point& pos) {
    if (!Enabled()) return;
    dragging_ = true;
    value_ = ValueAt(pos.x - bounds_.x);
    ApplyStep();
    Invalidate();
    if (OnChanged) OnChanged(value_);
}

void Slider::OnMouseMove(const Point& pos) {
    if (dragging_ && Enabled()) {
        value_ = ValueAt(pos.x - bounds_.x);
        ApplyStep();
        Invalidate();
        if (OnChanged) OnChanged(value_);
    }
}

void Slider::OnMouseUp(const Point&) {
    dragging_ = false;
}

void Slider::OnCaptureLost() {
    // capture loss (Alt+Tab/system popups) must end the drag, otherwise after returning to the window merely
    // moving the mouse drags the value to the cursor (round-19 F-03 measured: value 40 → 100)
    if (dragging_) {
        dragging_ = false;
        Invalidate();
    }
}

// ---------------------------------------------------------------------------
// ComboBox implementation
// ---------------------------------------------------------------------------

ComboBox::ComboBox() {
    crossAlign_ = CrossAlign::Start;
}

ComboBox& ComboBox::AddItem(std::wstring_view text) {
    items_.emplace_back(text);
    if (selected_ < 0 && items_.size() == 1) selected_ = 0;
    Relayout();
    Invalidate();

    return *this;
}

ComboBox& ComboBox::SetItems(const std::vector<std::wstring>& items) {
    items_ = items;
    selected_ = items_.empty() ? -1 : 0;
    Relayout();
    Invalidate();

    return *this;
}

void ComboBox::ClearItems() {
    items_.clear();
    selected_ = -1;
    Relayout();
    Invalidate();
}

ComboBox& ComboBox::SetSelectedIndex(int index, bool notify) {
    if (index < -1 || index >= static_cast<int>(items_.size())) return *this;
    selected_ = index;
    Invalidate();
    if (notify && OnChanged) OnChanged(selected_);

    return *this;
}

ComboBox& ComboBox::SetTriggerPrefix(std::wstring_view prefix) {
    triggerPrefix_.assign(prefix.begin(), prefix.end());
    Relayout();   // the prefix joins the display text ⇒ DesiredSize changes (same family as SetItems/AddItem, C-2)
    Invalidate();

    return *this;
}

std::wstring ComboBox::SelectedText() const {
    if (selected_ < 0 || selected_ >= static_cast<int>(items_.size())) return {};
    return items_[selected_];
}

Size ComboBox::DesiredSize() const {
    // display text = prefix + item (same source as OnPaint's triggerPrefix_ + SelectedText(),
    // C-1); +46 = padding 12+8 + gap 8 + chevron 16 + border 2
    float textW = 0;
    for (const auto& it : items_)
        textW = std::max(textW,
                         detail::MeasureTextWidth(triggerPrefix_ + it, Font{}));
    return { std::max(minWidth_, textW + 46.0f), 32.0f };
}

void ComboBox::OnPaint(Painter& p, const Theme& theme) {
    bool disabled = !Enabled();
    Color fill = disabled ? theme.disabledBackground
               : pressed_ ? theme.controlPressed
               : hover_   ? theme.controlHover
                          : theme.control;
    p.FillRoundedRect(bounds_, kRadiusControl, fill);
    // baseline .btn: --stroke on three sides, darkened --stroke-bot on the bottom; stroke follows the rounded corners
    p.StrokeRoundedRect({ bounds_.x + 0.5f, bounds_.y + 0.5f,
                          bounds_.w - 1, bounds_.h - 1 },
                        kRadiusControl, theme.controlStroke);
    if (!disabled) {
        // darken the entire bottom edge (including both corner arcs): clip to the lower rounded band and re-stroke as a whole
        p.PushClip({ bounds_.x, bounds_.Bottom() - kRadiusControl,
                     bounds_.w, kRadiusControl });
        p.StrokeRoundedRect({ bounds_.x + 0.5f, bounds_.y + 0.5f,
                              bounds_.w - 1, bounds_.h - 1 },
                            kRadiusControl, theme.controlStrokeBottom);
        p.PopClip();
    }
    Color fg = disabled ? theme.textDisabled : theme.controlText;
    // baseline .cb-btn padding-left 12 + 1px border ⇒ text starts +13 (same "border-inclusive" contract as TextBox's
    // ContentLeft, round-17 I-08)
    p.DrawText(triggerPrefix_ + SelectedText(), Font{},
               { bounds_.x + 13, bounds_.y,
                 std::max(0.0f, bounds_.w - 32), bounds_.h },
               fg, HAlign::Left, VAlign::Center);
    // dropdown chevron (two-segment polyline); rotates 180° while the popup is open. baseline .cb-btn right padding 8
    bool open = PopupOpen();
    float cx = bounds_.Right() - 16, cy = bounds_.y + bounds_.h * 0.5f;
    float d = open ? -1.0f : 1.0f;
    p.DrawLine({ cx - 5, cy - 2.5f * d }, { cx, cy + 2.5f * d }, theme.textSecondary, 1.6f);
    p.DrawLine({ cx, cy + 2.5f * d }, { cx + 5, cy - 2.5f * d }, theme.textSecondary, 1.6f);
    DrawFocusRing(this, p, theme);
}

void ComboBox::OnMouseMove(const Point& pos) {
    bool inside = bounds_.Contains(pos.x, pos.y);
    if (hover_ != inside) { hover_ = inside; Invalidate(); }
}

void ComboBox::OnMouseLeave() {
    if (hover_) { hover_ = false; Invalidate(); }
}

void ComboBox::OnCaptureLost() {
    if (pressed_) { pressed_ = false; Invalidate(); }
}

void ComboBox::OnMouseDown(const Point&) {
    if (!Enabled()) return;
    pressed_ = true;
    Invalidate();
}

void ComboBox::OpenPopup() {
    if (!window_ || !window_->impl_) return;
    window_->impl_->CloseMenus();
    std::vector<detail::Flyout::Item> items;
    for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
        detail::Flyout::Item it;
        it.text = items_[i];
        it.accent = i == selected_;
        it.onClick = [this, i]() { SetSelectedIndex(i, true); };
        items.push_back(std::move(it));
    }
    window_->impl_->flyout = std::make_unique<detail::Flyout>(
        window_->impl_->hwnd, window_->impl_->dpi, window_->GetTheme(),
        bounds_, std::move(items), std::max(minWidth_, bounds_.w), this);
    // ↑ baseline .menu{min-width:100%}: the popup is never narrower than the trigger's real width
    Invalidate();
}

bool ComboBox::PopupOpen() const {
    if (!window_ || !window_->impl_) return false;
    const auto& f = window_->impl_->flyout;
    return f && f->IsAlive() && f->OwnerWidget() == this;
}

void ComboBox::OnKeydown(uint32_t vk) {
    if (!Enabled()) return;
    switch (vk) {
    case VK_SPACE: case VK_RETURN:
        if (!PopupOpen()) OpenPopup();
        break;
    case VK_DOWN:
        if (PopupOpen() && selected_ + 1 < static_cast<int>(items_.size()))
            SetSelectedIndex(selected_ + 1, true);
        else if (!PopupOpen())
            OpenPopup();
        break;
    case VK_UP:
        if (PopupOpen() && selected_ > 0)
            SetSelectedIndex(selected_ - 1, true);
        else if (!PopupOpen())
            OpenPopup();
        break;
    case VK_ESCAPE:
        if (PopupOpen()) window_->impl_->CloseMenus();
        break;
    default: break;
    }
}

void ComboBox::OnMouseUp(const Point& pos) {
    bool fire = pressed_ && bounds_.Contains(pos.x, pos.y);
    pressed_ = false;
    Invalidate();
    if (fire && Enabled()) OpenPopup();
}

// ---------------------------------------------------------------------------
// SplitButton implementation
// ---------------------------------------------------------------------------

SplitButton::SplitButton(std::wstring_view text) {
    text_.assign(text.begin(), text.end());
}

SplitButton& SplitButton::SetText(std::wstring_view text) {
    text_.assign(text.begin(), text.end());
    Relayout();
    Invalidate();

    return *this;
}

Menu& SplitButton::Menu() {
    if (!menu_) menu_ = std::make_unique<class Menu>();
    return *menu_;
}

Size SplitButton::DesiredSize() const {
    // baseline .split: primary segment padding 0 14px; the arrow segment's .menu is hidden so :last-child misses,
    // and it likewise takes .btn's padding 0 14px (round-9 B-04, not the CSS-literal 7px) ⇒ 16 icon + 28 + border
    float w = detail::MeasureTextWidth(text_, font_) + (icon_.IsEmpty() ? 0 : 24) + 29.6f;
    return { std::max(56.0f, w) + kChevW, 32.0f };
}

void SplitButton::OnPaint(Painter& p, const Theme& theme) {
    bool disabled = !Enabled();
    float chevW = kChevW;   // baseline arrow cell: 16px icon + padding 0 14px + border (round-9 B-04)
    Rect main{ bounds_.x, bounds_.y, std::max(0.0f, bounds_.w - chevW), bounds_.h };
    Rect chev{ bounds_.Right() - chevW, bounds_.y, chevW, bounds_.h };
    // the two segments fill independently (matching the baseline .split .btn per-segment hover/active)
    Color mainFill = disabled ? theme.disabledBackground
                   : pressed_ ? theme.controlPressed
                   : hover_   ? theme.controlHover
                              : theme.control;
    Color chevFill = disabled ? theme.disabledBackground
                   : chevPressed_ ? theme.controlPressed
                   : chevHover_   ? theme.controlHover
                                  : theme.control;
    // primary segment: rounded left; inner corners square (rounded rect + square rect completion; baseline radius 0 4px 4px 0)
    p.FillRoundedRect({ main.x, main.y, main.w + kRadiusControl, main.h },
                      kRadiusControl, mainFill);
    p.FillRect({ main.Right(), main.y, kRadiusControl, main.h }, mainFill);
    // arrow segment: square corners (baseline .split .btn:last-child never matches — the JS appends .menu
    // as .split's last child, so the arrow segment only gets .split .btn{border-radius:0};
    // confirmed on both sides by computed styles + pixel profile, round-28 R28-02; the primary segment hits :first-child, its left rounding intact)
    p.FillRect(chev, chevFill);
    // outer stroke: left/right/top --stroke, bottom --stroke-bot (baseline .btn per-side border coloring,
    // E-03). The primary segment follows its left rounding (:first-child hit), the arrow segment square (R28-02);
    // the bottom-edge row is cut out and redrawn separately, no compounding with the ring (round-19 F-01)
    Color edge = theme.controlStroke;
    Color edgeBottom = disabled ? theme.controlStroke : theme.controlStrokeBottom;
    auto strokeSegments = [&](const Color& c) {
        // primary segment: the rounded stroke rect extends past the clip region ⇒ left rounding, right rounding clipped away (square)
        p.PushClip({ bounds_.x, bounds_.y, main.w, bounds_.h });
        p.StrokeRoundedRect({ main.x + 0.5f, main.y + 0.5f,
                              main.w + kRadiusControl, bounds_.h - 1 },
                            kRadiusControl, c);
        p.PopClip();
        // arrow segment: square stroke
        p.PushClip({ chev.x, bounds_.y, chev.w, bounds_.h });
        p.StrokeRect({ chev.x + 0.5f, bounds_.y + 0.5f,
                       chev.w - 1, bounds_.h - 1 }, c);
        p.PopClip();
    };
    strokeSegments(edge);
    p.PushClip({ bounds_.x, bounds_.Bottom() - 1, bounds_.w, 1 });
    strokeSegments(edgeBottom);
    p.PopClip();
    // primary/arrow divider: full height (baseline border-right full height)
    p.FillRect({ main.Right(), bounds_.y, 1, bounds_.h }, theme.divider);
    Color fg = disabled ? theme.textDisabled : theme.controlText;
    Rect textArea = main;
    if (!icon_.IsEmpty()) {
        float textW = detail::MeasureTextWidth(text_, font_);
        float group = 16 + 8 + textW;
        float start = main.x + (main.w - group) * 0.5f;
        p.DrawIcon(icon_, { start, bounds_.y + 8, 16, 16 }, fg);
        textArea = { start + 24, bounds_.y, std::max(0.0f, main.Right() - start - 24), bounds_.h };
    }
    p.DrawText(text_, font_, textArea, fg, HAlign::Center, VAlign::Center);
    float cx = chev.x + chevW * 0.5f;
    float cy = bounds_.y + bounds_.h * 0.5f;
    p.DrawLine({ cx - 4.5f, cy - 2 }, { cx, cy + 2.5f }, fg, 1.6f);
    p.DrawLine({ cx, cy + 2.5f }, { cx + 4.5f, cy - 2 }, fg, 1.6f);
    DrawFocusRing(this, p, theme);
}

void SplitButton::OnMouseMove(const Point& pos) {
    bool inside = bounds_.Contains(pos.x, pos.y);
    bool onChev = inside && pos.x >= bounds_.Right() - kChevW;
    if (hover_ != (inside && !onChev) || chevHover_ != onChev) {
        hover_ = inside && !onChev;
        chevHover_ = onChev;
        Invalidate();
    }
}

void SplitButton::OnMouseLeave() {
    hover_ = chevHover_ = false;
    Invalidate();
}

void SplitButton::OnCaptureLost() {
    if (pressed_ || chevPressed_) {
        pressed_ = chevPressed_ = false;
        Invalidate();
    }
}

void SplitButton::OnMouseDown(const Point& pos) {
    if (!Enabled()) return;
    if (pos.x >= bounds_.Right() - kChevW) chevPressed_ = true;
    else pressed_ = true;
    Invalidate();
}

void SplitButton::OpenMenu() {
    if (!window_ || !window_->impl_ || !menu_) return;
    window_->impl_->CloseMenus();
    std::vector<detail::Flyout::Item> items;
    for (const auto& mi : menu_->items_) {
        detail::Flyout::Item it;
        it.separator = mi.separator;
        it.disabled = mi.disabled;
        it.checkable = mi.checkable;
        it.checked = mi.checked;
        it.icon = mi.icon;
        it.text = mi.text;
        it.shortcut = mi.shortcut;
        it.onClick = mi.onClick;
        items.push_back(std::move(it));
    }
    Rect anchor{ bounds_.Right() - kChevW, bounds_.y, kChevW, bounds_.h };
    window_->impl_->flyout = std::make_unique<detail::Flyout>(
        window_->impl_->hwnd, window_->impl_->dpi, window_->GetTheme(),
        anchor, std::move(items), std::max(150.0f, bounds_.w));
}

void SplitButton::OnMouseUp(const Point& pos) {
    bool onChev = pos.x >= bounds_.Right() - kChevW;
    bool fireMain = pressed_ && !onChev && bounds_.Contains(pos.x, pos.y);
    bool fireChev = chevPressed_ && onChev;
    pressed_ = chevPressed_ = false;
    Invalidate();
    if (!Enabled()) return;
    if (fireMain && OnClick) OnClick();
    if (fireChev) OpenMenu();
}

void SplitButton::OnKeydown(uint32_t vk) {
    if (!Enabled()) return;
    if (vk == VK_RETURN && OnClick) OnClick();
    if (vk == VK_SPACE || vk == VK_DOWN) OpenMenu();
}

// ---------------------------------------------------------------------------
// ListView implementation
// ---------------------------------------------------------------------------

ListView::ListView() = default;

float ListView::RowHeight(int index) const {
    // baseline .li{padding:8px 12px}: a 32px avatar row = 48; rows with subtitles are taller = 55,
    // per-row adaptive (no longer one uniform height for the whole table when mixed)
    if (index >= 0 && index < static_cast<int>(entries_.size()))
        return entries_[index].subtitle.empty() ? 48.0f : 55.0f;
    return 48.0f;
}

int ListView::AddItem(const Icon& icon, std::wstring_view title,
                      std::wstring_view subtitle, std::wstring_view time,
                      Color avatarTop, Color avatarBottom) {
    Entry e;
    e.icon = icon;
    e.title.assign(title.begin(), title.end());
    e.subtitle.assign(subtitle.begin(), subtitle.end());
    e.time.assign(time.begin(), time.end());
    if (avatarTop.a > 0.0f) {
        e.avatarTop = avatarTop;
        e.avatarBottom = avatarBottom;
    }
    entries_.push_back(std::move(e));
    Relayout();
    Invalidate();
    return static_cast<int>(entries_.size()) - 1;
}

void ListView::Clear() {
    entries_.clear();
    selected_ = -1;
    Relayout();
    Invalidate();
}

ListView& ListView::SetSelectedIndex(int index, bool notify) {
    if (index < -1 || index >= static_cast<int>(entries_.size())) return *this;
    selected_ = index;
    Invalidate();
    if (notify && OnSelected) OnSelected(selected_);

    return *this;
}

Size ListView::DesiredSize() const {
    int n = static_cast<int>(entries_.size());
    if (n <= 0) return { 280.0f, 0.0f };
    float h = 0;
    for (int i = 0; i < n; ++i) h += RowHeight(i);
    return { 280.0f, h + 2.0f * (n - 1) };   // 2px gaps between rows (baseline .lv gap:2px)
}

int ListView::RowAt(const Point& local) const {
    // GAP50-01: during capture, out-of-bounds coordinates are still delivered to this widget (WM_MOUSEMOVE's
    // captured ? captured : HitTest routing); without a local.y < 0 guard, hits above the list
    // (negative y) resolve to row 0 — releasing a drag above the list would mis-select and fire OnSelected.
    // Same contract as TreeView::RowAt.
    if (local.x < 0 || local.x >= bounds_.w || local.y < 0) return -1;
    float y = local.y;
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
        float rh = RowHeight(i) + 2.0f;
        if (y < rh) return i;
        y -= rh;
    }
    return -1;
}

void ListView::OnPaint(Painter& p, const Theme& theme) {
    const Font kTitle{ .size = 14.0f };
    const Font kSub{ .size = 12.0f };
    // long text truncates with an ellipsis at the available width (baseline .txt{min-width:0} shrinking in favor of the time slot)
    auto fit = [](const std::wstring& s, const Font& f, float maxW) {
        if (maxW <= 0.0f) return std::wstring();
        if (detail::MeasureTextWidth(s, f) <= maxW) return s;
        std::wstring t = s;
        while (!t.empty() && detail::MeasureTextWidth(t + L'…', f) > maxW)
            t.pop_back();
        return t + L'…';
    };
    float y = bounds_.y;
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
        const auto& e = entries_[i];
        float rh = RowHeight(i);
        Rect row{ bounds_.x, y, bounds_.w, rh };
        if (i == selected_) {
            p.FillRoundedRect(row, 4, theme.selectedSoft);
            // baseline .li.on::before{left:0}: flush with the row's left edge (I-05, previously shifted right by +1)
            p.FillRoundedRect({ row.x, row.y + 8, 3, rh - 16 }, 2, theme.accent);
        } else if (i == hover_) {
            p.FillRoundedRect(row, 4, theme.hoverSoft);
        }
        // avatar (135° diagonal gradient, baseline linear-gradient(135deg,…))
        Rect av{ row.x + 12, row.y + (rh - 32.0f) / 2.0f, 32, 32 };
        if (e.avatarTop) {
            const GradientStop stops[2] = { { 0.0f, *e.avatarTop },
                                            { 1.0f, *e.avatarBottom } };
            p.FillRoundedRectGradientStops(av, 16, stops, 2,
                                           { av.x, av.y },
                                           { av.Right(), av.Bottom() });
        } else {
            p.FillEllipse(av, theme.control);
        }
        if (!e.icon.IsEmpty())
            p.DrawIcon(e.icon, { av.x + 8, av.y + 8, 16, 16 },
                       e.avatarTop ? Color::Rgb(0xFFFFFF)
                                   : theme.textSecondary);
        // time slot: right-aligned, 10 from the top (baseline .time padding-top:2 + row padding 8)
        if (!e.time.empty())
            p.DrawText(e.time, kSub,
                       { row.Right() - 64, row.y + 10, 52, 18 },
                       theme.textTertiary, HAlign::Right, VAlign::Top);
        // the text area shrinks to avoid the time slot; title and subtitle truncate independently (L-03)
        float textX = row.x + 56;
        float textW = std::max(0.0f, row.Right() - 12 - textX -
                               (e.time.empty() ? 0.0f : 64.0f));
        if (e.subtitle.empty()) {
            p.DrawText(fit(e.title, kTitle, textW), kTitle,
                       { textX, row.y, textW, row.h },
                       theme.text, HAlign::Left, VAlign::Center);
        } else {
            p.DrawText(fit(e.title, kTitle, textW), kTitle,
                       { textX, row.y + 9, textW, 20 },
                       theme.text, HAlign::Left, VAlign::Center);
            p.DrawText(fit(e.subtitle, kSub, textW), kSub,
                       { textX, row.y + 29, textW, 18 },
                       theme.textSecondary, HAlign::Left, VAlign::Center);
        }
        y += rh + 2.0f;
    }
}

void ListView::OnMouseMove(const Point& pos) {
    Point local{ pos.x - bounds_.x, pos.y - bounds_.y };
    int row = RowAt(local);
    if (hover_ != row) { hover_ = row; Invalidate(); }
}

void ListView::OnMouseLeave() {
    if (hover_ != -1) { hover_ = -1; Invalidate(); }
}

void ListView::OnMouseUp(const Point& pos) {
    Point local{ pos.x - bounds_.x, pos.y - bounds_.y };
    int row = RowAt(local);
    if (row >= 0 && Enabled()) SetSelectedIndex(row, true);
}

// ---------------------------------------------------------------------------
// TreeView implementation
// ---------------------------------------------------------------------------

TreeView::TreeView() = default;

TreeView::~TreeView() = default;

TreeView::Node& TreeView::AddRoot(std::wstring_view text, const Icon& icon) {
    auto node = std::make_unique<Node>();
    node->text.assign(text.begin(), text.end());
    node->icon = icon;
    Node& ref = *node;
    roots_.push_back(std::move(node));
    rowsDirty_ = true;
    Relayout();
    Invalidate();
    return ref;
}

TreeView::Node& TreeView::AddChild(Node& parent, std::wstring_view text,
                                   const Icon& icon) {
    auto node = std::make_unique<Node>();
    node->text.assign(text.begin(), text.end());
    node->icon = icon;
    node->parent = &parent;
    Node& ref = *node;
    parent.children.push_back(std::move(node));
    rowsDirty_ = true;
    Relayout();
    Invalidate();
    return ref;
}

TreeView& TreeView::SetExpanded(Node& node, bool expanded) {
    node.expanded_ = expanded;
    rowsDirty_ = true;
    Relayout();
    Invalidate();

    return *this;
}

TreeView& TreeView::SetNodeText(Node& node, std::wstring_view text) {
    node.text.assign(text.begin(), text.end());
    // row heights are text-independent (leaf 30 / summary 32), so a repaint suffices
    Invalidate();

    return *this;
}

void TreeView::Clear() {
    roots_.clear();
    rowsDirty_ = true;
    hover_ = keyRow_ = -1;
    Relayout();
    Invalidate();
}

int TreeView::SelectedRow() const {
    if (rowsDirty_) RebuildRows();
    return keyRow_;
}

TreeView& TreeView::SetSelectedRow(int row, bool notify) {
    if (rowsDirty_) RebuildRows();
    // same "reject out-of-range" contract as ComboBox/ListView/NavigationView::SetSelectedIndex
    // (OBS53-01): -1 = clear the selection, anything else outside [0, rows) is ignored
    if (row < -1 || row >= static_cast<int>(rows_.size())) return *this;
    if (keyRow_ != row) { keyRow_ = row; Invalidate(); }
    // notify replays one activation — the same pair a leaf click fires (OnMouseUp); a -1
    // clear has no row to activate and stays silent even with notify
    if (notify && row >= 0) {
        Node* node = rows_[row].node;
        if (node->onClick) node->onClick();
        if (OnClick) OnClick(*node);
    }
    return *this;
}

TreeView& TreeView::Select(Node& node, bool notify) {
    // ownership first (R9.2.4): a node of another tree must not be touched here — no
    // expansion of its ancestors, no selection change anywhere
    bool owned = false;
    std::function<void(const std::vector<std::unique_ptr<Node>>&)> walk =
        [&](const std::vector<std::unique_ptr<Node>>& nodes) {
            for (const auto& n : nodes) {
                if (owned) return;
                if (n.get() == &node) { owned = true; return; }
                walk(n->children);
            }
        };
    walk(roots_);
    if (!owned) return *this;

    // expand collapsed ancestors so the target row is rendered (R9.2.5); the node's own
    // expansion state is left exactly as it is
    bool grew = false;
    for (Node* a = node.parent; a; a = a->parent)
        if (!a->expanded_) { a->expanded_ = true; grew = true; }
    if (grew) { rowsDirty_ = true; Relayout(); }
    if (rowsDirty_) RebuildRows();
    for (int i = 0; i < static_cast<int>(rows_.size()); ++i)
        if (rows_[i].node == &node) { keyRow_ = i; break; }
    Invalidate();
    if (notify) {
        if (node.onClick) node.onClick();
        if (OnClick) OnClick(node);
    }
    return *this;
}

void TreeView::RebuildRows() const {
    // keyRow_ indexes rows_, so rebuilding under it would leave the selection dangling —
    // silently pointing at a different node, or past the end where OnKeydown reads
    // rows_[keyRow_] out of bounds (reachable through the public SetExpanded on an
    // ancestor of the selected row). Re-point the selection to its node first; mouse and
    // keyboard activation always select the row they act on, so internal paths re-point
    // to the same index and only the "selected row got hidden" path observes a change
    // (to -1, R9 round-69).
    Node* selected = keyRow_ >= 0 && keyRow_ < static_cast<int>(rows_.size())
                         ? rows_[keyRow_].node : nullptr;
    rows_.clear();
    std::function<void(const std::vector<std::unique_ptr<Node>>&, int)> walk =
        [&](const std::vector<std::unique_ptr<Node>>& nodes, int depth) {
            for (const auto& n : nodes) {
                bool leaf = n->children.empty();
                rows_.push_back({ n.get(), depth, leaf });
                if (!leaf && n->expanded_) walk(n->children, depth + 1);
            }
        };
    walk(roots_, 0);
    keyRow_ = -1;
    if (selected)
        for (int i = 0; i < static_cast<int>(rows_.size()); ++i)
            if (rows_[i].node == selected) { keyRow_ = i; break; }
    rowsDirty_ = false;
}

Size TreeView::DesiredSize() const {
    if (rowsDirty_) RebuildRows();
    float h = 0;
    for (const auto& r : rows_) h += r.leaf ? 30.0f : 32.0f;
    return { 220.0f, h };
}

int TreeView::RowAt(const Point& local) const {
    if (local.x < 0 || local.x >= bounds_.w || local.y < 0) return -1;
    if (rowsDirty_) RebuildRows();
    float y = 0;
    for (int i = 0; i < static_cast<int>(rows_.size()); ++i) {
        y += rows_[i].leaf ? 30.0f : 32.0f;
        if (local.y < y) return i;
    }
    return -1;
}

void TreeView::OnPaint(Painter& p, const Theme& theme) {
    if (rowsDirty_) RebuildRows();
    static const Icon kChevR = Icon::FromSvgPath("M9.5 5.5 15.5 12l-6 6");
    static const Icon kChevD = Icon::FromSvgPath("M5.5 9.5 12 15.5l6-6");
    const Font kText{ .size = 14.0f };
    const Font kLeaf{ .size = 13.5f };
    // baseline .tree geometry: 25 indent per level (.kids margin 15 + border 1 + padding 9);
    // summary rows put the chevron at 8+25d, folder icon +24, text +48; leaf icons share the column with same-depth
    // chevrons, text +24 (leaves sit one level left of the parent folder — the baseline's alignment pattern).
    // row highlights cover only their own post-indent row box; one indicator line per level (.kids border-left).
    constexpr int kMaxDepth = 15;
    float lineTop[kMaxDepth + 1], lineBottom[kMaxDepth + 1];
    for (int d = 0; d <= kMaxDepth; ++d) { lineTop[d] = -1.0f; lineBottom[d] = -1.0f; }
    auto lineX = [&](int d) { return bounds_.x + 15.0f + 25.0f * (d - 1); };
    auto closeLine = [&](int d) {
        if (lineTop[d] >= 0) {
            p.FillRect({ lineX(d), lineTop[d], 1, lineBottom[d] - lineTop[d] },
                       theme.divider);
            lineTop[d] = -1;
        }
    };
    float y = bounds_.y;
    for (int i = 0; i < static_cast<int>(rows_.size()); ++i) {
        const Row& r = rows_[i];
        int depth = std::min(r.depth, kMaxDepth);
        float rh = r.leaf ? 30.0f : 32.0f;
        for (int d = depth + 1; d <= kMaxDepth; ++d) closeLine(d);   // return to this level
        Rect row{ bounds_.x + 25.0f * depth, y,
                  std::max(0.0f, bounds_.w - 25.0f * depth), rh };
        if (i == hover_)
            p.FillRoundedRect(row, 4, theme.hoverSoft);
        else if (i == keyRow_)
            p.FillRoundedRect(row, 4, theme.selectedSoft);   // keyboard-focus row (U-02)
        if (depth > 0) {   // the indicator line runs through the same-level child block (nested grandchildren included)
            if (lineTop[depth] < 0) lineTop[depth] = y;
            lineBottom[depth] = y + rh;
        }
        float indent = 8.0f + 25.0f * depth;
        if (!r.leaf) {
            Rect chev{ bounds_.x + indent, y + (rh - 16.0f) / 2.0f, 16, 16 };
            p.DrawIcon(r.node->expanded_ ? kChevD : kChevR, chev, theme.textTertiary);
        }
        float iconX = bounds_.x + indent + (r.leaf ? 0.0f : 24.0f);
        float textX = iconX;
        if (!r.node->icon.IsEmpty()) {
            // icons follow the row text color (R25-02: baseline .tree .leaf{color:var(--text2)}
            // and the icon's stroke=currentColor inherits; summary icons inherit --text1)
            // — previously always textTertiary, leaving leaf icons one step paler than their own text
            Color ic = r.node->iconColor.value_or(
                r.leaf ? theme.textSecondary : theme.text);
            p.DrawIcon(r.node->icon,
                       { iconX, y + (rh - 16.0f) / 2.0f, 16, 16 }, ic);
            textX = iconX + 24.0f;
        } else {
            textX = r.leaf ? iconX : bounds_.x + indent + 48.0f;
        }
        p.DrawText(r.node->text, r.leaf ? kLeaf : kText,
                   { textX, y, std::max(0.0f, bounds_.Right() - textX - 8.0f), rh },
                   r.leaf ? theme.textSecondary : theme.text,
                   HAlign::Left, VAlign::Center);
        y += rh;
    }
    for (int d = 1; d <= kMaxDepth; ++d) closeLine(d);
    DrawFocusRing(this, p, theme);
}

void TreeView::OnMouseMove(const Point& pos) {
    Point local{ pos.x - bounds_.x, pos.y - bounds_.y };
    int row = RowAt(local);
    if (hover_ != row) { hover_ = row; Invalidate(); }
}

void TreeView::OnMouseLeave() {
    if (hover_ != -1) { hover_ = -1; Invalidate(); }
}

void TreeView::OnMouseUp(const Point& pos) {
    if (!Enabled()) return;
    Point local{ pos.x - bounds_.x, pos.y - bounds_.y };
    int row = RowAt(local);
    if (row < 0) return;
    if (keyRow_ != row) { keyRow_ = row; Invalidate(); }   // pointer selection joins the keyboard sequence
    Node* node = rows_[row].node;
    if (!rows_[row].leaf) {
        // baseline <summary>: the whole row toggles expand/collapse (L-03; hit step matches drawing, 25/level, L-02)
        node->expanded_ = !node->expanded_;
        rowsDirty_ = true;
        Relayout();
        Invalidate();
        return;
    }
    if (node->onClick) node->onClick();
    if (OnClick) OnClick(*node);
}

void TreeView::OnKeydown(uint32_t vk) {
    if (!Enabled()) return;
    if (rowsDirty_) RebuildRows();
    int last = static_cast<int>(rows_.size()) - 1;
    auto activate = [&](int row) {
        if (row < 0 || row > last) return;
        Node* node = rows_[row].node;
        if (!rows_[row].leaf) {
            node->expanded_ = !node->expanded_;
            rowsDirty_ = true;
            Relayout();
        }
        if (node->onClick) node->onClick();
        if (OnClick) OnClick(*node);
        Invalidate();
    };
    switch (vk) {
    case VK_DOWN:
        keyRow_ = std::min(keyRow_ + 1, last);
        Invalidate();
        break;
    case VK_UP:
        keyRow_ = std::max(keyRow_ - 1, 0);
        Invalidate();
        break;
    case VK_RIGHT:
        if (keyRow_ < 0) break;
        if (!rows_[keyRow_].leaf && !rows_[keyRow_].node->expanded_) {
            rows_[keyRow_].node->expanded_ = true;   // expanding does not change its own row number
            rowsDirty_ = true;
            Relayout();
            Invalidate();
        } else if (keyRow_ < last) {
            keyRow_ += 1;
            Invalidate();
        }
        break;
    case VK_LEFT:
        if (keyRow_ < 0) break;
        if (!rows_[keyRow_].leaf && rows_[keyRow_].node->expanded_) {
            rows_[keyRow_].node->expanded_ = false;  // collapsing does not change its own row number
            rowsDirty_ = true;
            Relayout();
        } else if (rows_[keyRow_].node->parent) {
            Node* parent = rows_[keyRow_].node->parent;
            for (int i = 0; i <= last; ++i)
                if (rows_[i].node == parent) { keyRow_ = i; break; }
        }
        Invalidate();
        break;
    case VK_SPACE: case VK_RETURN:
        activate(keyRow_);
        break;
    default: break;
    }
}

// ---------------------------------------------------------------------------
// InfoBar implementation
// ---------------------------------------------------------------------------

InfoBar::InfoBar(InfoSeverity severity, std::wstring_view title,
                 std::wstring_view message)
    : severity_(severity) {
    title_.assign(title.begin(), title.end());
    message_.assign(message.begin(), message.end());
}

InfoBar& InfoBar::SetTitle(std::wstring_view title) {
    title_.assign(title.begin(), title.end());
    Relayout();
    Invalidate();

    return *this;
}

InfoBar& InfoBar::SetMessage(std::wstring_view message) {
    message_.assign(message.begin(), message.end());
    Relayout();
    Invalidate();

    return *this;
}

InfoBar& InfoBar::SetAction(std::wstring_view text, std::function<void()> onClick) {
    actionText_.assign(text.begin(), text.end());
    onAction_ = std::move(onClick);
    Relayout();
    Invalidate();

    return *this;
}

Rect InfoBar::ActionRect() const {
    if (actionText_.empty()) return {};
    float w = detail::MeasureTextWidth(actionText_, Font{ .size = 14.0f });
    // baseline: text right edge at right−49 (.x 30 + gap 11 + padding 10 − margin 4); text
    // spans the whole hot zone (I-06② — previously 4px on each side put the text's right edge at −53)
    float right = bounds_.w - (closable_ ? 49.0f : 12.0f);
    return { right - w, 0, w, bounds_.h };
}

Rect InfoBar::CloseRect() const {
    if (!closable_) return {};
    // baseline .x 30×30, center at right−21, right edge 6 from the edge (margin-right:-4px) (B-07)
    return { bounds_.w - 36.0f, (bounds_.h - 30.0f) / 2.0f, 30, 30 };
}

Size InfoBar::DesiredSize() const {
    // baseline .ib: padding 12+12 + line box 28 (.x 30 raised 2) = 52 (A-10)
    float h = 52.0f;
    if (!message_.empty() && lastWidth_ > 0.0f) {
        // long copy wraps, bar height grows with content (M-03; lastWidth_ is written back by SetBounds;
        // treated as 52 before the first layout pass, converging via ScrollViewer's second pass)
        float textX = 41.0f;
        float textW;
        if (!actionText_.empty()) {
            float aw = detail::MeasureTextWidth(actionText_, Font{ .size = 14.0f });
            textW = (lastWidth_ - (closable_ ? 49.0f : 12.0f) - aw - 8) - textX - 8;
        } else {
            textW = lastWidth_ - (closable_ ? 44.0f : 14.0f) - textX;
        }
        float avail = std::max(40.0f,
                               textW - detail::MeasureTextWidth(title_, Font{ .size = 14.0f }) - 4.0f);
        float msgH = detail::MeasureTextBlockHeight(message_, Font{ .size = 14.0f }, avail);
        h = std::max(52.0f, 24.0f + std::max(msgH, 28.0f));
    }
    return { 320.0f, h };
}

void InfoBar::SetBounds(const Rect& bounds) {
    bool widthChanged = bounds.w != lastWidth_;
    lastWidth_ = bounds.w;
    Widget::SetBounds(bounds);
    if (widthChanged) Invalidate();
}

void InfoBar::OnPaint(Painter& p, const Theme& theme) {
    Color bg = theme.infoBackground, fg = theme.info;
    switch (severity_) {
    case InfoSeverity::Success: bg = theme.successBackground; fg = theme.success; break;
    case InfoSeverity::Warning: bg = theme.warningBackground; fg = theme.warning; break;
    case InfoSeverity::Error:   bg = theme.errorBackground;   fg = theme.error;   break;
    default: break;
    }
    p.FillRoundedRect(bounds_, 4, bg);
    // semantic icons: info/success/error are a ring + inner symbol, warning a triangle + exclamation mark.
    // slot 16 (text start stays 41); ink scaled by baseline .i 16 box × 2/3 (B-06)
    // icons top-anchor per baseline align-items:flex-start: in-bar center fixed at y+22 (4px above the
    // 52-high bar's center, I-06① — previously vertically centered); action links share the anchor (§3.2)
    float cyi = bounds_.y + 22.0f;
    Rect ring{ bounds_.x + 14, cyi - 8, 16, 16 };
    if (severity_ == InfoSeverity::Warning) {
        // triangle ink 12×10 (baseline 18×15 units × 2/3) + exclamation mark
        float cxm = ring.x + ring.w * 0.5f;
        Point t1{ cxm, cyi - 5 }, t2{ cxm - 6, cyi + 5 }, t3{ cxm + 6, cyi + 5 };
        p.DrawLine(t1, t2, fg, 1.4f);
        p.DrawLine(t2, t3, fg, 1.4f);
        p.DrawLine(t3, t1, fg, 1.4f);
        p.FillRect({ cxm - 0.8f, cyi - 2.2f, 1.6f, 3.6f }, fg);
        p.FillEllipse({ cxm - 1.1f, cyi + 2.4f, 2.2f, 2.2f }, fg);
    } else {
        // ring ink 13px, stroke 1.2 (I-06④ — previously 13.5/1.3)
        p.StrokeEllipse({ ring.x + 2.1f, cyi - 5.9f, 11.8f, 11.8f }, fg, 1.2f);
        if (severity_ == InfoSeverity::Success) {
            p.DrawLine({ ring.x + 4.5f, cyi + 0.4f }, { ring.x + 6.8f, cyi + 2.7f }, fg, 1.4f);
            p.DrawLine({ ring.x + 6.8f, cyi + 2.7f }, { ring.x + 11.2f, cyi - 2.6f }, fg, 1.4f);
        } else if (severity_ == InfoSeverity::Error) {
            p.DrawLine({ ring.x + 5.2f, cyi - 3.2f }, { ring.x + 10.8f, cyi + 3.2f }, fg, 1.4f);
            p.DrawLine({ ring.x + 10.8f, cyi - 3.2f }, { ring.x + 5.2f, cyi + 3.2f }, fg, 1.4f);
        } else {
            p.FillRect({ ring.x + 7.2f, cyi - 1.6f, 1.6f, 4.2f }, fg);
            p.FillEllipse({ ring.x + 6.8f, cyi - 5.2f, 2.4f, 2.4f }, fg);
        }
    }

    // title (semibold) + description inline; icon 16 + gap 11 (baseline .ib gap:11px).
    // title/description share the icon's anchor (flex-start first line): ink center at bar top +22.5, previously
    // in-bar centered +25.5 — the title side of the same §3.2 root cause (newly found in this round's recheck, unlisted in round-15)
    float textX = bounds_.x + 41;
    float textW = ActionRect().x > 0 ? ActionRect().x - textX - 8
                                     : bounds_.Right() - (closable_ ? 44.0f : 14.0f) - textX;
    float titleW = detail::MeasureTextWidth(title_, Font{ .size = 14.0f });
    p.DrawText(title_, Font{ .size = 14.0f, .weight = FontWeight::SemiBold },
               { textX, cyi - 12.0f, std::max(0.0f, textW), 26.0f },
               theme.text, HAlign::Left, VAlign::Center);
    if (!message_.empty()) {
        // the message can wrap (baseline .msg{flex:1;min-width:0}, M-03); first line shares the title's anchor.
        // no extra gap to the title — the baseline's b and span are inline; the gap is only the span's leading space
        // (previously +4 shifted the dash right by 4 px, round-17 I-03).
        // note: centering wrapped continuation lines in the 26 box would sit high — all InfoBars in the audit grid
        // are single-line; multi-line top-anchoring awaits calibration with real DWrite line boxes
        p.DrawText(message_, Font{ .size = 14.0f },
                   { textX + titleW, cyi - 12.0f,
                     std::max(0.0f, textW - titleW), 26.0f },
                   theme.textSecondary, HAlign::Left, VAlign::Center, true);
    }
    if (!actionText_.empty()) {
        Rect ar = ActionRect().Translated(bounds_.x, bounds_.y);
        // baseline .act shares the icon's anchor (flex-start inline): ink center at bar top +21.5,
        // not the in-bar centered +25.5 (round-15 §3.2 — round-14's I-06 only fixed icon top-anchoring and
        // the action's right edge, not the vertical anchors)
        p.DrawText(actionText_, Font{ .size = 14.0f },
                   { ar.x, cyi - 13.0f, ar.w, 26.0f },
                   theme.accentText, HAlign::Center, VAlign::Center);
        if (hover_ == 0) {
            // underline hugs the text baseline (baseline text-decoration:underline, B-08);
            // moves up with the anchor (cy+6 → cyi+6)
            float aw = detail::MeasureTextWidth(actionText_, Font{ .size = 14.0f });
            p.FillRect({ ar.x + ar.w * 0.5f - aw * 0.5f, cyi + 6.0f, aw, 1 },
                       theme.accentText);
        }
        // GAP38-04: keyboard focus ring — first stop when focused-but-not-moved = the action link (or the close ×)
        const int firstStop = actionText_.empty() ? 1 : 0;
        if ((keyFocus_ == 0 || (keyFocus_ < 0 && firstStop == 0)) &&
            Focused() && detail::g_focusFromKeyboard)
            p.StrokeRoundedRect({ ar.x - 2, ar.y - 2, ar.w + 4, ar.h + 4 }, 5,
                                theme.accentText, 2.0f);
    }
    if (closable_) {
        Rect cr = CloseRect().Translated(bounds_.x, bounds_.y);
        if (hover_ == 1) p.FillRoundedRect(cr, 4, theme.hoverSoft);
        float cx = cr.x + 15, cyy = cr.y + 15;
        // × ink 10×10 (baseline .x .i 16 box, I-06③ — previously 8×8). Takes the semantic color:
        // baseline .ib .i{color:var(--sev)} descendant selector hits the close icon (R25-01,
        // previously neutral gray, mismatching the bar's semantic icons)
        p.DrawLine({ cx - 5, cyy - 5 }, { cx + 5, cyy + 5 }, fg, 1.3f);
        p.DrawLine({ cx + 5, cyy - 5 }, { cx - 5, cyy + 5 }, fg, 1.3f);
        // GAP38-04: keyboard focus ring (first stop when the close × is the only activatable item)
        const int firstStop = actionText_.empty() ? 1 : -1;
        if ((keyFocus_ == 1 || (keyFocus_ < 0 && firstStop == 1 &&
                                actionText_.empty())) &&
            Focused() && detail::g_focusFromKeyboard)
            p.StrokeRoundedRect({ cr.x - 2, cr.y - 2, cr.w + 4, cr.h + 4 }, 5,
                                theme.accentText, 2.0f);
    }
}

void InfoBar::OnMouseMove(const Point& pos) {
    Point local{ pos.x - bounds_.x, pos.y - bounds_.y };
    int h = -1;
    if (!actionText_.empty() && ActionRect().Contains(local.x, local.y)) h = 0;
    else if (closable_ && CloseRect().Contains(local.x, local.y)) h = 1;
    if (hover_ != h) { hover_ = h; Invalidate(); }
}

void InfoBar::OnMouseLeave() {
    if (hover_ != -1) { hover_ = -1; Invalidate(); }
}

void InfoBar::OnMouseUp(const Point& pos) {
    if (!Enabled()) return;
    // re-decide the hit by coordinates, never trusting the cached hover_ (L-02): a WM_MOUSELEAVE
    // landing between move→press clears hover_ to -1, silently breaking commit/close previously
    Point local{ pos.x - bounds_.x, pos.y - bounds_.y };
    if (!actionText_.empty() && ActionRect().Contains(local.x, local.y)) {
        if (onAction_) onAction_();
    } else if (closable_ && CloseRect().Contains(local.x, local.y)) {
        SetVisible(false);
    }
}

// GAP38-04: action-link/close-× keyboard path (baseline .act and .icon-btn.x are native
// <button>s) — ←→ toggle between them (focusing whichever exists alone), Enter/Space
// activate (the close × hides via SetVisible(false), same as clicking). Tab is intercepted by WindowImpl for focus cycling
void InfoBar::OnKeydown(uint32_t vk) {
    if (!Enabled()) return;
    bool hasAction = !actionText_.empty();
    if (vk == VK_LEFT || vk == VK_RIGHT) {
        if (keyFocus_ < 0)
            keyFocus_ = hasAction ? 0 : (closable_ ? 1 : -1);
        else if (hasAction && closable_)
            keyFocus_ = keyFocus_ == 0 ? 1 : 0;
        Invalidate();
    } else if (vk == VK_RETURN || vk == VK_SPACE) {
        if (keyFocus_ == 0 && hasAction) {
            if (onAction_) onAction_();
        } else if (keyFocus_ == 1 && closable_) {
            SetVisible(false);
        }
    }
}

// ---------------------------------------------------------------------------
// ProgressBar / ProgressRing / Rating / badge implementations
// ---------------------------------------------------------------------------

ProgressBar::ProgressBar() = default;

ProgressBar& ProgressBar::SetValue(float percent) {
    value_ = std::clamp(percent, 0.0f, 100.0f);
    if (display_ != value_) StartAnimation();   // ease the value toward the new one
    Invalidate();

    return *this;
}

ProgressBar& ProgressBar::SetIndeterminate(bool indeterminate) {
    indeterminate_ = indeterminate;
    if (indeterminate_) StartAnimation();
    Invalidate();

    return *this;
}

ProgressBar& ProgressBar::SetLabel(std::wstring_view text) {
    label_.assign(text.begin(), text.end());
    Relayout();
    Invalidate();

    return *this;
}

Size ProgressBar::DesiredSize() const {
    // labeled box height 27 = bar 3 + gap 6 + label line box 18 (baseline .pgw gap:6px +
    // .lab 12px×line-height 1.5, R25-05; demo spacing changed 10→6 in the same pair)
    return { 160.0f, label_.empty() ? 3.0f : 27.0f };
}

/// CSS cubic-bezier(x1,y1,x2,y2) easing (Newton iteration; B-13 indeterminate progress bar).
static float CubicBezierEasing(float t, float x1, float y1, float x2, float y2) {
    t = std::clamp(t, 0.0f, 1.0f);
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    auto bx = [&](float u) {
        float v = 1.0f - u;
        return 3.0f * u * v * v * x1 + 3.0f * u * u * v * x2 + u * u * u;
    };
    auto by = [&](float u) {
        float v = 1.0f - u;
        return 3.0f * u * v * v * y1 + 3.0f * u * u * v * y2 + u * u * u;
    };
    float u = t;
    for (int i = 0; i < 10; ++i) {
        float err = bx(u) - t;
        if (std::abs(err) < 1e-4f) break;
        float d = (bx(u + 1e-4f) - bx(u - 1e-4f)) / 2e-4f;
        if (std::abs(d) < 1e-6f) break;
        u -= err / d;
    }
    return std::clamp(by(std::clamp(u, 0.0f, 1.0f)), 0.0f, 1.0f);
}

void ProgressBar::OnPaint(Painter& p, const Theme& theme) {
    if (indeterminate_) {
        paintedSinceTick_ = true;
        StartAnimation();   // resume dispatch when scrolled back into view / re-shown (repeat calls are no-ops)
    }
    // baseline order: bar on top, label row below. labY at box top +10: after the box grew 22→27 the ink position
    // exactly matches the old 22 box (Bottom−12), so the R25-05 paired change introduces no label shift
    float barY = bounds_.y;
    float labY = bounds_.y + 10.0f;
    p.FillRoundedRect({ bounds_.x, barY, bounds_.w, 3 }, 2, theme.track);
    if (indeterminate_) {
        float segW = bounds_.w * 0.38f;
        float t = phase_ - std::floor(phase_);
        // baseline @keyframes indet: left -40% → 100%, cubic-bezier(.3,.1,.7,.9);
        // the light segment slides in and out (clipped by intersection with the track, no longer popping out as a block at the start, B-13)
        float e = CubicBezierEasing(t, 0.3f, 0.1f, 0.7f, 0.9f);
        float x = e * 1.40f * bounds_.w - 0.40f * bounds_.w;
        float lo = std::max(bounds_.x, bounds_.x + x);
        float hi = std::min(bounds_.Right(), bounds_.x + x + segW);
        if (hi > lo)
            p.FillRoundedRect({ lo, barY, hi - lo, 3 }, 2, theme.accent);
    } else {
        p.FillRoundedRect({ bounds_.x, barY, bounds_.w * display_ / 100.0f, 3 },
                          2, theme.accent);
    }
    if (!label_.empty()) {
        p.DrawText(label_, Font{ .size = 12.0f },
                   { bounds_.x, labY, bounds_.w, 12 },
                   theme.textSecondary, HAlign::Left, VAlign::Top);
        if (!indeterminate_) {
            std::wstring pct = std::to_wstring(
                static_cast<int>(display_ + 0.5f)) + L"%";
            p.DrawText(pct, Font{ .size = 12.0f },
                       { bounds_.x, labY, bounds_.w, 12 },
                       theme.textSecondary, HAlign::Right, VAlign::Top);
        }
    }
}

void ProgressBar::OnAnimate() {
    if (indeterminate_) {
        // PERF-03: the indeterminate state shares the ProgressRing contract — unpainted while outside the viewport stops the clock
        if (!paintedSinceTick_) {
            StopAnimation();
            return;
        }
        paintedSinceTick_ = false;
        phase_ += 0.022f;   // 33ms/frame ⇒ 1.5s period (baseline animation:indet 1.5s)
    } else {
        // PERF38-01: the determinate state, same contract — otherwise a hidden/off-viewport progress bar triggered by SetValue
        // invalidates the whole window every frame until convergence (the demo's 2.2s tick ⇒ 6-9 whole-window
        // repaints per second, pixels static while CPU burns)
        if (!paintedSinceTick_) {
            StopAnimation();
            return;
        }
        paintedSinceTick_ = false;
        display_ += (value_ - display_) * 0.25f;   // ease the value smoothly
        if (std::abs(display_ - value_) < 0.3f) {
            display_ = value_;
            StopAnimation();
        }
    }
    Invalidate();
}

ProgressRing::ProgressRing(float diameter) : diameter_(diameter) {
    crossAlign_ = CrossAlign::Start;
}

ProgressRing& ProgressRing::SetDiameter(float diameter) {
    diameter_ = diameter;
    Relayout();
    Invalidate();

    return *this;
}

void ProgressRing::OnPaint(Painter& p, const Theme& theme) {
    paintedSinceTick_ = true;
    StartAnimation();   // resume dispatch when scrolled back into view / re-shown (repeat calls are no-ops)
    Point c{ bounds_.x + bounds_.w * 0.5f, bounds_.y + bounds_.h * 0.5f };
    float r = diameter_ * 0.4f;   // baseline 40px ring radius 16
    float t = diameter_ >= 32.0f ? 3.5f : 2.5f;
    p.StrokeEllipse({ c.x - r, c.y - r, r * 2, r * 2 }, theme.track, t);
    // arc length converts from the baseline stroke-dasharray: on segment 34 (or 68) / circumference = 67.64%
    // = 243.5° (same ratio at both sizes; 240° leaves the ring 0.5~1.0 DIP short, round-26 C-12)
    p.StrokeArc(c, r, t, angle_, 243.5f, theme.accent);
}

void ProgressRing::OnAnimate() {
    // PERF-03: OnPaint did not run this tick (scrolled out of the viewport and skipped by ChildPaintClip,
    // widget hidden, window minimized/occluded etc.) ⇒ no repaint is needed; stop the animation and return the 33ms
    // timer to the system; back in view, OnPaint Starts it again
    if (!paintedSinceTick_) {
        StopAnimation();
        return;
    }
    paintedSinceTick_ = false;
    angle_ += 8.0f;   // 45 steps × 33ms ≈ 1.5s/turn (baseline spin 1.5s)
    if (angle_ >= 360.0f) angle_ -= 360.0f;
    Invalidate();
}

RatingControl::RatingControl() {
    crossAlign_ = CrossAlign::Start;
}

RatingControl& RatingControl::SetValue(float value, bool notify) {
    value_ = std::clamp(std::round(value), 0.0f, 5.0f);
    Invalidate();
    if (notify && OnChanged) OnChanged(value_);

    return *this;
}

RatingControl& RatingControl::SetLabel(std::wstring_view text) {
    label_.assign(text.begin(), text.end());
    Relayout();
    Invalidate();

    return *this;
}

Size RatingControl::DesiredSize() const {
    // star track width = 4×32 + 30 = 158 (no trailing gap in the last cell; baseline 5×30 + 4×2 gap = 158, B-05)
    float stars = 4 * 32.0f + 30.0f;
    float label = label_.empty() ? 0.0f : 8.0f + detail::MeasureTextWidth(label_, Font{ .size = 12.0f });
    return { stars + label, 30.0f };
}

void RatingControl::OnPaint(Painter& p, const Theme& theme) {
    static const Icon kStarFull = Icon::FromSvgPath(
        "M12 3.8 14.3 8.7 19.5 9.4 15.7 13.1 16.6 18.4 12 15.9 7.4 18.4 "
        "8.3 13.1 4.5 9.4 9.7 8.7Z");
    static const Icon kStarEmpty = Icon::FromSvgPath(
        "M12 3.8 14.3 8.7 19.5 9.4 15.7 13.1 16.6 18.4 12 15.9 7.4 18.4 "
        "8.3 13.1 4.5 9.4 9.7 8.7Z"
        "M12 6.75 13.73 9.78 16.65 10.22 14.29 12.51 14.85 15.8 12 14.25 "
        "9.15 15.8 9.71 12.51 7.35 10.22 10.57 9.78Z");
    // baseline paintHot only adds hot without clearing on: preview = max(value, hover+1),
    // hovering a low star never "downgrades" the shown rating (round-19 I-01)
    int shown = static_cast<int>(value_);
    if (hover_ >= 0 && hover_ + 1 > shown) shown = hover_ + 1;
    for (int i = 0; i < 5; ++i) {
        Rect cell{ bounds_.x + i * 32.0f, bounds_.y, 30.0f, 30.0f };
        // baseline star ink = 20×15.1/24 + 1.8 ≈ 14.4 DIP; the 22.9 box's fill geometry ≈ the same ink
        // (round-10's 24 box overshot ≈ +11%, B-04)
        float size = 22.9f;
        // pressed state: the held star scales to .88 (baseline .rate button:active svg, U-02)
        if (pressed_ && i == hover_) size *= 0.88f;
        Rect star{ cell.x + (30.0f - size) * 0.5f, cell.y + (30.0f - size) * 0.5f,
                   size, size };
        bool on = i < shown;
        p.DrawIcon(on ? kStarFull : kStarEmpty, star,
                   on ? theme.accent : theme.textTertiary);
    }
    if (!label_.empty())
        p.DrawText(label_, Font{ .size = 12.0f },
                   { bounds_.x + 4 * 32.0f + 30.0f + 8, bounds_.y,
                     std::max(0.0f, bounds_.w - 4 * 32.0f - 30.0f - 8), bounds_.h },
                   theme.textSecondary, HAlign::Left, VAlign::Center);
    DrawFocusRing(this, p, theme);
}

void RatingControl::OnMouseMove(const Point& pos) {
    // only the star grid updates the preview; moving to the right-side label does not leave the .rate container (baseline mouseleave
    // hangs on the container), so the preview persists (round-19 I-01)
    Point local{ pos.x - bounds_.x, pos.y - bounds_.y };
    // GAP50-05: floor instead of truncation toward zero — dragging left of the edge during capture
    // (local.x ∈ (-32,0)) truncates to star=0, making the star<0 guard useless
    int star = static_cast<int>(std::floor(local.x / 32.0f));
    if (star >= 0 && star <= 4 && hover_ != star) { hover_ = star; Invalidate(); }
}

void RatingControl::OnMouseLeave() {
    if (hover_ != -1) { hover_ = -1; Invalidate(); }
    if (pressed_) { pressed_ = false; Invalidate(); }
}

void RatingControl::OnCaptureLost() { OnMouseLeave(); }

void RatingControl::OnMouseDown(const Point& pos) {
    if (!Enabled()) return;
    Point local{ pos.x - bounds_.x, pos.y - bounds_.y };
    // GAP50-05: floor makes the star<0 guard reachable (truncation toward zero would land (-32,0) on star 1)
    int star = static_cast<int>(std::floor(local.x / 32.0f));
    // respond only on the star grid; pressing in the label area does not move the preview (consistent with the baseline's .rlab plain text)
    if (star < 0 || star > 4) return;
    hover_ = star;
    pressed_ = true;
    Invalidate();
}

void RatingControl::OnMouseUp(const Point& pos) {
    // clear the pressed state before the disabled check: if disabled while a star is held, not resetting would
    // stick at the 0.88 scale forever (round-15 §3.15)
    bool wasPressed = pressed_;
    pressed_ = false;
    if (!Enabled()) {
        if (wasPressed) Invalidate();
        return;
    }
    Point local{ pos.x - bounds_.x, pos.y - bounds_.y };
    int star = static_cast<int>(std::floor(local.x / 32.0f));   // GAP50-05
    if (star >= 0 && star <= 4) SetValue(static_cast<float>(star + 1), true);
    else if (wasPressed) Invalidate();
}

void RatingControl::OnKeydown(uint32_t vk) {
    if (!Enabled()) return;
    switch (vk) {
    case VK_RIGHT: case VK_UP: SetValue(value_ + 1.0f, true); break;
    case VK_LEFT: case VK_DOWN: SetValue(value_ - 1.0f, true); break;
    case VK_HOME: SetValue(0.0f, true); break;
    case VK_END:  SetValue(5.0f, true); break;
    default: break;
    }
}

InfoBadge::InfoBadge(int count) : count_(count) {}

InfoBadge& InfoBadge::SetCount(int count) {
    count_ = count;
    Relayout();
    Invalidate();

    return *this;
}

Size InfoBadge::DesiredSize() const {
    if (count_ <= 0) return { 8.0f, 8.0f };
    std::wstring text = std::to_wstring(count_);
    // width = max(18, text + 2×5 padding + 2×2 border): baseline .badge-n padding 0 5px
    // + border 2px, *{box-sizing:border-box} ⇒ the border takes 4px inside the box (R25-04)
    return { std::max(18.0f, detail::MeasureTextWidth(text, Font{ .size = 11.0f, .weight = FontWeight::SemiBold }) + 14.0f), 18.0f };
}

void InfoBadge::OnPaint(Painter& p, const Theme& theme) {
    // baseline badges carry a 2px --bg stroke ring. InfoBadge aligns with the real element .badge-n:
    // the global *{box-sizing:border-box} applies ⇒ the ring draws inside the box, the accent core inset 2px
    // (same contract as Avatar badge I-07). The pseudo-element .dotb::after is not bound by the global rule
    // (content-box, ring outside the box — Button dot B-11 contract); the two are not interchangeable (G-5)
    if (count_ <= 0) {
        p.FillEllipse(bounds_, theme.windowBackground);
        p.FillEllipse({ bounds_.x + 2, bounds_.y + 2, bounds_.w - 4, bounds_.h - 4 },
                      theme.accent);
        return;
    }
    p.FillRoundedRect(bounds_, 9, theme.windowBackground);
    p.FillRoundedRect({ bounds_.x + 2, bounds_.y + 2, bounds_.w - 4, bounds_.h - 4 },
                      7, theme.accent);
    std::wstring text = std::to_wstring(count_);
    p.DrawText(text, Font{ .size = 11.0f, .weight = FontWeight::SemiBold },
               bounds_, theme.textOnAccent, HAlign::Center, VAlign::Center);
}

Avatar::Avatar(std::wstring_view initials) {
    initials_.assign(initials.begin(), initials.end());
}

Avatar& Avatar::SetInitials(std::wstring_view initials) {
    initials_.assign(initials.begin(), initials.end());
    Invalidate();

    return *this;
}

Avatar& Avatar::SetBadge(int count) {
    badge_ = count;
    Invalidate();

    return *this;
}

Avatar& Avatar::SetColors(Color top, Color bottom) {
    top_ = top;
    bottom_ = bottom;
    Invalidate();

    return *this;
}

void Avatar::OnPaint(Painter& p, const Theme& theme) {
    Rect av{ bounds_.x, bounds_.y, 44, 44 };   // baseline .avatar 44×44 (B-10)
    // 135° diagonal gradient (baseline linear-gradient(135deg,#5a5d66,#3f424a))
    const GradientStop stops[2] = {
        { 0.0f, top_ ? *top_ : Color::Rgb(0x5A5D66) },
        { 1.0f, bottom_ ? *bottom_ : Color::Rgb(0x3F424A) } };
    p.FillRoundedRectGradientStops(av, 22, stops, 2,
                                   { av.x, av.y }, { av.Right(), av.Bottom() });
    // baseline avatar content is fixed white (dark theme's textOnAccent is black, unusable)
    p.DrawText(initials_, Font{ .size = 15.0f, .weight = FontWeight::SemiBold },
               av, Color::Rgb(0xFFFFFF), HAlign::Center, VAlign::Center);
    if (badge_ > 0) {
        std::wstring text = std::to_wstring(badge_);
        // width includes the 2×2 border (border-box, R25-04): max(18, text + 10 + 4)
        float w = std::max(18.0f, detail::MeasureTextWidth(
                       text, Font{ .size = 11.0f, .weight = FontWeight::SemiBold }) + 14.0f);
        // baseline .badge-n top:-3 right:-5 anchors the right edge (right edge = av.x+49): for a single digit
        // w=18 this coincides with the old center anchor (40,6); multi-digit no longer shifts right (R25-04 note)
        Rect badge{ av.x + 49.0f - w, av.y - 3.0f, w, 18 };
        // border-box: the 2px background stroke is inside the 18 box ⇒ visible accent 14×14 (I-07,
        // previously accent 18×18 plus a 2px outer ring, 22×22 total)
        p.FillRoundedRect(badge, 9, theme.windowBackground);
        p.FillRoundedRect({ badge.x + 2, badge.y + 2, badge.w - 4, badge.h - 4 },
                          7, theme.accent);
        p.DrawText(text, Font{ .size = 11.0f, .weight = FontWeight::SemiBold },
                   badge, theme.textOnAccent, HAlign::Center, VAlign::Center);
    }
}

// ---------------------------------------------------------------------------
// CommandBar implementation
// ---------------------------------------------------------------------------

CommandBar::CommandBar() = default;

CommandBar& CommandBar::AddButton(const Icon& icon, std::wstring_view label,
                                  std::function<void()> onClick) {
    Item item;
    item.icon = icon;
    item.label.assign(label.begin(), label.end());
    item.onClick = std::move(onClick);
    items_.push_back(std::move(item));
    itemsDirty_ = true;
    Invalidate();
    return *this;
}

CommandBar& CommandBar::AddSeparator() {
    Item item;
    item.separator = true;
    items_.push_back(std::move(item));
    itemsDirty_ = true;
    Invalidate();
    return *this;
}

CommandBar& CommandBar::AddSpace() {
    Item item;
    item.space = true;
    items_.push_back(std::move(item));
    itemsDirty_ = true;
    Invalidate();
    return *this;
}

CommandBar& CommandBar::AddMenuButton(const Icon& icon, std::wstring_view label,
                                      Menu& menu, float minW) {
    Item item;
    item.icon = icon;
    item.label.assign(label.begin(), label.end());
    item.menu = true;
    item.menuOwned = menu;   // ARCH-06: copies into an owned duplicate; signature unchanged, ownership
                             // matches MenuBar (owns) / ShowContextMenu (copies)
    item.minW = minW;
    items_.push_back(std::move(item));
    itemsDirty_ = true;
    Invalidate();
    return *this;
}

CommandBar& CommandBar::SetItemText(int index, std::wstring_view text) {
    if (index < 0 || index >= static_cast<int>(items_.size())) return *this;
    auto& item = items_[index];
    if (item.separator || item.space) return *this;
    item.label.assign(text.begin(), text.end());
    itemsDirty_ = true;   // label width feeds the item rects
    Invalidate();

    return *this;
}

CommandBar& CommandBar::EnableItem(int index, bool enabled) {
    if (index < 0 || index >= static_cast<int>(items_.size())) return *this;
    items_[index].enabled = enabled;
    Invalidate();
    return *this;
}

void CommandBar::Clear() {
    items_.clear();
    itemsDirty_ = true;
    hover_ = pressed_ = keyFocus_ = -1;
    Relayout();
    Invalidate();
}

void CommandBar::LayoutItems() const {
    // width-change self-invalidation (Widget::SetBounds is non-virtual and SetBounds does not mark dirty ⇒ after rescaling
    // the right group and second divider stayed at old positions, L-01)
    if (!itemsDirty_ && lastLayoutW_ == bounds_.w) return;
    const int n = static_cast<int>(items_.size());
    std::vector<float> widths(static_cast<size_t>(n), 0.0f);
    for (int i = 0; i < n; ++i) {
        const auto& item = items_[i];
        if (item.separator) widths[i] = 11.0f;
        else if (item.space) widths[i] = 0.0f;
        else widths[i] = std::max(item.minW > 0.0f ? item.minW : 58.0f,
                                  detail::MeasureTextWidth(item.label, Font{ .size = 11.5f }) + 16.0f);
    }
    int lastSpace = -1;
    for (int i = 0; i < n; ++i)
        if (items_[i].space) lastSpace = i;
    if (lastSpace < 0) lastSpace = n;   // no AddSpace: all items still lay out in the left column (otherwise the whole bar breaks)
    float x = 4.0f;
    for (int i = 0; i < n; ++i) {
        if (i > lastSpace) break;
        items_[i].rect = { x, 4.0f, widths[i], 48.0f };
        x += widths[i] + 2.0f;
    }
    if (lastSpace >= 0 && lastSpace < n) {
        float rightW = 0;
        for (int i = lastSpace + 1; i < n; ++i) rightW += widths[i] + 2.0f;
        float rx = bounds_.w - 4.0f - (rightW - 2.0f);
        for (int i = lastSpace + 1; i < n; ++i) {
            items_[i].rect = { rx, 4.0f, widths[i], 48.0f };
            rx += widths[i] + 2.0f;
        }
        items_[lastSpace].rect = { x, 0, std::max(0.0f, rx - 2.0f - x), 0.0f };
    }
    itemsDirty_ = false;
    lastLayoutW_ = bounds_.w;
}

int CommandBar::ItemAt(const Point& local) const {
    LayoutItems();
    for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
        const auto& item = items_[i];
        // disabled items are un-hittable (API62-03, same as ToolBar::ItemAt): hover, press and
        // release all route through here, so filtering once covers the whole pointer path
        if (!item.separator && !item.space && item.enabled &&
            item.rect.Contains(local.x, local.y))
            return i;
    }
    return -1;
}

Size CommandBar::DesiredSize() const {
    // plain sum of item widths (excluding AddSpace residue, else desired width would depend on allocated width — self-referential, L-01)
    float w = 8.0f;
    for (const auto& item : items_) {
        if (item.space) continue;
        w += (item.separator ? 11.0f
             : std::max(item.minW > 0.0f ? item.minW : 58.0f,
                        detail::MeasureTextWidth(item.label, Font{ .size = 11.5f }) + 16.0f)) +
             2.0f;
    }
    return { w, 56.0f };
}

void CommandBar::OnPaint(Painter& p, const Theme& theme) {
    // baseline .cbar: card2 fill + radius 4 + 4px padding (already inset in layout)
    p.FillRoundedRect(bounds_, 4, theme.cardSecondary);
    LayoutItems();
    for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
        const auto& item = items_[i];
        Rect abs = item.rect.Translated(bounds_.x, bounds_.y);
        if (item.separator) {
            // the 11px slot centers the line (baseline .vsep margin:0 5px + gap 2 ⇒ 7 on each side)
            p.FillRect({ abs.x + 5.0f, bounds_.y + (bounds_.h - 24.0f) / 2.0f, 1, 24 },
                       theme.divider);
            continue;
        }
        if (item.space) continue;
        bool disabled = !item.enabled;   // API62-03: pressed_/hover_ can never hold a disabled
                                         // index (ItemAt filters), only keyFocus_ possibly can
        if (i == pressed_)
            p.FillRoundedRect(abs, 4, theme.subtleActive);
        else if (i == hover_)
            p.FillRoundedRect(abs, 4, theme.hoverSoft);
        // icons stay textSecondary (the baseline only recolors item backgrounds, never icons);
        // content 18+3+17 is vertically centered in the 48 line box: icon top +5, label box +26 (B-14).
        // disabled items gray both glyph layers, same convention as ToolBar
        p.DrawIcon(item.icon, { abs.x + (abs.w - 18.0f) / 2.0f, abs.y + 5, 18, 18 },
                   disabled ? theme.textDisabled : theme.textSecondary);
        p.DrawText(item.label, Font{ .size = 11.5f },
                   { abs.x, abs.y + 26, abs.w, 16 },
                   disabled ? theme.textDisabled : theme.text,
                   HAlign::Center, VAlign::Center);
        // GAP38-03: keyboard focus ring; when focused-but-not-moved the ring lands on the first activatable item
        // (same convention as the Tabs/Expander composite widgets)
        bool firstActivatable = keyFocus_ < 0 &&
            std::none_of(items_.begin(), items_.begin() + i, [](const Item& it) {
                return !it.separator && !it.space && it.enabled;
            });
        if ((i == keyFocus_ || firstActivatable) &&
            Focused() && detail::g_focusFromKeyboard)
            p.StrokeRoundedRect({ abs.x - 2, abs.y - 2, abs.w + 4, abs.h + 4 }, 5,
                                theme.accentText, 2.0f);
    }
}

void CommandBar::OnMouseMove(const Point& pos) {
    Point local{ pos.x - bounds_.x, pos.y - bounds_.y };
    int index = ItemAt(local);
    if (hover_ != index) { hover_ = index; Invalidate(); }
}

void CommandBar::OnMouseLeave() {
    if (hover_ != -1) { hover_ = -1; Invalidate(); }
}

void CommandBar::OnMouseDown(const Point& pos) {
    Point local{ pos.x - bounds_.x, pos.y - bounds_.y };
    pressed_ = ItemAt(local);
    if (pressed_ != -1) Invalidate();
}

void CommandBar::OpenItemMenu(const Item& item) {
    if (!window_ || !window_->impl_ || item.menuOwned.items_.empty()) return;
    window_->impl_->CloseMenus();
    std::vector<detail::Flyout::Item> items;
    for (const auto& mi : item.menuOwned.items_) {
        detail::Flyout::Item it;
        it.separator = mi.separator;
        it.disabled = mi.disabled;
        it.checkable = mi.checkable;
        it.checked = mi.checked;
        it.icon = mi.icon;
        it.text = mi.text;
        it.shortcut = mi.shortcut;
        it.onClick = mi.onClick;
        items.push_back(std::move(it));
    }
    window_->impl_->flyout = std::make_unique<detail::Flyout>(
        window_->impl_->hwnd, window_->impl_->dpi, window_->GetTheme(),
        item.rect.Translated(bounds_.x, bounds_.y), std::move(items), 180.0f,
        nullptr, 6.0f);
    // ↑ button anchors gap 6 below (baseline JS r.bottom + 6, R28-09; other popups use 5 =
    // .menu.up's upward measure, registered round-3 R-05)
}

void CommandBar::OnMouseUp(const Point& pos) {
    Point local{ pos.x - bounds_.x, pos.y - bounds_.y };
    int index = ItemAt(local);
    bool fire = pressed_ != -1 && index == pressed_;
    pressed_ = -1;
    Invalidate();
    if (!fire || index < 0 || !Enabled()) return;
    const Item& item = items_[index];
    if (item.menu) OpenItemMenu(item);
    else if (item.onClick) item.onClick();
}

void CommandBar::OnCaptureLost() {
    if (pressed_ != -1) {   // capture loss cancels the item's pressed state (round-19 F-03)
        pressed_ = -1;
        Invalidate();
    }
}

// GAP38-03: command-item keyboard path (baseline .cb-i is a native <button>) — ←→ cycle keyboard focus
// among activatable items (skipping separators/flexible spacers), Enter/Space activate (menu
// items pop a Flyout, same as clicking). Tab is intercepted by WindowImpl for focus cycling and never reaches here
void CommandBar::OnKeydown(uint32_t vk) {
    if (!Enabled()) return;
    LayoutItems();
    int n = static_cast<int>(items_.size());
    if (n <= 0) return;
    if (vk == VK_LEFT || vk == VK_RIGHT) {
        std::vector<int> sel;
        for (int i = 0; i < n; ++i)
            if (!items_[i].separator && !items_[i].space && items_[i].enabled)
                sel.push_back(i);   // API62-03: the focus walk skips disabled items
        if (sel.empty()) return;
        int pos = 0;
        if (keyFocus_ >= 0) {
            auto it = std::find(sel.begin(), sel.end(), keyFocus_);
            if (it != sel.end()) pos = static_cast<int>(it - sel.begin());
        }
        int step = vk == VK_LEFT ? -1 : 1;
        pos = (pos + step + static_cast<int>(sel.size())) % static_cast<int>(sel.size());
        keyFocus_ = sel[pos];
        Invalidate();
    } else if (vk == VK_RETURN || vk == VK_SPACE) {
        if (keyFocus_ < 0) return;
        const Item& item = items_[keyFocus_];
        if (!item.enabled) return;   // item disabled after it was focused: no activation
        if (item.menu) OpenItemMenu(item);
        else if (item.onClick) item.onClick();
    }
}

// ---------------------------------------------------------------------------
// Expander / Tabs implementation
// ---------------------------------------------------------------------------

Expander::Expander() : Container(true) {
    content_ = &Add<Column>();
}

Expander& Expander::SetHeader(std::wstring_view text) {
    header_.assign(text.begin(), text.end());
    Invalidate();

    return *this;
}

Expander& Expander::SetExpanded(bool expanded) {
    expanded_ = expanded;
    if (content_) content_->SetVisible(expanded);   // ARCH39-03: empty after ClearChildren (lazy rebuild via Content())
    Relayout();
    Invalidate();

    return *this;
}

void Expander::OnChildRemoved(Widget* child) {
    // ARCH39-03: the content column removed (or bulk-cleared) resets the cache, no dangling
    if (!child || child == content_) content_ = nullptr;
}

void Expander::AssignBounds(const Rect& area) {
    bounds_ = area;
    if (expanded_) {
        // baseline .exp-b padding 2px 14px 14px (M-01)
        Rect content{ area.x + 14.0f, area.y + HeaderHeight() + 2.0f,
                      std::max(0.0f, area.w - 28.0f),
                      std::max(0.0f, area.h - HeaderHeight() - 16.0f) };
        LayoutChildren(content);
    }
}

Size Expander::DesiredSize() const {
    // expanded height = header + content vertical padding (2+14) + content desired height.
    // the expanded state used to omit HeaderHeight: the parent allocated by content height, then AssignBounds deducted
    // the header again, clipping/overflowing the expanded content (round-5 S-02).
    Size s = Container::DesiredSize();
    if (!expanded_) {
        s.h = HeaderHeight();
    } else {
        s.w += 28.0f;
        s.h += HeaderHeight() + 16.0f;
    }
    return s;
}

void Expander::OnPaint(Painter& p, const Theme& theme) {
    p.FillRoundedRect(bounds_, 4, theme.control);
    // stroke follows the 4px radius (baseline .exp border-radius:4px, round-19 F-01, same as Button)
    p.StrokeRoundedRect({ bounds_.x + 0.5f, bounds_.y + 0.5f, bounds_.w - 1, bounds_.h - 1 },
                        4, theme.controlStroke);
    Rect header{ bounds_.x, bounds_.y, bounds_.w, HeaderHeight() };
    if (headerHover_)
        // hover background inset 1px: .exp-h is a child of .exp (baseline HTML:296),
        // the parent border draws outside the child ⇒ hover must not cover the top/left/right 1px stroke (C-14)
        p.FillRoundedRect({ bounds_.x + 1.0f, bounds_.y + 1.0f,
                            bounds_.w - 2.0f, HeaderHeight() - 1.0f },
                          4, theme.hoverSoft);
    p.DrawText(header_, Font{ .size = 14.0f },
               { header.x + 14, header.y, std::max(0.0f, header.w - 44), header.h },
               theme.text, HAlign::Left, VAlign::Center);
    // chevron (expanded points up, collapsed down)
    float cx = header.Right() - 24, cy = header.y + header.h * 0.5f;
    float dir = expanded_ ? -1.0f : 1.0f;
    p.DrawLine({ cx - 5, cy - 1.5f * dir }, { cx, cy + 2.5f * dir }, theme.textSecondary, 1.6f);
    p.DrawLine({ cx, cy + 2.5f * dir }, { cx + 5, cy - 1.5f * dir }, theme.textSecondary, 1.6f);
    DrawFocusRingRect(this, header, p, theme);   // focus ring on the header (baseline .exp-h, B-06)
}

void Expander::OnMouseMove(const Point& pos) {
    Point local{ pos.x - bounds_.x, pos.y - bounds_.y };
    bool h = local.y >= 0 && local.y < HeaderHeight() && local.x >= 0 &&
             local.x < bounds_.w;
    if (headerHover_ != h) { headerHover_ = h; Invalidate(); }
}

void Expander::OnMouseLeave() {
    if (headerHover_) { headerHover_ = false; Invalidate(); }
}

void Expander::OnMouseUp(const Point& pos) {
    Point local{ pos.x - bounds_.x, pos.y - bounds_.y };
    if (local.y >= 0 && local.y < HeaderHeight() && Enabled())
        SetExpanded(!expanded_);
}

void Expander::OnKeydown(uint32_t vk) {
    if (vk == VK_SPACE || vk == VK_RETURN) {
        if (Enabled()) SetExpanded(!expanded_);
    }
}

Tabs::Tabs() : Container(true) {}

const std::vector<TabPage*>& Tabs::Pages() const {
    pagesScratch_.clear();
    for (const auto& c : children_)
        if (auto* pg = dynamic_cast<TabPage*>(c.get())) pagesScratch_.push_back(pg);
    return pagesScratch_;
}

Column& Tabs::AddPage(std::wstring_view title) {
    // registration paths unify through OnChildAdded (GAP43-02): padding/visibility/dirty-marking share
    // the exact path of Add<TabPage>() and upcast Add; this function only fills in the title.
    TabPage& page = Add<TabPage>();
    page.title_.assign(title);
    return page;
}

void Tabs::OnChildAdded(Widget* child) {
    auto* page = dynamic_cast<TabPage*>(child);
    if (!page) return;   // non-page children take no part in the page mechanism (GAP42-01/02 contract unchanged)
    page->SetPadding(4.0f, 14.0f, 4.0f, 4.0f);   // baseline .tabpage page padding
    // the old AddPage visibility decision (E10 boundary derivation) moved here: a new non-selected trailing page
    // is hidden — pages injected via Add<TabPage>() therefore no longer overprint the current page, same as AddPage
    if (selected_ != PageCount() - 1)
        page->SetVisible(false);
    tabsDirty_ = true;
    Relayout();
    Invalidate();
}

void Tabs::OnChildrenReordered() {
    // GAP43-01: Pages() is the children_ order; after a reorder the slot order of the tab geometry cache tabRects_ is stale —
    // mark dirty; LayoutTabs rebuilds before the next draw/hit test
    tabsDirty_ = true;
    // the selection semantic is an **index** (same contract as OnMouseUp/OnKeydown and the ARCH40-02 removal path):
    // a reorder only permutes children_ and carries no selected object. Dirty-marking alone is not enough —
    // after SetSelected(n), reordering that page leaves selected_ pointing at the old slot; clicking the "selected" tab
    // takes the i==selected_ early-out and the stale visibility exposes as "underlined tab ⇄ visible page"
    // mismatch (r75 group B, end-to-end). Re-push each page's visibility from selected_; SetVisible
    // early-outs on equal values, so untouched orders are undisturbed. Selection thus becomes a pure function of
    // (page order, selected_); construction history is unobservable (r74probe group B's acceptance contract).
    int pageIdx = 0;
    for (auto& c : children_) {
        if (auto* pg = dynamic_cast<TabPage*>(c.get())) {
            pg->SetVisible(pageIdx == selected_);
            ++pageIdx;
        }
    }
    if (selected_ >= pageIdx) selected_ = pageIdx > 0 ? pageIdx - 1 : 0;
}

Tabs& Tabs::SetSelected(int index) {
    const auto& pages = Pages();   // PERF43-01: zero allocations (member scratch); do not hold across Pages() calls
    if (index < 0 || index >= static_cast<int>(pages.size())) return *this;
    selected_ = index;
    for (int i = 0; i < static_cast<int>(pages.size()); ++i)
        pages[i]->SetVisible(i == selected_);
    Relayout();
    Invalidate();

    return *this;
}

void Tabs::OnChildRemoved(Widget* child) {
    // ARCH39-08: pages shrink together with removal, preventing "ghost tabs" (the header still draws N,
    // but SetSelected lands on an emptied children_ → blank content area)
    if (!child) {   // ClearChildren bulk clear
        tabRects_.clear();
        tabsDirty_ = true;
        selected_ = 0;
        hover_ = -1;
        return;
    }
    // single-page removal: callback before erase, child still in children_. Decide pagehood by pointer
    // (GAP42-01/02: titles travel with pages, no titles_ to erase; non-page children have no tab semantics)
    if (!dynamic_cast<TabPage*>(child)) return;
    int idx = 0;
    for (auto& c : children_) {
        if (c.get() == child) break;
        if (dynamic_cast<TabPage*>(c.get())) ++idx;
    }
    // GAP41-02: when hover points to a tab right of the removed one, shift it down one; when it points at the removed
    // one itself, clear it — clamping only the upper bound would let the highlight land on the next tab (transient artifact)
    if (hover_ > idx) --hover_;
    else if (hover_ == idx) hover_ = -1;
    tabsDirty_ = true;
    // ARCH40-02: resync immediately after a single-page removal — when the removed page preceded the selected one,
    // selected_ moves back one (keeping the user's current page), clamped on overflow. children_ still contains
    // the removed page at this point; re-set each page's visibility by post-erase page order, skipping the removed page,
    // so selected_ and the visible page cannot diverge (underline/content/keyboard navigation disagreeing)
    if (idx < selected_) --selected_;
    const int pageCount = PageCount() - 1;   // page count after erase
    if (selected_ >= pageCount)
        selected_ = pageCount <= 0 ? 0 : pageCount - 1;
    int pageIdx = 0;
    for (auto& c : children_) {
        if (auto* pg = dynamic_cast<TabPage*>(c.get())) {
            if (c.get() == child) { ++pageIdx; continue; }   // the removed page is about to be erased: placeholder and skip
            pg->SetVisible((pageIdx > idx ? pageIdx - 1 : pageIdx) == selected_);
            ++pageIdx;
        }
    }
    if (hover_ >= pageCount) hover_ = -1;
}

void Tabs::LayoutTabs() const {
    if (!tabsDirty_) return;
    tabRects_.clear();
    float x = 0;
    const Font font{ .size = 14.0f };
    for (auto* pg : Pages()) {
        float w = detail::MeasureTextWidth(pg->Title(), font) + 24.0f;
        tabRects_.push_back({ x, 0, w, BarHeight() });
        x += w + 4.0f;
    }
    tabsDirty_ = false;
}

void Tabs::AssignBounds(const Rect& area) {
    bounds_ = area;
    Rect pages{ area.x, area.y + BarHeight(), area.w,
                std::max(0.0f, area.h - BarHeight()) };
    LayoutChildren(pages);
}

Size Tabs::DesiredSize() const {
    Size s = Container::DesiredSize();
    s.h += BarHeight();
    return s;
}

void Tabs::OnPaint(Painter& p, const Theme& theme) {
    LayoutTabs();
    const auto& pages = Pages();   // PERF43-01: zero allocations (member scratch)
    // GAP43-02 defense: page count and tab slot count should agree once all of OnChildAdded/OnChildRemoved/
    // OnChildrenReordered mark dirty, but any mismatch used to read tabRects_[i] out of bounds unchecked
    // (round-43 proved the UB end-to-end) — draw by the smaller count,
    // better one tab less than out of bounds.
    const int n = static_cast<int>(std::min(pages.size(), tabRects_.size()));
    // PERF43-01: both font weights hoisted out of the loop; with the empty-family default stack, Font construction is already zero-allocation
    const Font fNormal{ .size = 14.0f };
    const Font fSemi{ .size = 14.0f, .weight = FontWeight::SemiBold };
    for (int i = 0; i < n; ++i) {
        Rect tr = tabRects_[i].Translated(bounds_.x, bounds_.y);
        bool on = i == selected_;
        // baseline .tab:hover{background:var(--subtle-hov)} applies to the active tab too
        // (.tab.on declares no background, round-19 I-03): fills the whole tab, square bottom corners
        //（radius 6px 6px 0 0）
        if (i == hover_) {
            p.FillRoundedRect(tr, 6, theme.hoverSoft);
            p.FillRect({ tr.x, tr.Bottom() - 6.0f, tr.w, 6.0f }, theme.hoverSoft);
        }
        p.DrawText(pages[i]->Title(),
                   on ? fSemi : fNormal,
                   tr, on ? theme.text : theme.textSecondary,
                   HAlign::Center, VAlign::Center);
        if (on) {
            // underline flush with the bar's bottom edge (baseline bottom:-1px): height 3, inset 10 left/right;
            // y is rounded to avoid landing on a half-pixel and producing 2 solid + 2 translucent rows (I-09)
            float uy = std::round(tr.Bottom()) - 2.0f;
            p.FillRoundedRect({ tr.x + 10, uy, tr.w - 20, 3 }, 1.5f, theme.accent);
        }
    }
    // the focus ring draws on the active tab (baseline :focus-visible lands on .tab, B-06). The upper bound follows
    // the draw loop's n rather than tabRects_.size() (round-45 defensive consistency: the guard's semantics are
    // "only a drawn active tab gets a ring"; the two are currently identical under the dirty protocol).
    if (selected_ >= 0 && selected_ < n)
        DrawFocusRingRect(this, tabRects_[selected_].Translated(bounds_.x, bounds_.y),
                          p, theme);
}

void Tabs::OnMouseMove(const Point& pos) {
    Point local{ pos.x - bounds_.x, pos.y - bounds_.y };
    LayoutTabs();
    int h = -1;
    for (int i = 0; i < static_cast<int>(tabRects_.size()); ++i)
        if (tabRects_[i].Contains(local.x, local.y)) h = i;
    if (hover_ != h) { hover_ = h; Invalidate(); }
}

void Tabs::OnMouseLeave() {
    if (hover_ != -1) { hover_ = -1; Invalidate(); }
}

void Tabs::OnMouseUp(const Point& pos) {
    Point local{ pos.x - bounds_.x, pos.y - bounds_.y };
    LayoutTabs();
    for (int i = 0; i < static_cast<int>(tabRects_.size()); ++i) {
        if (tabRects_[i].Contains(local.x, local.y)) {
            if (i != selected_) SetSelected(i);
            return;
        }
    }
}

void Tabs::OnKeydown(uint32_t vk) {
    const int pageCount = PageCount();
    switch (vk) {
    case VK_RIGHT:
        if (selected_ + 1 < pageCount) SetSelected(selected_ + 1);
        break;
    case VK_LEFT:
        if (selected_ > 0) SetSelected(selected_ - 1);
        break;
    case VK_HOME: if (pageCount > 0) SetSelected(0); break;
    case VK_END:
        if (pageCount > 0) SetSelected(pageCount - 1);
        break;
    default: break;
    }
}

// ---------------------------------------------------------------------------
// ScrollViewer implementation
// ---------------------------------------------------------------------------

ScrollViewer::ScrollViewer() : Container(true) {}

/// Hit-testing clips by viewport: children scrolled out no longer take part (aligned with the draw side's PushClip),
/// so scrolled-away cards cannot swallow clicks aimed at upper-layer widgets (top bar etc.) outside the viewport.
bool ScrollViewer::ChildHitClip(Rect* out) const {
    *out = viewport_;
    return true;
}

/// Draw-side viewport (PERF-02): subtrees scrolled out are skipped whole in PaintTree; partially visible
/// subtrees PushClip by viewport — idle repaint cost drops from O(all widgets) to O(visible widgets).
bool ScrollViewer::ChildPaintClip(Rect* out) const {
    *out = viewport_;
    return true;
}

void ScrollViewer::AssignBounds(const Rect& area) {
    bounds_ = area;
    viewport_ = area;
    // total content height: reuse the base class's content-based sum (including spacing and padding).
    // Flow-like containers only update height after layout, so re-check once more after a layout pass.
    contentH_ = Container::DesiredSize().h;
    for (int pass = 0; pass < 2; ++pass) {
        float maxScroll = std::max(0.0f, contentH_ - area.h);
        scrollY_ = std::clamp(scrollY_, 0.0f, maxScroll);
        Rect content{ area.x + padL_, area.y + padT_ - scrollY_,
                      std::max(0.0f, area.w - padL_ - padR_),
                      std::max(0.0f, contentH_ - padT_ - padB_) };
        LayoutChildren(content);
        float h2 = Container::DesiredSize().h;
        if (std::abs(h2 - contentH_) <= 0.5f) break;
        contentH_ = h2;
    }
}

Size ScrollViewer::DesiredSize() const {
    return { 200.0f, 200.0f };
}

void ScrollViewer::OnPaint(Painter& p, const Theme&) {
    p.PushClip(bounds_);
}

Rect ScrollViewer::ThumbRect() const {
    float maxScroll = std::max(0.0f, contentH_ - viewport_.h);
    if (maxScroll <= 0) return {};
    float vh = viewport_.h;
    float thumbH = std::max(28.0f, vh * vh / contentH_);
    float t = scrollY_ / maxScroll;
    return { viewport_.Right() - 10, viewport_.y + t * (vh - thumbH), 6, thumbH };
}

void ScrollViewer::OnPostPaint(Painter& p, const Theme&) {
    p.PopClip();
    Rect tr = ThumbRect();
    if (tr.h <= 0) return;
    // baseline scrollbar-color: rgba(128,128,128,.45) (hover .6 and widened to 8px),
    // neutral gray so the hue does not drift with the theme
    Color c = Color::Rgb(0x808080, draggingThumb_ || thumbHover_ ? 0.6f : 0.45f);
    if (draggingThumb_ || thumbHover_)
        p.FillRoundedRect({ tr.x - 1, tr.y, tr.w + 2, tr.h }, 4, c);
    else
        p.FillRoundedRect(tr, 3, c);
}

bool ScrollViewer::OnWheel(float delta) {
    float maxScroll = std::max(0.0f, contentH_ - viewport_.h);
    if (maxScroll <= 0) return false;
    float old = scrollY_;
    scrollY_ = std::clamp(scrollY_ - delta * 54.0f, 0.0f, maxScroll);
    if (scrollY_ != old) {
        Relayout();
        Invalidate();
        if (OnScrolled) OnScrolled();
    }
    return true;
}

ScrollViewer& ScrollViewer::SetScrollOffset(float offset) {
    float maxScroll = std::max(0.0f, contentH_ - viewport_.h);
    scrollY_ = std::clamp(offset, 0.0f, maxScroll);
    Relayout();
    Invalidate();
    if (OnScrolled) OnScrolled();

    return *this;
}

// keyboard scrolling (R27-02): magnitudes from the baseline .content's measured truth (r27cdp.py trusted keys) —
// page = 0.875×viewport height (PageDown +625 / clientHeight 715), line scroll 40 DIP,
// Home/End to top/bottom; space does not scroll (the baseline's body overflow:hidden makes space-scroll inert, same contract)
void ScrollViewer::OnKeydown(uint32_t vk) {
    switch (vk) {
    case VK_PRIOR: SetScrollOffset(scrollY_ - viewport_.h * 0.875f); break;
    case VK_NEXT:  SetScrollOffset(scrollY_ + viewport_.h * 0.875f); break;
    case VK_UP:    SetScrollOffset(scrollY_ - 40.0f); break;
    case VK_DOWN:  SetScrollOffset(scrollY_ + 40.0f); break;
    case VK_HOME:  SetScrollOffset(0.0f); break;
    case VK_END:   SetScrollOffset(contentH_); break;   // SetScrollOffset clamps internally
    default: break;
    }
}


ScrollViewer& ScrollViewer::ScrollToWidget(const Widget* target) {
    if (!target) return *this;
    // child absolute y converts to content offset: the content origin is viewport.y + padding - scrollY_.
    // landing aligns with the baseline scroll-margin-top:10px: the target's top edge stops 10 DIP below the scroll port
    // (R25-07; previously -8 put the landing at padT_+8 = scroll port +22, 12px lower than the baseline)
    float offset = target->Bounds().y -
                   (viewport_.y + padT_ - scrollY_) + (padT_ - 10.0f);
    float maxScroll = std::max(0.0f, contentH_ - viewport_.h);
    scrollY_ = std::clamp(offset, 0.0f, maxScroll);
    Relayout();
    Invalidate();
    if (OnScrolled) OnScrolled();
    return *this;
}

void ScrollViewer::OnMouseMove(const Point& pos) {
    Rect tr = ThumbRect();
    bool over = tr.w > 0 && pos.x >= tr.x - 2 && pos.x <= tr.Right() + 2 &&
                pos.y >= tr.y && pos.y <= tr.Bottom();
    if (thumbHover_ != over) { thumbHover_ = over; Invalidate(); }
    if (draggingThumb_) {
        float maxScroll = std::max(0.0f, contentH_ - viewport_.h);
        float t = (pos.y - dragGrabOffset_ - viewport_.y) /
                  std::max(1.0f, viewport_.h - tr.h);
        SetScrollOffset(t * maxScroll);
    }
}

void ScrollViewer::OnMouseLeave() {
    if (thumbHover_) { thumbHover_ = false; Invalidate(); }
}

void ScrollViewer::OnMouseDown(const Point& pos) {
    Rect tr = ThumbRect();
    if (tr.w > 0 && pos.x >= tr.x - 2 && pos.x <= tr.Right() + 2 &&
        pos.y >= tr.y - 2 && pos.y <= tr.Bottom() + 2) {
        draggingThumb_ = true;
        dragGrabOffset_ = pos.y - tr.y;
        Invalidate();
    } else if (pos.x > viewport_.Right() - 14) {
        // click the scrollbar track: page
        float maxScroll = std::max(0.0f, contentH_ - viewport_.h);
        SetScrollOffset(scrollY_ + (pos.y < tr.y ? -viewport_.h : viewport_.h) * 0.9f *
                        (maxScroll > 0 ? 1.0f : 0.0f));
    }
}

void ScrollViewer::OnMouseUp(const Point&) {
    if (draggingThumb_) { draggingThumb_ = false; Invalidate(); }
}

// ---------------------------------------------------------------------------
// NavigationView / TopBar implementation
// ---------------------------------------------------------------------------

NavigationView::NavigationView() = default;

float NavigationView::DockWidth() const { return 264.0f; }

NavigationView& NavigationView::SetBrand(std::wstring_view text) {
    brand_.assign(text.begin(), text.end());
    Invalidate();

    return *this;
}

NavigationView& NavigationView::AddGroup(std::wstring_view text) {
    Entry e;
    e.group = true;
    e.text.assign(text.begin(), text.end());
    entries_.push_back(std::move(e));
    entriesDirty_ = true;
    Invalidate();
    return *this;
}

int NavigationView::AddItem(const Icon& icon, std::wstring_view text,
                            std::function<void(size_t)> onClick) {
    Entry e;
    e.icon = icon;
    e.text.assign(text.begin(), text.end());
    entries_.push_back(std::move(e));
    onClicks_.push_back(std::move(onClick));
    entriesDirty_ = true;
    Invalidate();
    return static_cast<int>(onClicks_.size()) - 1;
}

void NavigationView::Clear() {
    entries_.clear();
    onClicks_.clear();
    selected_ = hover_ = keyFocus_ = -1;
    scrollOffset_ = 0;
    thumbDrag_ = thumbHover_ = false;
    entriesDirty_ = true;
    Relayout();
    Invalidate();
}

NavigationView& NavigationView::SetSelectedIndex(int index, bool notify) {
    // OBS53-01: same "reject out-of-range" contract as the same-signature ComboBox/ListView/Tabs
    // (-1 = no selection); previously wrote without clamping, and SelectedIndex() could return a value with no matching item
    if (index < -1 || index >= static_cast<int>(onClicks_.size())) return *this;
    selected_ = index;
    Invalidate();
    // OBS52-02: programmatic navigation notification, fires only OnNavigate (not onClicks_ — same contract
    // as ListView notify firing only OnSelected); after the guard, index is a valid item
    if (notify && index >= 0 && OnNavigate)
        OnNavigate(static_cast<size_t>(index));

    return *this;
}

NavigationView& NavigationView::SetFooter(std::wstring_view text) {
    footer_.assign(text.begin(), text.end());
    Invalidate();

    return *this;
}

void NavigationView::LayoutEntries() const {
    if (!entriesDirty_) return;
    // baseline .pane padding-top 10 + .brand(10+26+12) + .navlist padding-top 2 = 60
    float y = 60.0f;
    for (auto& e : entries_) {
        if (e.group) {
            // baseline .ngroup padding 14px 20px 5px + line-height 18 ⇒ box height 37
            // (14+18+5; headless measured the group rect at full height 37; previously 35 drifted items
            // −4…+1 px, round-19 I-04). Text shifts down 14 in OnPaint to reproduce
            // "sparse top, dense bottom"
            e.rect = { 0, y, bounds_.w, 37.0f };
            y += 38.0f;   // box 37 + the 1px gap from collapsed item margins
        } else {
            // baseline .nitem margin 1px 10px, box height 34: always a 1px gap to adjacent boxes
            // (adjacent margins collapse), item step 35; item→group also 1px (baseline
            // rect tops 60/133/276/524/632 and 98/171/206/…/670 all match)
            e.rect = { 10.0f, y, bounds_.w - 20.0f, 34.0f };
            y += 35.0f;
        }
    }
    contentBottom_ = y;
    entriesDirty_ = false;
}

int NavigationView::EntryAt(const Point& local) const {
    LayoutEntries();
    // hit-testing happens in content coordinates (plus the scroll offset)
    Point p{ local.x, local.y + scrollOffset_ };
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i)
        if (entries_[i].rect.Contains(p.x, p.y)) return i;
    return -1;
}

bool NavigationView::OnWheel(float delta) {
    // the item area scrolls by wheel (M-02: baseline .navlist{overflow-y:auto}; brand row/footer fixed)
    LayoutEntries();
    float old = scrollOffset_;
    scrollOffset_ = std::clamp(scrollOffset_ - delta * 54.0f, 0.0f, maxScroll());
    if (scrollOffset_ != old) {
        Invalidate();
        return true;
    }
    return false;
}

float NavigationView::maxScroll() const {
    LayoutEntries();
    float viewportH = std::max(0.0f, bounds_.h - 48.0f - 48.0f);
    return std::max(0.0f, contentBottom_ - 60.0f - viewportH);
}

Rect NavigationView::ThumbRect() const {
    if (maxScroll() <= 0.0f) return {};
    float viewportH = std::max(0.0f, bounds_.h - 48.0f - 48.0f);
    float trackH = viewportH - 8.0f;
    float th = std::max(24.0f, trackH * viewportH / (viewportH + maxScroll()));
    float t = scrollOffset_ / maxScroll();
    return { bounds_.w - 10.0f, 52.0f + t * (trackH - th), 6.0f, th };   // widget-local
}

void NavigationView::DrawScrollbar(Painter& p, const Theme&) {
    // same neutral gray as ScrollViewer (baseline scrollbar-color rgba(128,128,128,.45/.6))
    Rect tr = ThumbRect().Translated(bounds_.x, bounds_.y);
    if (tr.h <= 0) return;
    Color c = Color::Rgb(0x808080, thumbDrag_ || thumbHover_ ? 0.6f : 0.45f);
    if (thumbDrag_ || thumbHover_)
        p.FillRoundedRect({ tr.x - 1, tr.y, tr.w + 2, tr.h }, 4, c);
    else
        p.FillRoundedRect(tr, 3, c);
}

void NavigationView::OnPaint(Painter& p, const Theme& theme) {
    p.FillRect(bounds_, theme.cardBackground);
    // brand row: gradient badge + white sparkle icon + title (aligned with the baseline .logo)
    static const Icon kSparkle = Icon::FromSvgPath(
        "M12 2.5c.6 3.4 2.9 5.7 6.3 6.3.4.07.4 1.53 0 1.6-3.4.6-5.7 2.9-6.3 6.3"
        "-.07.4-1.53.4-1.6 0-.6-3.4-2.9-5.7-6.3-6.3-.4-.07-.4-1.53 0-1.6 "
        "3.4-.6 5.7-2.9 6.3-6.3.07-.4 1.53-.4 1.6 0Z");
    Rect logo{ bounds_.x + 18, bounds_.y + 20, 26, 26 };   // baseline .pane 10 + .brand 10 = 20
    // shadow (baseline box-shadow: 0 1px 3px rgba(0,60,140,.35), two-tier tint approximation)
    p.FillRoundedRect({ logo.x - 2, logo.y - 1, logo.w + 4, logo.h + 4 }, 9,
                      Color::Rgb(0x003C8C, 0.10f));
    p.FillRoundedRect({ logo.x - 1, logo.y, logo.w + 2, logo.h + 2 }, 8,
                      Color::Rgb(0x003C8C, 0.14f));
    // 135° diagonal gradient (baseline linear-gradient(135deg,#4ea3f5,#0067c0))
    const GradientStop kLogoStops[2] = {
        { 0.0f, Color::Rgb(0x4EA3F5) }, { 1.0f, Color::Rgb(0x0067C0) } };
    p.FillRoundedRectGradientStops(logo, 7, kLogoStops, 2,
                                   { logo.x, logo.y }, { logo.Right(), logo.Bottom() });
    p.DrawIcon(kSparkle, { logo.x + 5.5f, logo.y + 5.5f, 15, 15 }, Color::Rgb(0xFFFFFF));
    p.DrawText(brand_, Font{ .size = 16.0f, .weight = FontWeight::SemiBold },
               { logo.Right() + 10, bounds_.y + 18,
                 std::max(0.0f, bounds_.w - 54), 30 },   // vertically centered with the logo (y+20..46)
               theme.text, HAlign::Left, VAlign::Center);
    LayoutEntries();
    const Font kGroup{ .size = 12.0f };
    const Font kItem{ .size = 13.5f };
    int itemIndex = -1;
    // item area clip-scrolls (brand row/footer fixed; item rects are content coordinates, minus scrollOffset_)
    p.PushClip({ bounds_.x, bounds_.y + 48.0f, bounds_.w,
                 std::max(0.0f, bounds_.h - 48.0f - 48.0f) });
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
        const auto& e = entries_[i];
        Rect r = e.rect.Translated(bounds_.x, bounds_.y - scrollOffset_);
        if (e.group) {
            // sparse top, dense bottom: text top at y+14 (baseline padding 14px 20px 5px)
            p.DrawText(e.text, kGroup,
                       { r.x + 20, r.y + 14, std::max(0.0f, r.w - 30),
                         std::max(0.0f, r.h - 19.0f) },
                       theme.textTertiary, HAlign::Left, VAlign::Top);
            continue;
        }
        ++itemIndex;
        bool selected = itemIndex == selected_;
        if (selected) {
            p.FillRoundedRect(r, 5, theme.selectedSoft);
            p.FillRoundedRect({ bounds_.x, r.y + 9, 3, 16 }, 2, theme.accent);
        } else if (i == hover_) {
            p.FillRoundedRect(r, 5, theme.hoverSoft);
        }
        if (!e.icon.IsEmpty())
            p.DrawIcon(e.icon, { r.x + 10, r.y + (r.h - 16.0f) / 2.0f, 16, 16 },
                       selected ? theme.accentText : theme.textSecondary);
        p.DrawText(e.text, Font{ .size = 13.5f,
                                 .weight = selected ? FontWeight::SemiBold : FontWeight::Normal },
                   { r.x + 37, r.y, std::max(0.0f, r.w - 47), r.h },
                   theme.text, HAlign::Left, VAlign::Center);
        // GAP38-02: keyboard focus ring (item rects include the 1px margin; a 2px outsert stays within the pane);
        // when focused-but-not-moved the ring lands on the selected item (same convention as the Tabs/Expander composite widgets)
        if ((keyFocus_ == itemIndex ||
             (keyFocus_ < 0 && selected_ == itemIndex)) &&
            Focused() && detail::g_focusFromKeyboard)
            p.StrokeRoundedRect({ r.x - 2, r.y - 2, r.w + 4, r.h + 4 }, 5,
                                theme.accentText, 2.0f);
    }
    p.PopClip();
    if (maxScroll() > 0.0f)
        DrawScrollbar(p, theme);   // baseline .navlist overflow-y:auto (M-01)
    if (!footer_.empty()) {
        // baseline .pane-foot: 11px/1.6, top padding 10; .pane padding-bottom 12 (A-12)
        int lines = 1 + static_cast<int>(
            std::count(footer_.begin(), footer_.end(), L'\n'));
        float boxH = 10.0f + static_cast<float>(lines) * 11.0f * 1.6f;
        p.DrawText(footer_, Font{ .size = 11.0f, .lineHeight = 1.6f },
                   { bounds_.x + 20, bounds_.Bottom() - 12.0f - boxH + 10.0f,
                     std::max(0.0f, bounds_.w - 40), boxH - 10.0f },
                   theme.textTertiary, HAlign::Left, VAlign::Top);
    }
}

void NavigationView::OnMouseMove(const Point& pos) {
    Point local{ pos.x - bounds_.x, pos.y - bounds_.y };
    if (thumbDrag_) {
        // thumb dragging (M-01): the grab point tracks the cursor
        Rect tr = ThumbRect();
        if (tr.h > 0) {
            float trackH = std::max(0.0f, bounds_.h - 104.0f) - 8.0f;
            float t = (local.y - thumbGrab_ - 52.0f) / std::max(1.0f, trackH - tr.h);
            float target = std::clamp(t, 0.0f, 1.0f) * maxScroll();
            if (scrollOffset_ != target) {
                scrollOffset_ = target;
                Invalidate();
            }
        }
        return;
    }
    Rect tr = ThumbRect();
    bool th = tr.w > 0 && local.x >= tr.x - 2 && local.x <= tr.Right() + 2 &&
              local.y >= tr.y && local.y <= tr.Bottom();
    if (thumbHover_ != th) { thumbHover_ = th; Invalidate(); }
    int index = EntryAt(local);
    if (index >= 0 && entries_[index].group) index = -1;
    if (hover_ != index) { hover_ = index; Invalidate(); }
}

void NavigationView::OnMouseLeave() {
    if (hover_ != -1) { hover_ = -1; Invalidate(); }
    if (thumbHover_) { thumbHover_ = false; Invalidate(); }
}

void NavigationView::OnMouseDown(const Point& pos) {
    Point local{ pos.x - bounds_.x, pos.y - bounds_.y };
    Rect tr = ThumbRect();
    if (tr.h > 0 && local.x >= tr.x - 3 && local.x <= tr.Right() + 3 &&
        local.y >= tr.y - 2 && local.y <= tr.Bottom() + 2) {
        thumbDrag_ = true;
        thumbGrab_ = local.y - tr.y;
        Invalidate();
    }
}

void NavigationView::OnMouseUp(const Point& pos) {
    if (thumbDrag_) {
        thumbDrag_ = false;
        Invalidate();
        return;
    }
    if (!Enabled()) return;
    Point local{ pos.x - bounds_.x, pos.y - bounds_.y };
    int index = EntryAt(local);
    if (index < 0 || entries_[index].group) return;
    int itemIndex = -1;
    for (int i = 0; i <= index; ++i)
        if (!entries_[i].group) ++itemIndex;
    selected_ = itemIndex;
    Invalidate();
    if (OnNavigate) OnNavigate(static_cast<size_t>(itemIndex));
    if (itemIndex < static_cast<int>(onClicks_.size()) && onClicks_[itemIndex])
        onClicks_[itemIndex](static_cast<size_t>(itemIndex));
}

void NavigationView::OnCaptureLost() {
    if (thumbDrag_) {   // scrollbar thumb dragging ends on capture loss (round-19 F-03)
        thumbDrag_ = false;
        Invalidate();
    }
}

// GAP38-02: navigation-item keyboard path (baseline .nitem is a native <button>, Tab-reachable,
// Enter/Space navigable) — ↑↓ move keyboard focus among items (skipping group labels),
// Enter/Space activate (same as mouse: select + OnNavigate + onClick)
void NavigationView::OnKeydown(uint32_t vk) {
    if (!Enabled()) return;
    LayoutEntries();
    int itemCount = static_cast<int>(onClicks_.size());
    if (itemCount <= 0) return;
    auto entryOfItem = [this](int itemIndex) -> int {
        int idx = -1;
        for (int i = 0; i < static_cast<int>(entries_.size()); ++i)
            if (!entries_[i].group && ++idx == itemIndex) return i;
        return -1;
    };
    if (vk == VK_DOWN || vk == VK_UP) {
        int step = vk == VK_DOWN ? 1 : -1;
        int t = keyFocus_ < 0
                    ? (selected_ >= 0 ? selected_ : (step > 0 ? 0 : itemCount - 1))
                    : keyFocus_ + step;
        keyFocus_ = std::clamp(t, 0, itemCount - 1);
        // the focused item scrolls into the item area's viewport (content-coordinate visible range [scroll+48, scroll+h-48])
        int ei = entryOfItem(keyFocus_);
        if (ei >= 0) {
            const Rect& r = entries_[ei].rect;
            if (r.y < scrollOffset_ + 48.0f)
                scrollOffset_ = std::max(0.0f, r.y - 48.0f);
            if (r.y + r.h > scrollOffset_ + bounds_.h - 48.0f)
                scrollOffset_ = std::min(maxScroll(), r.y + r.h - (bounds_.h - 48.0f));
        }
        Invalidate();
    } else if (vk == VK_RETURN || vk == VK_SPACE) {
        if (keyFocus_ < 0) return;
        selected_ = keyFocus_;
        Invalidate();
        if (OnNavigate) OnNavigate(static_cast<size_t>(keyFocus_));
        if (keyFocus_ < itemCount && onClicks_[keyFocus_])
            onClicks_[keyFocus_](static_cast<size_t>(keyFocus_));
    }
}

TopBar::TopBar() : Container(false) {
    SetSpacing(8.0f);   // baseline .topbar{gap:8px} (B-01)
    // construction shares the same configuration as the "lazy rebuild after clear" (ARCH39-03) accessors
    CrumbLabel();
    SearchBox();
    ThemeButton();
}

TopBar& TopBar::SetCrumb(std::wstring_view text) {
    CrumbLabel().SetText(text);   // ARCH39-03: lazy rebuild after clear

    return *this;
}

void TopBar::OnChildRemoved(Widget* child) {
    // ARCH39-03: removed cached children are nulled; the next accessor call lazily rebuilds
    if (!child || child == crumb_) crumb_ = nullptr;
    if (!child || child == search_) search_ = nullptr;
    if (!child || child == themeBtn_) themeBtn_ = nullptr;
}

void TopBar::OnPaint(Painter& p, const Theme& theme) {
    (void)p; (void)theme;
    // no background fill: the window Clear is already windowBackground, and the baseline top bar is transparent
    // (the body::before blobs must show through the top bar, round-9 B-03)
}

// ---------------------------------------------------------------------------
// menu bar implementation
// ---------------------------------------------------------------------------

Menu& Menu::AddItem(std::wstring_view text, std::function<void()> onClick) {
    Item item;
    item.text.assign(text.begin(), text.end());
    item.onClick = std::move(onClick);
    items_.push_back(std::move(item));
    return *this;
}

Menu& Menu::AddItem(const Icon& icon, std::wstring_view text,
                    std::wstring_view shortcut, std::function<void()> onClick) {
    Item item;
    item.icon = icon;
    item.text.assign(text.begin(), text.end());
    item.shortcut.assign(shortcut.begin(), shortcut.end());
    item.onClick = std::move(onClick);
    items_.push_back(std::move(item));
    return *this;
}

Menu& Menu::AddSeparator() {
    Item item;
    item.separator = true;
    items_.push_back(std::move(item));
    return *this;
}

Menu& Menu::AddCheckItem(std::wstring_view text, bool checked,
                         std::function<void()> onClick) {
    Item item;
    item.checkable = true;
    item.checked = checked;
    item.text.assign(text.begin(), text.end());
    item.onClick = std::move(onClick);
    items_.push_back(std::move(item));
    return *this;
}

void Menu::Clear() {
    items_.clear();
}

MenuBar::~MenuBar() {
    CloseMenu();
}

float MenuBar::DockHeight() const { return 38.0f; }

Menu& MenuBar::AddMenu(std::wstring_view title) {
    auto menu = std::make_unique<Menu>();
    menu->title_.assign(title.begin(), title.end());
    Menu& ref = *menu;
    menus_.push_back(std::move(menu));
    titlesDirty_ = true;
    Invalidate();
    return ref;
}

void MenuBar::Clear() {
    CloseMenu();   // collapses an open popup; resets open_/hover_ when one was open
    hover_ = -1;
    menus_.clear();
    titlesDirty_ = true;
    Invalidate();
}

void MenuBar::LayoutTitles() const {
    if (!titlesDirty_) return;
    titleRects_.clear();
    float x = 10.0f;
    const Font font{ .size = 13.0f };
    for (auto& menu : menus_) {
        float w = detail::MeasureTextWidth(menu->title_, font) + 24.0f;
        titleRects_.push_back({ x, 0, w, DockHeight() });
        x += w;
    }
    titlesDirty_ = false;
}

Rect MenuBar::TitleRect(int index) const {
    LayoutTitles();
    return titleRects_[index];
}

int MenuBar::TitleAt(const Point& local) const {
    LayoutTitles();
    for (int i = 0; i < static_cast<int>(titleRects_.size()); ++i) {
        if (titleRects_[i].Contains(local.x, local.y)) return i;
    }
    return -1;
}

void MenuBar::OpenMenu(int index) {
    CloseMenu();
    if (index < 0 || index >= static_cast<int>(menus_.size())) return;
    if (!window_ || !window_->impl_) return;
    open_ = index;
    hover_ = index;
    Rect tr = TitleRect(index);
    Rect target{ bounds_.x + tr.x, bounds_.y + tr.y + tr.h, tr.w, 0 };
    popup_ = std::make_unique<detail::MenuPopup>(
        this, menus_[index].get(), window_->impl_->hwnd, window_->impl_->dpi,
        target, window_->GetTheme());
    Invalidate();
}

void MenuBar::SwitchMenu(int index) {
    CloseMenu();
    OpenMenu(index);
}

void MenuBar::CloseMenu() {
    if (popup_) {
        // close after transferring ownership: messages triggered by Close still touch the popup object itself
        std::unique_ptr<detail::MenuPopup> popup = std::move(popup_);
        popup_ = nullptr;
        popup->Close();
    }
    if (open_ != -1) {
        open_ = -1;
        hover_ = -1;
        Invalidate();
    }
}

void MenuBar::OnPaint(Painter& p, const Theme& theme) {
    p.FillRect(bounds_, theme.menuBackground);
    LayoutTitles();
    const Font font{ .size = 13.0f };
    for (int i = 0; i < static_cast<int>(menus_.size()); ++i) {
        const Rect& tr = titleRects_[i];
        Rect pill{ bounds_.x + tr.x, bounds_.y + 6, tr.w, bounds_.h - 12 };
        if (open_ == i || hover_ == i)
            p.FillRoundedRect(pill, 6, theme.accentSoft);
        p.DrawText(menus_[i]->title_, font, pill, theme.text,
                   HAlign::Center, VAlign::Center);
    }
}

void MenuBar::OnMouseMove(const Point& pos) {
    Point local{ pos.x - bounds_.x, pos.y - bounds_.y };
    int title = TitleAt(local);
    if (hover_ != title) {
        hover_ = title;
        Invalidate();
    }
}

void MenuBar::OnMouseLeave() {
    if (hover_ != -1) {
        hover_ = -1;
        Invalidate();
    }
}

void MenuBar::OnMouseUp(const Point& pos) {
    Point local{ pos.x - bounds_.x, pos.y - bounds_.y };
    int title = TitleAt(local);
    if (title < 0) return;
    if (title == open_) CloseMenu();
    else OpenMenu(title);
}

// ---------------------------------------------------------------------------
// toolbar implementation
// ---------------------------------------------------------------------------

float ToolBar::DockHeight() const { return 52.0f; }

ToolBar& ToolBar::AddButton(std::wstring_view text, std::function<void()> onClick) {
    Item item;
    item.text.assign(text.begin(), text.end());
    item.onClick = std::move(onClick);
    items_.push_back(std::move(item));
    itemsDirty_ = true;
    Invalidate();
    return *this;
}

ToolBar& ToolBar::AddButton(const Icon& icon, std::function<void()> onClick) {
    Item item;
    item.icon = icon;
    item.onClick = std::move(onClick);
    items_.push_back(std::move(item));
    itemsDirty_ = true;
    Invalidate();
    return *this;
}

ToolBar& ToolBar::AddButton(const Icon& icon, std::wstring_view text,
                            std::function<void()> onClick) {
    Item item;
    item.icon = icon;
    item.text.assign(text.begin(), text.end());
    item.onClick = std::move(onClick);
    items_.push_back(std::move(item));
    itemsDirty_ = true;
    Invalidate();
    return *this;
}

ToolBar& ToolBar::AddSeparator() {
    Item item;
    item.separator = true;
    items_.push_back(std::move(item));
    itemsDirty_ = true;
    Invalidate();
    return *this;
}

ToolBar& ToolBar::AddSpace() {
    Item item;
    item.space = true;
    items_.push_back(std::move(item));
    itemsDirty_ = true;
    Invalidate();
    return *this;
}

int ToolBar::AddChip(const Icon& icon, std::wstring_view text,
                     std::function<void()> onClick) {
    Item item;
    item.chip = true;
    item.icon = icon;
    item.text.assign(text.begin(), text.end());
    item.onClick = std::move(onClick);
    items_.push_back(std::move(item));
    itemsDirty_ = true;
    Invalidate();
    return static_cast<int>(items_.size()) - 1;
}

ToolBar& ToolBar::SetItemText(int index, std::wstring_view text) {
    if (index < 0 || index >= static_cast<int>(items_.size())) return *this;
    auto& item = items_[index];
    if (item.separator || item.space) return *this;
    item.text.assign(text.begin(), text.end());
    itemsDirty_ = true;
    Invalidate();

    return *this;
}

ToolBar& ToolBar::EnableItem(int index, bool enabled) {
    if (index < 0 || index >= static_cast<int>(items_.size())) return *this;
    items_[index].enabled = enabled;
    Invalidate();
    return *this;
}

void ToolBar::Clear() {
    items_.clear();
    hover_ = pressed_ = -1;
    itemsDirty_ = true;
    Relayout();
    Invalidate();
}

void ToolBar::LayoutItems() const {
    if (!itemsDirty_) return;
    const Font font{ .size = 13.0f };
    const int n = static_cast<int>(items_.size());
    std::vector<float> widths(static_cast<size_t>(n), 0.0f);
    for (int i = 0; i < n; ++i) {
        const auto& item = items_[i];
        if (item.separator) widths[i] = 8.0f;
        else if (item.space) widths[i] = 0.0f;
        else if (item.chip) {
            float w = 28.0f;
            if (!item.icon.IsEmpty()) w += 14.0f + 7.0f;
            w += detail::MeasureTextWidth(item.text, Font{ .size = 12.0f });
            widths[i] = w;
        } else if (!item.text.empty()) {
            widths[i] = detail::MeasureTextWidth(item.text, font) +
                        (item.icon.IsEmpty() ? 24.0f : 44.0f);
        } else {
            widths[i] = 36.0f;   // icon button
        }
    }
    // items after the last flexible spacer pack right (the toolbar's right-side pill group)
    int lastSpace = -1;
    for (int i = 0; i < n; ++i)
        if (items_[i].space) lastSpace = i;
    float x = 12.0f;
    for (int i = 0; i < n; ++i) {
        if (i > lastSpace) break;
        float h = items_[i].chip ? 30.0f : 36.0f;
        items_[i].rect = { x, items_[i].chip ? 11.0f : 8.0f, widths[i], h };
        x += widths[i] + 6.0f;
    }
    if (lastSpace >= 0) {
        float rightW = 0;
        for (int i = lastSpace + 1; i < n; ++i) rightW += widths[i] + 6.0f;
        float rx = bounds_.w - 12.0f - (rightW - 6.0f);
        for (int i = lastSpace + 1; i < n; ++i) {
            float h = items_[i].chip ? 30.0f : 36.0f;
            items_[i].rect = { rx, items_[i].chip ? 11.0f : 8.0f, widths[i], h };
            rx += widths[i] + 6.0f;
        }
        items_[lastSpace].rect = { x, 0, std::max(0.0f, rx - 6.0f - x), 0.0f };
    }
    itemsDirty_ = false;
}

int ToolBar::ItemAt(const Point& local) const {
    LayoutItems();
    for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
        const auto& item = items_[i];
        if (!item.separator && !item.space && item.enabled &&
            item.rect.Contains(local.x, local.y)) return i;
    }
    return -1;
}

void ToolBar::OnPaint(Painter& p, const Theme& theme) {
    p.FillRect(bounds_, theme.toolbarBackground);
    LayoutItems();
    const Font font{ .size = 13.0f };
    const Font chipFont{ .size = 12.0f };
    for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
        const auto& item = items_[i];
        if (item.separator || item.space) continue;   // separators render as spacing
        Rect abs{ bounds_.x + item.rect.x, bounds_.y + item.rect.y,
                  item.rect.w, item.rect.h };
        if (item.chip) {
            // pill buttons: white fill + double micro-shadow (neutral black, opacity by theme light/dark)
            float a = theme.windowBackground.r > 0.5f ? 0.12f : 0.30f;
            p.DrawShadow(abs, 15, 1, 1.5f, Color::Rgb(0x000000, a));
            p.DrawShadow(abs, 15, 0, 1, Color::Rgb(0x000000, a * 0.65f));
            Color fill = pressed_ == i ? theme.controlPressed
                       : hover_ == i    ? theme.controlHover
                                        : theme.control;
            p.FillRoundedRect(abs, 15, fill);
            float cx = abs.x + 14;
            if (!item.icon.IsEmpty()) {
                p.DrawIcon(item.icon, { cx, abs.y + (abs.h - 14.0f) / 2.0f,
                                        14, 14 }, theme.accent);
                cx += 14 + 7;
            }
            p.DrawText(item.text, chipFont,
                       { cx, abs.y, std::max(0.0f, abs.Right() - 14 - cx), abs.h },
                       theme.controlText, HAlign::Left, VAlign::Center);
            continue;
        }
        bool disabled = !item.enabled;
        if (!disabled) {
            if (i == pressed_) p.FillRoundedRect(abs, 8, theme.accentSoft);
            else if (i == hover_) p.FillRoundedRect(abs, 8, theme.hoverSoft);
        }
        Color fg = disabled ? theme.textDisabled : theme.text;
        bool hasIcon = !item.icon.IsEmpty();
        if (hasIcon) {
            // icon-only buttons center; with text, the icon leads
            float ix = item.text.empty() ? (abs.w - 18.0f) / 2.0f : 12.0f;
            p.DrawIcon(item.icon, { abs.x + ix, abs.y + (abs.h - 18.0f) / 2.0f, 18, 18 }, fg);
        }
        if (!item.text.empty()) {
            float textX = abs.x + (hasIcon ? 36.0f : 12.0f);
            float textW = abs.w - (hasIcon ? 36.0f : 24.0f);
            p.DrawText(item.text, font,
                       { textX, abs.y, std::max(0.0f, textW), abs.h },
                       fg, HAlign::Left, VAlign::Center);
        }
    }
}

void ToolBar::OnMouseMove(const Point& pos) {
    Point local{ pos.x - bounds_.x, pos.y - bounds_.y };
    int index = ItemAt(local);
    if (hover_ != index) {
        hover_ = index;
        Invalidate();
    }
}

void ToolBar::OnMouseLeave() {
    if (hover_ != -1) {
        hover_ = -1;
        Invalidate();
    }
}

void ToolBar::OnMouseDown(const Point& pos) {
    Point local{ pos.x - bounds_.x, pos.y - bounds_.y };
    pressed_ = ItemAt(local);
    if (pressed_ != -1) Invalidate();
}

void ToolBar::OnMouseUp(const Point& pos) {
    Point local{ pos.x - bounds_.x, pos.y - bounds_.y };
    int index = ItemAt(local);
    bool fire = pressed_ != -1 && index == pressed_;
    pressed_ = -1;
    Invalidate();
    if (fire && items_[index].onClick) items_[index].onClick();
}

void ToolBar::OnCaptureLost() {
    if (pressed_ != -1) {   // capture loss cancels the item's pressed state, no spurious callback
        pressed_ = -1;      // (same as CommandBar; previously the only one of the 12 pressed_-maintaining
        Invalidate();       // widgets missing this closure, round-26 C-4)
    }
}

// ---------------------------------------------------------------------------
// status bar implementation
// ---------------------------------------------------------------------------

float StatusBar::DockHeight() const { return 34.0f; }

int StatusBar::AddSection(std::wstring_view text, float width) {
    sections_.push_back({ std::wstring(text), width, false });
    Invalidate();
    return static_cast<int>(sections_.size()) - 1;
}

StatusBar& StatusBar::SetSectionText(int index, std::wstring_view text) {
    if (index < 0 || index >= static_cast<int>(sections_.size())) return *this;
    sections_[index].text.assign(text.begin(), text.end());
    Invalidate();

    return *this;
}

StatusBar& StatusBar::SetSectionDot(int index, bool dot) {
    if (index < 0 || index >= static_cast<int>(sections_.size())) return *this;
    sections_[index].dot = dot;
    Invalidate();

    return *this;
}

std::wstring StatusBar::SectionText(int index) const {
    if (index < 0 || index >= static_cast<int>(sections_.size())) return {};
    return sections_[index].text;
}

void StatusBar::Clear() {
    sections_.clear();
    Invalidate();
}

void StatusBar::OnPaint(Painter& p, const Theme& theme) {
    p.FillRect(bounds_, theme.statusBackground);

    // compute section positions: fixed-width sections pack left, flexible sections split the rest
    float springCount = 0, fixedTotal = 0;
    for (auto& s : sections_) {
        if (s.width > 0) fixedTotal += s.width;
        else springCount += 1.0f;
    }
    float springW = springCount > 0
        ? std::max(0.0f, (bounds_.w - fixedTotal) / springCount) : 0.0f;

    Font font{ .size = 12.0f };
    float x = bounds_.x;
    for (int i = 0; i < static_cast<int>(sections_.size()); ++i) {
        auto& s = sections_[i];
        float w = s.width > 0 ? s.width : springW;
        bool spring = s.width <= 0;
        float dotW = s.dot ? 6.0f + 7.0f : 0.0f;
        if (s.dot)
            p.FillRoundedRect({ x + 12, bounds_.y + (bounds_.h - 6.0f) / 2.0f,
                                6, 6 }, 3, theme.accent);
        Rect area = spring
            ? Rect{ x, bounds_.y, std::max(0.0f, w - 12.0f), bounds_.h }
            : Rect{ x + 12 + dotW, bounds_.y,
                    std::max(0.0f, w - 12 - dotW - 6.0f), bounds_.h };
        p.DrawText(s.text, font, area, theme.statusText,
                   spring ? HAlign::Right : HAlign::Left, VAlign::Center);
        x += w;
    }
}

// ---------------------------------------------------------------------------
// global state
// ---------------------------------------------------------------------------

template <class T, class... Args>
T& Window::Add(Args&&... args) {
    auto widget = std::make_unique<T>(std::forward<Args>(args)...);
    T& ref = *widget;
    impl_->Attach(std::move(widget));
    return ref;
}

std::vector<Window*> g_windows;
detail::ComPtr<ID2D1Factory1> g_d2d;
detail::ComPtr<IDWriteFactory> g_dwrite;
detail::GraphicsGlobals g_gfx;

void FlushPendingDestroyAll() {
    for (Window* w : g_windows)
        if (w->impl_) w->impl_->FlushPendingDestroy();
}

namespace detail {
std::string g_lastError;

void SetError(std::string s) { g_lastError = std::move(s); }
}

std::string LastError() { return detail::g_lastError; }

Window::Window() : impl_(std::make_unique<detail::WindowImpl>()) {
    static const wchar_t kClassName[] = L"SlenderWindow";
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc = &detail::WindowImpl::WndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClassName;
        wc.style = CS_DBLCLKS;   // double-click word-select / triple-click select-all (R27-03)
        if (!RegisterClassExW(&wc)) {
            detail::SetError("RegisterClassExW(SlenderWindow) failed");   // ARCH38-04
            return;
        }
        registered = true;
    }
    impl_->owner = this;
    TitleBar& tb = Add<TitleBar>();
    tb.dock_ = detail::Dock::Top;         // the owner-drawn title bar docks topmost
    impl_->titlebar = &tb;
    impl_->hwnd = CreateWindowExW(
        WS_EX_NOREDIRECTIONBITMAP, kClassName, L"", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        nullptr, nullptr, GetModuleHandleW(nullptr), this);
    if (impl_->hwnd) {
        g_windows.push_back(this);
        SetClientSize(impl_->clientW, impl_->clientH);
        impl_->RefreshIcon();   // default window icon (regenerated once title/theme are ready)
    } else {
        detail::SetError("CreateWindowExW failed");   // ARCH38-04: previously returned silently
    }
}

Window::~Window() {
    // ARCH39-04: destruction is non-vetoable — Close() follows the API-10 contract and can be intercepted by SetOnClosing;
    // a veto leaks the HWND, dangles GWLP_USERDATA/g_windows, and Run()'s window-count exit
    // condition would never be satisfied. The destructor path destroys directly (WM_DESTROY completes the g_windows cleanup synchronously)
    if (impl_ && impl_->hwnd) DestroyWindow(impl_->hwnd);
}

Window& Window::SetTitle(std::wstring_view title) {
    std::wstring t(title);
    if (impl_->titlebar) impl_->titlebar->SetText(t);
    if (impl_->hwnd) SetWindowTextW(impl_->hwnd, t.c_str());
    impl_->RefreshIcon();   // the badge letter follows title changes (R34-02)

    return *this;
}

Window& Window::SetClientSize(float widthDip, float heightDip) {
    impl_->clientW = widthDip;
    impl_->clientH = heightDip;
    if (!impl_->hwnd) return *this;
    // owner-drawn title bar: WM_NCCALCSIZE treats the whole window rect as the client area ⇒ client pixels are
    // the window's outer-frame pixels. Previously AdjustWindowRectExForDpi's non-client frame was added on top,
    // making the actual client 16×39 larger than requested (L-01), amplified further at fractional DPI.
    SetWindowPos(impl_->hwnd, nullptr, 0, 0,
                 static_cast<int>(std::lround(widthDip * impl_->dpi / 96.0f)),
                 static_cast<int>(std::lround(heightDip * impl_->dpi / 96.0f)),
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    return *this;
}

Size Window::ClientSize() const {
    return { impl_->clientW, impl_->clientH };
}

Window& Window::SetMinimumSize(float widthDip, float heightDip) {
    impl_->minWidthDip = std::max(0.0f, widthDip);
    impl_->minHeightDip = std::max(0.0f, heightDip);
    // existing windows already below the new floor are not force-grown (native contract: only later interactions are constrained)

    return *this;
}

Window& Window::SetMaximizable(bool enabled) {
    if (!impl_) return *this;
    impl_->maximizable = enabled;
    if (impl_->hwnd) {
        LONG_PTR style = GetWindowLongPtrW(impl_->hwnd, GWL_STYLE);
        LONG_PTR desired =
            enabled ? (style | WS_MAXIMIZEBOX) : (style & ~WS_MAXIMIZEBOX);
        if (desired != style) {
            SetWindowLongPtrW(impl_->hwnd, GWL_STYLE, desired);
            // WS_MAXIMIZEBOX is the OS-level gate for the snap/maximize suite (Win+Up, Aero Snap,
            // system-menu entry, caption double-click); SC_MAXIMIZE posts are swallowed in WndProc.
            // FRAMECHANGED applies the style without moving, sizing or activating the window
            SetWindowPos(impl_->hwnd, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                         SWP_FRAMECHANGED);
        }
    }

    return *this;
}

bool Window::Maximizable() const {
    return impl_ && impl_->maximizable;
}

const Theme& Window::GetTheme() const {
    return impl_->theme;
}

Window& Window::SetTheme(const Theme& theme) {
    impl_->theme = theme;
    // the swap chain background color fills newly exposed regions in scaling gaps (DXGI_SCALING_NONE)
    if (impl_->rt.swap) {
        DXGI_RGBA bg = detail::ToDxgi(theme.windowBackground);
        impl_->rt.swap->SetBackgroundColor(&bg);
    }
    impl_->Invalidate();
    impl_->RefreshIcon();   // the default window icon follows the theme accent (R34-02)

    return *this;
}

Window& Window::SetIcon(HICON iconBig, HICON iconSmall) {
    if (!impl_ || !impl_->hwnd) return *this;
    if (!iconBig && !iconSmall) {   // both empty = restore the library default icon
        impl_->iconUser = false;
        impl_->iconLetter.clear();  // clear the cache key to force regeneration
        impl_->RefreshIcon();
        return *this;               // STY48-01: indentation aligned (the original 4 spaces misled)
    }
    impl_->iconUser = true;
    // GAP48-01: install the user icon first; library-generated icons are not destroyed — still owned by iconCache_
    // for reuse when restoring the default, so the window holds valid handles at any moment (the old way DestroyIcon'd
    // first then WM_SETICON, briefly holding freed handles)
    // R35-02/GAP52-02: when one side passes 0, the other tier falls back to the non-empty handle (isomorphic to RefreshIcon's
    // two-way fallback); never post an empty handle to silently clear an existing tier — once iconUser is set,
    // RefreshIcon early-outs and the library default would not come back, leaving a blank icon forever
    SendMessageW(impl_->hwnd, WM_SETICON, ICON_BIG,
                 (LPARAM)(iconBig ? iconBig : iconSmall));
    SendMessageW(impl_->hwnd, WM_SETICON, ICON_SMALL,
                 (LPARAM)(iconSmall ? iconSmall : iconBig));
    impl_->generatedIconBig = impl_->generatedIconSmall = nullptr;   // aliases voided (ownership stays with the cache)
    impl_->iconLetter.clear();

    return *this;
}

void Window::BeginLayoutBatch() {
    if (impl_) ++impl_->layoutBatchDepth_;
}

void Window::EndLayoutBatch() {
    if (!impl_) return;
    if (impl_->layoutBatchDepth_ > 0) --impl_->layoutBatchDepth_;
    if (impl_->layoutBatchDepth_ == 0) impl_->FlushLayout();
}

Label& Window::AddLabel(std::wstring_view text, const Rect& bounds) {
    Label& label = Add<Label>();
    label.SetText(text);
    label.SetBounds(bounds);
    return label;
}

Button& Window::AddButton(std::wstring_view text, const Rect& bounds,
                          std::function<void()> onClick) {
    Button& button = Add<Button>();
    button.SetText(text);
    button.SetBounds(bounds);
    button.OnClick = std::move(onClick);
    return button;
}

MenuBar& Window::AddMenuBar() {
    MenuBar& bar = Add<MenuBar>();
    bar.dock_ = detail::Dock::Top;
    impl_->Layout();
    impl_->Invalidate();
    return bar;
}

ToolBar& Window::AddToolBar() {
    ToolBar& bar = Add<ToolBar>();
    bar.dock_ = detail::Dock::Top;
    impl_->Layout();
    impl_->Invalidate();
    return bar;
}

StatusBar& Window::AddStatusBar() {
    StatusBar& bar = Add<StatusBar>();
    bar.dock_ = detail::Dock::Bottom;
    impl_->Layout();
    impl_->Invalidate();
    return bar;
}

TopBar& Window::AddTopBar() {
    TopBar& bar = Add<TopBar>();
    bar.dock_ = detail::Dock::Top;
    impl_->Layout();
    impl_->Invalidate();
    return bar;
}

NavigationView& Window::AddNavigationView() {
    NavigationView& nav = Add<NavigationView>();
    nav.dock_ = detail::Dock::Left;
    impl_->Layout();
    impl_->Invalidate();
    return nav;
}

void Window::ShowContentDialog(const ContentDialogDesc& desc) {
    if (impl_) impl_->ShowDialog(desc);
}

void Window::ShowContextMenu(Menu& menu, const Point& clientPos, float minWidth) {
    if (!impl_ || !impl_->hwnd) return;
    impl_->CloseMenus();
    std::vector<detail::Flyout::Item> items;
    for (const auto& mi : menu.items_) {
        detail::Flyout::Item it;
        it.separator = mi.separator;
        it.disabled = mi.disabled;
        it.checkable = mi.checkable;
        it.checked = mi.checked;
        it.accent = mi.selected;
        it.icon = mi.icon;
        it.text = mi.text;
        it.shortcut = mi.shortcut;
        it.onClick = mi.onClick;
        items.push_back(std::move(it));
    }
    impl_->flyout = std::make_unique<detail::Flyout>(
        impl_->hwnd, impl_->dpi, impl_->theme,
        Rect{ clientPos.x, clientPos.y, 0, 0 }, std::move(items), minWidth);
}

Rect Window::ContentArea() const {
    impl_->Layout();
    return impl_->contentRect;
}

void Window::ShowToast(std::wstring_view title, std::wstring_view subtitle) {
    if (!impl_ || !impl_->hwnd) return;
    impl_->toast = std::make_unique<detail::ToastPopup>(
        impl_->hwnd, impl_->dpi, impl_->theme, title, subtitle);
}

Window& Window::SetTickHandler(std::function<void()> handler, uint32_t intervalMs) {
    if (!impl_ || !impl_->hwnd) return *this;
    impl_->onTick = std::move(handler);
    if (impl_->onTick && intervalMs > 0)
        SetTimer(impl_->hwnd, detail::kTickTimerId, intervalMs, nullptr);
    else
        KillTimer(impl_->hwnd, detail::kTickTimerId);

    return *this;
}

void Window::Show() {
    if (impl_->hwnd) ShowWindow(impl_->hwnd, SW_SHOW);
}

void Window::Hide() {
    if (impl_->hwnd) ShowWindow(impl_->hwnd, SW_HIDE);
}

bool Window::Minimized() const {
    // round-66 R4: SW_SHOW does not restore an iconic window, so tray-click
    // patterns need to tell "show" from "restore" — read the OS truth.
    // (Named Minimized, not IsMinimized: windowsx.h owns IsMinimized as a
    // function-like macro alias of IsIconic.)
    return impl_->hwnd && IsIconic(impl_->hwnd);
}

void Window::Restore() {
    // round-66 R4: SW_RESTORE — a minimized window comes back to its previous
    // size and position (a window that was maximized before minimizing comes
    // back maximized) and is activated; on a visible window this is just an
    // activation. Show() keeps its plain SW_SHOW semantics.
    if (impl_->hwnd) ShowWindow(impl_->hwnd, SW_RESTORE);
}

Window& Window::SetTopmost(bool topmost) {
    if (!impl_ || !impl_->hwnd) return *this;
    SetWindowPos(impl_->hwnd, topmost ? HWND_TOPMOST : HWND_NOTOPMOST,
                 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    return *this;
}

bool Window::IsTopmost() const {
    if (!impl_ || !impl_->hwnd) return false;
    return (GetWindowLongPtrW(impl_->hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
}

Window& Window::SetRoundedCorners(bool rounded) {
    if (!impl_ || !impl_->hwnd) return *this;
    // round-68 R8: the main window shares the popup host's DWM corner-preference path
    // (PopupWindow::Create). One-shot: DWM keeps the preference for the window's lifetime, so
    // no re-application is needed on show/hide, minimize/restore, WM_DPICHANGED or
    // SWP_FRAMECHANGED. false restores DWMWCP_DEFAULT — on this borderless swap-chain window
    // the system default is square corners, which is exactly the pre-call state. A failed call
    // (Windows 10) leaves the window square; no error is raised, matching the popup contract —
    // RoundedCorners() is the consumer's verification read.
    DWORD pref = rounded ? detail::kDwmwcpRound : detail::kDwmwcpDefault;
    DwmSetWindowAttribute(impl_->hwnd, detail::kDwmwaWindowCornerPreference,
                          &pref, sizeof(pref));

    return *this;
}

bool Window::RoundedCorners() const {
    if (!impl_ || !impl_->hwnd) return false;
    // OS truth (mirrors IsTopmost): reads the live DWM attribute back, so a host-side or
    // system-side change stays reflected; on Windows 10 the read fails and returns false.
    DWORD pref = 0;
    return SUCCEEDED(DwmGetWindowAttribute(impl_->hwnd,
                      detail::kDwmwaWindowCornerPreference,
                      &pref, sizeof(pref))) &&
           pref == detail::kDwmwcpRound;
}

void Window::Close() {
    if (impl_ && impl_->hwnd) {
        // API-10: programmatic close shares the WM_CLOSE contract and can be intercepted by SetOnClosing
        if (impl_->onClosing && !impl_->onClosing()) return;
        DestroyWindow(impl_->hwnd);
    }
}

Window& Window::SetOnClosing(std::function<bool()> handler) {
    impl_->onClosing = std::move(handler);

    return *this;
}

Window& Window::SetDpiChangedHandler(std::function<void(float newDpi)> handler) {
    impl_->onDpiChanged = std::move(handler);

    return *this;
}

Window& Window::SetSystemThemeChangedHandler(std::function<void()> handler) {
    impl_->onSystemThemeChanged = std::move(handler);

    return *this;
}

Window& Window::SetSizeChangedHandler(std::function<void()> handler) {
    impl_->onSizeChanged = std::move(handler);

    return *this;
}

bool SystemPrefersDark() {
    DWORD light = 1, size = sizeof(light);
    if (FAILED(RegGetValueW(
            HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size)))
        return false;
    return light == 0;
}

Window& Window::AddAccelerator(uint32_t vk, bool ctrl, bool shift, bool alt,
                               std::function<void()> onClick) {
    impl_->accelerators.push_back(
        { vk, ctrl, shift, alt, std::move(onClick) });
    return *this;
}

Window& Window::SetPosition(float xDip, float yDip) {
    if (!impl_ || !impl_->hwnd) return *this;
    SetWindowPos(impl_->hwnd, nullptr,
                 static_cast<int>(std::lround(xDip * impl_->dpi / 96.0f)),
                 static_cast<int>(std::lround(yDip * impl_->dpi / 96.0f)),
                 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

    return *this;
}

Point Window::Position() const {
    Point p{};
    if (!impl_ || !impl_->hwnd) return p;
    RECT rc;
    if (GetWindowRect(impl_->hwnd, &rc)) {
        p.x = static_cast<float>(rc.left) * 96.0f / impl_->dpi;
        p.y = static_cast<float>(rc.top) * 96.0f / impl_->dpi;
    }
    return p;
}

void Window::CenterOnScreen() {
    if (!impl_ || !impl_->hwnd) return;
    RECT wrc;
    if (!GetWindowRect(impl_->hwnd, &wrc)) return;
    MONITORINFO mi{ sizeof(mi) };
    if (!GetMonitorInfoW(
            MonitorFromWindow(impl_->hwnd, MONITOR_DEFAULTTONEAREST), &mi))
        return;
    int x = mi.rcWork.left +
            ((mi.rcWork.right - mi.rcWork.left) - (wrc.right - wrc.left)) / 2;
    int y = mi.rcWork.top +
            ((mi.rcWork.bottom - mi.rcWork.top) - (wrc.bottom - wrc.top)) / 2;
    SetWindowPos(impl_->hwnd, nullptr, x, y, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

bool Window::IsAlive() const {
    return impl_ && impl_->hwnd != nullptr;
}

TitleBar& Window::GetTitleBar() {
    // the title bar is created with the window (ctor) and never null (round-65 R1 entry point)
    return *impl_->titlebar;
}

// ---------------------------------------------------------------------------
// global implementation
// ---------------------------------------------------------------------------

bool Initialize() {
    if (g_d2d) return true;

    // Per-Monitor V2 DPI awareness (fails if a manifest already set it; ignore — ARCH-15:
    // the comment and contract are made explicit here, the return value deliberately discarded rather than ignored unknowingly)
    (void)SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    D2D1_FACTORY_OPTIONS options{};
    HRESULT hr = D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &options,
        reinterpret_cast<void**>(g_d2d.GetAddressOf()));
    if (FAILED(hr)) {
        detail::SetError("D2D1CreateFactory failed");
        return false;
    }

    hr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_dwrite.GetAddressOf()));
    if (FAILED(hr)) {
        g_d2d.Reset();
        detail::SetError("DWriteCreateFactory failed");
        return false;
    }
    detail::SetError("");   // success clears any prior failure record
    return true;
}

void Shutdown() {
    // ARCH-17 + ARCH38-02: explicitly clear cached COM objects and strings instead of relying on function-local
    // static destruction order (their destructors run after g_dwrite/g_d2d's Reset).
    // contract: Shutdown must be called after all Windows are destroyed (a window's device resources are released
    // with impl_); window-class registration flags are not cleared — window classes live at process scope and
    // clearing them would break the Initialize→Shutdown→Initialize restart path
    detail::WindowImpl::DiscardAllDevices();
    // ARCH38-02: static Icons' device geometry is released before g_d2d.Reset (tried reset,
    // rebuilt via GetGeometry after restart)
    for (auto& w : detail::IconData::Live())
        if (auto d = w.lock()) {
            d->geometry.Reset();
            d->tried = false;
        }
    detail::TextCacheInstance().ClearAll();
    detail::FontResolveCache().clear();
    detail::SetError("");
    detail::g_focusFromKeyboard = false;
    g_gfx = detail::GraphicsGlobals{};
    g_dwrite.Reset();
    g_d2d.Reset();
}

Size MeasureText(std::wstring_view text, const Font& font) {
    DWRITE_TEXT_METRICS m = detail::MeasureTextMetrics(text, font);
    return { m.widthIncludingTrailingWhitespace, m.height };
}

int Run() {
    MSG msg{};
    // ARCH-09: exit driven by the window count — return once the last window is destroyed, without depending
    // on WM_QUIT (a WM_QUIT posted during window destruction would poison the next Run())
    bool sawWindow = !g_windows.empty();
    for (;;) {
        int ret = GetMessageW(&msg, nullptr, 0, 0);
        if (ret <= 0) return ret < 0 ? -1 : static_cast<int>(msg.wParam);
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        FlushPendingDestroyAll();   // ARCH38-07: the deferred destroy queue destructs in bulk
        if (!sawWindow && !g_windows.empty()) sawWindow = true;
        if (sawWindow && g_windows.empty()) return 0;
    }
}

bool PumpOnce() {
    MSG msg{};
    bool quit = false;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) { quit = true; break; }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    // ARCH41-01: the flush moved out of the message-drain loop — flushed even when the queue is empty, honoring Remove's
    // documented contract that "users with their own message loops must call PumpOnce periodically for destruction"
    // (otherwise pendingDestroy_ grows monotonically in idle rounds and pending widgets are never reclaimed)
    FlushPendingDestroyAll();
    return !quit;
}

} // namespace slender

#endif // SLENDER_IMPLEMENTATION_INCLUDED
#endif // SLENDER_IMPLEMENTATION
