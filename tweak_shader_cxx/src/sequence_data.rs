use std::collections::BTreeMap;
use std::sync::RwLock;

use tweak_shader::{wgpu::TextureFormat, *};

use crate::ffi::ImageInput;

pub struct Pipelines {
    pub ctx: tweak_shader::RenderContext,
    pub from_ctx: tweak_shader::RenderContext,
    pub to_ctx: tweak_shader::RenderContext,
    pub input_textures: BTreeMap<String, wgpu::Texture>,
    pub staging_buffer: Option<wgpu::Buffer>,
    pub target: Option<wgpu::Texture>,
    pub final_target: Option<wgpu::Texture>,
    pub bit_depth: u32,
    pub is_default: bool,
    pub scene_was_reloaded: bool,
    pub src: Option<String>,
}

pub struct SequenceData {
    pub pipelines: RwLock<Pipelines>,
}

impl SequenceData {
    pub fn render_to_slice(
        &self,
        device: &wgpu::Device,
        queue: &wgpu::Queue,
        format: &wgpu::TextureFormat,
        render_data: super::ffi::RenderData,
        inputs: &Vec<super::input::Input>,
        image_inputs: &cxx::CxxVector<ImageInput>,
        width: u32,
        height: u32,
        slice: &mut [u8],
    ) {
        let mut pipe = self.pipelines.write().unwrap();

        let Pipelines {
            ctx,
            to_ctx,
            staging_buffer,
            target,
            final_target,
            bit_depth,
            input_textures,
            from_ctx,
            ..
        } = &mut *pipe;

        if !target
            .as_ref()
            .is_some_and(|t| t.width() == width && t.height() == height)
        {
            let new_format = if *bit_depth == 1 {
                TextureFormat::Rgba16Float
            } else {
                *format
            };

            let tex = device.create_texture(&target_desc(width, height, new_format));
            to_ctx.load_shared_texture(&tex, "input_image");
            *target = Some(tex);

            let final_tex = device.create_texture(&target_desc(width, height, *format));
            *final_target = Some(final_tex);
        };

        let block_size = format.block_size(Some(wgpu::TextureAspect::All)).unwrap();
        let row_byte_ct = block_size * width;
        let padded_row_byte_ct = (row_byte_ct + 255) & !255;

        if !staging_buffer
            .as_ref()
            .is_some_and(|b| b.size() as u32 == height * padded_row_byte_ct)
        {
            // Create a buffer to store the texture data
            *staging_buffer = Some(device.create_buffer(&wgpu::BufferDescriptor {
                label: Some("Texture Read Buffer"),
                size: (height * padded_row_byte_ct) as u64,
                usage: wgpu::BufferUsages::MAP_READ | wgpu::BufferUsages::COPY_DST,
                mapped_at_creation: false,
            }));
        }

        let out_tex = target.as_ref().unwrap().create_view(&Default::default());

        let final_tex = final_target
            .as_ref()
            .unwrap()
            .create_view(&Default::default());

        // Update resolutions
        to_ctx
            .get_input_mut("height")
            .unwrap()
            .as_float()
            .unwrap()
            .current = height as f32;

        to_ctx
            .get_input_mut("width")
            .unwrap()
            .as_float()
            .unwrap()
            .current = width as f32;

        ctx.update_resolution([width as f32, height as f32]);
        let time = render_data.time as f32 / render_data.time_scale as f32;
        ctx.update_time(time);
        ctx.update_frame_count(render_data.time / render_data.delta);
        ctx.update_delta(render_data.delta as f32 * render_data.time_scale as f32);

        // Update inputs with interpolated values
        for i in inputs {
            match (&i.inner, ctx.get_input_mut(&i.name)) {
                (input_type::InputType::Float(f_new), Some(mut f)) => {
                    f.as_float().map(|e| e.current = f_new.current);
                }
                (input_type::InputType::Int(i_new, _), Some(mut int)) => {
                    int.as_int().map(|e| e.value.current = i_new.current);
                }
                (input_type::InputType::Point(p_new), Some(mut p)) => {
                    p.as_point().map(|e| e.current = p_new.current);
                }
                (input_type::InputType::Bool(b_new), Some(mut b)) => {
                    b.as_bool().map(|e| e.current = b_new.current);
                }
                (input_type::InputType::Color(c_new), Some(mut c)) => {
                    c.as_color().map(|e| e.current = c_new.current);
                }
                _ => {}
            }
        }

        let mut render_encoder = device.create_command_encoder(&Default::default());

        for image in image_inputs {
            let ImageInput {
                name,
                data,
                width,
                height,
                bytes_per_row,
                bit_depth,
            } = &image;

            if data.is_empty() {
                continue;
            }

            let in_format = match *bit_depth {
                0 => wgpu::TextureFormat::Rgba8Unorm,
                1 => wgpu::TextureFormat::Rgba16Unorm,
                2 => wgpu::TextureFormat::Rgba32Float,
                _ => continue,
            };

            let out_format = match *bit_depth {
                0 => wgpu::TextureFormat::Rgba8Unorm,
                1 => wgpu::TextureFormat::Rgba16Float,
                2 => wgpu::TextureFormat::Rgba32Float,
                _ => continue,
            };

            let scale = if *bit_depth == 1 {
                u16::MAX as f32 / 32768 as f32
            } else {
                1.0
            };

            let texture = if input_textures.get(*name).is_some_and(|t| {
                t.width() == *width && t.height() == *height && t.format() == out_format
            }) {
                let tex = input_textures.get(*name).unwrap();
                ctx.load_shared_texture(&tex, name);
                tex
            } else {
                let new_tex = device.create_texture(&target_desc(*width, *height, out_format));
                ctx.load_shared_texture(&new_tex, name);
                input_textures.insert(name.to_string(), new_tex);
                input_textures.get(*name).unwrap()
            };

            from_ctx.load_image_immediate(
                "input_image",
                *height,
                *width,
                *bytes_per_row,
                device,
                queue,
                &in_format,
                *data,
            );

            from_ctx
                .get_input_mut("depth_scale")
                .unwrap()
                .as_float()
                .unwrap()
                .current = scale;

            from_ctx
                .get_input_mut("height")
                .unwrap()
                .as_float()
                .unwrap()
                .current = *height as f32;

            from_ctx
                .get_input_mut("width")
                .unwrap()
                .as_float()
                .unwrap()
                .current = *width as f32;

            from_ctx.encode_render(
                queue,
                device,
                &mut render_encoder,
                &texture.create_view(&Default::default()),
                *width,
                *height,
            )
        }

        // Render actual scene
        ctx.encode_render(queue, device, &mut render_encoder, &out_tex, width, height);

        // Convert it to AE, This is a bit depth dependant pipeline
        to_ctx.encode_render(
            queue,
            device,
            &mut render_encoder,
            &final_tex,
            width,
            height,
        );

        // Dump the bytes somewhere the CPU can read them
        render_encoder.copy_texture_to_buffer(
            final_target.as_ref().unwrap().as_image_copy(),
            wgpu::ImageCopyBuffer {
                buffer: &staging_buffer.as_ref().unwrap(),
                layout: wgpu::ImageDataLayout {
                    offset: 0,
                    bytes_per_row: Some(padded_row_byte_ct),
                    rows_per_image: None,
                },
            },
            wgpu::Extent3d {
                width,
                height,
                depth_or_array_layers: 1,
            },
        );

        queue.submit([render_encoder.finish()]);

        {
            let buffer_slice = pipe.staging_buffer.as_ref().unwrap().slice(..);
            buffer_slice.map_async(wgpu::MapMode::Read, move |r| r.unwrap());
            device.poll(wgpu::Maintain::Wait);

            let gpu_slice = buffer_slice.get_mapped_range();
            let gpu_chunks = gpu_slice.chunks(padded_row_byte_ct as usize);

            let slice_chunks = slice.chunks_mut(row_byte_ct as usize);
            let iter = slice_chunks.zip(gpu_chunks);

            for (output_chunk, gpu_chunk) in iter {
                output_chunk.copy_from_slice(&gpu_chunk[..row_byte_ct as usize]);
            }
        };

        pipe.staging_buffer.as_ref().unwrap().unmap();
    }
}

fn target_desc(width: u32, height: u32, format: TextureFormat) -> wgpu::TextureDescriptor<'static> {
    wgpu::TextureDescriptor {
        label: None,
        size: wgpu::Extent3d {
            width,
            height,
            depth_or_array_layers: 1,
        },
        mip_level_count: 1,
        sample_count: 1, // crunch crunch
        dimension: wgpu::TextureDimension::D2,
        format,
        usage: wgpu::TextureUsages::COPY_DST
            | wgpu::TextureUsages::TEXTURE_BINDING
            | wgpu::TextureUsages::RENDER_ATTACHMENT
            | wgpu::TextureUsages::COPY_SRC,
        view_formats: &[],
    }
}
