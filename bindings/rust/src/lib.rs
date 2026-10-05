//! Idiomatic Rust bindings for [libteptris](https://github.com/leptris/teptris),
//! a pure-C99 TOML 1.1 parser and emitter.
//!
//! ```no_run
//! use teptris::{loads, dump, Value};
//!
//! let v = loads(b"name = \"demo\"\nport = 8080\n").unwrap();
//! assert_eq!(v.get("name").and_then(Value::as_str), Some("demo"));
//! assert_eq!(v.get("port").and_then(Value::as_integer), Some(8080));
//!
//! let toml = dump(&v).unwrap(); // canonical, parse(dump(v)) == v
//! assert!(!toml.is_empty());
//! ```
//!
//! The C library is resolved at link time: set `TEPTRIS_LIB_PATH`
//! (see `build.rs`) or install libteptris on the library path.
//!
//! Zero-copy note: `teptris_parse` NUL-terminates strings IN the input
//! buffer, so the safe `Document` owns a writable copy of the input
//! for the document's lifetime. (A `&mut [u8]`-borrowing variant is a
//! possible future API; a `&[u8]` borrow would be UB.)

mod ffi;

use std::ffi::{CStr, CString};
use std::fmt;
use std::os::raw::c_char;

/// A TOML value in document order. Tables keep insertion order (the
/// TOML semantic); use [`Table::get`] for lookup.
#[derive(Debug, Clone, PartialEq)]
pub enum Value {
    String(String),
    Integer(i64),
    Float(f64),
    Boolean(bool),
    Datetime(Datetime),
    Array(Vec<Value>),
    Table(Table),
}

/// One of the four TOML datetime shapes. Fields unused by the shape
/// are zeroed (matching the C struct contract).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Datetime {
    pub kind: DatetimeKind,
    pub year: i32,
    pub month: u8,
    pub day: u8,
    pub hour: u8,
    pub minute: u8,
    pub second: u8,
    pub nanosecond: u32,
    /// Only `Some` for [`DatetimeKind::Offset`].
    pub offset_seconds: Option<i32>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DatetimeKind {
    Offset,
    LocalDateTime,
    Date,
    Time,
}

/// An insertion-ordered table.
#[derive(Debug, Clone, PartialEq, Default)]
pub struct Table {
    entries: Vec<(String, Value)>,
}

impl Table {
    pub fn get(&self, key: &str) -> Option<&Value> {
        self.entries.iter().find(|(k, _)| k == key).map(|(_, v)| v)
    }
    pub fn iter(&self) -> impl Iterator<Item = (&str, &Value)> {
        self.entries.iter().map(|(k, v)| (k.as_str(), v))
    }
    pub fn len(&self) -> usize {
        self.entries.len()
    }
    pub fn is_empty(&self) -> bool {
        self.entries.is_empty()
    }
}

impl Value {
    pub fn as_str(&self) -> Option<&str> {
        match self {
            Value::String(s) => Some(s),
            _ => None,
        }
    }
    pub fn as_integer(&self) -> Option<i64> {
        match self {
            Value::Integer(i) => Some(*i),
            _ => None,
        }
    }
    pub fn as_float(&self) -> Option<f64> {
        match self {
            Value::Float(f) => Some(*f),
            _ => None,
        }
    }
    pub fn as_boolean(&self) -> Option<bool> {
        match self {
            Value::Boolean(b) => Some(*b),
            _ => None,
        }
    }
    pub fn as_array(&self) -> Option<&[Value]> {
        match self {
            Value::Array(a) => Some(a),
            _ => None,
        }
    }
    pub fn as_table(&self) -> Option<&Table> {
        match self {
            Value::Table(t) => Some(t),
            _ => None,
        }
    }
    /// Table lookup convenience: `v.get("k")` on tables, `None` on
    /// every other kind.
    pub fn get(&self, key: &str) -> Option<&Value> {
        self.as_table().and_then(|t| t.get(key))
    }
}

/// Parse failure with position. The message is copied out of the
/// document before it is freed.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ParseError {
    pub status: i32,
    pub message: String,
    pub line: usize,
    pub column: usize,
}

