#include "tweak_shader.h"
#include "AEFX_SuiteHelper.h"
#include "cxx.h"
#include "misc_util.h"
#include "src/lib.rs.h"
#include "Param_Utils.h"
#include "Smart_Utils.h"

#include <cassert>
#include <limits>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <algorithm>
#include <sstream>

static PF_Err About(
	PF_InData* in_data,
	PF_OutData* out_data,
	PF_ParamDef* params[],
	PF_LayerDef* output
)
{
	AEGP_SuiteHandler suites(in_data->pica_basicP);

	suites.ANSICallbacksSuite1()->sprintf(
		out_data->return_msg,
		"%s v%d.%d\r%s",
		"Tweak Shader",
		MAJOR_VERSION,
		MINOR_VERSION,
		"Tweak Shader plugin, exposing a fexible shader format"
	);
	return PF_Err_NONE;
}

static PF_Err GlobalSetup(
	PF_InData* in_data,
	PF_OutData* out_data,
	PF_ParamDef* params[],
	PF_LayerDef* output
)
{
	out_data->my_version = PF_VERSION(
		MAJOR_VERSION, MINOR_VERSION, BUG_VERSION, STAGE_VERSION, BUILD_VERSION
	);

	out_data->out_flags = PF_OutFlag_DEEP_COLOR_AWARE | PF_OutFlag_I_DO_DIALOG
						| PF_OutFlag_NON_PARAM_VARY
						| PF_OutFlag_SEND_UPDATE_PARAMS_UI
						| PF_OutFlag_CUSTOM_UI;

	out_data->out_flags2 = PF_OutFlag2_FLOAT_COLOR_AWARE
						 | PF_OutFlag2_SUPPORTS_SMART_RENDER
						 | PF_OutFlag2_SUPPORTS_QUERY_DYNAMIC_FLAGS
						 | PF_OutFlag2_SUPPORTS_THREADED_RENDERING;

	AEGP_SuiteHandler suites(in_data->pica_basicP);

	PF_Handle global_data_handle
		= suites.HandleSuite1()->host_new_handle(sizeof(FfiGlobalData));

	if( !global_data_handle )
	{
		return PF_Err_OUT_OF_MEMORY;
	}

	out_data->global_data = global_data_handle;

	auto* data = reinterpret_cast<FfiGlobalData*>(
		suites.HandleSuite1()->host_lock_handle(global_data_handle)
	);

	AEFX_CLR_STRUCT(*data);

	data->rust_data = create_render_ctx();

	suites.HandleSuite1()->host_unlock_handle(out_data->global_data);

	return PF_Err_NONE;
}

static PF_Err GlobalSetdown(
	PF_InData* in_data,
	PF_OutData* out_data,
	PF_ParamDef* params[],
	PF_LayerDef* output
)
{
	PF_Err err = PF_Err_NONE;

	AEGP_SuiteHandler suites(in_data->pica_basicP);

	auto* sequence_data = reinterpret_cast<FfiGlobalData*>(
		suites.HandleSuite1()->host_lock_handle(in_data->global_data)
	);

	sequence_data->rust_data.~Box<GlobalData>();
	suites.HandleSuite1()->host_dispose_handle(in_data->global_data);

	return err;
}

