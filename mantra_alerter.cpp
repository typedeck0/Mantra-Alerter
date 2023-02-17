#include <stdint.h>
#include <stdio.h>
#include <unordered_map>
#include <Windows.h>
#include <mutex>
#include <vector>
#include <algorithm>
#include "imgui/imgui.h"
#include <string>
#include <sstream>
#include <cstdio>
#include <time.h>
#include <fstream>
#include <map>

std::mutex mtx;

/* combat state change */
enum cbtstatechange {
	CBTS_NONE, // not used - not this kind of event
	CBTS_ENTERCOMBAT, // src_agent entered combat, dst_agent is subgroup
	CBTS_EXITCOMBAT, // src_agent left combat
	CBTS_CHANGEUP, // src_agent is now alive
	CBTS_CHANGEDEAD, // src_agent is now dead
	CBTS_CHANGEDOWN, // src_agent is now downed
	CBTS_SPAWN, // src_agent is now in game tracking range (not in realtime api)
	CBTS_DESPAWN, // src_agent is no longer being tracked (not in realtime api)
	CBTS_HEALTHUPDATE, // src_agent is at health percent. dst_agent = percent * 10000 (eg. 99.5% will be 9950) (not in realtime api)
	CBTS_LOGSTART, // log start. value = server unix timestamp **uint32**. buff_dmg = local unix timestamp. src_agent = 0x637261 (arcdps id) if evtc, species id if realtime
	CBTS_LOGEND, // log end. value = server unix timestamp **uint32**. buff_dmg = local unix timestamp. src_agent = 0x637261 (arcdps id) if evtc, species id if realtime
	CBTS_WEAPSWAP, // src_agent swapped weapon set. dst_agent = current set id (0/1 water, 4/5 land)
	CBTS_MAXHEALTHUPDATE, // src_agent has had it's maximum health changed. dst_agent = new max health (not in realtime api)
	CBTS_POINTOFVIEW, // src_agent is agent of "recording" player  (not in realtime api)
	CBTS_LANGUAGE, // src_agent is text language  (not in realtime api)
	CBTS_GWBUILD, // src_agent is game build  (not in realtime api)
	CBTS_SHARDID, // src_agent is sever shard id  (not in realtime api)
	CBTS_REWARD, // src_agent is self, dst_agent is reward id, value is reward type. these are the wiggly boxes that you get
	CBTS_BUFFINITIAL, // combat event that will appear once per buff per agent on logging start (statechange==18, buff==18, normal cbtevent otherwise)
	CBTS_POSITION, // src_agent changed, cast float* p = (float*)&dst_agent, access as x/y/z (float[3]) (not in realtime api)
	CBTS_VELOCITY, // src_agent changed, cast float* v = (float*)&dst_agent, access as x/y/z (float[3]) (not in realtime api)
	CBTS_FACING, // src_agent changed, cast float* f = (float*)&dst_agent, access as x/y (float[2]) (not in realtime api)
	CBTS_TEAMCHANGE, // src_agent change, dst_agent new team id
	CBTS_ATTACKTARGET, // src_agent is an attacktarget, dst_agent is the parent agent (gadget type), value is the current targetable state (not in realtime api)
	CBTS_TARGETABLE, // dst_agent is new target-able state (0 = no, 1 = yes. default yes) (not in realtime api)
	CBTS_MAPID, // src_agent is map id  (not in realtime api)
	CBTS_REPLINFO, // internal use, won't see anywhere
	CBTS_STACKACTIVE, // src_agent is agent with buff, dst_agent is the stackid marked active
	CBTS_STACKRESET, // src_agent is agent with buff, value is the duration to reset to (also marks inactive), pad61-pad64 buff instance id
	CBTS_GUILD, // src_agent is agent, dst_agent through buff_dmg is 16 byte guid (client form, needs minor rearrange for api form)
	CBTS_BUFFINFO, // is_flanking = probably invuln, is_shields = probably invert, is_offcycle = category, pad61 = stacking type, pad62 = probably resistance, src_master_instid = max stacks, overstack_value = duration cap (not in realtime)
	CBTS_BUFFFORMULA, // (float*)&time[8]: type attr1 attr2 param1 param2 param3 trait_src trait_self, (float*)&src_instid[2] = buff_src buff_self, is_flanking = !npc, is_shields = !player, is_offcycle = break, overstack = value of type determined by pad61 (none/number/skill) (not in realtime, one per formula)
	CBTS_SKILLINFO, // (float*)&time[4]: recharge range0 range1 tooltiptime (not in realtime)
	CBTS_SKILLTIMING, // src_agent = action, dst_agent = at millisecond (not in realtime, one per timing)
	CBTS_BREAKBARSTATE, // src_agent is agent, value is u16 game enum (active, recover, immune, none) (not in realtime api)
	CBTS_BREAKBARPERCENT, // src_agent is agent, value is float with percent (not in realtime api)
	CBTS_ERROR, // (char*)&time[32]: error string (not in realtime api)
	CBTS_TAG, // src_agent is agent, value is the id (volatile, game build dependent) of the tag, buff will be non-zero if commander
	CBTS_BARRIERUPDATE,  // src_agent is at barrier percent. dst_agent = percent * 10000 (eg. 99.5% will be 9950) (not in realtime api)
	CBTS_STATRESET,  // with arc ui stats reset (not in log), src_agent = npc id of active log
	CBTS_EXTENSION, // cbtevent with statechange byte set to this
	CBTS_APIDELAYED, // cbtevent with statechange byte set to this
	CBTS_INSTANCESTART, // src_agent is ms time at which the instance likely was started
	CBTS_TICKRATE, // every 500ms, src_agent = 25 - tickrate (when tickrate < 21)
	CBTS_LAST90BEFOREDOWN, // src_agent is enemy agent that went down, dst_agent is time in ms since last 90% (for downs contribution)
	CBTS_EFFECT, // src_agent is owner. dst_agent if at agent, else &value = float[3] xyz, &iff = float[2] xy orient, &pad61 = float[1] z orient, skillid = effectid. if is_flanking: duration = trackingid. &is_shields = uint16 duration. if effectid = 0, end &is_shields = trackingid (not in realtime api)
	CBTS_IDTOGUID, // &src_agent = 16byte persistent content guid, overstack_value is of contentlocal enum, skillid is content id  (not in realtime api)
	CBTS_UNKNOWN
};

