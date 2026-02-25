# Research: Dockable/Detachable Views in GTK4 C++ Applications

**Date:** 2026-02-20
**Context:** arma-tools-gtk uses **gtkmm-4.0** (C++ bindings for GTK4)

## Requirements Recap

1. Components (3D preview, asset browser, config panels) can be dragged out of the main window into separate windows (multi-monitor)
2. Components can be rearranged within the main window (drag-and-drop repositioning)
3. Default layout with user customization
4. Save/restore layout state

---

## 1. libpanel (GNOME Panel Library for GTK4) — RECOMMENDED

**Repository:** https://gitlab.gnome.org/GNOME/libpanel
**API Docs:** https://gnome.pages.gitlab.gnome.org/libpanel/panel-1.0/
**License:** LGPL-3.0-or-later
**Current Version:** 1.10.4 (tracking GNOME 48)
**GTK Version:** GTK4 only (requires GTK4 + libadwaita)
**Language:** C (GObject-based), no official C++ (gtkmm-style) bindings

### Overview

libpanel is the only actively maintained, production-quality docking library for GTK4. It was designed for GNOME Builder (the GNOME IDE) and is now used by multiple GNOME applications. It provides a complete set of widgets for IDE-like panel/dock layouts.

### Key Widgets

| Widget | Purpose |
|--------|---------|
| **PanelDock** | Top-level container dividing space into 5 areas: top, bottom, start, end, center. Surrounding areas can be revealed/hidden. |
| **PanelGrid** | Manages the center area layout with columns of frames. |
| **PanelGridColumn** | A column within the grid. |
| **PanelFrame** | Contains multiple PanelWidgets in an AdwTabView (one visible at a time, like tabs). |
| **PanelWidget** | Base class for dockable content. Can be used as-is or subclassed. Has title, icon, tooltip, modified state, busy state. |
| **PanelPosition** | Specifies a position in the dock (area, row, column, depth). |
| **PanelFrameHeaderBar** | Header bar for a frame with close/maximize buttons. |
| **PanelFrameTabBar** | Tab bar for switching between panels in a frame. |
| **PanelFrameSwitcher** | Alternative switcher for frame panels. |
| **PanelPaned** | Concrete widget for a panel area (sidebar). |
| **PanelDockChild** | Wrapper for dock area children (auto-managed, not created directly). |

### Layout Serialization (Session API)

| Class | Purpose |
|-------|---------|
| **PanelSession** | Container for session state. Serializable to/from `GVariant` via `panel_session_to_variant()` and `panel_session_new_from_variant()` (since v1.4). |
| **PanelSessionItem** | Individual item in a session, storing: `id`, `module-name`, `position` (PanelPosition), `type-hint`, `workspace`, plus arbitrary key-value metadata. |
| **PanelSaveDelegate** | Attached to PanelWidget to handle save-on-close logic. |
| **PanelWorkspace** | High-level workspace container. |
| **PanelWorkbench** | Central application workbench. |

**Save/Restore Flow:**
1. Iterate panels, create PanelSessionItem for each with position + metadata
2. Serialize PanelSession to GVariant
3. Store GVariant (e.g., GSettings, file)
4. On restore: deserialize GVariant -> PanelSession -> recreate widgets at saved positions

### Drag and Drop

- Panels can be dragged between frames within the dock
- `PanelDock::create-frame` signal fires when the dock needs a new frame (e.g., during drag)
- `PanelDock::adopt-widget` signal fires during DnD when a widget wants to join the dock
- `panel-drag-begin` / `panel-drag-end` signals track the drag lifecycle
- Tab reordering within frames is built-in (via AdwTabView)

### Key Signals

```c
// Emitted when dock needs a new frame (during drag operations)
PanelFrame* create_frame(PanelDock* dock, PanelPosition* position);

// Emitted when a widget wants to be adopted during DnD
gboolean adopt_widget(PanelDock* dock, PanelWidget* widget);
```

### Strengths