impl fmt::Display for ParseError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} (line {}, column {})",
            self.message, self.line, self.column
        )
    }
}

impl std::error::Error for ParseError {}

/// A non-parse binding failure (allocation, bad argument, state).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Error(pub i32);

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        unsafe {
            let msg = ffi::teptris_status_string(self.0);
            let msg = if msg.is_null() {
                "unknown error"
            } else {
                CStr::from_ptr(msg).to_str().unwrap_or("unknown error")
            };
            write!(f, "{msg}")
        }
    }
}

impl std::error::Error for Error {}

fn check(status: i32) -> Result<(), Error> {
    if status == ffi::TEPTRIS_OK {
        Ok(())
    } else {
        Err(Error(status))
    }
}

/// A parsed TOML document. Owns its copy of the input (see the module
/// docs); the tree is freed on drop.
pub struct Document {
    ptr: *mut ffi::teptris_document,
    // Must drop AFTER ptr: views point into it until the free.
    _input: Vec<u8>,
}

// The document pointer is owned exclusively; the engine is thread-safe
// across distinct documents (the concurrency suite covers cross-thread
// document lifetimes).
unsafe impl Send for Document {}

impl Document {
    /// Parse a TOML document. The input is copied into the document.
    pub fn parse(input: &[u8]) -> Result<Document, ParseError> {
        Self::parse_buf(input.to_vec())
    }

    /// Parse, taking ownership of a writable buffer without copying.
    /// The buffer is NUL-terminated in place by the engine.
    pub fn parse_buf(mut input: Vec<u8>) -> Result<Document, ParseError> {
        input.push(0);
        unsafe { Self::from_buffer(&mut input) }.map(|ptr| Document { ptr, _input: input })
    }

    /// # Safety
    /// `input` must outlive the returned raw pointer's document.
    unsafe fn from_buffer(input: &mut [u8]) -> Result<*mut ffi::teptris_document, ParseError> {
        let mut doc: *mut ffi::teptris_document = std::ptr::null_mut();
        let st = ffi::teptris_parse(
            input.as_ptr() as *const c_char,
            input.len() - 1, // exclude the sentinel
            std::ptr::null(),
            &mut doc,
        );
        if st != ffi::TEPTRIS_OK {
            let (message, line, column) = if doc.is_null() {
                ("parse failed".to_string(), 0, 0)
            } else {
                let e = ffi::teptris_document_error(doc);
                (
                    CStr::from_ptr((*e).message).to_string_lossy().into_owned(),
                    (*e).line,
                    (*e).column,
                )
            };
            if !doc.is_null() {
                ffi::teptris_document_free(doc);
            }
            return Err(ParseError { status: st, message, line, column });
        }
        Ok(doc)
    }

    /// The whole tree as an owned [`Value`] (the root is a table).
    pub fn to_value(&self) -> Result<Value, Error> {
        unsafe { node_to_value(ffi::teptris_document_root(self.ptr)) }
    }

    /// The root table, materialized.
    pub fn root_table(&self) -> Result<Table, Error> {
        match self.to_value()? {
            Value::Table(t) => Ok(t),
            _ => Err(Error(ffi::TEPTRIS_ERR_STATE)),
        }
    }

    /// Canonical TOML (deterministic; parse(emit(d)) == d).
    pub fn to_toml_string(&self) -> Result<String, Error> {
        self.emit_str(false)
    }

    /// The toml-test wire shape (tagged values) as a JSON string.
    pub fn to_json_string(&self) -> Result<String, Error> {
        self.emit_str(true)
    }

    fn emit_str(&self, json: bool) -> Result<String, Error> {
        unsafe {
            let mut buf: *mut c_char = std::ptr::null_mut();
            let mut len: usize = 0;
            let st = if json {
                ffi::teptris_document_emit_json(self.ptr, &mut buf, &mut len)
            } else {
                ffi::teptris_document_emit(self.ptr, &mut buf, &mut len)
            };
            check(st)?;
            let out = std::slice::from_raw_parts(buf as *const u8, len).to_vec();
            libc_free(buf as *mut std::os::raw::c_void);
            String::from_utf8(out).map_err(|_| Error(ffi::TEPTRIS_ERR_ENCODING))
        }
    }