static PF_Err ParamsSetup(
	PF_InData* in_data,
	PF_OutData* out_data,
	PF_ParamDef* params[],
	PF_LayerDef* output
)
{
	PF_Err err = PF_Err_NONE;
	AEGP_SuiteHandler suites(in_data->pica_basicP);

	auto* global_data = reinterpret_cast<FfiGlobalData*>(
		suites.HandleSuite1()->host_lock_handle(in_data->global_data)
	);

	PF_ParamDef def;

	AEFX_CLR_STRUCT(def);
	PF_ADD_BUTTON(
		"Source",
		"Load Source",
		0,
		PF_ParamFlag_SUPERVISE | PF_ParamFlag_CANNOT_TIME_VARY
			| PF_ParamFlag_COLLAPSE_TWIRLY,
		Params::TWEAK_SOURCE
	);

	AEFX_CLR_STRUCT(def);
	PF_ADD_BUTTON(
		"Unload",
		"Unload Source",
		0,
		PF_ParamFlag_SUPERVISE | PF_ParamFlag_CANNOT_TIME_VARY
			| PF_ParamFlag_COLLAPSE_TWIRLY,
		Params::UNLOAD_SOURCE
	);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX(
		"time",
		0,
		1000,
		0,
		1000,
		0,
		2,
		PF_ValueDisplayFlag_NONE,
		PF_ParamFlag_COLLAPSE_TWIRLY,
		Params::TIME
	);

	AEFX_CLR_STRUCT(def);
	PF_ADD_CHECKBOXX(
		"Is Image Filter",
		TRUE,
		PF_ParamFlag_SUPERVISE | PF_ParamFlag_CANNOT_TIME_VARY
			| PF_ParamFlag_COLLAPSE_TWIRLY,
		Params::IS_FILTER
	);

	AEFX_CLR_STRUCT(def);
	PF_ADD_CHECKBOXX(
		"Use Layer Time",
		TRUE,
		PF_ParamFlag_SUPERVISE | PF_ParamFlag_CANNOT_TIME_VARY
			| PF_ParamFlag_COLLAPSE_TWIRLY,
		Params::LOCK_TIME_TO_LAYER
	);

	A_char name[32];
	for( int i = 0; i < MAX_PARAMS; i++ )
	{
		uint32_t row_start = (i * NUM_INPUT_TYPES) + LOCK_TIME_TO_LAYER;
		createOneOfEveryInputType(in_data, out_data, params, output, row_start);
	}

	suites.HandleSuite1()->host_unlock_handle(in_data->global_data);

	out_data->num_params = TWEAK_NUM_PARAMS + (MAX_PARAMS * NUM_INPUT_TYPES);
	return err;
}
static PF_Err UserChangedParam(
	PF_InData* in_data,
	PF_OutData* out_data,
	PF_ParamDef* params[],
	PF_LayerDef* output,
	PF_UserChangedParamExtra* extra
)
{
	PF_Err err = PF_Err_NONE;
	AEGP_SuiteHandler suites(in_data->pica_basicP);
	auto param_suite = suites.ParamUtilsSuite3();

	auto seq_suite = AEFX_SuiteScoper<PF_EffectSequenceDataSuite1>(
		in_data,
		kPFEffectSequenceDataSuite,
		kPFEffectSequenceDataSuiteVersion1,
		out_data
	);

	PF_ConstHandle const_seq = {};

	ERR(seq_suite->PF_GetConstSequenceData(in_data->effect_ref, &const_seq));
	auto sequence_data = reinterpret_cast<const FfiSequenceData*>(*const_seq);

	auto* global_data = reinterpret_cast<FfiGlobalData*>(
		suites.HandleSuite1()->host_lock_handle(in_data->global_data)
	);

	auto inputs = input_vec(sequence_data->rust_data);
	int num_user_inputs = static_cast<int>(inputs.size());

	switch( extra->param_index )
	{
	case Params::IS_FILTER:
	{

		auto inputs = input_vec(sequence_data->rust_data);
		int num_user_inputs = static_cast<int>(inputs.size());

		for( int i = 0; i < num_user_inputs; i++ )
		{

			auto& input = inputs[i];
			auto name = name_from_input(input);

			uint32_t variant = static_cast<uint32_t>(variant_from_input(input));

			uint32_t index
				= (i * NUM_INPUT_TYPES) + LOCK_TIME_TO_LAYER + variant + 1;

			PF_ParamDef* param_ref = params[index];

			if( variant == static_cast<uint32_t>(InputVariant::Image) )
			{
				if( params[Params::IS_FILTER]->u.bd.value == 1 )
				{
					setParamVisibility(PLUGIN_ID, in_data, index, false);
					param_ref->u.ld.dephault = PF_LayerDefault_MYSELF;
					out_data->out_flags |= PF_OutFlag_FORCE_RERENDER;
				}
				else
				{
					setParamVisibility(PLUGIN_ID, in_data, index, true);
					param_ref->u.ld.dephault = PF_LayerDefault_NONE;
					out_data->out_flags |= PF_OutFlag_FORCE_RERENDER;
				}
				break;
			}
		}
	}
	break;
	case Params::UNLOAD_SOURCE:
		out_data->out_flags |= PF_OutFlag_FORCE_RERENDER;
		unload_scene(global_data->rust_data, sequence_data->rust_data);
		// Simulate a change to force the UI to reload
		params[LOCK_TIME_TO_LAYER]->uu.change_flags
			|= PF_ChangeFlag_CHANGED_VALUE;
		break;
	case Params::TWEAK_SOURCE:
		// The scene is now invalid
		out_data->out_flags |= PF_OutFlag_FORCE_RERENDER;
		// Simulate a change to force the UI to reload
		params[LOCK_TIME_TO_LAYER]->uu.change_flags
			|= PF_ChangeFlag_CHANGED_VALUE;
		// Opens a file dialog
		rust::String err
			= load_scene(global_data->rust_data, sequence_data->rust_data);

		if( err.size() != 0 )
		{
			memcpy(
				out_data->return_msg,
				err.c_str(),
				std::min(rust::usize(256), err.size())
			);
			out_data->out_flags |= PF_OutFlag_DISPLAY_ERROR_MESSAGE;
		}
		setParamsToMatchSequence(in_data, sequence_data, params);
		break;
	}

	return err;
}

