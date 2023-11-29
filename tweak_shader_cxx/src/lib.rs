mod input;
mod sequence_data;

use crate::input::Input;
use crate::sequence_data::{Pipelines, SequenceData};
use cxx::CxxVector;
use ffi::ImageInput;
use homedir::get_my_home;
use rfd::FileDialog;
use std::collections::BTreeMap;
use std::sync::RwLock;

use tweak_shader::{
    wgpu::{Device, Queue, TextureFormat},
    *,
};

const FORMATS: [wgpu::TextureFormat; 3] = [
    TextureFormat::Rgba8Unorm,
    TextureFormat::Rgba16Uint,
    TextureFormat::Rgba32Float,
];

struct GlobalData {
    device: Device,
    queue: Queue,
}

fn create_render_ctx() -> Box<GlobalData> {
    let instance = wgpu::Instance::default();

    let adapter = pollster::block_on(async {
        instance
            .request_adapter(&wgpu::RequestAdapterOptions {
                power_preference: wgpu::PowerPreference::HighPerformance,
                force_fallback_adapter: false,
                compatible_surface: None,
            })
            .await
            .expect("Failed to find an appropriate adapter")
    });
    let mut limits = wgpu::Limits::downlevel_webgl2_defaults().using_resolution(adapter.limits());
    limits.max_push_constant_size = 256;

    let (device, queue) = pollster::block_on(async {
        adapter
            .request_device(
                &wgpu::DeviceDescriptor {
                    label: None,
                    features: wgpu::Features::PUSH_CONSTANTS
                        | wgpu::Features::TEXTURE_FORMAT_16BIT_NORM,
                    limits,
                },
                None,
            )
            .await
            .expect("Failed to create device")
    });

    device.on_uncaptured_error(Box::new(|e| match e {
        wgpu::Error::OutOfMemory { .. } => {
            panic!("Out Of GPU Memory! bailing");
        }
        wgpu::Error::Validation {
            description,
            source,
        } => {
            panic!("{description} : {source}");
        }
    }));

    Box::new(GlobalData { device, queue })
}

fn render_to_slice(
    global_data: &Box<GlobalData>,
    seq_data: &Box<SequenceData>,
    render_data: ffi::RenderData,
    inputs: &Vec<Input>,
    image_inputs: &CxxVector<ImageInput>,
    bit_depth: u32,
    width: u32,
    height: u32,
    slice: &mut [u8],
) {
    seq_data.render_to_slice(
        &global_data.device,
        &global_data.queue,
        &FORMATS[bit_depth as usize],
        render_data,
        inputs,
        image_inputs,
        width,
        height,
        slice,
    );
}

fn update_bitdepth(seq_data: &Box<SequenceData>, global_data: &Box<GlobalData>, bit_depth: u32) {
    if seq_data.pipelines.read().unwrap().bit_depth != bit_depth {
        let to_ctx = if bit_depth == 1 {
            tweak_shader::RenderContext::new(
                include_str!("../resources/wgpu_to_ae_16.fs"),
                FORMATS[bit_depth as usize],
                &global_data.device,
                &global_data.queue,
            )
            .expect("gl to ae 16 bpc context failed to build")
        } else {
            tweak_shader::RenderContext::new(
                include_str!("../resources/wgpu_to_ae.fs"),
                FORMATS[bit_depth as usize],
                &global_data.device,
                &global_data.queue,
            )
            .expect("gl to ae context failed to build")
        };

        let fmt = if bit_depth == 1 {
            TextureFormat::Rgba16Float
        } else {
            FORMATS[bit_depth as usize]
        };

        let mut new_src = None;
        let ctx = if let Some(src) = seq_data.pipelines.read().unwrap().src.as_ref() {
            new_src = Some(src.to_owned());
            tweak_shader::RenderContext::new(src, fmt, &global_data.device, &global_data.queue)
                .unwrap()
        } else {
            tweak_shader::RenderContext::error_state(&global_data.device, &global_data.queue, fmt)
        };

        let from_ctx = tweak_shader::RenderContext::new(
            include_str!("../resources/ae_to_wgpu.fs"),
            fmt,
            &global_data.device,
            &global_data.queue,
        )
        .expect(" ae to gl context failed to build");

        let mut pipelines = seq_data.pipelines.write().unwrap();

        *pipelines = Pipelines {
            bit_depth,
            ctx,
            to_ctx,
            from_ctx,
            input_textures: BTreeMap::new(),
            target: None,
            staging_buffer: None,
            final_target: None,
            is_default: true,
            scene_was_reloaded: true,
            src: new_src,
        };
    }
}