    fn as_value(&self) -> Value {
        unsafe { node_to_value(ffi::teptris_document_root(self.ptr)).expect("root conversion") }
    }
}

impl Drop for Document {
    fn drop(&mut self) {
        unsafe { ffi::teptris_document_free(self.ptr) }
    }
}

unsafe fn node_to_value(node: *const ffi::teptris_node) -> Result<Value, Error> {
    match ffi::teptris_node_kind(node) {
        ffi::TEPTRIS_STRING => {
            let mut v: ffi::teptris_view = std::mem::zeroed();
            check(ffi::teptris_node_string(node, &mut v))?;
            let bytes = std::slice::from_raw_parts(v.ptr as *const u8, v.len);
            Ok(Value::String(String::from_utf8_lossy(bytes).into_owned()))
        }
        ffi::TEPTRIS_INTEGER => {
            let mut out: i64 = 0;
            check(ffi::teptris_node_integer(node, &mut out))?;
            Ok(Value::Integer(out))
        }
        ffi::TEPTRIS_FLOAT => {
            let mut out: f64 = 0.0;
            check(ffi::teptris_node_float(node, &mut out))?;
            Ok(Value::Float(out))
        }
        ffi::TEPTRIS_BOOLEAN => {
            let mut out: bool = false;
            check(ffi::teptris_node_boolean(node, &mut out))?;
            Ok(Value::Boolean(out))
        }
        ffi::TEPTRIS_DATETIME_OFFSET
        | ffi::TEPTRIS_DATETIME_LOCAL
        | ffi::TEPTRIS_DATE_LOCAL
        | ffi::TEPTRIS_TIME_LOCAL => {
            let mut dt: ffi::teptris_datetime = std::mem::zeroed();
            check(ffi::teptris_node_datetime(node, &mut dt))?;
            let kind = match ffi::teptris_node_kind(node) {
                ffi::TEPTRIS_DATETIME_OFFSET => DatetimeKind::Offset,
                ffi::TEPTRIS_DATETIME_LOCAL => DatetimeKind::LocalDateTime,
                ffi::TEPTRIS_DATE_LOCAL => DatetimeKind::Date,
                _ => DatetimeKind::Time,
            };
            Ok(Value::Datetime(Datetime {
                kind,
                year: dt.year,
                month: dt.month,
                day: dt.day,
                hour: dt.hour,
                minute: dt.minute,
                second: dt.second,
                nanosecond: dt.nanosecond,
                offset_seconds: if kind == DatetimeKind::Offset {
                    Some(dt.offset_seconds)
                } else {
                    None
                },
            }))
        }
        ffi::TEPTRIS_ARRAY => {
            let n = ffi::teptris_node_array_length(node);
            let mut items = Vec::with_capacity(n);
            for i in 0..n {
                items.push(node_to_value(ffi::teptris_node_array_at(node, i))?);
            }
            Ok(Value::Array(items))
        }
        ffi::TEPTRIS_TABLE => {
            let n = ffi::teptris_node_table_length(node);
            let mut entries = Vec::with_capacity(n);
            for i in 0..n {
                let mut key: ffi::teptris_view = std::mem::zeroed();
                let child = ffi::teptris_node_table_at(node, i, &mut key);
                let bytes = std::slice::from_raw_parts(key.ptr as *const u8, key.len);
                entries.push((String::from_utf8_lossy(bytes).into_owned(), node_to_value(child)?));
            }
            Ok(Value::Table(Table { entries }))
        }
        other => Err(Error(other)), // unreachable for valid kinds
    }
}

// The engine allocates the emit buffers with malloc/free — hand them
// back to libc rather than Rust's allocator.
unsafe fn libc_free(p: *mut std::os::raw::c_void) {
    extern "C" {
        fn free(p: *mut std::os::raw::c_void);
    }
    free(p);
}

/// Parse a TOML document into an owned [`Value`].
pub fn loads(input: &[u8]) -> Result<Value, ParseError> {
    Document::parse(input).map(|d| d.as_value())
}

/// Emit a [`Value`] as canonical TOML via the engine's builder — the
/// emitter stays the single source of formatting truth.
pub fn dump(v: &Value) -> Result<String, Error> {
    let doc = build(v)?;
    doc.to_toml_string()
}