// makes inputs visible and invisible
static PF_Err UpdateParamsUI(
	PF_InData* in_data,
	PF_OutData* out_data,
	PF_ParamDef* params[],
	PF_LayerDef* output
)
{
	PF_Err err = PF_Err_NONE;
	AEGP_SuiteHandler suites(in_data->pica_basicP);

	auto seq_suite = AEFX_SuiteScoper<PF_EffectSequenceDataSuite1>(
		in_data,
		kPFEffectSequenceDataSuite,
		kPFEffectSequenceDataSuiteVersion1,
		out_data
	);

	PF_ConstHandle const_seq = {};
	ERR(seq_suite->PF_GetConstSequenceData(in_data->effect_ref, &const_seq));
	auto sequence_data = reinterpret_cast<const FfiSequenceData*>(*const_seq);

	if( !sequence_data || err != PF_Err_NONE )
	{
		LOG(ERROR, "Sequence Data NULL!");
		return err;
	}

	setParamVisibility(
		PLUGIN_ID, in_data, IS_FILTER, has_image_input(sequence_data->rust_data)
	);
	if( is_default(sequence_data->rust_data) )
	{
		setParamVisibility(PLUGIN_ID, in_data, LOCK_TIME_TO_LAYER, false);
		setParamVisibility(PLUGIN_ID, in_data, UNLOAD_SOURCE, false);
		setParamVisibility(PLUGIN_ID, in_data, TIME, false);
		setParamVisibility(PLUGIN_ID, in_data, TWEAK_SOURCE, true);
	}
	else
	{
		setParamVisibility(PLUGIN_ID, in_data, LOCK_TIME_TO_LAYER, true);
		setParamVisibility(PLUGIN_ID, in_data, UNLOAD_SOURCE, true);
		bool show_time = params[LOCK_TIME_TO_LAYER]->u.bd.value == 0;
		setParamVisibility(PLUGIN_ID, in_data, TIME, show_time);
		setParamVisibility(PLUGIN_ID, in_data, TWEAK_SOURCE, false);
	}

	if( scene_was_reloaded(sequence_data->rust_data) )
	{
		auto inputs = input_vec(sequence_data->rust_data);
		int num_user_inputs = static_cast<int>(inputs.size());

		A_char name[32];
		PF_ParamDef def;
		AEFX_CLR_STRUCT(def);

		for( int i = 1; i <= (MAX_PARAMS * NUM_INPUT_TYPES); i++ )
		{
			setParamVisibility(
				PLUGIN_ID, in_data, LOCK_TIME_TO_LAYER + i, false
			);
		}

		bool first_image = true;

		for( int i = 0; i < num_user_inputs; i++ )
		{
			auto& input = inputs[i];
			uint32_t variant = static_cast<uint32_t>(variant_from_input(input));
			uint32_t index
				= (i * NUM_INPUT_TYPES) + LOCK_TIME_TO_LAYER + variant + 1;

			// keep the first layer invisible if it's a filter
			if( params[IS_FILTER]->u.bd.value == 1
				&& variant == static_cast<uint32_t>(InputVariant::Image)
				&& first_image )
			{
				first_image = false;
				continue;
			}

			setParamVisibility(PLUGIN_ID, in_data, index, true);
		}

		setParamsToMatchSequence(in_data, sequence_data, params);
	}

	return err;
}

