#pragma once

#include <arm/types.h>
#ifndef TWEAK_SHADER_H
#define TWEAK_SHADER_H

typedef unsigned char u_char;
typedef unsigned short u_short;
typedef unsigned short u_int16;
typedef unsigned long u_long;
typedef short int int16;
#define PF_TABLE_BITS 12
#define PF_TABLE_SZ_16 4096

#define PF_DEEP_COLOR_AWARE                                                    \
	1 // make sure we get 16bpc pixels; \
      // AE_Effect.h checks for this.

#include "AE_Macros.h"
#include "AEConfig.h"

#ifdef AE_OS_WIN
typedef unsigned short PixelType;
#include <Windows.h>
#endif

#include "Param_Utils.h"
#include "entry.h"

#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_EffectCBSuites.h"
#include "String_Utils.h"
#include "AE_GeneralPlug.h"
#include "AEFX_ChannelDepthTpl.h"
#include "AEGP_SuiteHandler.h"

#include "./tweak_shader_cxx/target/cxxbridge/rust/cxx.h"
#include "./tweak_shader_cxx/target/cxxbridge/tweak_shader_cxx/src/lib.rs.h"

#include <string>

/* Versioning information */

// NOTE: If you change this, update the handwritten hexadecimal encoding
// of it in the pipl.r
#define MAJOR_VERSION 1
#define MINOR_VERSION 1
#define BUG_VERSION 0
#define STAGE_VERSION PF_Stage_DEVELOP
#define BUILD_VERSION 1

// maximum number of exposed uniforms per instance
const uint32_t MAX_PARAMS = 32;

// Upsetting - I can find NO docs on how or why
// to actually set this. so its 10. 10 seems reasonable.
const AEGP_PluginID PLUGIN_ID = 10;

const AEGP_PluginID INPUT_LAYER_ID = 1234;

// The total number of inputs types we use
// to represent uniforms
const uint32_t NUM_INPUT_TYPES = 7;

struct FfiGlobalData
{
	rust::Box<GlobalData> rust_data;
};

struct FfiSequenceData
{
	bool is_flat = false;
	bool needs_reload = false;
	rust::Box<SequenceData> rust_data;
};

struct SequenceDataFlat
{
	bool is_flat = true;
	user_size_t run_length;
	char source[];
};

enum Params
{
	TWEAK_INPUT_BASE = 0,
	TWEAK_SOURCE,
	UNLOAD_SOURCE,
	TIME,
	IS_FILTER,
	LOCK_TIME_TO_LAYER,
	TWEAK_NUM_PARAMS
};

extern "C" {

DllExport PF_Err EffectMain(
	PF_Cmd cmd,
	PF_InData* in_data,
	PF_OutData* out_data,
	PF_ParamDef* params[],
	PF_LayerDef* output,
	void* extra
);
}

#endif // include guard