/* is friend/foe */
enum iff {
	IFF_FRIEND,
	IFF_FOE,
	IFF_UNKNOWN
};

/* arcdps export table */
typedef struct arcdps_exports {
	uintptr_t size; /* size of exports table */
	uint32_t sig; /* pick a number between 0 and uint32_t max that isn't used by other modules */
	uint32_t imguivers; /* set this to IMGUI_VERSION_NUM. if you don't use imgui, 18000 (as of 2021-02-02) */
	const char* out_name; /* name string */
	const char* out_build; /* build string */
	void* wnd_nofilter; /* wndproc callback, fn(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam), return assigned to umsg */
	void* combat; /* combat event callback, fn(cbtevent* ev, ag* src, ag* dst, char* skillname, uint64_t id, uint64_t revision) */
	void* imgui; /* ::present callback, before imgui::render, fn(uint32_t not_charsel_or_loading, uint32_t hide_if_combat_or_ooc) */
	void* options_end; /* ::present callback, appending to the end of options window in arcdps, fn() */
	void* combat_local;  /* combat event callback like area but from chat log, fn(cbtevent* ev, ag* src, ag* dst, char* skillname, uint64_t id, uint64_t revision) */
	void* wnd_filter; /* wndproc callback like wnd_nofilter above, input filered using modifiers */
	void* options_windows; /* called once per 'window' option checkbox, with null at the end, non-zero return disables arcdps drawing that checkbox, fn(char* windowname) */
} arcdps_exports;