// Smart Pre rendering allows the user to communicate to after effects
// what portion of the effected layer they will be modifying. In many
// cases this means making an AABB around an inverted mask. We just
// use the whole layer. You need smart render to render to layers with
// floating point color depth though so we have to go through
// this dance anyways. This should be named "CullingStep"
static PF_Err SmartPreRender(
	PF_InData* in_data, PF_OutData* out_data, PF_PreRenderExtra* extra
)
{
	AEGP_SuiteHandler suites(in_data->pica_basicP);

	PF_Err err = PF_Err_NONE;

	PF_RenderRequest req = extra->input->output_request;
	req.preserve_rgb_of_zero_alpha = true;
	req.channel_mask = PF_ChannelMask_ARGB;
	req.field = PF_Field_FRAME;

	PF_CheckoutResult checkout_result;

	// checkout the input layer, just to get it's max size
	ERR(extra->cb->checkout_layer(
		in_data->effect_ref,
		0,
		1320,
		&req,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&checkout_result
	));

	PF_RenderRequest full_req = req;

	full_req.rect = checkout_result.max_result_rect;
	full_req.field = PF_Field_FRAME;
	full_req.preserve_rgb_of_zero_alpha = true;
	full_req.channel_mask = PF_ChannelMask_ARGB;

	// check it out again, with it's max size
	ERR(extra->cb->checkout_layer(
		in_data->effect_ref,
		0,
		INPUT_LAYER_ID,
		&full_req,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&checkout_result
	));

	auto seq_suite = AEFX_SuiteScoper<PF_EffectSequenceDataSuite1>(
		in_data,
		kPFEffectSequenceDataSuite,
		kPFEffectSequenceDataSuiteVersion1,
		out_data
	);

	PF_ConstHandle const_seq = {};
	ERR(seq_suite->PF_GetConstSequenceData(in_data->effect_ref, &const_seq));

	const auto* sequence_data
		= reinterpret_cast<const FfiSequenceData*>(*const_seq);
	auto vec = input_vec(sequence_data->rust_data);

	for( uint32_t i = 0; i < vec.size(); i++ )
	{
		auto& input = vec[i];
		if( variant_from_input(input) == InputVariant::Image )
		{
			PF_CheckoutResult res;

			uint32_t index = (i * NUM_INPUT_TYPES) + LOCK_TIME_TO_LAYER
						   + static_cast<uint32_t>(InputVariant::Image) + 1;

			PF_RenderRequest req = extra->input->output_request;

			ERR(extra->cb->checkout_layer(
				in_data->effect_ref,
				index,
				index,
				&req,
				in_data->current_time,
				in_data->time_step,
				in_data->time_scale,
				&res
			));
		}
	}

	PF_PreRenderOutput* output = extra->output;

	output->result_rect = checkout_result.result_rect;
	output->max_result_rect = checkout_result.result_rect;
	output->flags = PF_RenderOutputFlag_RETURNS_EXTRA_PIXELS;

	return err;
}

