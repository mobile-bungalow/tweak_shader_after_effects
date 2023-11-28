fn main() {
    cxx_build::bridge("src/lib.rs")
        .flag_if_supported("-std=c++17")
        .compile("libtweak_shader_cxx");

    println!("cargo:rerun-if-changed=src/lib.rs");
    println!("cargo:rerun-if-changed=src/input.rs");
    println!("cargo:rerun-if-changed=src/sequence_data.rs");
}