- **Production-proven:** Used by GNOME Builder, which has complex multi-panel layouts
- **Actively maintained:** Part of GNOME core, tracks GNOME releases
- **Full feature set:** Docking, drag-and-drop, tab management, session serialization
- **Multi-monitor support:** GNOME Builder uses secondary workspaces for additional monitors with automatic state save/restore
- **Modern GTK4:** Uses libadwaita for consistent GNOME styling
- **Well-structured API:** Clean GObject hierarchy

### Weaknesses / Concerns

- **No C++ bindings:** Must use raw C GObject API from C++ (or use `gi` / write thin wrappers). No `panelmm` exists.
- **libadwaita dependency:** Requires libadwaita, not just GTK4. This adds the GNOME-specific styling/widgets.
- **Acknowledged TODO items:** Maintainers note: "Review API, there are a lot of things I'd like to change", drag-and-drop has known issues, some naming is poor.
- **IDE-focused:** Designed around IDE use cases; may need adaptation for non-IDE apps.
- **Documentation:** API reference exists but lacks tutorials/examples. Must study GNOME Builder source code.
- **No tear-off to separate OS window:** Panels move between dock areas, but tearing off into a fully independent OS window requires the PanelWorkspace pattern (as Builder does for multi-monitor).

### Usage from C++ (gtkmm-4.0)

Since there are no C++ bindings, you would:
```cpp
// Option A: Use C API directly with Glib::wrap()
#include <panel.h>

auto* dock = panel_dock_new();
auto* frame = panel_frame_new();
auto* widget = panel_widget_new();
panel_widget_set_title(widget, "Asset Browser");
panel_widget_set_child(widget, GTK_WIDGET(my_gtkmm_widget->gobj()));

// Option B: Write thin C++ wrappers around the GObject types
// Option C: Use Gtk::Widget::wrap_new() patterns from gtkmm
```

### GNOME Builder as Reference Implementation

GNOME Builder 43+ demonstrates:
- Panel positions persisting across sessions
- Secondary workspaces on additional monitors
- State of editors/terminals/browsers saved and restored
- Panels draggable around the workspace

Source: https://gitlab.gnome.org/GNOME/gnome-builder

---

## 2. GDL (GNOME Docking Library) — DEPRECATED

**Repository:** https://gitlab.gnome.org/GNOME/gdl (archived)
**Docs:** https://developer-old.gnome.org/gdl/unstable/
**Latest Version:** 3.40.0
**GTK Version:** GTK3 only
**Language:** C (GObject-based)

### Overview

GDL was the original GNOME docking library, used primarily by the Anjuta IDE and Inkscape. It provided a full docking framework with floating windows, tab-based stacking, and drag-and-drop rearrangement.

### Key Components

- **GdlDock** — Top-level dock container
- **GdlDockItem** — A dockable item/panel
- **GdlDockNotebook** — Tabbed container for stacked dock items
- **GdlDockPaned** — Split container
- **GdlDockBar** — Iconified/minimized dock items
- **GdlDockLayout** — Layout serialization to/from XML
- **GdlSwitcher** — Tab switcher widget

### Why It's Not Suitable

- **Archived/unmaintained:** The GitLab repository is archived. No GTK4 port exists or is planned.
- **Broken on Wayland:** Cannot position floating windows relative to each other on Wayland, which breaks the core drag-and-drop docking.
- **Inkscape abandoned it:** Inkscape was the last major user and has moved to a custom GtkNotebook-based system for their GTK4 migration.
- **Complex/confusing API:** Inkscape's own wiki notes: "Functionality is very confusing!"

### Lessons from GDL

- Floating dock windows are problematic on Wayland (cannot control window placement)
- The XML layout serialization approach worked well conceptually
- Tab-based stacking (GdlDockNotebook) is the pattern everyone converged on

---

## 3. GtkNotebook + Drag-and-Drop (DIY Approach)

**GTK Version:** GTK3 and GTK4
**Language:** C, C++ (gtkmm), any binding

### Overview

GTK's built-in `GtkNotebook` (or `Gtk::Notebook` in gtkmm) supports detachable tabs natively. This is the most lightweight approach and what Inkscape migrated to after abandoning GDL.

### Key API

