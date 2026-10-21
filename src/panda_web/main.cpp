// Browser frontend: SDL2 window on a WebGL2 canvas, one emulated frame per requestAnimationFrame.
// JS writes a ROM into the virtual FS and calls panda_load_rom(path); see wasm/web/index.html.
#include <SDL.h>
#include <glad/gl.h>
#include <emscripten.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>

#include "audio/dsp_binary.hpp"
#include "emulator.hpp"
#include "io_file.hpp"
#include "input_mappings.hpp"
#include "screen_layout.hpp"

namespace {
	constexpr int windowWidth = 400;
	constexpr int windowHeight = 480;

	Emulator* emu;
	SDL_Window* window;
	InputMappings keyboardMappings;
	ScreenLayout::WindowCoordinates screenCoordinates;
	bool touching = false;
	u64 framesRun = 0;
	double emuMs = 0;
	u32 panics = 0;

	// The 3DS refreshes at ~59.83 Hz; rAF can fire at 60, 120 or 144 Hz, so emulate by elapsed time instead of per callback
	constexpr double emuFrameMs = 1000.0 / 59.8261;
	double lastTickMs = 0;
	double owedMs = 0;

	void handleTouch(int mouseX, int mouseY) {
		auto& hid = emu->getServiceManager().getHID();
		const auto& c = screenCoordinates;
		const int bx = int(c.bottomScreenX), by = int(c.bottomScreenY), bw = int(c.bottomScreenWidth), bh = int(c.bottomScreenHeight);

		if (mouseX >= bx && mouseX < bx + bw && mouseY >= by && mouseY < by + bh) {
			const float relX = float(mouseX - bx) / float(bw);
			const float relY = float(mouseY - by) / float(bh);
			hid.setTouchScreenPress(
				u16(std::clamp(relX * ScreenLayout::BOTTOM_SCREEN_WIDTH, 0.f, float(ScreenLayout::BOTTOM_SCREEN_WIDTH - 1))),
				u16(std::clamp(relY * ScreenLayout::BOTTOM_SCREEN_HEIGHT, 0.f, float(ScreenLayout::BOTTOM_SCREEN_HEIGHT - 1)))
			);
		} else {
			hid.releaseTouchScreen();
		}
	}

	void setKey(u32 key, bool pressed) {
		auto& hid = emu->getServiceManager().getHID();
		switch (key) {
			case HID::Keys::CirclePadRight: hid.setCirclepadX(pressed ? 0x9C : 0); break;
			case HID::Keys::CirclePadLeft: hid.setCirclepadX(pressed ? -0x9C : 0); break;
			case HID::Keys::CirclePadUp: hid.setCirclepadY(pressed ? 0x9C : 0); break;
			case HID::Keys::CirclePadDown: hid.setCirclepadY(pressed ? -0x9C : 0); break;
			default: pressed ? hid.pressKey(key) : hid.releaseKey(key); break;
		}
	}

	// libctru's ndspInit refuses to start without sdmc:/3ds/dspfirm.cdc, a firmware dump users take from their console.
	// Panda's HLE DSP only parses the DSP1 header, so an empty header with no segments is enough to get audio going.
	void ensureDspFirmwareStub() {
		const auto path = IOFile::getAppData() / "SDMC" / "3ds" / "dspfirm.cdc";
		if (std::filesystem::exists(path)) return;
		std::filesystem::create_directories(path.parent_path());

		Dsp1 header{};
		std::memcpy(header.magic, "DSP1", 4);
		header.size = sizeof(header);
		FILE* f = std::fopen(path.string().c_str(), "wb");
		if (!f) return;
		std::fwrite(&header, sizeof(header), 1, f);
		std::fclose(f);
		std::printf("[web] wrote stub %s\n", path.string().c_str());
	}

