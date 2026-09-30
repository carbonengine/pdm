// Copyright © 2026 CCP ehf.

#include "cpu.h"
#include "../include/pdm.h"

namespace PDM
{
	bool HasVMExecutionTiming()
	{
		static bool _finished, _ret;
		if (_finished) return _ret;
		_finished = true;

		// Average time on a modern processor natively is around 25 cycles
		// Time on paravirtualized VM is over 5000 cycles
		const unsigned THRESHOLD_CYCLES = 100;
		const unsigned RUNS = 1024;

		volatile unsigned thresholdCrossings = 0;
		
		for (volatile unsigned i = 0; i < RUNS; i++)
		{
			if (GetTimingCycles() > THRESHOLD_CYCLES) thresholdCrossings++;
		}

		_ret = thresholdCrossings > (RUNS / 2);
		return _ret;
	}

	bool IsSuspectedVM()
	{
		if (HasVMExecutionTiming() || IsHyperVGuestOS()) return true;

		// A Hyper-V host (e.g. Windows with virtualization-based security) also reports a hypervisor
		std::string name = GetHypervisorName();
		if (HasHypervisorBit() && name != HYPER_V_NAME) return true;

		// Platforms can report a hypervisor name without the hypervisor bit
		return !name.empty() && name != HYPER_V_NAME;
	}
}
