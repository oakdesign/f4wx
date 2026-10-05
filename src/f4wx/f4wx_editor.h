// Copyright 2026 syn111. Licensed under the Apache License, Version 2.0.
#pragma once
#include <Windows.h>
#include "gdiplus_support.h"
#include "fmap.h"
#include <string>

enum class editor_start { new_map, open_map, current_map };
/** Modal single-frame editor. Background and snapshot are borrowed only for the modal lifetime. */
void show_weather_editor(HWND parent, const std::string& theater, unsigned rows, unsigned columns,
	Gdiplus::Bitmap* background, const fmap* snapshot, editor_start start);
