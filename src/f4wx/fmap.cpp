// Copyright 2026 syn111. Licensed under the Apache License, Version 2.0.
#include <Windows.h>
#include "fmap.h"
#include <algorithm>
#include <limits>

std::unique_ptr<fmap> fmap::clone() const {
	auto result = std::make_unique<fmap>(m_sizeY, m_sizeX);
	result->m_mapWindHeading = m_mapWindHeading;
	result->m_mapWindSpeed = m_mapWindSpeed;
	result->m_mapStratusZFair = m_mapStratusZFair;
	result->m_mapStratusZInc = m_mapStratusZInc;
	std::copy(std::begin(m_mapContrailLayer), std::end(m_mapContrailLayer), result->m_mapContrailLayer);
	for (size_t i = 0; i < m_cells.size(); ++i)
		static_cast<cell_data&>(result->m_cells[i]) = m_cells[i];
	return result;
}

bool fmap::validate(std::string& error) const {
	error.clear();
	auto finite = [](float f) { return std::isfinite(f); };
	if (!m_sizeX || !m_sizeY || m_sizeX > 512 || m_sizeY > 512) {
		error = "Map dimensions must be between 1 and 512 cells."; return false;
	}
	if (m_mapWindHeading < 0 || m_mapWindHeading > 360 || !finite(m_mapWindSpeed) || m_mapWindSpeed < 0 || m_mapWindSpeed > 500 ||
		m_mapStratusZFair < 0 || m_mapStratusZFair > 100000 || m_mapStratusZInc < 0 || m_mapStratusZInc > 100000 ||
		std::any_of(std::begin(m_mapContrailLayer), std::end(m_mapContrailLayer), [](int z) { return z < 0 || z > 100000; })) {
		error = "Invalid map-wide weather values."; return false;
	}
	for (size_t i = 0; i < m_cells.size(); ++i) {
		const auto& c = m_cells[i];
		bool valid = c.basicCondition >= WX_SUNNY && c.basicCondition < NUM_WEATHER_TYPES &&
			finite(c.pressure) && c.pressure >= 100 && c.pressure <= 1200 && finite(c.temperature) && c.temperature >= -100 && c.temperature <= 100 &&
			finite(c.cumulusBase) && c.cumulusBase >= 0 && c.cumulusBase <= 100000 && c.cumulusDensity >= 0 && c.cumulusDensity <= 13 &&
			finite(c.cumulusSize) && c.cumulusSize >= 0 && c.cumulusSize <= 5 &&
			(c.hasTowerCumulus == 0 || c.hasTowerCumulus == 1) && (c.hasShowerCumulus == 0 || c.hasShowerCumulus == 1) &&
			finite(c.fogEndBelowLayerMapData) && c.fogEndBelowLayerMapData >= 0 && c.fogEndBelowLayerMapData <= 60 &&
			finite(c.fogLayerZ) && c.fogLayerZ >= 0 && c.fogLayerZ <= 100000;
		for (size_t k = 0; k < NUM_ALOFT_BREAKPOINTS; ++k)
			valid = valid && finite(c.windSpeed[k]) && c.windSpeed[k] >= 0 && c.windSpeed[k] <= 500 && finite(c.windDir[k]) && c.windDir[k] >= 0 && c.windDir[k] <= 360;
		if (!valid) { error = "Invalid weather values at row " + std::to_string(i / m_sizeX) + ", column " + std::to_string(i % m_sizeX) + "."; return false; }
	}
	return true;
}

std::unique_ptr<fmap> fmap::load(const std::filesystem::path& path, std::string& error) {
	static_assert(sizeof(int) == 4 && sizeof(unsigned) == 4 && sizeof(float) == 4);
	error.clear();
	try {
		std::ifstream in(path, std::ios::binary | std::ios::ate);
		if (!in) { error = "Cannot open weather map."; return nullptr; }
		auto length = in.tellg(); in.seekg(0);
		auto read = [&in](auto& value) { return static_cast<bool>(in.read(reinterpret_cast<char*>(&value), sizeof(value))); };
		int version = 0; unsigned rows = 0, columns = 0;
		if (!read(version) || !read(rows) || !read(columns)) { error = "Truncated fmap header."; return nullptr; }
		if (version != c_version) { error = "Only version 8 weather maps are supported."; return nullptr; }
		if (!rows || !columns || rows > 512 || columns > 512) { error = "Invalid or excessive fmap dimensions."; return nullptr; }
		constexpr size_t header_bytes = 44, cell_bytes = 120;
		if (length != static_cast<std::streamoff>(header_bytes + static_cast<size_t>(rows) * columns * cell_bytes)) {
			error = "Weather map length does not match its dimensions (truncated file or unsupported layout)."; return nullptr;
		}
		auto map = std::make_unique<fmap>(rows, columns);
		if (!read(map->m_mapWindHeading) || !read(map->m_mapWindSpeed) || !read(map->m_mapStratusZFair) ||
			!read(map->m_mapStratusZInc) || !read(map->m_mapContrailLayer)) { error = "Truncated fmap header."; return nullptr; }
		auto block = [&](auto member) { for (auto& c : map->m_cells) if (!read(c.*member)) return false; return true; };
		if (!block(&cell_data::basicCondition) || !block(&cell_data::pressure) || !block(&cell_data::temperature) ||
			!block(&cell_data::windSpeed) || !block(&cell_data::windDir) || !block(&cell_data::cumulusBase) ||
			!block(&cell_data::cumulusDensity) || !block(&cell_data::cumulusSize) || !block(&cell_data::hasTowerCumulus) ||
			!block(&cell_data::hasShowerCumulus) || !block(&cell_data::fogEndBelowLayerMapData) || !block(&cell_data::fogLayerZ)) {
			error = "Truncated fmap cell data."; return nullptr;
		}
		for (auto& c : map->m_cells) {
			if (c.basicCondition < 1 || c.basicCondition > NUM_WEATHER_TYPES) { error = "Invalid fmap weather category."; return nullptr; }
			--c.basicCondition;
		}
		if (!map->validate(error)) return nullptr;
		return map;
	} catch (const std::exception& e) { error = e.what(); return nullptr; }
}

bool fmap::save_atomic(const std::filesystem::path& path, std::string& error) const {
	if (!validate(error)) return false;
	// Reserve an exclusive sibling directory, so no existing file can be used as our temporary file.
	std::filesystem::path temporary;
	std::error_code ec;
	for (unsigned i = 0; i < 100; ++i) {
		temporary = path; temporary += L".f4wx-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(i);
		if (std::filesystem::create_directory(temporary, ec)) break;
		if (i == 99) { error = "Cannot create temporary export directory."; return false; }
	}
	const auto file = temporary / L"map.tmp";
	bool ok = false;
	try {
		ok = save(file);
		if (ok) ok = MoveFileExW(file.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
	} catch (const std::exception&) { ok = false; }
	if (!ok) error = "Could not save weather map. The existing destination was preserved.";
	std::filesystem::remove(file, ec);
	std::filesystem::remove(temporary, ec);
	return ok;
}
