
#pragma once

#ifndef MISC_UTIL_H
#define MISC_UTIL_H

#include "AE_Effect.h"
#include "AEGP_SuiteHandler.h"
#include "tweak_shader.h"
#include <string>



#ifdef AE_OS_WIN
#define LOG(level, message) 
#else
enum LogLevel
{
	INFO,
	WARNING,
	ERROR,
};

void log(
	LogLevel level, const char* file, int line, const std::string& message
);

#define LOG(level, message) log(level, __FILE__, __LINE__, message)
#endif

PF_Err setParamVisibility(
	AEGP_PluginID aegpId,
	PF_InData* in_data,
	PF_ParamIndex index,
	A_Boolean visible
);

PF_Err createOneOfEveryInputType(
	PF_InData* in_data,
	PF_OutData* out_data,
	PF_ParamDef* params[],
	PF_LayerDef* output,
	rust::u32 index
);

PF_Err setParamsToMatchSequence(
	PF_InData* in_data,
	const FfiSequenceData* sequence_data,
	PF_ParamDef* params[]
);

#endif