fn input_vec(sequence_data: &Box<SequenceData>) -> Vec<Input> {
    let pipelines = sequence_data.pipelines.read().unwrap();
    pipelines
        .ctx
        .iter_inputs()
        .map(|(name, i)| Input {
            name: name.to_owned().clone(),
            inner: i.clone(),
        })
        .collect()
}

fn new_sequence_data(global_data: &Box<GlobalData>, bit_depth: u32) -> Box<SequenceData> {
    let to_ctx = if bit_depth == 1 {
        tweak_shader::RenderContext::new(
            include_str!("../resources/wgpu_to_ae_16.fs"),
            FORMATS[bit_depth as usize],
            &global_data.device,
            &global_data.queue,
        )
        .expect("gl to ae 16 bpc context failed to build")
    } else {
        tweak_shader::RenderContext::new(
            include_str!("../resources/wgpu_to_ae.fs"),
            FORMATS[bit_depth as usize],
            &global_data.device,
            &global_data.queue,
        )
        .expect("gl to ae context failed to build")
    };

    let ctx = if bit_depth == 1 {
        tweak_shader::RenderContext::error_state(
            &global_data.device,
            &global_data.queue,
            TextureFormat::Rgba16Float,
        )
    } else {
        tweak_shader::RenderContext::error_state(
            &global_data.device,
            &global_data.queue,
            FORMATS[bit_depth as usize],
        )
    };

    let from_ctx = if bit_depth == 1 {
        tweak_shader::RenderContext::new(
            include_str!("../resources/ae_to_wgpu.fs"),
            TextureFormat::Rgba16Float,
            &global_data.device,
            &global_data.queue,
        )
        .expect(" ae to gl context failed to build")
    } else {
        tweak_shader::RenderContext::new(
            include_str!("../resources/ae_to_wgpu.fs"),
            FORMATS[bit_depth as usize],
            &global_data.device,
            &global_data.queue,
        )
        .expect(" ae to gl context failed to build")
    };

    Box::new(SequenceData {
        pipelines: RwLock::new(Pipelines {
            ctx,
            from_ctx,
            bit_depth,
            input_textures: BTreeMap::new(),
            to_ctx,
            target: None,
            staging_buffer: None,
            final_target: None,
            is_default: true,
            scene_was_reloaded: true,
            src: None,
        }),
    })
}

fn is_default(sequence_data: &Box<SequenceData>) -> bool {
    sequence_data.pipelines.read().unwrap().is_default
}

fn scene_was_reloaded(sequence_data: &Box<SequenceData>) -> bool {
    let mut pipes = sequence_data.pipelines.write().unwrap();
    let load_val = pipes.scene_was_reloaded;
    pipes.scene_was_reloaded = false;
    load_val
}

fn source_string(sequence_data: &Box<SequenceData>) -> String {
    sequence_data
        .pipelines
        .read()
        .unwrap()
        .src
        .clone()
        .unwrap_or_default()
}

