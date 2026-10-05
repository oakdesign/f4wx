// Copyright 2026 syn111. Licensed under the Apache License, Version 2.0.
#include <Windows.h>
#include "weather_document.h"
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

#define CHECK(expression) do { if (!(expression)) throw std::runtime_error("Check failed: " #expression); } while (false)
std::string bytes(const std::filesystem::path& path) {
	std::ifstream in(path, std::ios::binary); return {std::istreambuf_iterator<char>(in), {}};
}
void write(const std::filesystem::path& path, const std::string& data) {
	std::ofstream out(path, std::ios::binary); out.write(data.data(), data.size());
}
void compare(const fmap& a, const fmap& b) {
	CHECK(a.get_sizeX() == b.get_sizeX() && a.get_sizeY() == b.get_sizeY());
	CHECK(a.get_mapWindHeading() == b.get_mapWindHeading()); CHECK(a.get_mapWindSpeed() == b.get_mapWindSpeed());
	CHECK(a.get_mapStratusZFair() == b.get_mapStratusZFair()); CHECK(a.get_mapStratusZInc() == b.get_mapStratusZInc());
	for (int i = 0; i < NUM_WEATHER_TYPES; ++i) CHECK(a.get_contrailLayer(static_cast<fmap_wxtype>(i)) == b.get_contrailLayer(static_cast<fmap_wxtype>(i)));
	for (unsigned y = 0; y < a.get_sizeY(); ++y) for (unsigned x = 0; x < a.get_sizeX(); ++x) CHECK(a.get_cell({y,x}) == b.get_cell({y,x}));
}

