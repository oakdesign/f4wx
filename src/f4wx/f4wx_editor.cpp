// Copyright 2026 syn111. Licensed under the Apache License, Version 2.0.
#include <Windows.h>
#include <Windowsx.h>
#include <CommCtrl.h>
#include <commdlg.h>
#include "gdiplus_support.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <optional>
#include "f4wx_editor.h"
#include "f4wx_preview.h"
#include "weather_document.h"
#include "resource.h"
#include "utils.h"

namespace {
enum { E_NEW = 20000, E_OPEN, E_CURRENT, E_SAVE, E_FIELD, E_PRESET, E_VALUE, E_RADIUS, E_STRENGTH,
	E_SOFT, E_LEVEL, E_TOOL, E_FILL, E_UNDO, E_REDO, E_GRID, E_GLOBAL, E_GLOBAL_VALUE, E_APPLY_GLOBAL,
	E_AVERAGE, E_STATUS, E_CANVAS, E_RANGE, E_VIEW };
const wchar_t* fields[] = {L"Weather preset", L"Temperature (C)", L"Pressure (hPa)", L"Visibility (km)",
	L"Cumulus base (ft)", L"Cumulus density (0-13)", L"Cumulus size (0=tall, 5=thin)",
	L"Towering cumulus (0/1)", L"Shower cumulus (0/1)", L"Fog layer altitude (ft)", L"Wind speed (kt)", L"Wind direction (deg)"};
const wchar_t* limits[] = {L"Preset replaces clouds and visibility.", L"Range: -100 to 100 C", L"Range: 100 to 1200 hPa", L"Range: 0 to 60 km",
	L"Range: 0 to 100000 ft", L"Whole number: 0 to 13", L"Range: 0 to 5 (0 is tallest)", L"0 = off, 1 = on", L"0 = off, 1 = on", L"Range: 0 to 100000 ft", L"Range: 0 to 500 kt", L"Range: 0 to 360 degrees"};
const float defaults[] = {0, 15, 1013.25f, 60, 8000, 5, 4, 0, 0, 8000, 10, 270};

class editor {
public:
	HWND hwnd = nullptr, canvas = nullptr;
	unsigned rows, columns;
	std::wstring theater;
	Gdiplus::Bitmap* background;
	const fmap* snapshot;
	editor_start start;
	std::unique_ptr<weather_document> document;
	f4wx_preview preview;
	std::unique_ptr<Gdiplus::Bitmap> fallback;
	HBITMAP bitmap = nullptr;
	HDC frame_dc = nullptr;
	HBITMAP frame_bitmap = nullptr;
	HGDIOBJ frame_original = nullptr;
	SIZE frame_size{};
	float zoom = 1, pan_x = 0, pan_y = 0;
	bool drawing = false, panning = false;
	POINT last_pan{}, cursor{};
	std::pair<float, float> rect_start{};
	weather_brush active_brush;
	int active_tool = 0;
	std::optional<cell_index> selected;
	float scale = 1;
	~editor() {
		if (frame_dc) {
			if (frame_original) SelectObject(frame_dc, frame_original);
			if (frame_bitmap) DeleteObject(frame_bitmap);
			DeleteDC(frame_dc);
		}
		if (bitmap) DeleteObject(bitmap);
	}