/* combat event - see evtc docs for details, revision param in combat cb is equivalent of revision byte header */
typedef struct cbtevent {
	uint64_t time;
	uint64_t src_agent;
	uint64_t dst_agent;
	int32_t value;
	int32_t buff_dmg;
	uint32_t overstack_value;
	uint32_t skillid;
	uint16_t src_instid;
	uint16_t dst_instid;
	uint16_t src_master_instid;
	uint16_t dst_master_instid;
	uint8_t iff;
	uint8_t buff;
	uint8_t result;
	uint8_t is_activation;
	uint8_t is_buffremove;
	uint8_t is_ninety;
	uint8_t is_fifty;
	uint8_t is_moving;
	uint8_t is_statechange;
	uint8_t is_flanking;
	uint8_t is_shields;
	uint8_t is_offcycle;
	uint8_t pad61;
	uint8_t pad62;
	uint8_t pad63;
	uint8_t pad64;
} cbtevent;

/* agent short */
typedef struct ag {
	char* name; /* agent name. may be null. valid only at time of event. utf8 */
	uintptr_t id; /* agent unique identifier */
	uint32_t prof; /* profession at time of event. refer to evtc notes for identification */
	uint32_t elite; /* elite spec at time of event. refer to evtc notes for identification */
	uint32_t self; /* 1 if self, 0 if not */
	uint16_t team; /* sep21+ */
} ag;

/* proto/globals */
uint32_t cbtcount = 0;
bool enabled = true;
bool mod_key1 = false;
bool mod_key2 = false;
bool self_only = false;
bool squad_flag = true;
bool lock_pos = false;
ImGuiWindowFlags wFlags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoTitleBar;