int main() {
	auto dir = std::filesystem::temp_directory_path() / ("f4wx-editor-tests-" + std::to_string(GetCurrentProcessId()));
	try {
		CHECK(std::filesystem::create_directory(dir));
		fmap map(3,5);
		for (unsigned y = 0; y < 3; ++y) for (unsigned x = 0; x < 5; ++x) {
			auto c = weather_document::preset(static_cast<fmap_wxtype>((x+y)%4));
			c.pressure = 990 + y*5.f+x; c.temperature = -10+y*3.f+x;
			for (unsigned k = 0; k < NUM_ALOFT_BREAKPOINTS; ++k) { c.windSpeed[k] = k*2.f+x; c.windDir[k] = k*30.f+y; }
			map.set_cell({y,x},c);
		}
		map.set_mapWindHeading(123); map.set_mapWindSpeed(17.5f); map.set_mapStratusZFair(31000);
		map.set_mapStratusZInc(24000); map.set_contrailLayer(WX_FAIR, 29000);
		std::string error;
		CHECK(map.save_atomic(dir/"a.fmap",error));
		CHECK(bytes(dir/"a.fmap").size() == 44+3*5*120);
		auto loaded = fmap::load(dir/"a.fmap",error); CHECK(loaded); compare(map,*loaded);
		CHECK(loaded->save_atomic(dir/"b.fmap",error)); CHECK(bytes(dir/"a.fmap") == bytes(dir/"b.fmap"));
		auto copy = map.clone(); compare(map,*copy); copy->set_temperature({0,0},20); CHECK(map.get_temperature({0,0}) != 20);
		// Byte-level layout check independent of the reader: category starts at offset 44, pressure follows its block.
		auto data = bytes(dir/"a.fmap"); int raw_type; float raw_pressure;
		std::memcpy(&raw_type,data.data()+44+4*14,4); CHECK(raw_type == map.get_type({2,4})+1);
		std::memcpy(&raw_pressure,data.data()+44+15*4+4*14,4); CHECK(raw_pressure == map.get_pressure({2,4}));
		write(dir/"bad.fmap",data.substr(0,data.size()-1)); CHECK(!fmap::load(dir/"bad.fmap",error));
		write(dir/"bad.fmap",data+"x"); CHECK(!fmap::load(dir/"bad.fmap",error));
		auto corrupt = data; int v = 7; std::memcpy(corrupt.data(),&v,4); write(dir/"bad.fmap",corrupt); CHECK(!fmap::load(dir/"bad.fmap",error));
		corrupt = data; v = INT_MIN; std::memcpy(corrupt.data()+44,&v,4); write(dir/"bad.fmap",corrupt); CHECK(!fmap::load(dir/"bad.fmap",error));
		corrupt = data; v = 513; std::memcpy(corrupt.data()+4,&v,4); write(dir/"bad.fmap",corrupt); CHECK(!fmap::load(dir/"bad.fmap",error));
		corrupt = data; float nan = std::numeric_limits<float>::quiet_NaN(); std::memcpy(corrupt.data()+44+15*4,&nan,4);
		write(dir/"bad.fmap",corrupt); CHECK(!fmap::load(dir/"bad.fmap",error));
		map.set_pressure({0,0},nan); CHECK(!map.save_atomic(dir/"a.fmap",error)); CHECK(bytes(dir/"a.fmap") == data);
		map.set_pressure({0,0},990); CHECK(!map.save_atomic(dir/"missing"/"a.fmap",error));
		CHECK(std::filesystem::create_directory(dir/"destination")); write(dir/"destination"/"keep.txt","keep");
		CHECK(!map.save_atomic(dir/"destination",error)); CHECK(bytes(dir/"destination"/"keep.txt") == "keep");
		for (const auto& file : std::filesystem::directory_iterator(dir)) CHECK(file.path().filename().string().find(".f4wx-") == std::string::npos);

		weather_document doc(map.clone()); auto original = doc.map().clone();
		weather_brush b; b.field = weather_field::temperature; b.value = 25; b.radius = 0;
		doc.begin_stroke(); doc.paint(-1,-1,b); doc.end_stroke(); CHECK(!doc.dirty());
		doc.begin_stroke(); doc.paint(.5f,.5f,b); doc.paint(4.5f,.5f,b); doc.end_stroke();
		for (unsigned x=0;x<5;++x) CHECK(doc.map().get_temperature({0,x}) == 25);
		CHECK(doc.dirty()); CHECK(doc.undo()); compare(doc.map(),*original); CHECK(!doc.dirty());
		CHECK(doc.redo()); doc.mark_saved(); CHECK(!doc.dirty()); CHECK(doc.undo()); CHECK(doc.dirty());
		CHECK(doc.redo()); CHECK(!doc.dirty());
		doc.begin_stroke(); doc.paint(.5f,1.5f,b); doc.cancel_stroke(); CHECK(doc.map().get_temperature({1,0}) == original->get_temperature({1,0}));
		CHECK(!doc.dirty()); CHECK(doc.undo());
		doc.begin_stroke(); doc.paint_rectangle(3,2,1,1,b); doc.end_stroke(); CHECK(!doc.can_redo());
		CHECK(doc.map().get_temperature({2,3}) == 25); CHECK(doc.map().get_temperature({1,1}) == 25);
		CHECK(doc.map().get_temperature({0,0}) == original->get_temperature({0,0}));
		CHECK(doc.undo()); compare(doc.map(),*original);
		// Multiple events in a stroke must not compound strength against the same cell.
		b.strength = .5f; b.value = 20; doc.begin_stroke(); doc.paint(.5f,.5f,b); doc.paint(.5f,.5f,b); doc.end_stroke();
		CHECK(doc.map().get_temperature({0,0}) == (original->get_temperature({0,0})+20)/2);
		doc.undo(); b.field = weather_field::wind_direction; b.value = 1;
		doc.map().set_windDirection({0,0},359); doc.begin_stroke(); doc.paint(.5f,.5f,b); doc.end_stroke();
		float direction = doc.map().get_windDirection({0,0}); CHECK(direction < .01f || direction > 359.99f);
		doc.undo(); b.field = weather_field::preset; b.value = WX_INCLEMENT; b.strength=1;
		auto before = doc.map().get_cell({0,0}); doc.begin_stroke(); doc.paint(.5f,.5f,b); doc.end_stroke();
		auto after = doc.map().get_cell({0,0}); CHECK(after.basicCondition == WX_INCLEMENT && after.hasShowerCumulus == 1);
		CHECK(after.cumulusBase > 0 && after.fogLayerZ == after.cumulusBase); CHECK(after.pressure == before.pressure && after.temperature == before.temperature);
		for (unsigned k=0;k<NUM_ALOFT_BREAKPOINTS;++k) CHECK(after.windDir[k] == before.windDir[k] && after.windSpeed[k] == before.windSpeed[k]);
		doc.undo(); auto g = doc.globals(), next = g; next.heading=222; next.contrails[2]=22000;
		doc.set_globals(next); CHECK(doc.globals() == next); doc.undo(); CHECK(doc.globals() == g); doc.redo(); CHECK(doc.globals() == next);
		b.field=weather_field::visibility; b.value=61; CHECK(!weather_document::valid_brush(b));
		b.value=20; b.radius=2; b.soft=true; b.strength=1;
		weather_document soft(std::make_unique<fmap>(5,5)); soft.begin_stroke(); soft.paint(2.5f,2.5f,b); soft.end_stroke();
		CHECK(soft.map().get_visibility({2,2}) == 20); CHECK(soft.map().get_visibility({2,3}) == 40); CHECK(soft.map().get_visibility({0,0}) == 60);
		soft.undo(); CHECK(soft.map().get_visibility({2,2}) == 60);
#ifdef FMAP_DEBUG
		struct diagnostic : fmap::debug_data { std::string str() const override { return "GRIB"; } };
		map.set_debugData({0,0},std::make_unique<diagnostic>()); auto cloned = map.clone(); CHECK(!cloned->get_debugData({0,0}));
		map.set_cell({0,0},map.get_cell({0,0})); CHECK(!map.get_debugData({0,0}));
#endif
		std::filesystem::remove_all(dir);
		std::cout << "All weather editor data tests passed.\n";
		return 0;
	} catch (const std::exception& e) {
		std::cerr << e.what() << "\nTest fixtures retained at " << dir << '\n'; return 1;
	}
}