static PF_Err SequenceResetup(
	PF_InData* in_data,
	PF_OutData* out_data,
	PF_ParamDef* params[],
	PF_LayerDef* output
)
{
	PF_Err err = PF_Err_NONE;
	AEGP_SuiteHandler suites(in_data->pica_basicP);

	void* in_seq_data
		= suites.HandleSuite1()->host_lock_handle(in_data->sequence_data);

	if( !in_seq_data )
	{
		LOG(INFO, "In Sequence Data Null");
		return err;
	}

	PF_Handle new_sequence_data_handle
		= suites.HandleSuite1()->host_new_handle(sizeof(FfiSequenceData));

	if( !new_sequence_data_handle )
	{
		return PF_Err_OUT_OF_MEMORY;
	}

	auto* global_data = reinterpret_cast<FfiGlobalData*>(
		suites.HandleSuite1()->host_lock_handle(in_data->global_data)
	);

	auto* out_sequence_data = reinterpret_cast<FfiSequenceData*>(
		suites.HandleSuite1()->host_lock_handle(new_sequence_data_handle)
	);

	out_sequence_data->rust_data = new_sequence_data(global_data->rust_data, 0);
	out_sequence_data->is_flat = false;
	out_data->sequence_data = new_sequence_data_handle;

	auto* maybe_flat = reinterpret_cast<SequenceDataFlat*>(in_seq_data);

	if( maybe_flat->is_flat )
	{
		if( maybe_flat->run_length != 0 )
		{
			std::string source(maybe_flat->source, maybe_flat->run_length);
			// Don't show errors here.
			load_scene_from_source(
				global_data->rust_data, out_sequence_data->rust_data, source
			);
		}
	}

	suites.HandleSuite1()->host_unlock_handle(in_data->sequence_data);
	suites.HandleSuite1()->host_unlock_handle(in_data->global_data);
	suites.HandleSuite1()->host_unlock_handle(new_sequence_data_handle);
	return err;
}

static PF_Err SequenceFlatten(
	PF_InData* in_data,
	PF_OutData* out_data,
	PF_ParamDef* params[],
	PF_LayerDef* output
)
{
	PF_Err err = PF_Err_NONE;
	AEGP_SuiteHandler suites(in_data->pica_basicP);

	auto* in_sequence_data = reinterpret_cast<FfiSequenceData*>(
		suites.HandleSuite1()->host_lock_handle(in_data->sequence_data)
	);

	rust::String src = source_string(in_sequence_data->rust_data);

	PF_Handle flat_data_handle = suites.HandleSuite1()->host_new_handle(
		sizeof(SequenceDataFlat) + src.size()
	);

	auto* out_sequence_data = reinterpret_cast<SequenceDataFlat*>(
		suites.HandleSuite1()->host_lock_handle(flat_data_handle)
	);

	if( !in_sequence_data || !out_sequence_data )
	{
		return err;
	}

	new(out_sequence_data) SequenceDataFlat();

	std::memcpy(out_sequence_data->source, src.data(), src.size());
	out_sequence_data->run_length = src.size();

	out_data->sequence_data = flat_data_handle;

	suites.HandleSuite1()->host_unlock_handle(in_data->sequence_data);
	suites.HandleSuite1()->host_unlock_handle(flat_data_handle);

	return err;
}

