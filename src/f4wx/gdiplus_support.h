// Copyright 2026 syn111. Licensed under the Apache License, Version 2.0.
#pragma once
#include <Windows.h>
#include <algorithm>
// Windows SDK GDI+ headers use unqualified min/max even when NOMINMAX is set.
namespace Gdiplus { using std::min; using std::max; }
#include <gdiplus.h>
