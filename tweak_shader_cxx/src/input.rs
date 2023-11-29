use super::ffi::{self, InputVariant};
use tweak_shader::input_type::*;

pub struct Input {
    pub name: String,
    pub inner: tweak_shader::input_type::InputType,
}

impl Input {
    pub fn variant(&self) -> InputVariant {
        match self.inner {
            InputType::Float(_) => InputVariant::Float,
            InputType::Int(_, Some(_)) => InputVariant::IntList,
            InputType::Int(_, None) => InputVariant::Int,
            InputType::Point(_) => InputVariant::Point2d,
            InputType::Bool(_) => InputVariant::Bool,
            InputType::Color(_) => InputVariant::Color,
            InputType::Image(_) => InputVariant::Image,
            InputType::Audio(_, _) => InputVariant::Audio,
            InputType::AudioFft(_, _) => InputVariant::Unsupported,
            InputType::Event(_) => InputVariant::Unsupported,
            InputType::RawBytes(_) => InputVariant::Unsupported,
        }
    }

    pub fn as_color(&self) -> ffi::ColorInput {
        if let InputType::Color(ColorInput { current, default }) = self.inner {
            ffi::ColorInput {
                current,
                deflt: default,
            }
        } else {
            panic!("wrong variant!");
        }
    }

    pub fn as_point(&self) -> ffi::PointInput {
        if let InputType::Point(PointInput {
            current,
            min,
            max,
            default,
        }) = self.inner
        {
            ffi::PointInput {
                current,
                min,
                max,
                deflt: default,
            }
        } else {
            panic!("wrong variant!");
        }
    }

    pub fn as_int_list(&self) -> ffi::IntListInput {
        if let InputType::Int(input, Some(v)) = &self.inner {
            let (names, values): (Vec<_>, Vec<_>) = v.iter().cloned().unzip();

            ffi::IntListInput {
                names: names.join("|"),
                values,
                current: input.current,
            }
        } else {
            panic!("wrong variant!");
        }
    }

    pub fn as_bool(&self) -> ffi::BoolInput {
        if let InputType::Bool(BoolInput { current, default }) = self.inner {
            ffi::BoolInput {
                current,
                deflt: default,
            }
        } else {
            panic!("wrong variant!");
        }
    }

    pub fn image_is_loaded(&self) -> bool {
        if let InputType::Image(TextureStatus::Loaded { .. }) = self.inner {
            true
        } else {
            false
        }
    }

    pub fn as_int(&self) -> ffi::IntInput {
        if let InputType::Int(
            IntInput {
                current,
                min,
                max,
                default,
            },
            None,
        ) = self.inner
        {
            ffi::IntInput {
                current,
                min,
                max,
                deflt: default,
            }
        } else {
            panic!("wrong variant!");
        }
    }

    pub fn as_float(&self) -> ffi::FloatInput {
        if let InputType::Float(FloatInput {
            current,
            min,
            max,
            default,
        }) = self.inner
        {
            ffi::FloatInput {
                current,
                min,
                max,
                deflt: default,
            }
        } else {
            panic!("wrong variant!");
        }
    }

    pub fn name(&self) -> &str {
        &self.name
    }

    // Setter method for the inner field, specifically for Float type
    pub fn set_float(&mut self, new_float: f32) {
        if let tweak_shader::input_type::InputType::Float(ref mut float_input) = self.inner {
            float_input.current = new_float;
        }
    }

    // Setter method for the inner field, specifically for Int type
    pub fn set_int(&mut self, new_int: i32) {
        if let tweak_shader::input_type::InputType::Int(ref mut int_input, None) = &mut self.inner {
            int_input.current = new_int;
        }
    }

    // Setter method for the inner field, specifically for IntList type
    pub fn set_int_list(&mut self, index: u32) {
        if let tweak_shader::input_type::InputType::Int(input, Some(v)) = &mut self.inner {
            input.current = v[index as usize - 1].1;
        }
    }

    // Setter method for the inner field, specifically for Point type
    pub fn set_point(&mut self, new_point: [f32; 2]) {
        if let tweak_shader::input_type::InputType::Point(ref mut point_input) = &mut self.inner {
            point_input.current = new_point;
        }
    }

    // Setter method for the inner field, specifically for Bool type
    pub fn set_bool(&mut self, new_bool: bool) {
        if let tweak_shader::input_type::InputType::Bool(ref mut bool_input) = &mut self.inner {
            bool_input.current = if new_bool { 1 } else { 0 };
        }
    }

    // Setter method for the inner field, specifically for Color type
    pub fn set_color(&mut self, new_color: [f32; 4]) {
        if let tweak_shader::input_type::InputType::Color(ref mut color_input) = &mut self.inner {
            color_input.current = new_color;
        }
    }
}