static PF_Err SequenceSetup(
	PF_InData* in_data,
	PF_OutData* out_data,
	PF_ParamDef* params[],
	PF_LayerDef* output
)
{
	PF_Err err = PF_Err_NONE;
	AEGP_SuiteHandler suites(in_data->pica_basicP);

	PF_Handle sequence_data_handle
		= suites.HandleSuite1()->host_new_handle(sizeof(FfiSequenceData));

	if( !sequence_data_handle )
	{
		return PF_Err_OUT_OF_MEMORY;
	}

	auto* global_data = reinterpret_cast<FfiGlobalData*>(
		suites.HandleSuite1()->host_lock_handle(in_data->global_data)
	);

	auto* sequence_data = reinterpret_cast<FfiSequenceData*>(
		suites.HandleSuite1()->host_lock_handle(sequence_data_handle)
	);

	AEFX_CLR_STRUCT(*sequence_data);

	sequence_data->rust_data = new_sequence_data(global_data->rust_data, 0);
	sequence_data->is_flat = false;

	out_data->sequence_data = sequence_data_handle;

	suites.HandleSuite1()->host_unlock_handle(sequence_data_handle);
	suites.HandleSuite1()->host_unlock_handle(in_data->global_data);

	return err;
}

static PF_Err SequenceSetdown(
	PF_InData* in_data,
	PF_OutData* out_data,
	PF_ParamDef* params[],
	PF_LayerDef* output
)
{
	PF_Err err = PF_Err_NONE;
	AEGP_SuiteHandler suites(in_data->pica_basicP);

	auto* sequence_data = reinterpret_cast<FfiSequenceData*>(
		suites.HandleSuite1()->host_lock_handle(in_data->sequence_data)
	);

	if( !sequence_data->is_flat )
	{
		sequence_data->rust_data.~Box<SequenceData>();
	}

	for( size_t i = 0; i < in_data->num_params; i++ )
	{
		auto param = params[i];
		if( param->param_type == PF_Param_POPUP )
		{
			// I malloced all these so that this was safe to do.
			// The API just expects you to keep all this data in
			// read only memory...
			free((void*)param->u.pd.u.namesptr);
		}
	}

	suites.HandleSuite1()->host_dispose_handle(in_data->sequence_data);

	return err;
}

