#include "script.h"
#include <set>
#include <inc/natives.h>
#include "Util/Logger.hpp"
#include "Util/Versions.h"
#include "menu.h"
#include "keyboard.h"
#include "settings.h"
#include "ExtraTypes.h"
#include "NativeMemory.hpp"
#include "Util/Util.hpp"

std::string manualVehicleName = "";
std::string searchVehicleName = "";
bool manualSpawnSelected = false;
bool searchEntrySelected = false;

extern NativeMenu::Menu menu;
extern NativeMenu::MenuControls controls;
extern Settings settings;
extern Ped playerPed;

extern std::vector<Hash> g_missingImages;
extern std::vector<Vehicle> g_persistentVehicles;

extern std::set<std::string> g_addonClasses;
extern std::set<std::string> g_addonMakes;
extern std::vector<ModelInfo> g_addonVehicles;	// all add-on vehicles
extern std::vector<AddonImage> g_addonImages;
extern std::vector<AddonImageMeta> g_addonImageMetadata;
extern std::vector<std::string> g_addonImageNames; // just all filenames separately for hashing in begin

extern std::vector<Hash> GameVehicles;             // all base vehicles
extern std::vector<DLC> g_dlcs;
extern std::set<std::string> g_dlcClasses;
extern std::set<std::string> g_dlcMakes;
extern std::vector<ModelInfo> g_dlcVehicles;
extern std::vector<SpriteInfo> g_dlcSprites;

std::string evaluateInput(std::string &searchFor) {
	PLAYER::IS_PLAYER_CONTROL_ON(false);
	UI::SET_PAUSE_MENU_ACTIVE(false);
	CONTROLS::DISABLE_ALL_CONTROL_ACTIONS(1);
	CONTROLS::IS_CONTROL_ENABLED(playerPed, false);

	for (char c = ' '; c < '~'; c++) {
		int key = str2key(std::string(1, c));
		if (key == -1) continue;
		if (IsKeyJustUp(key)) {
			searchFor += c;
		}
	}
	if (IsKeyJustUp(str2key("SPACE"))) {
		searchFor += ' ';
	}
	if (IsKeyJustUp(str2key("VK_OEM_MINUS"))) {
		searchFor += '_';
	}
	if (IsKeyJustUp(str2key("DELETE")) && searchFor.size() > 0) {
		searchFor.pop_back();
	}
	if (IsKeyJustUp(str2key("BACKSPACE"))) {
		searchFor.clear();
	}

	return searchFor;
}

void updateSettings() {
	settings.SaveSettings();
	settings.ReadSettings();
	menu.ReadSettings();
}

void onMenuOpen() {
	updateSettings();
	cacheAddons();
	cacheDLCs();
	resolveVehicleSpriteInfo();
}

void onMenuExit() {
	manualVehicleName.clear();
}

void spawnMenu(std::string category, std::vector<ModelInfo> addonVehicles, std::string origin, bool asMake) {
	menu.Title(category);
	menu.Subtitle(origin);
	for (auto addonVehicle : addonVehicles) {
		if (category == (asMake ? addonVehicle.MakeName : addonVehicle.ClassName)) {
			char *name = VEHICLE::GET_DISPLAY_NAME_FROM_VEHICLE_MODEL(addonVehicle.ModelHash);
			std::string displayName = UI::_GET_LABEL_TEXT(name);
			if (displayName == "NULL") {
				displayName = name;
			}

			std::vector<std::string> extras = {};
			bool visible = false;
			if (menu.OptionPlus(displayName, extras, &visible, nullptr, nullptr, "Vehicle info", {})) {
				spawnVehicle(addonVehicle.ModelHash);
			}
			if (visible) {
				extras = resolveVehicleInfo(addonVehicle);
				menu.OptionPlusPlus(extras, "Vehicle info");
			}
		}
	}
}

