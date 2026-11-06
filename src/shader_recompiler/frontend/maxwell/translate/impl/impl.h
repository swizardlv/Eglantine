// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "shader_recompiler/environment.h"
#include "shader_recompiler/frontend/ir/basic_block.h"
#include "shader_recompiler/frontend/ir/ir_emitter.h"
#include "shader_recompiler/frontend/maxwell/instruction.h"

namespace Shader::Maxwell {

enum class CompareOp : u64 {
    False,
    LessThan,
    Equal,
    LessThanEqual,
    GreaterThan,
    NotEqual,
    GreaterThanEqual,
    True,
};

enum class BooleanOp : u64 {
    AND,
    OR,
    XOR,
};

enum class PredicateOp : u64 {
    False,
    True,
    Zero,
    NonZero,
};

enum class FPCompareOp : u64 {
    F,
    LT,
    EQ,
    LE,
    GT,
    NE,
    GE,
    NUM,
    Nan,
    LTU,
    EQU,
    LEU,
    GTU,
    NEU,
    GEU,
    T,
};

// Resolve a texture descriptor handle from a Maxwell TEX/TLD4/TLD/TXQ
// cbuf_offset (already multiplied by 4 to a byte offset). The Ampere
// lowering may encode an explicit constant-buffer bank in the offset:
// bit 14 = explicit-bank flag, bits 12-13 = bank, bits 0-11 = byte offset.
// In that case return a GetCbuf(bank, byte_offset) so the backend's
// texture-descriptor tracking resolves the descriptor in the right bank;
// otherwise return the plain immediate offset as before.
[[nodiscard]] inline IR::U32 ResolveTextureHandle(IR::IREmitter& ir, u32 encoded_byte_offset) {
    if ((encoded_byte_offset & 0x4000U) != 0) {
        const u32 bank = (encoded_byte_offset >> 12) & 0x3U;
        const u32 byte_offset = encoded_byte_offset & 0xfffU;
        return ir.GetCbuf(ir.Imm32(bank), ir.Imm32(byte_offset));
    }
    return ir.Imm32(encoded_byte_offset);
}

struct TranslatorVisitor {
    explicit TranslatorVisitor(Environment& env_, IR::Block& block) : env{env_}, ir(block) {}
    Environment& env;
    IR::IREmitter ir;

#define INST(name, cute, encode) void name(u64 insn);
#include "shader_recompiler/frontend/maxwell/maxwell.inc"
#undef INST

    [[nodiscard]] IR::U32 X(IR::Reg reg);
    [[nodiscard]] IR::U64 L(IR::Reg reg);
    [[nodiscard]] IR::F32 F(IR::Reg reg);
    [[nodiscard]] IR::F64 D(IR::Reg reg);

    void X(IR::Reg dest_reg, const IR::U32& value);
    void L(IR::Reg dest_reg, const IR::U64& value);
    void F(IR::Reg dest_reg, const IR::F32& value);
    void D(IR::Reg dest_reg, const IR::F64& value);

    [[nodiscard]] IR::U32 GetReg8(u64 insn);
    [[nodiscard]] IR::U32 GetReg20(u64 insn);
    [[nodiscard]] IR::U32 GetReg39(u64 insn);
    [[nodiscard]] IR::F32 GetFloatReg8(u64 insn);
    [[nodiscard]] IR::F32 GetFloatReg20(u64 insn);
    [[nodiscard]] IR::F32 GetFloatReg39(u64 insn);
    [[nodiscard]] IR::F64 GetDoubleReg20(u64 insn);
    [[nodiscard]] IR::F64 GetDoubleReg39(u64 insn);

    [[nodiscard]] IR::U32 GetCbuf(u64 insn);
    [[nodiscard]] IR::F32 GetFloatCbuf(u64 insn);
    [[nodiscard]] IR::F64 GetDoubleCbuf(u64 insn);
    [[nodiscard]] IR::U64 GetPackedCbuf(u64 insn);

    [[nodiscard]] IR::U32 GetImm20(u64 insn);
    [[nodiscard]] IR::F32 GetFloatImm20(u64 insn);
    [[nodiscard]] IR::F64 GetDoubleImm20(u64 insn);
    [[nodiscard]] IR::U64 GetPackedImm20(u64 insn);

    [[nodiscard]] IR::U32 GetImm32(u64 insn);
    [[nodiscard]] IR::F32 GetFloatImm32(u64 insn);

    void SetZFlag(const IR::U1& value);
    void SetSFlag(const IR::U1& value);
    void SetCFlag(const IR::U1& value);
    void SetOFlag(const IR::U1& value);

    void ResetZero();
    void ResetSFlag();
    void ResetCFlag();
    void ResetOFlag();
};

} // namespace Shader::Maxwell
