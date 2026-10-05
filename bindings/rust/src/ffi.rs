//! Raw FFI declarations mirroring the public header
//! (`src/include/teptris/teptris.h`). Hand-maintained like the Python
//! and Ruby mirrors — the integration tests keep the surface in check.
//!
//! Strings and keys handed out by document accessors are views into
//! the document's own copy of the input and live until
//! `teptris_document_free`.

#![allow(non_camel_case_types)]
// The mirror declares the full public contract, not just what the
// wrappers currently use — unused decls are intentional.
#![allow(dead_code)]

use std::os::raw::{c_char, c_int, c_void};

pub type TeptrisStatus = c_int;

pub const TEPTRIS_OK: c_int = 0;
pub const TEPTRIS_ERR_ALLOC: c_int = 1;
pub const TEPTRIS_ERR_SYNTAX: c_int = 2;
pub const TEPTRIS_ERR_SEMANTIC: c_int = 3;
pub const TEPTRIS_ERR_ENCODING: c_int = 4;
pub const TEPTRIS_ERR_DEPTH: c_int = 5;
pub const TEPTRIS_ERR_ARG: c_int = 6;
pub const TEPTRIS_ERR_STATE: c_int = 7;

pub type TeptrisKind = c_int;
pub const TEPTRIS_STRING: c_int = 0;
pub const TEPTRIS_INTEGER: c_int = 1;
pub const TEPTRIS_FLOAT: c_int = 2;
pub const TEPTRIS_BOOLEAN: c_int = 3;
pub const TEPTRIS_DATETIME_OFFSET: c_int = 4;
pub const TEPTRIS_DATETIME_LOCAL: c_int = 5;
pub const TEPTRIS_DATE_LOCAL: c_int = 6;
pub const TEPTRIS_TIME_LOCAL: c_int = 7;
pub const TEPTRIS_ARRAY: c_int = 8;
pub const TEPTRIS_TABLE: c_int = 9;

/* Opaque handles. */
#[repr(C)]
pub struct teptris_document {
    _private: [u8; 0],
}
#[repr(C)]
pub struct teptris_node {
    _private: [u8; 0],
}
#[repr(C)]
pub struct teptris_builder {
    _private: [u8; 0],
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct teptris_view {
    pub ptr: *const c_char,
    pub len: usize,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct teptris_datetime {
    pub year: i32,
    pub month: u8,
    pub day: u8,
    pub hour: u8,
    pub minute: u8,
    pub second: u8,
    pub nanosecond: u32,
    pub offset_seconds: i32,
}

#[repr(C)]
pub struct teptris_error {
    pub status: TeptrisStatus,
    pub line: usize,
    pub column: usize,
    pub message: *const c_char,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct teptris_options {
    pub max_depth: u32,
    pub _reserved: [u32; 3],
}

extern "C" {
    pub fn teptris_parse(
        data: *const c_char,
        len: usize,
        opts: *const teptris_options,
        out: *mut *mut teptris_document,
    ) -> TeptrisStatus;

    pub fn teptris_document_free(doc: *mut teptris_document);
    pub fn teptris_document_error(doc: *const teptris_document) -> *const teptris_error;
    pub fn teptris_document_root(doc: *const teptris_document) -> *const teptris_node;

    pub fn teptris_node_kind(node: *const teptris_node) -> TeptrisKind;
    pub fn teptris_node_string(node: *const teptris_node, out: *mut teptris_view) -> TeptrisStatus;
    pub fn teptris_node_integer(node: *const teptris_node, out: *mut i64) -> TeptrisStatus;
    pub fn teptris_node_float(node: *const teptris_node, out: *mut f64) -> TeptrisStatus;
    pub fn teptris_node_boolean(node: *const teptris_node, out: *mut bool) -> TeptrisStatus;
    pub fn teptris_node_datetime(node: *const teptris_node, out: *mut teptris_datetime)
        -> TeptrisStatus;

    pub fn teptris_node_array_length(node: *const teptris_node) -> usize;
    pub fn teptris_node_array_at(node: *const teptris_node, index: usize) -> *const teptris_node;
    pub fn teptris_node_table_length(node: *const teptris_node) -> usize;
    pub fn teptris_node_table_at(
        node: *const teptris_node,
        index: usize,
        key_out: *mut teptris_view,
    ) -> *const teptris_node;
    pub fn teptris_node_table_get(
        node: *const teptris_node,
        key: *const c_char,
        key_len: usize,
    ) -> *const teptris_node;

    pub fn teptris_document_emit(
        doc: *const teptris_document,
        buf: *mut *mut c_char,
        len: *mut usize,
    ) -> TeptrisStatus;
    pub fn teptris_document_emit_json(
        doc: *const teptris_document,
        buf: *mut *mut c_char,
        len: *mut usize,
    ) -> TeptrisStatus;

    pub fn teptris_document_flatten(
        doc: *const teptris_document,
        buf: *mut *mut u8,
        len: *mut usize,
    ) -> TeptrisStatus;
    pub fn teptris_flatten_free(buf: *mut c_void);

    pub fn teptris_status_string(status: TeptrisStatus) -> *const c_char;
    pub fn teptris_version_string() -> *const c_char;

    pub fn teptris_builder_new() -> *mut teptris_builder;
    pub fn teptris_builder_free(b: *mut teptris_builder);
    pub fn teptris_builder_put_string(
        b: *mut teptris_builder,
        key: *const c_char,
        key_len: usize,
        val: *const c_char,
        val_len: usize,
    ) -> TeptrisStatus;
    pub fn teptris_builder_put_integer(
        b: *mut teptris_builder,
        key: *const c_char,
        key_len: usize,
        v: i64,
    ) -> TeptrisStatus;
    pub fn teptris_builder_put_float(
        b: *mut teptris_builder,
        key: *const c_char,
        key_len: usize,
        v: f64,
    ) -> TeptrisStatus;
    pub fn teptris_builder_put_boolean(
        b: *mut teptris_builder,
        key: *const c_char,
        key_len: usize,
        v: bool,
    ) -> TeptrisStatus;
    pub fn teptris_builder_put_datetime(
        b: *mut teptris_builder,
        key: *const c_char,
        key_len: usize,
        kind: TeptrisKind,
        dt: *const teptris_datetime,
    ) -> TeptrisStatus;
    pub fn teptris_builder_open_table(
        b: *mut teptris_builder,
        key: *const c_char,
        key_len: usize,
    ) -> TeptrisStatus;
    pub fn teptris_builder_open_array(
        b: *mut teptris_builder,
        key: *const c_char,
        key_len: usize,
    ) -> TeptrisStatus;
    pub fn teptris_builder_open_inline_array(
        b: *mut teptris_builder,
        key: *const c_char,
        key_len: usize,
    ) -> TeptrisStatus;
    pub fn teptris_builder_close(b: *mut teptris_builder) -> TeptrisStatus;
    pub fn teptris_builder_finish(
        b: *mut teptris_builder,
        out: *mut *mut teptris_document,
    ) -> TeptrisStatus;
}
