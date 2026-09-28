#include "chzzk-dock.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")
OBS_MODULE_AUTHOR("OBS Live Editor contributors")

namespace {
constexpr const char *kDockId = "obs-live-editor-dock";
ChzzkDock *g_dock = nullptr;
} // namespace

const char *obs_module_description(void)
{
	return "Edit live title, category, and tags from an OBS dock.";
}

bool obs_module_load(void)
{
	g_dock = new ChzzkDock();
	if (!obs_frontend_add_dock_by_id(kDockId, obs_module_text("Dock.Title"), g_dock)) {
		delete g_dock;
		g_dock = nullptr;
		obs_log(LOG_ERROR, "could not register dock");
		return false;
	}

	obs_log(LOG_INFO, "plugin loaded (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_dock(kDockId);
	delete g_dock;
	g_dock = nullptr;
	obs_log(LOG_INFO, "plugin unloaded");
}
