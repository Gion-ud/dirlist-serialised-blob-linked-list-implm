#pragma once

enum BlobEntryType {
    BENTT_TEXT  = 0x00,
    BENTT_BLOB  = 0x01,
    BENTT_I16   = 0x02,
    BENTT_I32   = 0x03,
    BENTT_I64   = 0x04,
    BENTT_U16   = 0x05,
    BENTT_U32   = 0x06,
    BENTT_U64   = 0x07,
    BENTT_F16   = 0x08,
    BENTT_F32   = 0x09,
    BENTT_F64   = 0x0A,
    BENTT_I128  = 0x0B,
    BENTT_U128  = 0x0C,
    BENTT_F128  = 0x0D,
    BENTT_U8    = 0x0E,
    BENTT_I8    = 0x0F,
    BENTT_ARRAY = 0x10,
};
