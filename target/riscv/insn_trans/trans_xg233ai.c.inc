/*
 * RISC-V translation routines for the Gevico Xg233ai extension
 *
 * Copyright (c) 2025 Gevico
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define REQUIRE_XG233AI(ctx) do {              \
    if (!ctx->cfg_ptr->ext_xg233ai) {          \
        return false;                          \
    }                                          \
} while (0)

/*
 * crush: 8-bit to 4-bit compression
 *
 * Synopsis: crush rd, rs1, rs2
 *   For each pair of bytes in memory at [rs1], extract the low 4 bits
 *   of each byte and pack them into a single byte, storing the result
 *   at [rd]. The number of source bytes is given by rs2.
 *
 *   Equivalent C code:
 *     for (i = 0; i < n/2; i++) {
 *         dst[i]  = src[2*i]   & 0x0F;
 *         dst[i] |= (src[2*i+1] & 0x0F) << 4;
 *     }
 *     if (n & 1) dst[n/2] = src[n-1] & 0x0F;
 */
static bool trans_crush(DisasContext *ctx, arg_crush *a)
{
    REQUIRE_XG233AI(ctx);

    TCGv src_addr = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv dst_addr = get_gpr(ctx, a->rd, EXT_NONE);
    TCGv len = get_gpr(ctx, a->rs2, EXT_NONE);

    gen_helper_crush(tcg_env, dst_addr, src_addr, len);

    return true;
}

/*
 * sort: INT32 array ascending bubble sort, with partial sorting support.
 *
 * Synopsis: sort rd, rs1, rs2
 *   A = mem at gpr[rs1]                           // INT32 array
 *   N = gpr[rs2]                                  // total elements (upper bound)
 *   K = gpr[rd]                                   // elements to sort
 *   for i in 0..K-2:
 *       for j in 0..K-i-2:
 *           if A[j] > A[j+1]:
 *               swap(A[j], A[j+1])
 */
static bool trans_sort(DisasContext *ctx, arg_sort *a)
{
    REQUIRE_XG233AI(ctx);

    TCGv addr = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv n = get_gpr(ctx, a->rs2, EXT_NONE);
    TCGv k = get_gpr(ctx, a->rd, EXT_NONE);

    gen_helper_sort(tcg_env, addr, n, k);

    return true;
}



/*
 * dma: FP32 matrix transpose
 *
 * Synopsis: dma rd, rs1, rs2
 *   Transpose an FP32 matrix in memory.
 *   rd = destination address, rs1 = source address
 *   rs2 = grain (0: 8x8, 1: 16x16, 2: 32x32)
 *
 *   Equivalent C code for grain=0 (8x8):
 *     for (i = 0; i < 8; i++)
 *         for (j = 0; j < 8; j++)
 *             dst[j * 8 + i] = src[i * 8 + j];
 */
static bool trans_dma(DisasContext *ctx, arg_dma *a)
{
    REQUIRE_XG233AI(ctx);

    TCGv dst_addr = get_gpr(ctx, a->rd, EXT_NONE);
    TCGv src_addr = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv grain = get_gpr(ctx, a->rs2, EXT_NONE);

    gen_helper_dma(tcg_env, dst_addr, src_addr, grain);

    return true;
}

/*
 * expand: 4-bit → 8-bit Decompress and expand, each byte is split into two 4-bit elements.
 *
 * Synopsis: expand rd, rs1, rs2
 *   src = mem at gpr[rs1]                         // UINT8 array, N elements
 *   dst = mem at gpr[rd]                          // UINT8 array, 2*N elements
 *   for i in 0..N-1:
 *       dst[2*i]     = src[i] & 0x0F
 *       dst[2*i + 1] = (src[i] >> 4) & 0x0F
 */
static bool trans_expand(DisasContext *ctx, arg_expand *a)
{
    REQUIRE_XG233AI(ctx);

    TCGv dst_addr = get_gpr(ctx, a->rd, EXT_NONE);
    TCGv src_addr = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv n = get_gpr(ctx, a->rs2, EXT_NONE);

    gen_helper_expand(tcg_env, dst_addr, src_addr, n);

    return true;
}


