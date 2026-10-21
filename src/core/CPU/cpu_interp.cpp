#ifdef CPU_INTERP
#include "cpu_interp.hpp"

#include "arm_defs.hpp"
#include "core/arm/dyncom/arm_dyncom_interpreter.h"
#include "core/arm/dyncom/arm_dyncom_trans.h"
#include "core/arm/skyeye_common/armstate.h"
#include "core/arm/skyeye_common/vfp/vfp.h"
#include "emulator.hpp"

CPU::CPU(Memory& mem, Kernel& kernel, Emulator& emu) : mem(mem), emu(emu), scheduler(emu.getScheduler()) {
	state = std::make_unique<ARMul_State>(mem, kernel, scheduler.currentTimestamp, USER32MODE);
	mem.setCPUTicks(getTicksRef());
}

CPU::~CPU() = default;

void CPU::reset() {
	state->Reset();
	state->ChangePrivilegeMode(USER32MODE);
	state->Reg.fill(0);
	state->ExtReg.fill(0);
	setCPSR(CPSR::UserMode);
	setFPSCR(FPSCR::MainThreadDefault);
	setTLSBase(VirtualAddrs::TLSBase);
	clearCache();
}

std::span<u32, 16> CPU::regs() { return state->Reg; }
std::span<u32, 32> CPU::fprs() { return std::span(state->ExtReg).first<32>(); }

void CPU::setCPSR(u32 value) {
	state->Cpsr = value;
	state->TFlag = (value >> 5) & 1;
}
u32 CPU::getCPSR() { return state->Cpsr; }
void CPU::setFPSCR(u32 value) { state->VFP[VFP_FPSCR] = value; }
u32 CPU::getFPSCR() { return state->VFP[VFP_FPSCR]; }
void CPU::setTLSBase(u32 value) { state->CP15[CP15_THREAD_URO] = value; }

void CPU::clearCache() {
	state->instruction_cache.clear();
	trans_cache_buf_top = 0;
}

void CPU::runFrame() {
	emu.frameDone = false;

	while (!emu.frameDone) {
		// ponytail: whole-cache flush when the translation buffer nears full, an LRU per block if it thrashes
		if (trans_cache_buf_top > TRANS_CACHE_SIZE - 1024 * 1024) {
			clearCache();
		}

		const s64 ticksLeft = s64(scheduler.nextTimestamp - scheduler.currentTimestamp);
		state->NumInstrsToExecute = ticksLeft > 0 ? u64(ticksLeft) : 1;
		scheduler.currentTimestamp += InterpreterMainLoop(state.get());
		emu.pollScheduler();
	}
}

#endif  // CPU_INTERP