fn load_scene_from_source(
    global_data: &Box<GlobalData>,
    sequence_data: &Box<SequenceData>,
    src: &str,
) -> String {
    let mut pipelines = sequence_data.pipelines.write().unwrap();
    let bit_depth = pipelines.bit_depth;

    if Some(src) == pipelines.src.as_ref().map(|s| s.as_str()) {
        return String::new();
    }

    global_data
        .device
        .push_error_scope(wgpu::ErrorFilter::Validation);

    let ctx = if bit_depth == 1 {
        tweak_shader::RenderContext::new(
            src,
            TextureFormat::Rgba16Float,
            &global_data.device,
            &global_data.queue,
        )
    } else {
        tweak_shader::RenderContext::new(
            src,
            FORMATS[bit_depth as usize],
            &global_data.device,
            &global_data.queue,
        )
    };

    let err = pollster::block_on(global_data.device.pop_error_scope());

    pipelines.src = Some(src.to_owned());
    pipelines.input_textures.clear();
    match (err, ctx) {
        (None, Ok(ctx)) => {
            pipelines.ctx = ctx;
            pipelines.is_default = false;
            pipelines.scene_was_reloaded = true;
            String::new()
        }
        (None, Err(e)) => format!("{e}"),
        (Some(e), _) => format!("{e}"),
    }
}

fn load_scene(global_data: &Box<GlobalData>, sequence_data: &Box<SequenceData>) -> String {
    let home_dir = match get_my_home() {
        Ok(Some(home)) => home,
        _ => "/".into(),
    };

    let file = FileDialog::new()
        .add_filter("shader", &["glsl", "fs", "vs", "frag"])
        .set_directory(home_dir)
        .pick_file();

    let Some(Ok(src)) = file.map(|path| std::fs::read_to_string(path)) else {
        return String::new();
    };

    load_scene_from_source(global_data, sequence_data, &src)
}

fn has_image_input(sequence_data: &Box<SequenceData>) -> bool {
    sequence_data
        .pipelines
        .read()
        .unwrap()
        .ctx
        .iter_inputs()
        .any(|(_, i)| matches!(i, tweak_shader::input_type::InputType::Image(_)))
}

fn unload_scene(global_data: &Box<GlobalData>, sequence_data: &Box<SequenceData>) {
    let mut pipelines = sequence_data.pipelines.write().unwrap();
    let bit_depth = pipelines.bit_depth;

    let ctx = if bit_depth == 1 {
        tweak_shader::RenderContext::error_state(
            &global_data.device,
            &global_data.queue,
            TextureFormat::Rgba16Float,
        )
    } else {
        tweak_shader::RenderContext::error_state(
            &global_data.device,
            &global_data.queue,
            FORMATS[bit_depth as usize],
        )
    };

    pipelines.is_default = true;
    pipelines.scene_was_reloaded = true;
    pipelines.ctx = ctx;
    pipelines.src = None;
}

fn variant_from_input(input: &Input) -> ffi::InputVariant {
    input.variant()
}

fn color_from_input(input: &Input) -> ffi::ColorInput {
    input.as_color()
}

fn point_from_input(input: &Input) -> ffi::PointInput {
    input.as_point()
}

fn int_list_from_input(input: &Input) -> ffi::IntListInput {
    input.as_int_list()
}

fn image_is_loaded(input: &Input) -> bool {
    input.image_is_loaded()
}

fn int_from_input(input: &Input) -> ffi::IntInput {
    input.as_int()
}

fn bool_from_input(input: &Input) -> ffi::BoolInput {
    input.as_bool()
}

fn float_from_input(input: &Input) -> ffi::FloatInput {
    input.as_float()
}

fn name_from_input(input: &Input) -> &str {
    input.name()
}

fn set_point(input: &mut Input, p: [f32; 2]) {
    input.set_point(p);
}

fn set_bool(input: &mut Input, b: bool) {
    input.set_bool(b);
}

fn set_int_list(input: &mut Input, index: u32) {
    input.set_int_list(index);
}

fn set_color(input: &mut Input, f: [u8; 4]) {
    input.set_color([
        f[0] as f32 / 255.0 as f32,
        f[1] as f32 / 255.0 as f32,
        f[2] as f32 / 255.0 as f32,
        f[3] as f32 / 255.0 as f32,
    ]);
}

fn set_int(input: &mut Input, f: i32) {
    input.set_int(f);
}

fn set_float(input: &mut Input, f: f32) {
    input.set_float(f);
}

