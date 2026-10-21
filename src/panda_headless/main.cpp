// Headless runner: boots a ROM, runs N frames and dumps the top screen framebuffer straight from emulated memory.
// Used both natively (fast debug loop for the interpreter CPU) and as the wasm smoke-test entry point.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "PICA/regs.hpp"
#include "emulator.hpp"

static u8 expand5(u32 v) { return u8((v << 3) | (v >> 2)); }
static u8 expand4(u32 v) { return u8(v * 17); }
static u8 expand6(u32 v) { return u8((v << 2) | (v >> 4)); }

// 3DS LCD framebuffers are stored rotated: each 240-pixel column is contiguous, so a 400x240 screen is 240 wide in memory
bool dumpScreen(Emulator& emu, bool top, const std::string& path) {
	using namespace PICA::ExternalRegs;
	auto& regs = emu.getGPU().getExtRegisters();
	const u32 select = regs[top ? Framebuffer0Select : Framebuffer1Select] & 1;
	const u32 addr = top ? regs[select == 0 ? Framebuffer0AFirstAddr : Framebuffer0ASecondAddr]
						 : regs[select == 0 ? Framebuffer1AFirstAddr : Framebuffer1ASecondAddr];
	const u32 format = regs[top ? Framebuffer0Config : Framebuffer1Config] & 7;
	const u32 stride = regs[top ? Framebuffer0Stride : Framebuffer1Stride];
	const int width = top ? 400 : 320, height = 240;
	static constexpr int bpp[] = {4, 3, 2, 2, 2, 0, 0, 0};
	u8* src = emu.getGPU().getPointerPhys<u8>(addr);
	std::printf("[headless] %s fb addr=%08X format=%u stride=%u ptr=%p\n", top ? "top" : "bottom", addr, format, stride, src);
	if (!src || bpp[format] == 0) return false;

	FILE* f = std::fopen(path.c_str(), "wb");
	if (!f) return false;
	std::fprintf(f, "P6\n%d %d\n255\n", width, height);
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			const u8* p = src + x * stride + (height - 1 - y) * bpp[format];
			u8 rgb[3];
			switch (format) {
				case 0: rgb[0] = p[3], rgb[1] = p[2], rgb[2] = p[1]; break;
				case 1: rgb[0] = p[2], rgb[1] = p[1], rgb[2] = p[0]; break;
				case 2: {
					u16 v = p[0] | (p[1] << 8);
					rgb[0] = expand5(v >> 11), rgb[1] = expand6((v >> 5) & 0x3F), rgb[2] = expand5(v & 0x1F);
					break;
				}
				case 3: {
					u16 v = p[0] | (p[1] << 8);
					rgb[0] = expand5(v >> 11), rgb[1] = expand5((v >> 6) & 0x1F), rgb[2] = expand5((v >> 1) & 0x1F);
					break;
				}
				default: {
					u16 v = p[0] | (p[1] << 8);
					rgb[0] = expand4(v >> 12), rgb[1] = expand4((v >> 8) & 0xF), rgb[2] = expand4((v >> 4) & 0xF);
					break;
				}
			}
			std::fwrite(rgb, 1, 3, f);
		}
	}
	std::fclose(f);
	return true;
}

int main(int argc, char* argv[]) {
	if (argc < 2) {
		std::fprintf(stderr, "usage: %s <rom> [frames=120] [out-prefix=frame]\n", argv[0]);
		return 1;
	}
	const int frames = argc > 2 ? std::atoi(argv[2]) : 120;
	const std::string out = argc > 3 ? argv[3] : "frame";

	Emulator emu;
	emu.initGraphicsContext(nullptr);
	if (!emu.loadROM(std::filesystem::absolute(argv[1]))) {
		std::fprintf(stderr, "[headless] failed to load %s\n", argv[1]);
		return 1;
	}

	const auto start = std::chrono::steady_clock::now();
	for (int i = 0; i < frames; i++) {
		emu.runFrame();
	}
	const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
	std::printf("[headless] ran %d frames in %.2fs (%.1f fps) pc=%08X\n", frames, secs, frames / secs, emu.getCPU().getReg(15));

	dumpScreen(emu, true, out + "_top.ppm");
	dumpScreen(emu, false, out + "_bottom.ppm");
	return 0;
}