	void frame() {
		auto& hid = emu->getServiceManager().getHID();
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			switch (event.type) {
				case SDL_KEYDOWN:
				case SDL_KEYUP: {
					if (event.key.repeat) break;
					const u32 key = keyboardMappings.getMapping(event.key.keysym.sym);
					if (key != HID::Keys::Null) setKey(key, event.type == SDL_KEYDOWN);
					break;
				}
				case SDL_MOUSEBUTTONDOWN:
					if (event.button.button == SDL_BUTTON_LEFT) {
						touching = true;
						handleTouch(event.button.x, event.button.y);
					}
					break;
				case SDL_MOUSEMOTION:
					if (touching) handleTouch(event.motion.x, event.motion.y);
					break;
				case SDL_MOUSEBUTTONUP:
					if (event.button.button == SDL_BUTTON_LEFT) {
						touching = false;
						hid.releaseTouchScreen();
					}
					break;
			}
		}

		const double now = emscripten_get_now();
		owedMs += lastTickMs == 0 ? emuFrameMs : now - lastTickMs;
		lastTickMs = now;
		// After a hidden tab or a slow stretch, drop the backlog rather than fast-forwarding through it
		owedMs = std::min(owedMs, 3 * emuFrameMs);

		for (int i = 0; i < 2 && owedMs >= emuFrameMs; i++) {
			owedMs -= emuFrameMs;
			hid.updateInputs(emu->getTicks());
			const double start = emscripten_get_now();
			try {
				emu->runFrame();
			} catch (const std::exception&) {
				panics++;
			}
			emuMs += emscripten_get_now() - start;
			framesRun++;
			SDL_GL_SwapWindow(window);
		}
	}
}  // namespace

extern "C" {
	EMSCRIPTEN_KEEPALIVE int panda_load_rom(const char* path) {
		if (!emu->loadROM(std::filesystem::path(path))) {
			std::fprintf(stderr, "[web] failed to load %s\n", path);
			return 0;
		}
		std::printf("[web] loaded %s\n", path);
		// The SD card dir is per ROM and only known once loadROM has set it; the game reads the file at ndspInit, after this
		ensureDspFirmwareStub();
		return 1;
	}

	EMSCRIPTEN_KEEPALIVE double panda_frames() { return double(framesRun); }
	// Wall time spent inside Emulator::runFrame (CPU + GPU command processing), excluding the browser's frame pacing
	EMSCRIPTEN_KEEPALIVE double panda_emu_ms() { return emuMs; }
	// Read 4 bytes at a byte offset through the same IOFile path the loaders use; tests large-file seeks
	EMSCRIPTEN_KEEPALIVE u32 panda_read_u32(const char* path, double offset) {
		IOFile file(std::filesystem::path(path), "rb");
		u32 value = 0;
		if (!file.isOpen() || !file.seek(s64(offset))) return 0xFFFFFFFF;
		file.readBytes(&value, sizeof(value));
		return value;
	}

	EMSCRIPTEN_KEEPALIVE u32 panda_panics() { return panics; }
	EMSCRIPTEN_KEEPALIVE u32 panda_pc() { return emu->getCPU().getReg(15); }

	// Simulate a key press from JS (tests drive input without focusing a window)
	EMSCRIPTEN_KEEPALIVE void panda_set_key(u32 key, int pressed) { setKey(key, pressed != 0); }
}

int main(int argc, char* argv[]) {
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) {
		Helpers::panic("SDL init failed: %s", SDL_GetError());
	}

	emu = new Emulator();
	keyboardMappings = InputMappings::defaultKeyboardMappings();

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
	window = SDL_CreateWindow("Panda3DS", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, windowWidth, windowHeight, SDL_WINDOW_OPENGL);
	if (!window) Helpers::panic("Window creation failed: %s", SDL_GetError());

	SDL_GLContext glContext = SDL_GL_CreateContext(window);
	if (!glContext) Helpers::panic("WebGL2 context creation failed: %s", SDL_GetError());
	if (!gladLoadGLES2Loader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) Helpers::panic("glad GLES2 loader failed");

	emu->setOutputSize(windowWidth, windowHeight);
	ScreenLayout::calculateCoordinates(
		screenCoordinates, u32(windowWidth), u32(windowHeight), emu->getConfig().topScreenSize, emu->getConfig().screenLayout
	);
	emu->getRenderer()->setupGLES();
	emu->initGraphicsContext(window);

	std::printf("[web] ready, renderer=%s\n", Renderer::typeToString(emu->getRendererType()));
	EM_ASM(if (Module.onPandaReady) Module.onPandaReady(););
	emscripten_set_main_loop(frame, 0, false);
	return 0;
}