arcdps_exports arc_exports;
char* arcvers;
void dll_init(HANDLE hModule);
void dll_exit();
extern "C" __declspec(dllexport) void* get_init_addr(char* arcversion, ImGuiContext* imguictx, void* id3dptr, HANDLE arcdll, void* mallocfn, void* freefn, uint32_t d3dversion);
extern "C" __declspec(dllexport) void* get_release_addr();
arcdps_exports* mod_init();
uintptr_t mod_release();
uintptr_t mod_wnd(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
uintptr_t mod_combat(cbtevent* ev, ag* src, ag* dst, char* skillname, uint64_t id, uint64_t revision);
void log_arc(char* str);
void save_ma_settings();

/* arcdps exports */
void* filelog;
void* arclog;
void* arccolors;
wchar_t*(*get_settings_path)();
uint64_t(*get_ui_settings)();
uint64_t(*get_key_settings)();

/* dll main -- winapi */
BOOL APIENTRY DllMain(HANDLE hModule, DWORD ulReasonForCall, LPVOID lpReserved) {
	switch (ulReasonForCall) {
	case DLL_PROCESS_ATTACH: dll_init(hModule); break;
	case DLL_PROCESS_DETACH: dll_exit(); break;

	case DLL_THREAD_ATTACH:  break;
	case DLL_THREAD_DETACH:  break;
	}
	return 1;
}

/* log to extensions tab in arcdps log window, thread/async safe */
void log_arc(char* str) {
	size_t(*log)(char*) = (size_t(*)(char*))arclog;
	if (log) (*log)(str);
	return;
}

/* dll attach -- from winapi */
void dll_init(HANDLE hModule) {
	return;
}

/* dll detach -- from winapi */
void dll_exit() {
	return;
}

/* export -- arcdps looks for this exported function and calls the address it returns on client load */
extern "C" __declspec(dllexport) void* get_init_addr(char* arcversion, ImGuiContext* imguictx, void* id3dptr, HANDLE arcdll, void* mallocfn, void* freefn, uint32_t d3dversion) {
	// id3dptr is IDirect3D9* if d3dversion==9, or IDXGISwapChain* if d3dversion==11
	arcvers = arcversion;
	get_settings_path = (wchar_t*(*)())GetProcAddress((HMODULE)arcdll, "e0");
	arclog = (void*)GetProcAddress((HMODULE)arcdll, "e8");
	get_ui_settings = (uint64_t(*)())GetProcAddress((HMODULE)arcdll, "e6");
	get_key_settings = (uint64_t(*)())GetProcAddress((HMODULE)arcdll, "e7");
	ImGui::SetCurrentContext(imguictx);
	ImGui::SetAllocatorFunctions((void *(*)(size_t, void*))mallocfn, (void (*)(void*, void*))freefn); // on imgui 1.80+
	return mod_init;
}

/* export -- arcdps looks for this exported function and calls the address it returns on client exit */
extern "C" __declspec(dllexport) void* get_release_addr() {
	arcvers = 0;
	return mod_release;
}

/* release mod -- return ignored */
uintptr_t mod_release() {
	FreeConsole();
	save_ma_settings();
	return 0;
}

/* window callback -- return is assigned to umsg (return zero to not be processed by arcdps or game) */
uintptr_t mod_wnd(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) 
{
	if ((get_ui_settings() >> 2) & 1)
	{
		wFlags |= ImGuiWindowFlags_NoMove;
		if (uMsg == WM_KEYDOWN || uMsg == WM_SYSKEYDOWN) 
		{
			uint64_t keys = get_key_settings();
			uint16_t* mod_key = (uint16_t*)&keys;
			if (wParam == *mod_key)
			{
				mod_key1 = true;
			}
			if (wParam == *(mod_key+1))
			{
				mod_key2 = true;
			}
		}

		if (uMsg == WM_KEYUP || uMsg == WM_SYSKEYUP) 
		{
			uint64_t keys = get_key_settings();
			uint16_t* mod_key = (uint16_t*)&keys;
			if (wParam == *mod_key)
			{
				mod_key1 = false;
			}
			if (wParam == *(mod_key+1))
			{
				mod_key2 = false;
			}
		}
		if (mod_key1 && mod_key2)
			wFlags &= ~ImGuiWindowFlags_NoMove;
	}
	return uMsg;
}

bool firebrand_died = false;
bool arc_end = false;
bool ooc = true;
bool changed_maps = false;

uintptr_t my_id = 0;

/* combat callback -- may be called asynchronously, use id param to keep track of order, first event id will be 2. return ignored */
/* at least one participant will be party/squad or minion of, or a buff applied by squad in the case of buff remove. not all statechanges present, see evtc statechange enum */
uintptr_t mod_combat(cbtevent* ev, ag* src, ag* dst, char* skillname, uint64_t id, uint64_t revision) 
{
	if (!ev)
	{
		if (!src->elite) 
		{
			if (src->prof) 
			{
				if (dst->self)
				{
					my_id = src->id;
				}
			}
			else
			{
				if (src->id == my_id)
				{
					changed_maps = true;
					my_id = 0;
				}
			}
		}
	}
	else if(ev && enabled)
	{
		switch (ev->is_statechange)
		{
		case 0:
			break;
		case CBTS_CHANGEDEAD:
			if (squad_flag && src->elite == 62)
				firebrand_died = true;
			else if (self_only && src->elite == 62 && src->self)
				firebrand_died = true;
			break;
		case CBTS_LOGEND:
			arc_end = true;
			break;
		case CBTS_EXITCOMBAT:
			if (src->self)
				ooc = true;
			break;
		case CBTS_ENTERCOMBAT:
			if (src->self)
				ooc = false;
			break;
		default:
			break;
		}
	}

	return 0;
}

void options_end_proc()
{
	ImGui::Checkbox("Mantra Alerter##2", &enabled);
	ImGui::Separator();
	if (ImGui::Checkbox("Alert on squad+self", &squad_flag))
		self_only = false;
	if (ImGui::Checkbox("Alert on self", &self_only))
		squad_flag = false;
	if (ImGui::Checkbox("Lock position", &lock_pos))
	{
		if (lock_pos)
			wFlags |= ImGuiWindowFlags_NoMove;
		else
			wFlags &= ~ImGuiWindowFlags_NoMove;
	}
}

ImFont* AddDefaultFont( float scale )
{
	ImGuiIO &io = ImGui::GetIO();
	ImFontConfig config;
	config.SizePixels = 13 * scale;
	config.OversampleH = config.OversampleV = 1;
	config.PixelSnapH = true;
	ImFont *font = io.Fonts->AddFontDefault(&config);
	return font;
}

int flash_iter = 0;
void DoFitTextToWindow(ImFont *font, const char *text)
{
	uint16_t color = std::sinf(flash_iter*(3.14159/60.0))*255;
	ImGui::PushFont( font );
	ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255,color,color,255));
	ImGui::Text("%s", text);
	ImGui::PopStyleColor();
	ImGui::PopFont();
	flash_iter = (flash_iter + 1) % 60;
}

