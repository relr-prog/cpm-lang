#include "ast.h"

const char *type_name(TyKind k) {
    switch (k) {
    case TY_I8: return "i8";
    case TY_I16: return "i16";
    case TY_I32: return "i32";
    case TY_I64: return "i64";
    case TY_U8: return "u8";
    case TY_U16: return "u16";
    case TY_U32: return "u32";
    case TY_U64: return "u64";
    case TY_F32: return "f32";
    case TY_F64: return "f64";
    case TY_BOOL: return "bool";
    case TY_STR: return "str";
    case TY_VOID: return "void";
    case TY_ERROR: return "error";
    }
    return "?";
}