static PF_Err SmartRender(
	PF_InData* in_data, PF_OutData* out_data, PF_SmartRenderExtra* extra
)
{
	AEGP_SuiteHandler suites(in_data->pica_basicP);
	PF_Err err = PF_Err_NONE;

	auto* global_data = reinterpret_cast<FfiGlobalData*>(
		suites.HandleSuite1()->host_lock_handle(in_data->global_data)
	);

	auto seq_suite = AEFX_SuiteScoper<PF_EffectSequenceDataSuite1>(
		in_data,
		kPFEffectSequenceDataSuite,
		kPFEffectSequenceDataSuiteVersion1,
		out_data
	);

	PF_ConstHandle const_seq = {};
	PF_EffectWorld* input_layer = {};
	PF_EffectWorld* output_layer = {};

	ERR(seq_suite->PF_GetConstSequenceData(in_data->effect_ref, &const_seq));
	ERR(extra->cb->checkout_layer_pixels(
		in_data->effect_ref, INPUT_LAYER_ID, &input_layer
	));
	ERR(extra->cb->checkout_output(in_data->effect_ref, &output_layer));

	const auto* sequence_data
		= reinterpret_cast<const FfiSequenceData*>(*const_seq);

	if( !global_data || !sequence_data || !output_layer || !input_layer
		|| err != PF_Err_NONE )
	{
		return err;
	}

	PF_ParamDef is_filter;
	bool b_is_filter = false;
	bool is_first_image = true;
	ERR(PF_CHECKOUT_PARAM(
		in_data,
		IS_FILTER,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&is_filter
	));

	b_is_filter = is_filter.u.bd.value == 1;

	ERR(PF_CHECKIN_PARAM(in_data, &is_filter));

	// This is lazy don't worry
	update_bitdepth(
		sequence_data->rust_data,
		global_data->rust_data,
		extra->input->bitdepth / 16
	);

	auto inputs = input_vec(sequence_data->rust_data);
	auto layer_data_vec = std::vector<ImageInput>();
	int num_user_inputs = static_cast<int>(inputs.size());

	// Update the scene paramaters
	for( int i = 0; i < num_user_inputs; i++ )
	{
		auto& input = inputs[i];
		uint32_t variant = static_cast<uint32_t>(variant_from_input(input));
		uint32_t index
			= (i * NUM_INPUT_TYPES) + LOCK_TIME_TO_LAYER + variant + 1;

		PF_ParamDef param;
		AEFX_CLR_STRUCT(param);
		ERR(PF_CHECKOUT_PARAM(
			in_data,
			index,
			in_data->current_time,
			in_data->time_step,
			in_data->time_scale,
			&param
		));

		switch( param.param_type )
		{
		case PF_Param_FLOAT_SLIDER:
			set_float(input, param.u.fs_d.value);
			break;
		case PF_Param_SLIDER:
			set_int(input, param.u.fd.value);
			break;
		case PF_Param_POPUP:
			set_int_list(input, param.u.pd.value);
			break;
		case PF_Param_CHECKBOX:
			set_bool(input, param.u.bd.value);
			break;
		case PF_Param_COLOR:
			std::array<uint8_t, 4> color;
			color[0] = param.u.cd.value.red;
			color[1] = param.u.cd.value.green;
			color[2] = param.u.cd.value.blue;
			color[3] = param.u.cd.value.alpha;
			set_color(input, color);
			break;
		case PF_Param_POINT:
			std::array<float, 2> point;
			color[0] = param.u.td.x_value;
			color[1] = param.u.td.y_value;
			set_point(input, point);
			break;
		case PF_Param_LAYER:
			PF_LayerDef* layer = &param.u.ld;
			auto name_str = name_from_input(input);

			// first image in filters is image data
			if( b_is_filter && is_first_image )
			{
				layer = input_layer;
				is_first_image = false;
			}
			else
			{
				ERR(extra->cb->checkout_layer_pixels(
					in_data->effect_ref, index, &layer
				));
			}

			if( !layer )
			{
				clear_image_input(sequence_data->rust_data, input);
				break;
			}

			auto data = rust::Slice<const uint8_t>(
				reinterpret_cast<uint8_t*>(layer->data),
				layer->rowbytes * layer->height
			);

			layer_data_vec.emplace_back(ImageInput{
				.name = name_str,
				.data = data,
				.width = static_cast<rust::u32>(layer->width),
				.height = static_cast<rust::u32>(layer->height),
				.bytes_per_row = static_cast<rust::u32>(layer->rowbytes),
				.bit_depth
				= static_cast<rust::u32>((layer->rowbytes / layer->width) / 8),
			});
			break;
		}

		ERR(PF_CHECKIN_PARAM(in_data, &param));
	}

	PF_ParamDef param;
	AEFX_CLR_STRUCT(param);
	ERR(PF_CHECKOUT_PARAM(
		in_data,
		LOCK_TIME_TO_LAYER,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&param
	));

	bool use_current_time = param.u.bd.value == 1;

	ERR(PF_CHECKIN_PARAM(in_data, &param));

	uint32_t time = 0;
	if( use_current_time )
	{
		time = static_cast<uint32_t>(in_data->current_time);
	}
	else
	{
		AEFX_CLR_STRUCT(param);
		ERR(PF_CHECKOUT_PARAM(
			in_data,
			TIME,
			in_data->current_time,
			in_data->time_step,
			in_data->time_scale,
			&param
		));

		time = param.u.fs_d.value * in_data->time_scale;

		ERR(PF_CHECKIN_PARAM(in_data, &param));
	}

	RenderData render_data = RenderData{
		.time = time,
		.time_scale = static_cast<uint32_t>(in_data->time_scale),
		.delta = static_cast<uint32_t>(in_data->time_step),
	};

	size_t data_len = output_layer->rowbytes * output_layer->height;
	auto ptr = reinterpret_cast<uint8_t*>(output_layer->data);
	auto slice = rust::slice<uint8_t>(ptr, data_len);

	// bytes per pixel = bits per channel * 4 (channels) / 8 bits per byte
	int bytes_per_pixel = extra->input->bitdepth / 2;

	render_to_slice(
		global_data->rust_data,
		sequence_data->rust_data,
		render_data,
		inputs,
		layer_data_vec,
		extra->input->bitdepth / 16,
		output_layer->rowbytes / bytes_per_pixel,
		output_layer->height,
		slice
	);

	suites.HandleSuite1()->host_unlock_handle(in_data->global_data);

	return err;
}