```cpp
// gtkmm-4.0 API
Gtk::Notebook notebook;

// Enable tab reordering within the notebook
notebook.set_tab_reorderable(child, true);

// Enable tab detachment (dragging out)
notebook.set_tab_detachable(child, true);

// Set group name — notebooks with same group can exchange tabs
notebook.set_group_name("my-dock-group");

// Signal: emitted when a tab is dropped on the root window
// Handler must create a new window + notebook and return it
notebook.signal_create_window().connect(
    [](Gtk::Widget* page) -> Gtk::Notebook* {
        auto* window = new Gtk::Window();
        auto* new_notebook = Gtk::make_managed<Gtk::Notebook>();
        new_notebook->set_group_name("my-dock-group");
        window->set_child(new_notebook);
        window->present();
        return new_notebook;
    }
);
```

### Implementation Pattern for Full Docking

```
MainWindow
├── Gtk::Paned (horizontal)
│   ├── Gtk::Paned (vertical, left sidebar)
│   │   ├── Gtk::Notebook [group="dock"] — "Asset Browser", "Config"
│   │   └── Gtk::Notebook [group="dock"] — "Properties"
│   └── Gtk::Paned (vertical, main area)
│       ├── Gtk::Notebook [group="dock"] — "3D Preview", "Map View"
│       └── Gtk::Notebook [group="dock"] — "Console", "Log"
│
DetachedWindow (created on tab tear-off)
└── Gtk::Notebook [group="dock"] — dragged-out panel
```

### Layout Serialization (Custom)

You must implement this yourself:
```cpp
struct PanelState {
    std::string panel_id;     // "asset_browser", "3d_preview", etc.
    int paned_index;          // which Gtk::Paned area
    int notebook_index;       // which notebook
    int tab_position;         // tab index within notebook
    bool is_detached;         // in separate window?
    int window_x, window_y;  // window position (if detached)
    int window_w, window_h;  // window size (if detached)
};
// Serialize to JSON, restore on startup
```

### Strengths

- **No extra dependencies:** Uses only GTK4 built-in widgets
- **Native gtkmm support:** Full C++ API through Gtk::Notebook
- **Simple concept:** Easy to understand and debug
- **Tab reordering built-in**
- **Tab detachment to new window built-in**
- **Works on Wayland:** The window creation for detached tabs works

### Weaknesses

- **No dock zones:** Cannot drop a tab onto the "left edge" of a panel to create a split. Only notebook-to-notebook or notebook-to-new-window.
- **No visual drop indicators:** No overlay showing "drop here to dock left/right/top/bottom"
- **Layout management is DIY:** All serialization, default layouts, split management must be hand-coded
- **No side panel reveal/hide:** Must implement sidebar show/hide toggle yourself
- **No "iconify" to dock bar:** No built-in way to minimize a panel to an icon strip
- **Limited rearrangement:** Tabs move between notebooks, but creating new split areas dynamically requires significant custom code

### Wayland Considerations

On Wayland, you cannot programmatically position windows. The `create-window` handler creates the window, but the compositor decides placement. Saving/restoring exact window positions across sessions may not work on Wayland.

---

## 4. GIMP's Custom Docking System

**GTK Version:** GTK3 (GIMP 3.0)
**Language:** C

### Overview

GIMP implements its own custom docking system with a large amount of custom code. It is the most feature-rich docking implementation in the GTK ecosystem but is entirely GIMP-specific and not available as a library.

### Key Components

| Component | Purpose |
|-----------|---------|
| **GimpDock** | Base dock container |
| **GimpDockWindow** | Standalone window containing docks |
| **GimpDockbook** | Tabbed notebook of dockable dialogs (extends GtkNotebook) |
| **GimpDockable** | A single dockable dialog |
| **GimpPanedBox** | Paned container with drop zone highlighting |
| **GimpDockColumns** | Column-based layout for multiple dockbooks |
| **GimpSessionInfo** | Layout serialization |

### Features

- Drag tabs between dockbooks
- Detach tabs into new floating dock windows
- Drop zones with visual highlighting (color change on the rectangle at the bottom of the toolbox)
- "Detach Tab" menu option
- Session save/restore (window positions, dock contents, tab order)
- Iconified dockable dialogs
- Dockable preview windows

### Why Not Reusable

