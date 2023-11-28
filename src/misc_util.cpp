#include "misc_util.h"
#include "src/lib.rs.h"
#include "tweak_shader.h"
#include "Param_Utils.h"

#include <iostream>
#include <string>
#include <sys/_types/_int32_t.h>

PF_Err setParamVisibility(
	AEGP_PluginID aegpId,
	PF_InData* in_data,
	PF_ParamIndex index,
	A_Boolean visible
)
{
	PF_Err err = PF_Err_NONE;
	AEGP_SuiteHandler suites(in_data->pica_basicP);

	AEGP_EffectRefH effectH = nullptr;
	AEGP_StreamRefH streamH = nullptr;

	ERR(suites.PFInterfaceSuite1()->AEGP_GetNewEffectForEffect(
		aegpId, in_data->effect_ref, &effectH
	));

	ERR(suites.StreamSuite5()->AEGP_GetNewEffectStreamByIndex(
		aegpId, effectH, index, &streamH
	));

	if( !effectH || !streamH )
	{
		return err;
	}

	ERR(suites.DynamicStreamSuite4()->AEGP_SetDynamicStreamFlag(
		streamH, AEGP_DynStreamFlag_HIDDEN, FALSE, !visible
	));

	ERR(suites.EffectSuite4()->AEGP_DisposeEffect(effectH));
	ERR(suites.StreamSuite5()->AEGP_DisposeStream(streamH));

	return err;
}

PF_Err createOneOfEveryInputType(
	PF_InData* in_data,
	PF_OutData* out_data,
	PF_ParamDef* params[],
	PF_LayerDef* output,
	int32_t index
)
{

	PF_ParamDef def;
	A_char name[32];

	PF_SPRINTF(name, "slider %d", index);
	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX(
		name,
		-10000,
		10000,
		0,
		1,
		0,
		2,
		PF_ValueDisplayFlag_NONE,
		PF_ParamFlag_COLLAPSE_TWIRLY,
		index + static_cast<uint32_t>(InputVariant::Float)
	);

	PF_SPRINTF(name, "int slider %d", index);
	AEFX_CLR_STRUCT(def);
	PF_ADD_SLIDER(
		name,
		-10000,
		10000,
		-100,
		100,
		0,
		index + static_cast<uint32_t>(InputVariant::Int)
	);

	A_char* names = new A_char[10];
	PF_SPRINTF(name, "select %d", index);
	PF_SPRINTF(names, "a|b|c");
	AEFX_CLR_STRUCT(def);
	PF_ADD_POPUPX(
		name,
		3,
		1,
		names,
		PF_ParamFlag_COLLAPSE_TWIRLY,
		index + static_cast<uint32_t>(InputVariant::IntList)
	);

	PF_SPRINTF(name, "cb %d", index);
	AEFX_CLR_STRUCT(def);
	PF_ADD_CHECKBOX(
		name,
		"",
		FALSE,
		PF_ParamFlag_COLLAPSE_TWIRLY,
		index + static_cast<uint32_t>(InputVariant::Bool)
	);

	PF_SPRINTF(name, "color %d", index);
	AEFX_CLR_STRUCT(def);
	def.flags |= PF_ParamFlag_COLLAPSE_TWIRLY;
	PF_ADD_COLOR(
		name, 1, 1, 1, index + static_cast<uint32_t>(InputVariant::Color)
	);

	PF_SPRINTF(name, "point %d", index);
	AEFX_CLR_STRUCT(def);
	def.flags |= PF_ParamFlag_COLLAPSE_TWIRLY;
	PF_ADD_POINT(
		name, 50L, 50L, 0, index + static_cast<uint32_t>(InputVariant::Point2d)
	);

	PF_SPRINTF(name, "image %d", index);
	AEFX_CLR_STRUCT(def);
	PF_ADD_LAYER(
		name,
		PF_LayerDefault_NONE,
		index + static_cast<uint32_t>(InputVariant::Image)
	);

	// TODO: audio

	return PF_Err_NONE;
}

#define RESET "\033[0m"
#define RED "\033[31m"
#define YELLOW "\033[33m"
#define BLUE "\033[34m"