extern "C" DllExport PF_Err PluginDataEntryFunction2(
	PF_PluginDataPtr inPtr,
	PF_PluginDataCB2 inPluginDataCallBackPtr,
	SPBasicSuite* inSPBasicSuitePtr,
	const char* inHostName,
	const char* inHostVersion
)
{
	PF_Err result = PF_Err_INVALID_CALLBACK;

	result = PF_REGISTER_EFFECT_EXT2(
		inPtr,
		inPluginDataCallBackPtr,
		"Tweak Shader",   // Name
		"Tweak Shader",   // Match Name
		"Shader",         // Category
		AE_RESERVED_INFO, // Reserved Info
		"EffectMain",     // Entry point
		"www.concentratedbursts.netlify.com"
	); // support URL

	return result;
}

PF_Err EffectMain(
	PF_Cmd cmd,
	PF_InData* in_data,
	PF_OutData* out_data,
	PF_ParamDef* params[],
	PF_LayerDef* output,
	void* extra
)
{
	PF_Err err = PF_Err_NONE;

	try
	{
		switch( cmd )
		{
			// Display tooltip
		case PF_Cmd_ABOUT:

			err = About(in_data, out_data, params, output);
			break;

			// Set up bookeeping
		case PF_Cmd_GLOBAL_SETUP:

			err = GlobalSetup(in_data, out_data, params, output);
			break;

			// free global data
		case PF_Cmd_GLOBAL_SETDOWN:
			err = GlobalSetdown(in_data, out_data, params, output);
			break;

			// Sets up a list of unions to pull user input from.
		case PF_Cmd_PARAMS_SETUP:

			err = ParamsSetup(in_data, out_data, params, output);
			break;

			// sets up effect instance local data.
		case PF_Cmd_SEQUENCE_SETUP:
			err = SequenceSetup(in_data, out_data, params, output);
			break;

		case PF_Cmd_UPDATE_PARAMS_UI:
			err = UpdateParamsUI(in_data, out_data, params, output);
			break;
			// called desiaralizing from disk, duplicating an effect...
			// everywhere
		case PF_Cmd_SEQUENCE_RESETUP:
			return SequenceResetup(in_data, out_data, params, output);
			// called before saving, duplicating, copying
		case PF_Cmd_SEQUENCE_FLATTEN:
			return SequenceFlatten(in_data, out_data, params, output);
			// Free up the sequence data
		case PF_Cmd_SEQUENCE_SETDOWN:
			err = SequenceSetdown(in_data, out_data, params, output);
			break;
		case PF_Cmd_USER_CHANGED_PARAM:
			err = UserChangedParam(
				in_data,
				out_data,
				params,
				output,
				(PF_UserChangedParamExtra*)extra
			);
			break;
			// Cull step (useless to me)
		case PF_Cmd_SMART_PRE_RENDER:
			err = SmartPreRender(
				in_data, out_data, reinterpret_cast<PF_PreRenderExtra*>(extra)
			);
			break;
			// multiframe render
		case PF_Cmd_SMART_RENDER:
			err = SmartRender(
				in_data, out_data, reinterpret_cast<PF_SmartRenderExtra*>(extra)
			);
			break;
		}
	}
	catch( PF_Err& thrown_err )
	{
		err = thrown_err;
	}
	return err;
}
