
// acre - ReXGlue Recompiled Project
//
// This file is yours to edit. 'rexglue migrate' will NOT overwrite it.
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/rex_app.h>

class AcreApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<AcreApp>(new AcreApp(ctx, "acre",
        PPCImageConfig));
  }

  // Override virtual hooks for customization:
  // void OnPreSetup(rex::RuntimeConfig& config) override {}
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostSetup() override {}
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // void OnShutdown() override {}
  void OnConfigurePaths(rex::PathConfig& paths) override {
    (void)paths;

    // Logging is initialized before acre.toml is loaded, so give the app a
    // stable default file sink unless the user supplied one via CLI or env.
    if (REXCVAR_GET(log_file).empty()) {
      REXCVAR_SET(log_file, (rex::filesystem::GetExecutableFolder() / "acre.log").string());
    }
  }
};