- Deeply integrated with GIMP's architecture (~20+ source files)
- Not a library; cannot be extracted without massive effort
- GTK3 only (GIMP 3.0 completed the GTK3 port; GTK4 port not yet started)
- Very complex codebase

### Lessons from GIMP

- Custom docking requires significant code investment (thousands of lines)
- GtkNotebook subclassing is the foundation
- Drop zone visual feedback is essential for usability
- Session serialization must capture: window positions, dock contents, tab order, panel sizes
- "Detach Tab" as a menu item is a good UX pattern (not everyone discovers drag-to-detach)

---

## 5. Inkscape's Approach (Post-GDL)

**GTK Version:** GTK3 -> GTK4 migration in progress
**Language:** C++ (gtkmm)

### Overview

Inkscape originally used GDL for docking but abandoned it due to:
- Wayland incompatibilities (cannot position windows relative to each other)
- Maintenance concerns (Inkscape was the last major GDL user)
- Confusing API

### Current Approach

Inkscape now uses **plain GtkNotebook/Gtk::Notebook** with custom dialog management:
- Dialogs are placed in notebooks docked to the right side of the main window
- Multiple notebooks can exist in a vertical paned layout
- Tabs can be dragged between notebooks (built-in GTK feature)
- The `create-window` signal handles tear-off into separate windows

### Relevant Source Files

- `src/ui/widget/desktop-widget.cpp` — Main desktop layout
- `src/ui/dialog/dialog-manager.cpp` — Dialog registration and lifecycle

### GTK4 Migration Status

- Active migration in progress (2024-2025)
- Dialog drag-and-drop has been a pain point (crashes when moving tabs to new windows then right-clicking)
- Not using libpanel — staying with custom notebook-based approach
- Focus is on porting away from deprecated GTK3 APIs rather than adopting new docking frameworks

### Lessons from Inkscape

- GDL was more trouble than it was worth
- Simple notebook-based docking is sufficient for most use cases
- Wayland breaks floating window positioning assumptions
- The simpler the docking system, the easier the GTK version migration

---

## 6. Custom Implementation with GTK4 Drag-and-Drop API

**GTK Version:** GTK4
**Language:** C, C++ (gtkmm)

### Overview

For maximum control, you can build a custom docking framework using GTK4's drag-and-drop primitives.

### Key GTK4 DnD API

```cpp
// Drag source (on the panel header/tab)
auto drag_source = Gtk::DragSource::create();
drag_source->set_actions(Gdk::DragAction::MOVE);
drag_source->signal_prepare().connect([](double x, double y) {
    return Gdk::ContentProvider::create(/* panel reference */);
});
panel_header->add_controller(drag_source);

// Drop target (on dock zones)
auto drop_target = Gtk::DropTarget::create(PanelWidget::get_type(), Gdk::DragAction::MOVE);
drop_target->signal_drop().connect([](const Glib::ValueBase& value, double x, double y) {
    // Reparent the panel widget to this dock zone
    return true;
});
dock_zone->add_controller(drop_target);
```

### What You'd Need to Build

1. **DockManager** — Tracks all panels and their current locations
2. **DockZone** — A container that accepts dropped panels (with visual indicators)
3. **DockSplitter** — Dynamic splitting when dropping on edges
4. **PanelTab** — Tab widget with drag source for tear-off
5. **FloatingWindow** — Window created when panel is torn off
6. **LayoutSerializer** — Save/restore to JSON/XML

### Effort Estimate

- 2000-4000 lines of code for a basic implementation
- 4000-8000+ lines for a polished implementation with visual drop zones, animations, and edge cases

### Strengths

- Full control over behavior and appearance
- No external dependencies beyond GTK4
- Can be tailored exactly to your needs
- Works with gtkmm C++ API natively

### Weaknesses

- Significant development effort
- Many edge cases (resize behavior, minimum sizes, nested splits, empty container cleanup)
- Must handle Wayland limitations yourself
- No community testing beyond your own

---

## Comparison Matrix