/*
 * vdot: INT32 vector dot product
 *
 * Synopsis: vdot rd, rs1, rs2
 *   A = mem at gpr[rs1]                           // INT32[16]
 *   B = mem at gpr[rs2]                           // INT32[16]
 *   acc = 0                                       // INT64 accumulator
 *   for i in 0..15:
 *       acc += (INT64)A[i] * (INT64)B[i]
 *   gpr[rd] = acc[XLEN-1:0]                      // truncate to XLEN
 */
static bool trans_vdot(DisasContext *ctx, arg_vdot *a)
{
    REQUIRE_XG233AI(ctx);

    TCGv addr_a = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv addr_b = get_gpr(ctx, a->rs2, EXT_NONE);
    TCGv ret = tcg_temp_new();

    gen_helper_vdot(ret, tcg_env, addr_a, addr_b);
    gen_set_gpr(ctx, a->rd, ret);

    return true;
}

/*
 * vrelu: INT32 vector dot product
 *
 * Synopsis: vrelu rd, rs1, rs2
 *   src = mem at gpr[rs1]                         // INT32 array
 *   dst = mem at gpr[rd]                          // INT32 array
 *   N   = gpr[rs2]
 *   for i in 0..N-1:
 *   dst[i] = (src[i] > 0) ? src[i] : 0
 */
static bool trans_vrelu(DisasContext *ctx, arg_vrelu *a)
{
    REQUIRE_XG233AI(ctx);
    TCGv src_addr = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv dst_addr = get_gpr(ctx, a->rd, EXT_NONE);
    TCGv N = get_gpr(ctx, a-> rs2, EXT_NONE);

    gen_helper_vrelu(tcg_env, dst_addr, src_addr, N);

    return true;
}

/*
 * vscale: INT32 Vector scalar multiplication
 *
 * Synopsis: vscale rd, rs1, rs2
 *   src   = mem at gpr[rs1]                       // INT32 array
 *   dst   = mem at gpr[rd]                        // INT32 array
 *   scale = gpr[rs2]                              // scalar multiplier (register value)
 *   N     = 16                                    // fixed vector length
 *   for i in 0..N-1:
 *       dst[i] = (INT32)((INT64)src[i] * scale)   // multiply, truncate to 32-bit
 */
static bool trans_vscale(DisasContext *ctx, arg_vscale *a)
{
    REQUIRE_XG233AI(ctx);
    TCGv src_addr = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv dst_addr = get_gpr(ctx, a->rd, EXT_NONE);
    TCGv scale = get_gpr(ctx, a->rs2, EXT_NONE);

    gen_helper_vscale(tcg_env, dst_addr, src_addr, scale);

    return true;
}

/*
 * vmax: INT32 Vector Maximum Reduction
 *
 * Synopsis: vmax rd, rs1, rs2
 *   A   = mem at gpr[rs1]                         // INT32 array
 *   N   = gpr[rs2]                                 // number of elements
 *   max = A[0]
 *   for i in 1..N-1:
 *       if A[i] > max:
 *           max = A[i]
 *   gpr[rd] = sign_extend(max, 32)                // sign-extend to XLEN
 */
static bool trans_vmax(DisasContext *ctx, arg_vmax *a)
{
    REQUIRE_XG233AI(ctx);
    TCGv addr = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv N = get_gpr(ctx, a->rs2, EXT_NONE);
    TCGv ret = tcg_temp_new();

    gen_helper_vmax(ret, tcg_env, addr, N);
    gen_set_gpr(ctx, a->rd, ret);

    return true;
}


/*
 * gemm: INT32 4×4 matrix multiplication
 *
 * Synopsis: gemm rd, rs1, rs2
 *   A = mem at gpr[rs1]                           // INT32[4][4], row-major
 *   B = mem at gpr[rs2]                           // INT32[4][4], row-major
 *   C = mem at gpr[rd]                            // INT32[4][4], row-major
 *   for i in 0..3:
 *       for j in 0..3:
 *           acc = 0                               // INT64 accumulator
 *           for k in 0..3:
 *               acc += (INT64)A[i][k] * (INT64)B[k][j]
 *           C[i][j] = (INT32)acc
 */
static bool trans_gemm(DisasContext *ctx, arg_gemm *a)
{
    REQUIRE_XG233AI(ctx);
    TCGv C = get_gpr(ctx, a->rd, EXT_NONE);
    TCGv A = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv B = get_gpr(ctx, a->rs2, EXT_NONE);

    gen_helper_gemm(tcg_env, C, A, B);

    return true;
}