#[cxx::bridge]
mod ffi {

    enum InputVariant {
        Float,
        Int,
        IntList,
        Bool,
        Color,
        Point2d,
        Image,
        Audio,
        Unsupported,
    }

    #[derive(Debug)]
    pub struct ImageInput<'a> {
        name: &'a str,
        data: &'a [u8],
        width: u32,
        height: u32,
        bytes_per_row: u32,
        bit_depth: u32,
    }

    pub struct RenderData {
        pub time: u32,
        pub time_scale: u32,
        pub delta: u32,
    }

    #[derive(Debug, Clone)]
    pub struct FloatInput {
        pub current: f32,
        pub min: f32,
        pub max: f32,
        pub deflt: f32,
    }

    #[derive(Debug, Clone, Default)]
    pub struct IntListInput {
        pub names: String,
        pub values: Vec<i32>,
        pub current: i32,
    }

    #[derive(Debug, Clone, Default)]
    pub struct IntInput {
        pub current: i32,
        pub min: i32,
        pub max: i32,
        pub deflt: i32,
    }

    #[derive(Debug, Clone, Default)]
    pub struct PointInput {
        pub current: [f32; 2],
        pub min: [f32; 2],
        pub max: [f32; 2],
        pub deflt: [f32; 2],
    }

    #[derive(Debug, Clone, Default)]
    pub struct BoolInput {
        pub current: u32,
        pub deflt: u32,
    }

    #[derive(Debug, Clone, Default)]
    pub struct ColorInput {
        pub current: [f32; 4],
        pub deflt: [f32; 4],
    }

    extern "Rust" {
        type GlobalData;
        type SequenceData;
        type Input;

        fn load_scene_from_source(
            global_data: &Box<GlobalData>,
            sequence_data: &Box<SequenceData>,
            src: &str,
        ) -> String;

        fn variant_from_input(input: &Input) -> InputVariant;
        fn color_from_input(input: &Input) -> ColorInput;
        fn point_from_input(input: &Input) -> PointInput;
        fn int_list_from_input(input: &Input) -> IntListInput;
        fn int_from_input(input: &Input) -> IntInput;
        fn float_from_input(input: &Input) -> FloatInput;
        fn bool_from_input(input: &Input) -> BoolInput;
        fn name_from_input(input: &Input) -> &str;
        fn image_is_loaded(input: &Input) -> bool;
        fn has_image_input(sequence_data: &Box<SequenceData>) -> bool;

        fn set_point(input: &mut Input, p: [f32; 2]);
        fn set_int_list(input: &mut Input, index: u32);
        fn set_color(input: &mut Input, f: [u8; 4]);
        fn set_int(input: &mut Input, f: i32);
        fn set_float(input: &mut Input, f: f32);
        fn set_bool(input: &mut Input, b: bool);

        fn is_default(sequence_data: &Box<SequenceData>) -> bool;
        fn scene_was_reloaded(sequence_data: &Box<SequenceData>) -> bool;

        fn input_vec(sequence_data: &Box<SequenceData>) -> Vec<Input>;

        fn unload_scene(global_data: &Box<GlobalData>, sequence_data: &Box<SequenceData>);

        fn load_scene(global_data: &Box<GlobalData>, sequence_data: &Box<SequenceData>) -> String;

        fn new_sequence_data(global_data: &Box<GlobalData>, bit_depth: u32) -> Box<SequenceData>;

        fn source_string(sequence_data: &Box<SequenceData>) -> String;

        fn update_bitdepth(
            seq_data: &Box<SequenceData>,
            global_data: &Box<GlobalData>,
            bit_depth: u32,
        );

        fn create_render_ctx() -> Box<GlobalData>;

        fn render_to_slice(
            ctx: &Box<GlobalData>,
            seq_data: &Box<SequenceData>,
            render_data: RenderData,
            inputs: &Vec<Input>,
            image_inputs: &CxxVector<ImageInput>,
            bit_depth: u32,
            width: u32,
            height: u32,
            slice: &mut [u8],
        );
    }
}
