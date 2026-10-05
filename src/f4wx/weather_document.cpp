// Copyright 2026 syn111. Licensed under the Apache License, Version 2.0.
#include "weather_document.h"
#include <algorithm>
#include <cmath>

fmap::cell_data weather_document::preset(fmap_wxtype type, fmap::cell_data c) {
	c.basicCondition = type;
	const float bases[] = { 8000, 7000, 4000, 2000 };
	const int densities[] = { 0, 5, 9, 13 };
	const float sizes[] = { 5, 4, 2, 0 };
	const float visibility[] = { 60, 40, 15, 5 };
	c.cumulusBase = bases[type]; c.cumulusDensity = densities[type]; c.cumulusSize = sizes[type];
	c.hasTowerCumulus = type == WX_INCLEMENT; c.hasShowerCumulus = type == WX_INCLEMENT;
	c.fogEndBelowLayerMapData = visibility[type]; c.fogLayerZ = c.cumulusBase;
	return c;
}

float weather_document::value(const fmap::cell_data& c, weather_field f, unsigned k) {
	switch (f) {
	case weather_field::preset: return static_cast<float>(c.basicCondition);
	case weather_field::temperature: return c.temperature;
	case weather_field::pressure: return c.pressure;
	case weather_field::visibility: return c.fogEndBelowLayerMapData;
	case weather_field::cloud_base: return c.cumulusBase;
	case weather_field::cloud_density: return static_cast<float>(c.cumulusDensity);
	case weather_field::cloud_size: return c.cumulusSize;
	case weather_field::tower: return static_cast<float>(c.hasTowerCumulus);
	case weather_field::shower: return static_cast<float>(c.hasShowerCumulus);
	case weather_field::fog_height: return c.fogLayerZ;
	case weather_field::wind_speed: return c.windSpeed[k];
	case weather_field::wind_direction: return c.windDir[k];
	}
	return 0;
}

bool weather_document::valid_brush(const weather_brush& b) {
	if (!std::isfinite(b.value) || !std::isfinite(b.radius) || b.radius < 0 || b.radius > 512 ||
		!std::isfinite(b.strength) || b.strength <= 0 || b.strength > 1 || b.wind_level >= NUM_ALOFT_BREAKPOINTS) return false;
	switch (b.field) {
	case weather_field::preset: return b.value >= 0 && b.value <= 3 && b.value == std::floor(b.value);
	case weather_field::temperature: return b.value >= -100 && b.value <= 100;
	case weather_field::pressure: return b.value >= 100 && b.value <= 1200;
	case weather_field::visibility: return b.value >= 0 && b.value <= 60;
	case weather_field::cloud_base: case weather_field::fog_height: return b.value >= 0 && b.value <= 100000;
	case weather_field::cloud_density: return b.value >= 0 && b.value <= 13 && b.value == std::floor(b.value);
	case weather_field::cloud_size: return b.value >= 0 && b.value <= 5;
	case weather_field::tower: case weather_field::shower: return b.value == 0 || b.value == 1;
	case weather_field::wind_speed: return b.value >= 0 && b.value <= 500;
	case weather_field::wind_direction: return b.value >= 0 && b.value <= 360;
	}
	return false;
}

