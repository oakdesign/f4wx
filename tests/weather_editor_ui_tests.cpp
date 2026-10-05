// Copyright 2026 syn111. Licensed under the Apache License, Version 2.0.
// Include the window implementation to inspect its document while driving real Win32 controls.
#include "../src/f4wx/f4wx_editor.cpp"
#include <iostream>
#include <stdexcept>
#define CHECK(expression) do { if (!(expression)) throw std::runtime_error("Check failed: " #expression); } while (false)

int main() {
	HWND window = nullptr;
	try {
		InitCommonControls();
		editor e; e.rows = 8; e.columns = 8; e.theater = L"Test theater"; e.background = nullptr;
		fmap original(8,8); original.set_temperature({0,0},17.5f);
		e.snapshot = &original; e.start = editor_start::current_map;
		window = CreateDialogParamW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(IDD_F4WX_EDITOR),nullptr,editor::dialog_proc,reinterpret_cast<LPARAM>(&e));
		CHECK(window && e.canvas && e.bitmap);
		CHECK(e.document->map().get_temperature({0,0}) == 17.5f);
		// Choose temperature, exact-cell radius, and a numeric value through actual controls.
		SendMessageW(e.control(E_FIELD),CB_SETCURSEL,1,0); SendMessageW(window,WM_COMMAND,MAKEWPARAM(E_FIELD,CBN_SELCHANGE),0);
		SetWindowTextW(e.control(E_VALUE),L"30"); SetWindowTextW(e.control(E_RADIUS),L"0");
		auto v = e.viewport(); POINT point{static_cast<LONG>(v.x+v.width/16),static_cast<LONG>(v.y+v.height/16)};
		SendMessageW(e.canvas,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(point.x,point.y));
		SendMessageW(e.canvas,WM_LBUTTONUP,0,MAKELPARAM(point.x,point.y));
		CHECK(e.document->map().get_temperature({0,0}) == 30); CHECK(original.get_temperature({0,0}) == 17.5f);
		CHECK(e.document->dirty()); CHECK(IsWindowEnabled(e.control(E_UNDO)));
		SendMessageW(window,WM_COMMAND,E_UNDO,0); CHECK(e.document->map().get_temperature({0,0}) == 17.5f);
		CHECK(!e.document->dirty()); SendMessageW(window,WM_COMMAND,E_REDO,0); CHECK(e.document->map().get_temperature({0,0}) == 30);
		SendMessageW(e.canvas,WM_RBUTTONDOWN,0,MAKELPARAM(point.x,point.y)); CHECK(e.selected && e.selected->x == 0 && e.selected->y == 0);
		CHECK(e.read(E_VALUE) == 30); CHECK(IsWindowVisible(e.control(E_PRESET)) == FALSE);
		// Soft numeric brush, capture cancellation, and rectangle behavior.
		SetWindowTextW(e.control(E_VALUE),L"10");
		SendMessageW(e.canvas,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(point.x,point.y));
		SendMessageW(e.canvas,WM_CAPTURECHANGED,0,0); CHECK(e.document->map().get_temperature({0,0}) == 30);
		SendMessageW(e.control(E_TOOL),CB_SETCURSEL,1,0);
		POINT end{static_cast<LONG>(v.x+v.width*3.5f/8),static_cast<LONG>(v.y+v.height*2.5f/8)};
		SendMessageW(e.canvas,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(point.x,point.y));
		SendMessageW(e.canvas,WM_LBUTTONUP,0,MAKELPARAM(end.x,end.y)); CHECK(e.document->map().get_temperature({2,3}) == 10);
		CHECK(e.document->map().get_temperature({3,3}) == 15);
		// Map-wide controls participate in undo/redo.
		SetWindowTextW(e.control(E_GLOBAL_VALUE),L"270"); SendMessageW(window,WM_COMMAND,E_APPLY_GLOBAL,0);
		CHECK(e.document->map().get_mapWindHeading() == 270);
		SendMessageW(window,WM_COMMAND,E_UNDO,0); CHECK(e.document->map().get_mapWindHeading() == 0);
		// Check every overlay can render and resize remains usable.
		for (int mode = 0; mode < 9; ++mode) {
			SendMessageW(e.control(E_VIEW),CB_SETCURSEL,mode,0); SendMessageW(window,WM_COMMAND,MAKEWPARAM(E_VIEW,CBN_SELCHANGE),0); CHECK(e.bitmap);
		}
		SetWindowPos(window,nullptr,-10000,-10000,1100,820,SWP_NOZORDER | SWP_NOACTIVATE);
		ShowWindow(window,SW_SHOWNOACTIVATE); // Off-screen rendering only; never overlaps the user's desktop.
		UpdateWindow(window);
		RECT client; GetClientRect(e.canvas,&client); CHECK(client.right > 500 && client.bottom > 500);
		std::string error; CHECK(e.document->map().validate(error));
		// Render the hidden canvas to an artifact for visual inspection; no desktop window is shown.
		SendMessageW(e.control(E_FIELD),CB_SETCURSEL,0,0); SendMessageW(window,WM_COMMAND,MAKEWPARAM(E_FIELD,CBN_SELCHANGE),0);
		SendMessageW(e.control(E_PRESET),CB_SETCURSEL,2,0); SendMessageW(window,WM_COMMAND,E_FILL,0);
		HDC screen = GetDC(nullptr), output = CreateCompatibleDC(screen);
		HBITMAP capture = CreateCompatibleBitmap(screen,client.right,client.bottom); CHECK(capture);
		auto previous = SelectObject(output,capture);
		SendMessageW(e.canvas,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(output),PRF_CLIENT);
		CHECK(e.frame_bitmap && e.frame_size.cx == client.right && e.frame_size.cy == client.bottom);
		const auto frame = e.frame_bitmap;
		const auto objects = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
		SendMessageW(e.control(E_TOOL), CB_SETCURSEL, 0, 0);
		SendMessageW(e.canvas, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(100,100));
		for (int i = 0; i < 50; ++i) {
			SendMessageW(e.canvas, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(100+i*3,100+i*2));
			SendMessageW(e.canvas, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(output), PRF_CLIENT);
			CHECK(e.frame_bitmap == frame); // The buffer is reused throughout the stroke.
		}
		SendMessageW(e.canvas, WM_LBUTTONUP, 0, MAKELPARAM(247,198));
		CHECK(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= objects+2);
		SelectObject(output,previous); DeleteDC(output); ReleaseDC(nullptr,screen);
		{
			Gdiplus::Bitmap rendered(capture,nullptr);
			const CLSID png{0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
			CHECK(rendered.Save(L"obj/editor-tests/canvas.png",&png,nullptr) == Gdiplus::Ok);
		}
		DeleteObject(capture);
		RECT dialog_client; GetClientRect(window,&dialog_client);
		screen = GetDC(nullptr); output = CreateCompatibleDC(screen);
		capture = CreateCompatibleBitmap(screen,dialog_client.right,dialog_client.bottom);
		previous = SelectObject(output,capture);
		SendMessageW(window,WM_PRINT,reinterpret_cast<WPARAM>(output),PRF_CLIENT | PRF_CHILDREN | PRF_ERASEBKGND);
		SelectObject(output,previous); DeleteDC(output); ReleaseDC(nullptr,screen);
		{
			Gdiplus::Bitmap rendered(capture,nullptr);
			const CLSID png{0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
			CHECK(rendered.Save(L"obj/editor-tests/editor.png",&png,nullptr) == Gdiplus::Ok);
		}
		DeleteObject(capture);
		DestroyWindow(window); window = nullptr;
		std::cout << "All weather editor Win32 control tests passed.\n"; return 0;
	} catch (const std::exception& ex) { if (window) DestroyWindow(window); std::cerr << ex.what() << '\n'; return 1; }
}
