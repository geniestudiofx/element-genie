#include "AEConfig.h"
#include "AE_EffectVers.h"

#ifndef AE_OS_WIN
	#include "AE_General.r"
#endif

// macOS build (Rez). The Windows build uses ElementGeniePiPL.rc.
resource 'PiPL' (16000) {
	{
		Kind { AEEffect },
		Name { "Element Genie" },
		Category { "Element Genie" },
		CodeMacIntel64 {"EffectMain"},
		CodeMacARM64 {"EffectMain"},
		AE_PiPL_Version { 2, 0 },
		AE_Effect_Spec_Version { PF_PLUG_IN_VERSION, PF_PLUG_IN_SUBVERS },
		AE_Effect_Version { 525825 },
		AE_Effect_Info_Flags { 0 },
		AE_Effect_Global_OutFlags { 0x00008024 },
		AE_Effect_Global_OutFlags_2 { 0x0800000E },
		AE_Effect_Match_Name { "TK Element Genie" },
		AE_Reserved_Info { 0 },
		AE_Effect_Support_URL { "https://github.com" }
	}
};