	HWND control(int id) { return GetDlgItem(hwnd, id); }
	int selection(int id) { return static_cast<int>(SendMessageW(control(id), CB_GETCURSEL, 0, 0)); }
	void number(int id, float value) { SetWindowTextW(control(id), std::format(L"{:g}", value).c_str()); }
	std::optional<float> read(int id) {
		wchar_t text[64]{}; GetWindowTextW(control(id), text, 64);
		wchar_t* end = nullptr; float v = std::wcstof(text, &end);
		while (end && *end == L' ') ++end;
		if (end == text || !end || *end || !std::isfinite(v)) return {};
		return v;
	}
	void error(const std::string& message) { MessageBoxW(hwnd, to_wide(message).c_str(), L"Weather Editor", MB_OK | MB_ICONERROR); }
	HWND add(const wchar_t* cls, const wchar_t* text, int id, int x, int y, int w, int h, DWORD style = 0) {
		HWND c = CreateWindowExW(cls == std::wstring_view(L"EDIT") ? WS_EX_CLIENTEDGE : 0, cls, text,
			WS_CHILD | WS_VISIBLE | style, static_cast<int>(x * scale), static_cast<int>(y * scale),
			static_cast<int>(w * scale), static_cast<int>(h * scale), hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
		SendMessageW(c, WM_SETFONT, SendMessageW(hwnd, WM_GETFONT, 0, 0), TRUE); return c;
	}
	void label(const wchar_t* text, int y) { add(L"STATIC", text, -1, 12, y, 240, 18); }
	void combo(int id, int y, const wchar_t* const* entries, size_t count) {
		HWND c = add(L"COMBOBOX", L"", id, 12, y, 240, 250, CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP);
		for (size_t i = 0; i < count; ++i) SendMessageW(c, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(entries[i]));
		SendMessageW(c, CB_SETCURSEL, 0, 0);
	}
	void initialize() {
		HDC screen = GetDC(hwnd);
		scale = GetDeviceCaps(screen, LOGPIXELSX) / 96.f;
		ReleaseDC(hwnd, screen);
		add(L"BUTTON", L"New", E_NEW, 12, 12, 74, 26, WS_TABSTOP);
		add(L"BUTTON", L"Open fmap", E_OPEN, 92, 12, 80, 26, WS_TABSTOP);
		add(L"BUTTON", L"Save As", E_SAVE, 178, 12, 74, 26, WS_TABSTOP);
		add(L"BUTTON", L"Copy current real weather", E_CURRENT, 12, 44, 240, 26, WS_TABSTOP);
		EnableWindow(control(E_CURRENT), snapshot != nullptr);
		label(L"Paint field", 78); combo(E_FIELD, 96, fields, std::size(fields));
		const wchar_t* presets[] = {L"Sunny", L"Fair", L"Poor", L"Inclement"}; combo(E_PRESET, 127, presets, 4);
		add(L"EDIT", L"0", E_VALUE, 12, 127, 240, 24, WS_TABSTOP | ES_AUTOHSCROLL);
		add(L"STATIC", limits[0], E_RANGE, 12, 155, 240, 30);
		add(L"STATIC", L"Radius (cells)", -1, 12, 186, 80, 18);
		add(L"EDIT", L"2", E_RADIUS, 12, 204, 70, 24, WS_TABSTOP | ES_AUTOHSCROLL);
		add(L"STATIC", L"Strength (%)", -1, 94, 186, 140, 18);
		add(L"EDIT", L"100", E_STRENGTH, 94, 204, 70, 24, WS_TABSTOP | ES_AUTOHSCROLL);
		add(L"BUTTON", L"Soft edge (numeric fields)", E_SOFT, 12, 232, 240, 22, BS_AUTOCHECKBOX | WS_TABSTOP);
		label(L"Wind altitude", 260);
		std::wstring levels[NUM_ALOFT_BREAKPOINTS]; const wchar_t* level_text[NUM_ALOFT_BREAKPOINTS];
		for (size_t k = 0; k < NUM_ALOFT_BREAKPOINTS; ++k) { levels[k] = std::format(L"{:.0f} ft", fmap_aloft_breakpoints[k]); level_text[k] = levels[k].c_str(); }
		combo(E_LEVEL, 278, level_text, NUM_ALOFT_BREAKPOINTS);
		const wchar_t* tools[] = {L"Brush", L"Rectangle", L"Eyedropper"}; combo(E_TOOL, 310, tools, 3);
		add(L"BUTTON", L"Fill whole map", E_FILL, 12, 342, 240, 26, WS_TABSTOP);
		add(L"BUTTON", L"Undo", E_UNDO, 12, 374, 115, 26, WS_TABSTOP);
		add(L"BUTTON", L"Redo", E_REDO, 137, 374, 115, 26, WS_TABSTOP);
		add(L"BUTTON", L"Show grid", E_GRID, 12, 406, 240, 22, BS_AUTOCHECKBOX | WS_TABSTOP);
		label(L"Map-wide settings (not painted)", 434);
		const wchar_t* globals[] = {L"Map wind heading (deg)", L"Map wind speed (kt)", L"Fair stratus height (ft)", L"Inclement stratus height (ft)",
			L"Sunny contrail height (ft)", L"Fair contrail height (ft)", L"Poor contrail height (ft)", L"Inclement contrail height (ft)"};
		combo(E_GLOBAL, 452, globals, 8);
		add(L"EDIT", L"0", E_GLOBAL_VALUE, 12, 484, 145, 24, WS_TABSTOP | ES_AUTOHSCROLL);
		add(L"BUTTON", L"Apply", E_APPLY_GLOBAL, 167, 484, 85, 24, WS_TABSTOP);
		add(L"BUTTON", L"Derive map wind from 12000 ft", E_AVERAGE, 12, 514, 240, 26, WS_TABSTOP);
		label(L"View", 548);
		const wchar_t* views[] = {L"Weather", L"Temperature", L"Pressure", L"Wind", L"Visibility", L"Cloud base", L"Cloud density", L"Cloud size", L"Fog altitude"}; combo(E_VIEW, 566, views, 9);
		add(L"STATIC", L"Wheel: zoom. Middle drag: pan.\r\nRight click: inspect / pick value.\r\nRadius 0: edit one cell.\r\nPresets preserve pressure, temperature\r\nand all winds. Map wind is manual.", -1, 12, 602, 240, 110);
		canvas = add(L"STATIC", L"", E_CANVAS, 270, 12, 650, 620, SS_NOTIFY | WS_BORDER);
		SetWindowSubclass(canvas, canvas_proc, 1, reinterpret_cast<DWORD_PTR>(this));
		add(L"STATIC", L"", E_STATUS, 270, 640, 650, 70);
		if (!background) { fallback = std::make_unique<Gdiplus::Bitmap>(800, 800); Gdiplus::Graphics g(fallback.get()); g.Clear(Gdiplus::Color(225, 225, 225)); background = fallback.get(); }
		preview.set_background(background);
		new_document(start == editor_start::current_map && snapshot ? snapshot->clone() : make_map());
		field_changed(); layout();
		if (start == editor_start::open_map) PostMessageW(hwnd, WM_COMMAND, E_OPEN, 0);
	}
	std::unique_ptr<fmap> make_map() {
		auto map = std::make_unique<fmap>(rows, columns);
		auto type = static_cast<fmap_wxtype>(std::max(0, selection(E_PRESET)));
		for (unsigned y = 0; y < rows; ++y) for (unsigned x = 0; x < columns; ++x) map->set_cell({y, x}, weather_document::preset(type));
		return map;
	}
	void new_document(std::unique_ptr<fmap> map) {
		document = std::make_unique<weather_document>(std::move(map));
		zoom = 1; pan_x = pan_y = 0; selected.reset(); update_global(); refresh();
	}
	void layout() {
		RECT r; GetClientRect(hwnd, &r);
		int left = static_cast<int>(270 * scale), margin = static_cast<int>(12 * scale), status = static_cast<int>(76 * scale);
		MoveWindow(canvas, left, margin, std::max(1, static_cast<int>(r.right) - left - margin), std::max(1, static_cast<int>(r.bottom) - status - margin * 2), TRUE);
		MoveWindow(control(E_STATUS), left, r.bottom - status, std::max(1, static_cast<int>(r.right) - left - margin), status - margin, TRUE);
	}
	void title() {
		SetWindowTextW(hwnd, (L"Weather Editor - " + theater + (document->dirty() ? L" *" : L"")).c_str());
		EnableWindow(control(E_UNDO), document->can_undo()); EnableWindow(control(E_REDO), document->can_redo());
	}
	void refresh() {
		preview.cleanup(); const auto& map = document->map(); int view = selection(E_VIEW);
		if (view == 0) preview.draw_clouds(map);
		else if (view == 1) preview.draw_temperature(map);
		else if (view == 2) preview.draw_pressure(map);
		else if (view == 3) preview.draw_wind(map, std::max(0, selection(E_LEVEL)));
		if (bitmap) { DeleteObject(bitmap); bitmap = nullptr; }
		preview.get_hbitmap(Gdiplus::Color::Gray, &bitmap);
		if (bitmap && view >= 4) {
			HDC dc = CreateCompatibleDC(nullptr); auto old = SelectObject(dc, bitmap);
			{
				Gdiplus::Graphics g(dc);
				weather_field f = view == 4 ? weather_field::visibility : view == 5 ? weather_field::cloud_base : view == 6 ? weather_field::cloud_density : view == 7 ? weather_field::cloud_size : weather_field::fog_height;
				float max = view == 4 ? 60.f : view == 6 ? 13.f : view == 7 ? 5.f : 30000.f;
				for (unsigned y = 0; y < map.get_sizeY(); ++y) for (unsigned x = 0; x < map.get_sizeX(); ++x) {
					float t = std::clamp(weather_document::value(map.get_cell({y,x}), f, 0) / max, 0.f, 1.f);
					Gdiplus::SolidBrush b(Gdiplus::Color(155, static_cast<BYTE>(255 * t), static_cast<BYTE>(180 * (1 - std::abs(2*t-1))), static_cast<BYTE>(255 * (1-t))));
					int px = x * preview.get_width() / map.get_sizeX(), py = y * preview.get_height() / map.get_sizeY();
					g.FillRectangle(&b, px, py, static_cast<int>((x+1)*preview.get_width()/map.get_sizeX())-px, static_cast<int>((y+1)*preview.get_height()/map.get_sizeY())-py);
				}
			}
			SelectObject(dc, old); DeleteDC(dc);
		}
		title(); inspect(); InvalidateRect(canvas, nullptr, FALSE);
	}
	struct transform { float x, y, width, height; };
	transform viewport() {
		RECT r; GetClientRect(canvas, &r);
		float aspect = static_cast<float>(document->map().get_sizeX()) / document->map().get_sizeY();
		float w = std::min(static_cast<float>(r.right), r.bottom * aspect), h = w / aspect;
		return {(r.right-w*zoom)/2 + pan_x, (r.bottom-h*zoom)/2 + pan_y, w*zoom, h*zoom};
	}
	std::pair<float,float> grid(POINT p) {
		auto v = viewport(); return {(p.x-v.x)*document->map().get_sizeX()/v.width, (p.y-v.y)*document->map().get_sizeY()/v.height};
	}
	bool inside(float x, float y) { return x >= 0 && y >= 0 && x < document->map().get_sizeX() && y < document->map().get_sizeY(); }
	void inspect() {
		if (!selected) { SetWindowTextW(control(E_STATUS), L"Select a cell with right click to inspect its weather. Numeric views: blue = low, red = high."); return; }
		auto c = document->map().get_cell(*selected); unsigned k = static_cast<unsigned>(std::max(0, selection(E_LEVEL)));
		auto text = std::format(L"Cell ({}, {})  |  {}  |  {:.1f} C  |  {:.1f} hPa  |  Visibility {:.1f} km\r\nCloud base {:.0f} ft, density {}, size {:.1f}, TCU {}, shower {}  |  Fog {:.0f} ft\r\nWind at {:.0f} ft: {:.0f} deg / {:.1f} kt", selected->y, selected->x, to_wide(fmap_wxtype_text[c.basicCondition]), c.temperature, c.pressure, c.fogEndBelowLayerMapData, c.cumulusBase, c.cumulusDensity, c.cumulusSize, c.hasTowerCumulus, c.hasShowerCumulus, c.fogLayerZ, fmap_aloft_breakpoints[k], c.windDir[k], c.windSpeed[k]);
		SetWindowTextW(control(E_STATUS), text.c_str());
	}
	void pick(float x, float y) {
		if (!inside(x,y)) return;
		selected = cell_index{static_cast<unsigned>(y), static_cast<unsigned>(x)};
		float v = weather_document::value(document->map().get_cell(*selected), static_cast<weather_field>(selection(E_FIELD)), std::max(0, selection(E_LEVEL)));
		if (selection(E_FIELD) == 0) SendMessageW(control(E_PRESET), CB_SETCURSEL, static_cast<int>(v), 0); else number(E_VALUE, v);
		inspect(); InvalidateRect(canvas, nullptr, FALSE);
	}
	void field_changed() {
		int field = selection(E_FIELD); bool preset = field == 0;
		ShowWindow(control(E_PRESET), preset ? SW_SHOW : SW_HIDE); ShowWindow(control(E_VALUE), preset ? SW_HIDE : SW_SHOW);
		number(E_VALUE, defaults[field]); SetWindowTextW(control(E_RANGE), limits[field]);
		EnableWindow(control(E_LEVEL), field >= 10); EnableWindow(control(E_SOFT), field != 0 && field != 5 && field != 7 && field != 8);
		int view = field == 1 ? 1 : field == 2 ? 2 : field >= 10 ? 3 : field == 3 ? 4 : field == 4 ? 5 : field == 5 ? 6 : field == 6 ? 7 : field == 9 ? 8 : 0;
		SendMessageW(control(E_VIEW), CB_SETCURSEL, view, 0); refresh();
	}
	std::optional<weather_brush> brush() {
		auto radius = read(E_RADIUS), strength = read(E_STRENGTH), val = read(E_VALUE);
		weather_brush b; b.field = static_cast<weather_field>(selection(E_FIELD));
		b.value = b.field == weather_field::preset ? static_cast<float>(selection(E_PRESET)) : val.value_or(NAN);
		b.radius = radius.value_or(NAN); b.strength = strength.value_or(NAN) / 100.f;
		b.soft = IsDlgButtonChecked(hwnd, E_SOFT) == BST_CHECKED && IsWindowEnabled(control(E_SOFT));
		b.wind_level = std::max(0, selection(E_LEVEL));
		if (!weather_document::valid_brush(b)) { error("Enter a value within the field's range, a radius from 0 to 512, and strength greater than 0 and at most 100 percent."); return {}; }
		return b;
	}
	void update_global() {
		auto g = document->globals(); int i = selection(E_GLOBAL);
		number(E_GLOBAL_VALUE, i == 0 ? static_cast<float>(g.heading) : i == 1 ? g.speed : i == 2 ? static_cast<float>(g.stratus_fair) : i == 3 ? static_cast<float>(g.stratus_inc) : static_cast<float>(g.contrails[i-4]));
	}
	void apply_global() {
		auto value = read(E_GLOBAL_VALUE); int i = selection(E_GLOBAL);
		float max = i == 0 ? 360.f : i == 1 ? 500.f : 100000.f;
		if (!value || *value < 0 || *value > max || (i != 1 && *value != std::floor(*value))) { error("Invalid map setting: heading 0-360, speed 0-500 kt, heights 0-100000 whole feet."); return; }
		auto g = document->globals(); int n = static_cast<int>(*value);
		if (i == 0) g.heading = n; else if (i == 1) g.speed = *value; else if (i == 2) g.stratus_fair = n; else if (i == 3) g.stratus_inc = n; else g.contrails[i-4] = n;
		document->set_globals(g); refresh();
	}
	void derive_wind() {
		double u = 0, v = 0; constexpr double rad = 0.017453292519943295;
		for (unsigned y = 0; y < rows; ++y) for (unsigned x = 0; x < columns; ++x) {
			float speed = document->map().get_windSpeed({y,x}, 4); double angle = document->map().get_windDirection({y,x},4)*rad;
			u += speed*std::sin(angle); v += speed*std::cos(angle);
		}
		auto g = document->globals(); g.heading = static_cast<int>(std::lround(std::fmod(std::atan2(u,v)/rad+360,360))) % 360;
		g.speed = static_cast<float>(std::hypot(u,v)/(static_cast<double>(rows)*columns));
		document->set_globals(g); update_global(); refresh();
	}
	std::optional<std::filesystem::path> choose_file(bool save) {
		wchar_t name[32768] = L"custom.fmap";
		OPENFILENAMEW ofn{}; ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd;
		ofn.lpstrFilter = L"BMS version 8 weather maps (*.fmap)\0*.fmap\0All files\0*.*\0";
		ofn.lpstrFile = name; ofn.nMaxFile = static_cast<DWORD>(std::size(name)); ofn.lpstrDefExt = L"fmap";
		ofn.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
		if (save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn)) return std::filesystem::path(name);
		return {};
	}
	bool save() {
		auto path = choose_file(true); if (!path) return false;
		std::string message;
		if (!document->map().save_atomic(*path, message)) { error(message); return false; }
		document->mark_saved(); title(); return true;
	}
	bool discard() {
		if (!document->dirty()) return true;
		int answer = MessageBoxW(hwnd, L"Save your changes before replacing or closing this map?", L"Unsaved weather", MB_YESNOCANCEL | MB_ICONQUESTION);
		return answer == IDNO || (answer == IDYES && save());
	}
	void command(int id, int notification) {
		switch (id) {
		case E_NEW: if (discard()) new_document(make_map()); break;
		case E_CURRENT: if (snapshot && discard()) new_document(snapshot->clone()); break;
		case E_OPEN: {
			auto path = choose_file(false); if (!path) break;
			std::string message; auto map = fmap::load(*path, message);
			if (!map) { error(message); break; }
			if (map->get_sizeY() != rows || map->get_sizeX() != columns) { error("This fmap's grid dimensions do not match the selected theater. Select its theater in the main window first."); break; }
			if (discard()) new_document(std::move(map)); break;
		}
		case E_SAVE: save(); break;
		case E_FIELD: if (notification == CBN_SELCHANGE) field_changed(); break;
		case E_LEVEL: case E_VIEW: if (notification == CBN_SELCHANGE) refresh(); break;
		case E_GLOBAL: if (notification == CBN_SELCHANGE) update_global(); break;
		case E_APPLY_GLOBAL: apply_global(); break;
		case E_AVERAGE: derive_wind(); break;
		case E_FILL: if (auto b = brush()) { document->begin_stroke(); document->paint_rectangle(0,0,columns-1,rows-1,*b); document->end_stroke(); refresh(); } break;
		case E_UNDO: document->undo(); update_global(); refresh(); break;
		case E_REDO: document->redo(); update_global(); refresh(); break;
		case E_GRID: InvalidateRect(canvas, nullptr, FALSE); break;
		case IDCANCEL: if (drawing) { stop(true); } else if (discard()) EndDialog(hwnd, 0); break;
		}
	}
	void stop(bool cancel) {
		if (!drawing) return;
		drawing = false;
		if (cancel) document->cancel_stroke(); else document->end_stroke();
		ReleaseCapture(); refresh();
	}
	void draw_canvas(HDC dc) {
		RECT r; GetClientRect(canvas, &r);
		FillRect(dc, &r, static_cast<HBRUSH>(GetStockObject(LTGRAY_BRUSH)));
		auto v = viewport();
		if (bitmap) {
			HDC source = CreateCompatibleDC(dc); auto old = SelectObject(source, bitmap);
			SetStretchBltMode(dc, HALFTONE); SetBrushOrgEx(dc, 0, 0, nullptr);
			StretchBlt(dc, static_cast<int>(v.x), static_cast<int>(v.y), static_cast<int>(v.width), static_cast<int>(v.height), source, 0, 0, preview.get_width(), preview.get_height(), SRCCOPY);
			SelectObject(source, old); DeleteDC(source);
		}
		{
			Gdiplus::Graphics g(dc); Gdiplus::Pen grid_pen(Gdiplus::Color(90,0,0,0));
			float cw = v.width / columns, ch = v.height / rows;
			if (IsDlgButtonChecked(hwnd,E_GRID) == BST_CHECKED && cw >= 5 && ch >= 5) {
				for (unsigned x = 0; x <= columns; ++x) g.DrawLine(&grid_pen,v.x+x*cw,v.y,v.x+x*cw,v.y+v.height);
				for (unsigned y = 0; y <= rows; ++y) g.DrawLine(&grid_pen,v.x,v.y+y*ch,v.x+v.width,v.y+y*ch);
			}
			Gdiplus::Pen outline(Gdiplus::Color::Black, 2), white(Gdiplus::Color::White, 1);
			if (selected) { g.DrawRectangle(&outline, v.x+selected->x*cw,v.y+selected->y*ch,cw,ch); }
			auto [gx,gy] = grid(cursor);
			if (drawing && active_tool == 1) {
				float x0 = std::floor(std::min(gx,rect_start.first)), y0 = std::floor(std::min(gy,rect_start.second));
				g.DrawRectangle(&outline,v.x+x0*cw,v.y+y0*ch,(std::floor(std::max(gx,rect_start.first))-x0+1)*cw,(std::floor(std::max(gy,rect_start.second))-y0+1)*ch);
			} else if (inside(gx,gy) && selection(E_TOOL) == 0) {
				float radius = drawing ? active_brush.radius : read(E_RADIUS).value_or(0);
				if (radius > 0 && radius <= 512) { g.DrawEllipse(&outline,cursor.x-radius*cw,cursor.y-radius*ch,2*radius*cw,2*radius*ch); g.DrawEllipse(&white,cursor.x-radius*cw,cursor.y-radius*ch,2*radius*cw,2*radius*ch); }
				else g.DrawRectangle(&outline,v.x+std::floor(gx)*cw,v.y+std::floor(gy)*ch,cw,ch);
			}
		}
	}
	void present_canvas(HDC destination) {
		RECT r; GetClientRect(canvas, &r);
		if (r.right <= 0 || r.bottom <= 0) return;
		if (!frame_dc) frame_dc = CreateCompatibleDC(destination);
		if (!frame_dc) return;
		if (!frame_bitmap || frame_size.cx != r.right || frame_size.cy != r.bottom) {
			HBITMAP next = CreateCompatibleBitmap(destination, r.right, r.bottom);
			if (!next) return;
			auto previous = SelectObject(frame_dc, next);
			if (!frame_original) frame_original = previous;
			if (frame_bitmap) DeleteObject(frame_bitmap);
			frame_bitmap = next;
			frame_size = {r.right, r.bottom};
		}
		// Compose background, map and brush outline off-screen. Only the finished
		// frame reaches the window, so the background clear cannot flash during a drag.
		draw_canvas(frame_dc);
		BitBlt(destination, 0, 0, r.right, r.bottom, frame_dc, 0, 0, SRCCOPY);
	}
	void paint_canvas() { PAINTSTRUCT ps; HDC dc = BeginPaint(canvas, &ps); present_canvas(dc); EndPaint(canvas,&ps); }
	static LRESULT CALLBACK canvas_proc(HWND window, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
		auto& e = *reinterpret_cast<editor*>(data);
		POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
		switch (msg) {
		case WM_PAINT: e.paint_canvas(); return 0;
		case WM_PRINTCLIENT: e.present_canvas(reinterpret_cast<HDC>(wp)); return 0;
		case WM_ERASEBKGND: return 1;
		case WM_LBUTTONDOWN: {
			auto [x,y] = e.grid(p); if (!e.inside(x,y)) return 0;
			if (e.selection(E_TOOL) == 2) { e.pick(x,y); return 0; }
			auto b = e.brush(); if (!b) return 0;
			e.active_brush = *b; e.active_tool = e.selection(E_TOOL); e.rect_start = {x,y}; e.cursor = p;
			e.document->begin_stroke(); e.drawing = true; SetCapture(window);
			if (e.active_tool == 0) { e.document->paint(x,y,*b); e.refresh(); }
			return 0;
		}
		case WM_MOUSEMOVE: {
			e.cursor = p;
			if (e.panning) { e.pan_x += p.x-e.last_pan.x; e.pan_y += p.y-e.last_pan.y; e.last_pan = p; }
			if (e.drawing && e.active_tool == 0) {
				auto [x,y] = e.grid(p);
				// Bound captured positions to keep interpolation work finite outside the canvas.
				e.document->paint(std::clamp(x,-1.f,static_cast<float>(e.columns)),std::clamp(y,-1.f,static_cast<float>(e.rows)),e.active_brush); e.refresh();
			} else InvalidateRect(window,nullptr,FALSE);
			return 0;
		}
		case WM_LBUTTONUP:
			if (e.drawing) {
				auto [x,y] = e.grid(p);
				if (e.active_tool == 1) e.document->paint_rectangle(static_cast<int>(e.rect_start.first),static_cast<int>(e.rect_start.second),static_cast<int>(std::floor(x)),static_cast<int>(std::floor(y)),e.active_brush);
				else e.document->paint(std::clamp(x,-1.f,static_cast<float>(e.columns)),std::clamp(y,-1.f,static_cast<float>(e.rows)),e.active_brush);
				e.stop(false);
			} return 0;
		case WM_CAPTURECHANGED: if (e.drawing) e.stop(true); e.panning = false; return 0;
		case WM_RBUTTONDOWN: { if (e.drawing) e.stop(true); auto [x,y] = e.grid(p); e.pick(x,y); return 0; }
		case WM_MBUTTONDOWN: if (!e.drawing) { e.panning = true; e.last_pan = p; SetCapture(window); } return 0;
		case WM_MBUTTONUP: e.panning = false; ReleaseCapture(); return 0;
		case WM_MOUSEWHEEL: {
			if (e.drawing) return 0;
			ScreenToClient(window,&p); auto [gx,gy] = e.grid(p);
			e.zoom = std::clamp(e.zoom * (GET_WHEEL_DELTA_WPARAM(wp)>0 ? 1.25f : .8f),1.f,16.f);
			auto v = e.viewport(); e.pan_x += p.x-(v.x+gx*v.width/e.columns); e.pan_y += p.y-(v.y+gy*v.height/e.rows);
			if (e.zoom == 1) e.pan_x = e.pan_y = 0;
			InvalidateRect(window,nullptr,FALSE); return 0;
		}
		}
		return DefSubclassProc(window,msg,wp,lp);
	}
	static INT_PTR CALLBACK dialog_proc(HWND window, UINT msg, WPARAM wp, LPARAM lp) {
		auto* e = reinterpret_cast<editor*>(GetWindowLongPtrW(window,DWLP_USER));
		if (msg == WM_INITDIALOG) { e = reinterpret_cast<editor*>(lp); e->hwnd = window; SetWindowLongPtrW(window,DWLP_USER,lp); e->initialize(); return TRUE; }
		if (!e) return FALSE;
		switch (msg) {
		case WM_SIZE: if (e->canvas) e->layout(); return TRUE;
		case WM_GETMINMAXINFO: {
			auto* size = reinterpret_cast<MINMAXINFO*>(lp); size->ptMinTrackSize = {static_cast<LONG>(880*e->scale),static_cast<LONG>(780*e->scale)}; return TRUE;
		}
		case WM_COMMAND: e->command(LOWORD(wp),HIWORD(wp)); return TRUE;
		case WM_CLOSE: if (e->drawing) e->stop(true); if (e->discard()) EndDialog(window,0); return TRUE;
		}
		return FALSE;
	}
};
}

void show_weather_editor(HWND parent, const std::string& theater, unsigned rows, unsigned columns,
	Gdiplus::Bitmap* background, const fmap* snapshot, editor_start start) {
	if (!rows || !columns || rows > 512 || columns > 512) {
		MessageBoxW(parent,L"The editor supports theater grids from 1 to 512 cells per dimension.",L"Weather Editor",MB_OK | MB_ICONERROR); return;
	}
	editor e; e.rows = rows; e.columns = columns; e.theater = to_wide(theater); e.background = background; e.snapshot = snapshot; e.start = start;
	if (DialogBoxParamW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(IDD_F4WX_EDITOR),parent,editor::dialog_proc,reinterpret_cast<LPARAM>(&e)) == -1)
		MessageBoxW(parent,L"Could not create the weather editor window.",L"Weather Editor",MB_OK | MB_ICONERROR);
}
