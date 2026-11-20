# Qt Designer UI files

The main window and the settings dialog shell are loaded at runtime from
Designer files (CMake AUTOUIC generates `ui_*.h`):

- **`src/ui/mainwindow.ui`** — `MainWindow::setupUi()` calls `Ui::MainWindow::setupUi(this)`
- **`src/ui/settingsdialog.ui`** — `SettingsDialog` calls `Ui::SettingsDialog::setupUi(this)`
  (categories/tabs are registered dynamically in code)
- **`src/ui/<page>.ui`** — internal layout of each settings page
  (`backendpage`, `mcppage`, `sttpage`, `ttspage`, `systempromptpage`,
  `shortcutpage`, `userinterfacepage`, `prompttemplatespage`); widget
  `objectName`s match the former `setObjectName()` calls (e.g. `themeCombo`,
  `apiKeyRadio`, `apiKeyEdit`, `headersRadio`, `headersEdit`).
- **`src/ui/sidebar.ui`** — internal layout of the `Sidebar` promoted widget
  (the chat tree `tree` with its view/drag-drop properties); loaded via
  `Ui::Sidebar::setupUi()` in `Sidebar::Sidebar()`.

Open them in Qt Widget Designer:

```sh
designer src/ui/mainwindow.ui        # or open via Qt Creator
```

Custom widgets are promoted widgets in the .ui file. Their class names in
the .ui are **plain (non-namespaced)** — Qt Designer's form engine cannot
resolve namespaced class names (`okapi::Sidebar`) and would silently drop
the widgets plus the `<customwidgets>` block when saving, after which the
generated `ui_*.h` no longer compiles. `tst_uifiles` guards this rule in CI.
The `using okapi::…` declarations in `mainwindow.cpp` make the plain names
resolve in the generated header.

| Widget in .ui | Class in .ui | Real class |
|---|---|---|
| `sidebar` | `Sidebar` | `okapi::Sidebar` (`ui/sidebar.h`) |
| `chatView` | `WebChatView` | `okapi::WebChatView` (`ui/webchatview.h`) |
| `composer` | `Composer` | `okapi::Composer` (`ui/composer.h`) |
| `modelPicker` | `ModelPickerButton` | `okapi::ModelPickerButton` (`ui/mainwindow.h`) |

The footer row (`micButton`, `attachButton`) and the status bar labels
(`statusLabel`, `mcpStatusLabel`) are declared in `mainwindow.ui` as well;
`Composer` adopts the two buttons via `adoptToolButtons()` after
`setupUi()`.

To see the promoted widgets as real widgets in Designer's widget box, build
and install the Designer plugin in this directory (needs the `qttools`
module; optional, skipped automatically when Qt6::Designer is missing):

```sh
cmake --build build --target okapiwidgets
cmake --install build --component designer   # or copy build/designer/okapiwidgets.so
                                               # to ~/.local/share/qt6/plugins/designer/
designer src/ui/mainwindow.ui
```

Edit the layout in Designer, keep `objectName`s stable (unit + E2E tests
locate widgets by name/accessible name), and rebuild.