void log(LogLevel level, const char* file, int line, const std::string& message)
{
	std::string levelStr;
	std::string color;

	switch( level )
	{
	case INFO:
		levelStr = "INFO";
		color = BLUE;
		break;
	case WARNING:
		levelStr = "WARNING";
		color = YELLOW;
		break;
	case ERROR:
		levelStr = "ERROR";
		color = RED;
		break;
	default:
		levelStr = "UNKNOWN";
		color = RESET;
		break;
	}

	std::cout << "[" << color << "Tweak Shader" << RESET << "][" << color
			  << levelStr << RESET << "] " << color << file << ":" << line
			  << ":" << RESET << " " << message << std::endl;
}

PF_Err setParamsToMatchSequence(
	PF_InData* in_data,
	const FfiSequenceData* sequence_data,
	PF_ParamDef* params[]
)
{
	PF_Err err = PF_Err_NONE;
	AEGP_SuiteHandler suites(in_data->pica_basicP);
	auto param_suite = suites.ParamUtilsSuite3();

	auto inputs = input_vec(sequence_data->rust_data);
	int num_user_inputs = static_cast<int>(inputs.size());
	bool first_image = true;

	// Rename the variants
	for( int i = 0; i < num_user_inputs; ++i )
	{
		auto& input = inputs[i];
		auto name = name_from_input(input);

		uint32_t variant = static_cast<uint32_t>(variant_from_input(input));

		uint32_t index
			= (i * NUM_INPUT_TYPES) + LOCK_TIME_TO_LAYER + variant + 1;

		auto& param = *params[index];
		PF_ParamDef new_param;
		AEFX_CLR_STRUCT(new_param);

		switch( param.param_type )
		{
		case PF_Param_FLOAT_SLIDER:
		{
			auto float_input = float_from_input(input);
			param.u.fs_d.valid_max = float_input.max;
			param.u.fs_d.valid_min = float_input.min;
			param.u.fs_d.slider_max = float_input.max;
			param.u.fs_d.slider_min = float_input.min;
			param.u.fs_d.value = float_input.current;
			param.u.fs_d.dephault = float_input.deflt;
		}
		break;
		case PF_Param_SLIDER:
		{
			auto int_input = int_from_input(input);
			param.u.sd.valid_max = int_input.max;
			param.u.sd.valid_min = int_input.min;
			param.u.sd.slider_max = int_input.max;
			param.u.sd.slider_min = int_input.min;
			param.u.sd.value = int_input.current;
			param.u.sd.dephault = int_input.deflt;
		}
		break;
		case PF_Param_POPUP:
		{
			auto int_list_input = int_list_from_input(input);
			param.u.pd.value = int_list_input.current;
			param.u.pd.num_choices = int_list_input.values.size();

			A_char* names = new A_char[int_list_input.names.length() + 1];

			PF_STRCPY(names, int_list_input.names.c_str());
			param.u.pd.u.namesptr = names;
		}
		break;
		case PF_Param_CHECKBOX:
		{
			auto bool_input = bool_from_input(input);
			param.u.bd.value = bool_input.current;
			param.u.bd.dephault = bool_input.deflt;
		}
		break;
		case PF_Param_COLOR:
		{
			auto color_input = color_from_input(input);
			param.u.cd.value.alpha = 255;
			param.u.cd.value.red
				= static_cast<A_u_char>(color_input.current[0] * 255);
			param.u.cd.value.green
				= static_cast<A_u_char>(color_input.current[1] * 255);
			param.u.cd.value.blue
				= static_cast<A_u_char>(color_input.current[2] * 255);

			param.u.cd.dephault.alpha = 255;
			param.u.cd.dephault.red
				= static_cast<A_u_char>(color_input.current[0] * 255);
			param.u.cd.dephault.green
				= static_cast<A_u_char>(color_input.current[1] * 255);
			param.u.cd.dephault.blue
				= static_cast<A_u_char>(color_input.current[2] * 255);
		}
		break;
		case PF_Param_POINT:
		{
			auto point_input = point_from_input(input);
			param.u.td.x_value = point_input.current[0];
			param.u.td.y_value = point_input.current[1];

			param.u.td.y_dephault = point_input.deflt[0];
			param.u.td.x_dephault = point_input.deflt[1];
		}
		break;
		case PF_Param_LAYER:
		{
			if( first_image )
			{
				param.u.ld.dephault = PF_LayerDefault_MYSELF;
				first_image = false;
			}
		}
		break;
		}

		param.uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;
		PF_STRCPY(param.name, name.data());

		suites.ParamUtilsSuite3()->PF_UpdateParamUI(
			in_data->effect_ref, index, &param
		);
	}

	return err;
}