/// Emit a [`Value`] as the toml-test conformance JSON (tagged values).
pub fn dump_json(v: &Value) -> Result<String, Error> {
    let doc = build(v)?;
    doc.to_json_string()
}

fn build(v: &Value) -> Result<Document, Error> {
    unsafe {
        let b = ffi::teptris_builder_new();
        let mut doc: *mut ffi::teptris_document = std::ptr::null_mut();
        // The root table is IMPLICIT: its entries attach directly to
        // the builder; opening a keyed table with a NULL key at the
        // root is TEPTRIS_ERR_ARG.
        let r = match v {
            Value::Table(t) => {
                let mut r = Ok(());
                for (k, child) in t.iter() {
                    r = build_value(b, Some(k), child);
                    if r.is_err() {
                        break;
                    }
                }
                r
            }
            other => build_value(b, None, other),
        }
        .and_then(|_| {
            let st = ffi::teptris_builder_finish(b, &mut doc);
            check(st).map(|_| ())
        });
        if r.is_err() && !doc.is_null() {
            ffi::teptris_document_free(doc);
            doc = std::ptr::null_mut();
        }
        if doc.is_null() {
            ffi::teptris_builder_free(b);
            return Err(r.unwrap_err());
        }
        // ownership transferred; the builder is consumed by finish
        let input = Vec::new(); // documents built this way own no input
        Ok(Document { ptr: doc, _input: input })
    }
}

unsafe fn build_value(
    b: *mut ffi::teptris_builder,
    key: Option<&str>,
    v: &Value,
) -> Result<(), Error> {
    let k = key.map(|k| (CString::new(k).unwrap(), k.len()));
    let (kp, kl) = match &k {
        Some((c, l)) => (c.as_ptr(), *l),
        None => (std::ptr::null(), 0),
    };
    match v {
        Value::String(s) => check(ffi::teptris_builder_put_string(
            b,
            kp,
            kl,
            s.as_ptr() as *const c_char,
            s.len(),
        )),
        Value::Integer(i) => check(ffi::teptris_builder_put_integer(b, kp, kl, *i)),
        Value::Float(f) => check(ffi::teptris_builder_put_float(b, kp, kl, *f)),
        Value::Boolean(b2) => check(ffi::teptris_builder_put_boolean(b, kp, kl, *b2)),
        Value::Datetime(dt) => {
            let mut cdt: ffi::teptris_datetime = std::mem::zeroed();
            cdt.year = dt.year;
            cdt.month = dt.month;
            cdt.day = dt.day;
            cdt.hour = dt.hour;
            cdt.minute = dt.minute;
            cdt.second = dt.second;
            cdt.nanosecond = dt.nanosecond;
            cdt.offset_seconds = dt.offset_seconds.unwrap_or(0);
            let kind = match dt.kind {
                DatetimeKind::Offset => ffi::TEPTRIS_DATETIME_OFFSET,
                DatetimeKind::LocalDateTime => ffi::TEPTRIS_DATETIME_LOCAL,
                DatetimeKind::Date => ffi::TEPTRIS_DATE_LOCAL,
                DatetimeKind::Time => ffi::TEPTRIS_TIME_LOCAL,
            };
            check(ffi::teptris_builder_put_datetime(b, kp, kl, kind, &cdt))
        }
        Value::Array(items) => {
            check(ffi::teptris_builder_open_array(b, kp, kl))?;
            for item in items {
                build_value(b, None, item)?;
            }
            check(ffi::teptris_builder_close(b))
        }
        Value::Table(t) => {
            // NULL key inside an open array = element of that array
            // (array-of-tables); a keyed call requires a table current
            check(ffi::teptris_builder_open_table(b, kp, kl))?;
            for (k2, v2) in t.iter() {
                build_value(b, Some(k2), v2)?;
            }
            check(ffi::teptris_builder_close(b))
        }
    }
}

/// The linked engine's version string.
pub fn engine_version() -> &'static str {
    unsafe {
        let v = ffi::teptris_version_string();
        CStr::from_ptr(v).to_str().unwrap_or("unknown")
    }
}