void update_menu() {
	menu.CheckKeys();
	std::set<std::string> addonCats = settings.CategorizeMake ? g_addonMakes : g_addonClasses;

	if (menu.CurrentMenu("mainmenu")) {
		menu.Title("Add-on spawner");
		menu.Subtitle("~b~" + std::string(DISPLAY_VERSION) + "~w~");

		menu.MenuOption("Settings", "settingsmenu");

		if (settings.SpawnByName) {
			std::vector<std::string> extraSpawnInfo = {
				"Use Delete for backspace",
				"Enter car model:",
				manualVehicleName,
			};

			if (manualSpawnSelected) {
				evaluateInput(manualVehicleName);
			}

			if (menu.OptionPlus("Spawn by name", extraSpawnInfo, &manualSpawnSelected, nullptr, nullptr, "Enter name")) {
				spawnVehicle(GAMEPLAY::GET_HASH_KEY((char *)(manualVehicleName.c_str())));
			}
		}

		if (settings.ListAllDLCs) {
			if (settings.MergeDLCs) {
				if (menu.MenuOption("Spawn official DLCs", "officialdlcmergedmenu")) {
					resolveVehicleSpriteInfo();
				}
			}
			else {
				if (menu.MenuOption("Spawn official DLCs", "officialdlcmenu")) {
					resolveVehicleSpriteInfo();
				}
			}
		}

		if (settings.SearchMenu) {
			menu.MenuOption("Search vehicles", "searchmenu");
		}

		for (auto category : addonCats) {
			menu.MenuOption(category, category);
		}
	}

	if (menu.CurrentMenu("searchmenu")) {
		menu.Title("Search");
		menu.Subtitle("");

		std::vector<std::string> extraSpawnInfo = {
			"Use Delete for backspace",
			"Enter search criteria:",
			searchVehicleName,
			"Press enter to search"
		};

		if (searchEntrySelected) {
			evaluateInput(searchVehicleName);
		}

		menu.StringArray("Search in", { "Game vehicles", "Add-on vehicles" }, settings.SearchCategory);

		if (menu.OptionPlus("Search", extraSpawnInfo, &searchEntrySelected, nullptr, nullptr, "Search")) {
		}

		for (auto addonVehicle : settings.SearchCategory == 0 ? g_dlcVehicles : g_addonVehicles) {
			char *name = VEHICLE::GET_DISPLAY_NAME_FROM_VEHICLE_MODEL(addonVehicle.ModelHash);
			std::string displayName = UI::_GET_LABEL_TEXT(name);
			std::string rawName = name; 
			if (displayName == "NULL") {
				displayName = name;
			}
			std::string modelName = guessModelName(addonVehicle.ModelHash);

			std::string makeNameRaw = MemoryAccess::GetVehicleMakeName(addonVehicle.ModelHash);
			std::string makeName = UI::_GET_LABEL_TEXT(MemoryAccess::GetVehicleMakeName(addonVehicle.ModelHash));

			if (ci_find_substr(rawName, searchVehicleName) != -1 ||
				ci_find_substr(displayName, searchVehicleName) != -1 ||
				ci_find_substr(modelName, searchVehicleName) != -1 ||
				ci_find_substr(makeName, searchVehicleName) != -1 ||
				ci_find_substr(makeNameRaw, searchVehicleName) != -1) {
				std::vector<std::string> extras = {};
				bool visible = false;
				if (menu.OptionPlus(displayName, extras, &visible, nullptr, nullptr, "Vehicle info", {})) {
					spawnVehicle(addonVehicle.ModelHash);
				}
				if (visible) {
					extras = resolveVehicleInfo(addonVehicle);
					menu.OptionPlusPlus(extras, "Vehicle info");
				}
			}
		}
	}

	if (menu.CurrentMenu("settingsmenu")) {
		menu.Title("Settings");
		menu.Subtitle("");

		if (menu.BoolOption("Spawn inside vehicle", settings.SpawnInside)) {
			settings.SaveSettings();
		}
		if (menu.BoolOption("Spawn inplace", settings.SpawnInplace, 
		{ "Don't spawn to the right of the previous car, but spawn at the current position. This replaces the current vehicle.",
		"Only active if \"Spawn inside vehicle\" is turned on."})) {
			settings.SaveSettings();
		}
		if (menu.BoolOption("Spawned cars are persistent", settings.Persistent)) {
			settings.SaveSettings();
		}
		if (menu.BoolOption("Spawn manually", settings.SpawnByName)) {
			settings.SaveSettings();
		}
		if (menu.BoolOption("Categorize by make", settings.CategorizeMake, 
		{ "Categorizing by " + std::string(settings.CategorizeMake ? "make" : "class") + "." })) {
			settings.SaveSettings();
		}
		if (menu.BoolOption("List all DLCs", settings.ListAllDLCs,
		{ "Show all official DLC vehicles."
			" These will appear in their own submenu, sorted per class, per DLC." })) {
			settings.SaveSettings();
		}
		if (menu.BoolOption("Merge DLCs", settings.MergeDLCs,
		{ "Don't sort per DLC and just show the vehicles per class." })) {
			settings.SaveSettings();
		}
		if (menu.BoolOption("Enable search menu", settings.SearchMenu)) {
			settings.SaveSettings();
		}
		if (menu.Option("Reload previews", { "Use for when you changed an image "
			"that's already been loaded."})) {
			resolveVehicleSpriteInfo();

			g_addonImageNames.clear();
			g_missingImages.clear();
			g_addonImages.clear();
			g_addonImageMetadata.clear();
			storeImageNames();
		}
		if (menu.Option("Clean up image preview folder", { "Remove images from the preview folder "
			"that aren't detected as add-ons." })) {
			g_addonImageNames.clear();
			g_missingImages.clear();
			g_addonImages.clear();
			g_addonImageMetadata.clear();

			cleanImageDirectory(true);

			storeImageNames();
		}

		if (settings.Persistent) {
			if (menu.Option("Clear persistence", {"Clears the persistence on spawned vehicles", 
				"Persistent vehicles: " + std::to_string(g_persistentVehicles.size())})) {
				clearPersistentVehicles();
			}
		}
	}

	for (auto category : addonCats) {
		if (menu.CurrentMenu(category)) {
			spawnMenu(category, g_addonVehicles, "Add-on vehicles", settings.CategorizeMake);
		}
	}	

	if (!settings.MergeDLCs) {
		if (menu.CurrentMenu("officialdlcmenu")) {
			menu.Title("Official DLC");
			menu.Subtitle("");

			for (auto dlc : g_dlcs) {
				if (menu.MenuOption(dlc.Name, dlc.Name)) {
					resolveVehicleSpriteInfo();
				}
			}
		}

		for (auto dlc : g_dlcs) {
			std::set<std::string> dlcCats = settings.CategorizeMake ? dlc.Makes : dlc.Classes;

			if (menu.CurrentMenu(dlc.Name)) {
				menu.Title(dlc.Name);
				menu.Subtitle("Sort by DLC");


				for (auto category : dlcCats) {
					if (menu.MenuOption(category, dlc.Name + " " + category)) {
						resolveVehicleSpriteInfo();
					}
				}
				if (dlcCats.empty()) {
					menu.Option("DLC unavailable.", { "This version of the game does not have the " + dlc.Name + " DLC content.",
						"Game version: " + eGameVersionToString(getGameVersion()) });
				}
			}
			for (auto className : dlcCats) {
				if (menu.CurrentMenu(dlc.Name + " " + className)) {
					menu.Title(className);
					spawnMenu(className, dlc.Vehicles, dlc.Name, settings.CategorizeMake);
				}
			}
		}
	}
	else {
		std::set<std::string> categories = settings.CategorizeMake ? g_dlcMakes : g_dlcClasses;

		if (menu.CurrentMenu("officialdlcmergedmenu")) {
			menu.Title("Official DLC");
			menu.Subtitle("Merged");
			for (auto category : categories) {
				menu.MenuOption(category, "dlc_" + category);
			}
		}
		for (auto category : categories) {
			if (menu.CurrentMenu("dlc_" + category)) {
				spawnMenu(category, g_dlcVehicles, "Original + All DLCs", settings.CategorizeMake);
			}
		}
	}


	menu.EndMenu();
}
