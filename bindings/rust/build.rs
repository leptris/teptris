//! Link against libteptris.
//!
//! Resolution order (the leptris bindings/rust model):
//!   1. TEPTRIS_LIB_PATH — full path to the shared library file, or a
//!      directory containing it. CI sets this after building the C
//!      library from source.
//!   2. Fallback: system linker search (`-lteptris`).

use std::path::{Path, PathBuf};
use std::{env, fs};

fn main() {
    // The lib path changes the emitted link directives; without this
    // the build script output is cached across TEPTRIS_LIB_PATH
    // changes and tests link a stale library.
    println!("cargo:rerun-if-env-changed=TEPTRIS_LIB_PATH");
    let name = "teptris";
    if let Ok(path) = env::var("TEPTRIS_LIB_PATH") {
        let mut p = PathBuf::from(&path);
        if !p.exists() {
            // this crate sits at bindings/rust: a repo-relative path
            // resolves against EVERY ancestor of the crate, not just
            // the parent (the one-level-up probe misses the root)
            if let Ok(manifest) = env::var("CARGO_MANIFEST_DIR") {
                for anc in Path::new(&manifest).ancestors().skip(1) {
                    let cand = anc.join(&p);
                    if cand.exists() {
                        p = cand;
                        break;
                    }
                }
            }
        }
        let dir: PathBuf = if p.is_file() {
            p.parent().unwrap().to_path_buf()
        } else {
            p.clone()
        };
        let dir = dir.canonicalize().unwrap_or(dir);
        let found = if p.is_file() { Some(p) } else { find_lib(&p) };
        assert!(
            found.is_some(),
            "TEPTRIS_LIB_PATH set but no libteptris shared library found under {path}"
        );
        println!("cargo:rustc-link-search=native={}", dir.display());
        println!("cargo:rustc-link-lib=dylib={name}");
        // Let test binaries find the dylib at run time without
        // requiring DYLD_LIBRARY_PATH.
        println!("cargo:rustc-link-arg=-Wl,-rpath,{}", dir.display());
        return;
    }
    println!("cargo:rustc-link-lib=dylib={name}");
}

fn find_lib(dir: &Path) -> Option<PathBuf> {
    let prefixes = ["lib", ""];
    let exts = ["dylib", "so", "dll"];
    for ext in exts {
        for prefix in prefixes {
            let cand = dir.join(format!("{prefix}teptris.{ext}"));
            if cand.exists() {
                return Some(cand);
            }
        }
    }
    None
}
