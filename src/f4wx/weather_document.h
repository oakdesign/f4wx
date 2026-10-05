// Copyright 2026 syn111. Licensed under the Apache License, Version 2.0.
#pragma once
#include "fmap.h"
#include <map>
#include <optional>
#include <deque>

enum class weather_field { preset, temperature, pressure, visibility, cloud_base, cloud_density, cloud_size,
	tower, shower, fog_height, wind_speed, wind_direction };
struct weather_brush {
	weather_field field = weather_field::preset;
	float value = 0;
	float radius = 2;
	float strength = 1;
	bool soft = false;
	unsigned wind_level = 0;
};
struct weather_globals {
	int heading; float speed; int stratus_fair, stratus_inc;
	int contrails[NUM_WEATHER_TYPES];
	bool operator==(const weather_globals&) const = default;
};

/** Owns authored weather; one command per stroke. No GRIB reconversion occurs here. */
class weather_document {
public:
	explicit weather_document(std::unique_ptr<fmap> map) : m_map(std::move(map)) {}
	fmap& map() { return *m_map; }
	const fmap& map() const { return *m_map; }
	bool dirty() const { return m_revision != m_saved_revision; }
	void mark_saved() { m_saved_revision = m_revision; }
	void begin_stroke();
	void paint(float x, float y, const weather_brush& brush);
	void paint_rectangle(int x0, int y0, int x1, int y1, const weather_brush& brush);
	void end_stroke();
	void cancel_stroke();
	bool undo();
	bool redo();
	bool can_undo() const { return !m_undo.empty(); }
	bool can_redo() const { return !m_redo.empty(); }
	weather_globals globals() const;
	void set_globals(const weather_globals& values);
	static fmap::cell_data preset(fmap_wxtype type, fmap::cell_data cell = {});
	static float value(const fmap::cell_data& cell, weather_field field, unsigned level);
	static bool valid_brush(const weather_brush& brush);
private:
	struct change { cell_index index; fmap::cell_data before, after; float coverage = 0; };
	struct command {
		std::vector<change> cells;
		std::optional<weather_globals> before_globals, after_globals;
		size_t before_revision = 0, after_revision = 0;
	};
	std::unique_ptr<fmap> m_map;
	std::map<size_t, change> m_pending;
	std::deque<command> m_undo, m_redo;
	std::optional<std::pair<float, float>> m_previous;
	size_t m_revision = 0, m_saved_revision = 0, m_next_revision = 0;
	void stamp(float x, float y, const weather_brush& brush);
	void apply(cell_index cell, float coverage, const weather_brush& brush);
	void commit(command cmd);
	void write_globals(const weather_globals& values);
};
