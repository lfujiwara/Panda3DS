#pragma once

#include <memory>
#include <span>

#include "helpers.hpp"
#include "kernel.hpp"
#include "memory.hpp"
#include "scheduler.hpp"

struct ARMul_State;
class Emulator;

// ARM11 interpreter backend built on Citra's dyncom (third_party/dyncom), for hosts without a JIT (wasm)
class CPU {
	std::unique_ptr<ARMul_State> state;
	Memory& mem;
	Scheduler& scheduler;
	Emulator& emu;

  public:
	static constexpr u64 ticksPerSec = Scheduler::arm11Clock;

	CPU(Memory& mem, Kernel& kernel, Emulator& emu);
	~CPU();
	void reset();

	void setReg(int index, u32 value) { regs()[index] = value; }
	u32 getReg(int index) { return regs()[index]; }

	std::span<u32, 16> regs();
	std::span<u32, 32> fprs();

	void setCPSR(u32 value);
	u32 getCPSR();
	void setFPSCR(u32 value);
	u32 getFPSCR();
	void setTLSBase(u32 value);

	u64 getTicks() { return scheduler.currentTimestamp; }
	u64& getTicksRef() { return scheduler.currentTimestamp; }
	Scheduler& getScheduler() { return scheduler; }

	void addTicks(u64 ticks) { scheduler.currentTimestamp += ticks; }

	void clearCache();
	void clearCacheRange(u32 start, u32 size) { clearCache(); }

	void runFrame();
};
