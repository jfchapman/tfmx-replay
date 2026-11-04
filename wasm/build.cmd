@echo off
rem Builds build\wasm\tfmxreplay.wasm with Emscripten. Run it from the repository's root, with em++ on the path
rem (emsdk_env.bat sets that up). The result is a single file that needs no JavaScript from Emscripten.
setlocal
if not exist build\wasm mkdir build\wasm
call em++ -O3 -std=c++17 -Iinclude -Isrc ^
	src/module.cpp src/paula.cpp src/replayer.cpp src/tfmxreplay.cpp src/variant.cpp wasm/tfmxreplay_wasm.cpp ^
	--no-entry -sSTANDALONE_WASM -sFILESYSTEM=0 -sALLOW_MEMORY_GROWTH=1 ^
	-sEXPORTED_FUNCTIONS=_malloc,_free,_tfmx_version,_tfmx_wasm_open,_tfmx_wasm_configure,_tfmx_close,_tfmx_subsong_index,_tfmx_select_subsong,_tfmx_render_float,_tfmx_seek ^
	-o build/wasm/tfmxreplay.wasm