void weather_document::begin_stroke() { cancel_stroke(); m_previous.reset(); }
void weather_document::apply(cell_index index, float coverage, const weather_brush& b) {
	if (coverage <= 0) return;
	size_t key = static_cast<size_t>(index.y) * map().get_sizeX() + index.x;
	auto [it, inserted] = m_pending.try_emplace(key, change{ index, map().get_cell(index), map().get_cell(index), 0 });
	auto& edit = it->second;
	if (!inserted && coverage <= edit.coverage) return;
	edit.coverage = coverage;
	auto c = edit.before;
	float v = value(c, b.field, b.wind_level) + coverage * (b.value - value(c, b.field, b.wind_level));
	switch (b.field) {
	case weather_field::preset: c = preset(static_cast<fmap_wxtype>(static_cast<int>(b.value)), c); break;
	case weather_field::temperature: c.temperature = v; break;
	case weather_field::pressure: c.pressure = v; break;
	case weather_field::visibility: c.fogEndBelowLayerMapData = v; break;
	case weather_field::cloud_base: c.cumulusBase = v; break;
	case weather_field::cloud_density: c.cumulusDensity = static_cast<int>(b.value); break;
	case weather_field::cloud_size: c.cumulusSize = v; break;
	case weather_field::tower: c.hasTowerCumulus = static_cast<int>(b.value); break;
	case weather_field::shower: c.hasShowerCumulus = static_cast<int>(b.value); break;
	case weather_field::fog_height: c.fogLayerZ = v; break;
	case weather_field::wind_speed: c.windSpeed[b.wind_level] = v; break;
	case weather_field::wind_direction: {
		constexpr float radians = 0.017453292519943295f;
		float a = c.windDir[b.wind_level] * radians, z = b.value * radians;
		float x = (1 - coverage) * std::cos(a) + coverage * std::cos(z);
		float y = (1 - coverage) * std::sin(a) + coverage * std::sin(z);
		c.windDir[b.wind_level] = std::hypot(x, y) < 0.00001f ? b.value : std::fmod(std::atan2(y, x) / radians + 360.f, 360.f);
		break;
	}
	}
	edit.after = c;
	if (!(c == edit.before)) map().set_cell(index, c);
}
void weather_document::stamp(float x, float y, const weather_brush& b) {
	if (b.radius == 0) {
		int cx = static_cast<int>(std::floor(x)), cy = static_cast<int>(std::floor(y));
		if (cx >= 0 && cy >= 0 && cx < static_cast<int>(map().get_sizeX()) && cy < static_cast<int>(map().get_sizeY()))
			apply({static_cast<unsigned>(cy), static_cast<unsigned>(cx)}, b.strength, b);
		return;
	}
	int x0 = std::max(0, static_cast<int>(std::floor(x - b.radius))), y0 = std::max(0, static_cast<int>(std::floor(y - b.radius)));
	int x1 = std::min(static_cast<int>(map().get_sizeX()) - 1, static_cast<int>(std::floor(x + b.radius)));
	int y1 = std::min(static_cast<int>(map().get_sizeY()) - 1, static_cast<int>(std::floor(y + b.radius)));
	for (int cy = y0; cy <= y1; ++cy) for (int cx = x0; cx <= x1; ++cx) {
		float distance = std::hypot(cx + .5f - x, cy + .5f - y);
		if (distance <= b.radius) apply({static_cast<unsigned>(cy), static_cast<unsigned>(cx)}, b.strength * (b.soft ? 1 - distance / b.radius : 1), b);
	}
}
void weather_document::paint(float x, float y, const weather_brush& b) {
	if (!valid_brush(b) || !std::isfinite(x) || !std::isfinite(y)) return;
	if (m_previous) {
		const auto [px, py] = *m_previous;
		int steps = static_cast<int>(std::ceil(std::hypot(x - px, y - py) / .25f));
		for (int i = 1; i <= steps; ++i) stamp(px + (x - px) * i / steps, py + (y - py) * i / steps, b);
	} else stamp(x, y, b);
	m_previous = {x, y};
}
void weather_document::paint_rectangle(int x0, int y0, int x1, int y1, const weather_brush& b) {
	if (!valid_brush(b)) return;
	if (x0 > x1) std::swap(x0, x1); if (y0 > y1) std::swap(y0, y1);
	x0 = std::max(0, x0); y0 = std::max(0, y0);
	x1 = std::min(x1, static_cast<int>(map().get_sizeX()) - 1); y1 = std::min(y1, static_cast<int>(map().get_sizeY()) - 1);
	for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x)
		apply({static_cast<unsigned>(y), static_cast<unsigned>(x)}, b.strength, b);
}
void weather_document::commit(command cmd) {
	cmd.before_revision = m_revision; cmd.after_revision = ++m_next_revision;
	m_revision = cmd.after_revision; m_redo.clear(); m_undo.push_back(std::move(cmd));
	size_t bytes = 0;
	for (const auto& c : m_undo) bytes += c.cells.size() * sizeof(change) + sizeof(command);
	while (m_undo.size() > 1 && (bytes > 64 * 1024 * 1024 || m_undo.size() > 100)) {
		bytes -= m_undo.front().cells.size() * sizeof(change) + sizeof(command); m_undo.pop_front();
	}
}
void weather_document::end_stroke() {
	command cmd;
	for (auto& [key, edit] : m_pending) if (!(edit.before == edit.after)) cmd.cells.push_back(edit);
	m_pending.clear(); m_previous.reset();
	if (!cmd.cells.empty()) commit(std::move(cmd));
}
void weather_document::cancel_stroke() {
	for (const auto& [key, edit] : m_pending) map().set_cell(edit.index, edit.before);
	m_pending.clear(); m_previous.reset();
}
bool weather_document::undo() {
	if (m_undo.empty()) return false;
	auto cmd = std::move(m_undo.back()); m_undo.pop_back();
	for (const auto& c : cmd.cells) map().set_cell(c.index, c.before);
	if (cmd.before_globals) write_globals(*cmd.before_globals);
	m_revision = cmd.before_revision; m_redo.push_back(std::move(cmd)); return true;
}
bool weather_document::redo() {
	if (m_redo.empty()) return false;
	auto cmd = std::move(m_redo.back()); m_redo.pop_back();
	for (const auto& c : cmd.cells) map().set_cell(c.index, c.after);
	if (cmd.after_globals) write_globals(*cmd.after_globals);
	m_revision = cmd.after_revision; m_undo.push_back(std::move(cmd)); return true;
}
weather_globals weather_document::globals() const {
	weather_globals g{map().get_mapWindHeading(), map().get_mapWindSpeed(), map().get_mapStratusZFair(), map().get_mapStratusZInc(), {}};
	for (int i = 0; i < NUM_WEATHER_TYPES; ++i) g.contrails[i] = map().get_contrailLayer(static_cast<fmap_wxtype>(i));
	return g;
}
void weather_document::write_globals(const weather_globals& g) {
	map().set_mapWindHeading(g.heading); map().set_mapWindSpeed(g.speed);
	map().set_mapStratusZFair(g.stratus_fair); map().set_mapStratusZInc(g.stratus_inc);
	for (int i = 0; i < NUM_WEATHER_TYPES; ++i) map().set_contrailLayer(static_cast<fmap_wxtype>(i), g.contrails[i]);
}
void weather_document::set_globals(const weather_globals& g) {
	const auto before = globals(); if (before == g) return;
	command cmd; cmd.before_globals = before; cmd.after_globals = g;
	write_globals(g); commit(std::move(cmd));
}