ImFont* big_font = nullptr;

uintptr_t imgui_proc(uint32_t not_charsel_or_loading, uint32_t hide_if_combat_or_ooc)
{
	if (not_charsel_or_loading && enabled)
	{
		if ((firebrand_died && arc_end && ooc) || changed_maps)
		{
			ImGui::Begin("Mantra Alerter##1", &enabled, wFlags);
			DoFitTextToWindow(big_font, "Stop! Mantra Time!");
			if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
			{
				firebrand_died = false;
				arc_end = false;
				ooc = true;
				changed_maps = false;
			}
			ImGui::SetWindowSize(ImVec2(0,0), 0);
			ImGui::End();
			
		}
	}
	return 0;
}

void save_ma_settings()
{
	std::wstring path = std::wstring(get_settings_path());
	std::string cpath(path.begin(), path.end());
	cpath = cpath.substr(0, cpath.find_last_of("\\")+1);
	cpath.append("mantra_alerter_settings.txt");
	std::fstream file(cpath.c_str(), std::fstream::in | std::fstream::out | std::fstream::trunc);
	if (file.good())
	{
		file << "enabled=" << (enabled ? '1' : '0') << "\n";
		file << "sqsf=" << (squad_flag ? '1' : '0') << "\n";
		file << "sf=" << (self_only ? '1' : '0') << "\n";
		file << "lock=" << (lock_pos ? '1' : '0') << "\n";
	}
	file.close();
}


void init_ma_settings()
{
	std::wstring path = std::wstring(get_settings_path());
	std::string cpath(path.begin(), path.end());
	cpath = cpath.substr(0, cpath.find_last_of("\\")+1);
	cpath.append("mantra_alerter_settings.txt");
	std::fstream file(cpath.c_str(), std::fstream::in | std::fstream::out | std::fstream::app);
	std::string line;
	bool success = false;
	if (file.good())
	{
		while (std::getline(file, line))
		{
			std::size_t sep = line.find_first_of('=');
			std::string key = line.substr(0, sep);
			char val = line.substr(sep+1, line.size() - key.size() - 1)[0];
			if (key.compare("enabled") == 0)
				enabled = val == '1';
			else if (key.compare("sqsf") == 0)
				squad_flag = val == '1';
			else if (key.compare("sf") == 0)
				self_only = val == '1';
			else if (key.compare("lock") == 0)
				lock_pos = val == '1';
		}
		if (lock_pos)
			wFlags |= ImGuiWindowFlags_NoMove;
	}
	if (!success)
	{
		file.open(cpath.c_str(), std::fstream::in | std::fstream::out | std::fstream::trunc);
		file << "enabled=1\n";
	}
	file.close();
}

/* initialize mod -- return table that arcdps will use for callbacks. exports struct and strings are copied to arcdps memory only once at init */
arcdps_exports* mod_init() {
	/* for arcdps */
	memset(&arc_exports, 0, sizeof(arcdps_exports));
	arc_exports.sig = 0xC1FFEE;
	arc_exports.imguivers = IMGUI_VERSION_NUM;
	arc_exports.size = sizeof(arcdps_exports);
	arc_exports.out_name = "Mantra Alerter";
	arc_exports.out_build = "1.0";
	arc_exports.imgui = imgui_proc;
	arc_exports.wnd_nofilter = mod_wnd;
	arc_exports.combat = mod_combat;
	arc_exports.options_end = options_end_proc;
	//arc_exports.size = (uintptr_t)"error message if you decide to not load, sig must be 0";
	init_ma_settings();

	if (big_font == nullptr)
		big_font = AddDefaultFont(5);

	log_arc((char*)"mantra_alerter mod_init"); // if using vs2015+, project properties > c++ > conformance mode > permissive to avoid const to not const conversion error
	return &arc_exports;
}