| Feature | libpanel | GDL | GtkNotebook DIY | GIMP-style | Custom DnD |
|---------|----------|-----|------------------|------------|------------|
| **GTK4 Support** | Yes | No (GTK3) | Yes | No (GTK3) | Yes |
| **C++ Bindings** | No (C only) | No | Yes (gtkmm) | No | Yes (gtkmm) |
| **Maintenance** | Active (GNOME) | Archived | N/A (built-in) | GIMP-only | You |
| **Tear-off to Window** | Via Workspace | Yes | Yes (built-in) | Yes | Must build |
| **Tab Reordering** | Yes | Yes | Yes (built-in) | Yes | Must build |
| **Dock Zones (edge drop)** | Yes (5 areas) | Yes | No | Yes | Must build |
| **Visual Drop Indicators** | Yes | Yes | No | Yes | Must build |
| **Layout Serialization** | Yes (GVariant) | Yes (XML) | Must build | Yes | Must build |
| **Sidebar Reveal/Hide** | Yes | Yes | Must build | Yes | Must build |
| **Effort to Integrate** | Medium | N/A | Low | N/A | High |
| **Wayland Compat** | Yes | Broken | Mostly | Unknown | Yes |
| **Dependency** | libpanel + libadwaita | gdl | None | N/A | None |

---

## Recommendation

### Primary: libpanel (with C wrapper layer)

**Why:** It is the only maintained, production-quality docking library for GTK4. It provides exactly the features needed (dock areas, drag-and-drop, session serialization, sidebar reveal/hide). GNOME Builder proves it works for complex IDE-like layouts.

**Integration strategy:**
1. Add libpanel as a dependency (available in all major Linux distros)
2. Write thin C++ wrapper classes around the key GObject types (PanelDock, PanelFrame, PanelWidget, PanelPosition, PanelSession)
3. Each existing tab (TabAssetBrowser, TabWrpInfo, etc.) becomes a PanelWidget child
4. Use PanelSession for layout save/restore

**Concern:** Requires libadwaita dependency. If the app currently does not use libadwaita, this adds visual/behavioral changes (GNOME HIG styling). Check if this is acceptable.

### Fallback: GtkNotebook DIY Approach

**Why:** Zero new dependencies, native gtkmm support, proven by Inkscape.

**When to choose this:**
- If libadwaita dependency is unacceptable
- If only basic tab tear-off is needed (no dock zone edge-dropping)
- If development time for layout serialization is available

**Implementation sketch:**
1. Replace the main Gtk::Notebook with multiple Gtk::Notebooks in Gtk::Paned containers
2. Set `set_tab_detachable(true)` and `set_group_name("arma-dock")` on all notebooks
3. Handle `signal_create_window()` to create floating windows
4. Implement JSON-based layout save/restore
5. Provide "Reset Layout" button for default arrangement

### Not Recommended

- **GDL:** Dead project, GTK3 only, broken on Wayland
- **Full custom DnD:** Too much effort for a non-UI-framework project
- **GIMP-style:** Not extractable, GTK3 only

---

## References

- libpanel GitLab: https://gitlab.gnome.org/GNOME/libpanel
- libpanel API docs: https://gnome.pages.gitlab.gnome.org/libpanel/panel-1.0/
- libpanel GitHub mirror: https://github.com/GNOME/libpanel
- GDL old docs: https://developer-old.gnome.org/gdl/unstable/
- GTK4 Notebook API: https://docs.gtk.org/gtk4/class.Notebook.html
- GTK4 Notebook detachable tabs: https://docs.gtk.org/gtk4/method.Notebook.set_tab_detachable.html
- GTK4 Notebook create-window signal: https://docs.gtk.org/gtk4/signal.Notebook.create-window.html
- GTK4 Drag-and-Drop overview: https://docs.gtk.org/gtk4/drag-and-drop.html
- Inkscape GDL wiki: https://wiki.inkscape.org/wiki/Gdl
- Inkscape GTK4 migration: https://wiki.inkscape.org/wiki/GTK+_4_Migration
- GNOME Builder 43 announcement: https://blogs.gnome.org/chergert/2022/09/22/gnome-builder-43/
- GIMP docking concepts: https://gimp.linux.it/www/manual/2.10/html/en/gimp-concepts-docks.html
- Qt-Advanced-Docking-System (for reference): https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System
