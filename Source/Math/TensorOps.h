//
// <copyright file="TensorView.h" company="Microsoft">
//     Copyright (c) Microsoft Corporation.  All rights reserved.
// </copyright>
//

// This implements the elementwise tensor operations, including helper macros and some actual functions.

#pragma once

#include "Basics.h"
#include "CommonMatrix.h"

#pragma push_macro("TENSOR_OPS_DECL")
#ifndef TENSOR_OPS_DECL     // to make these accessible to CUDA kernels, say '#define TENSOR_OPS_DECL __device__ __host__'
#define TENSOR_OPS_DECL
#endif

#pragma push_macro("DECL")
#define DECL static inline TENSOR_OPS_DECL

// This class is exported from the Math.dll.
namespace Microsoft { namespace MSR { namespace CNTK {

    // -----------------------------------------------------------------------
    // unified overloads for float/double math functions
    //
    // Declare float and double versions of the functions x we need as x_().
    // This macro overloads x_() with float and double arguments, and inlines the correct library function,
    // e.g. exp_ -> exp(double), expf(float). This simplifies templated kernel code.
    // -----------------------------------------------------------------------

#pragma push_macro("OverloadUnaryMathFns")
    #define OverloadUnaryMathFns(x) DECL float x ## _(float f) { return x ## f(f); } DECL double x ## _(double f) { return x(f); }

    OverloadUnaryMathFns(exp);
    OverloadUnaryMathFns(log);
    OverloadUnaryMathFns(tanh);
    OverloadUnaryMathFns(sqrt);
    OverloadUnaryMathFns(fabs);
    OverloadUnaryMathFns(cos);
    OverloadUnaryMathFns(sin);

#pragma push_macro("OverloadUnaryMathFns")

    // -----------------------------------------------------------------------
    // additional functions that are standard in our context
    // -----------------------------------------------------------------------

    template<class ElemType>
    DECL ElemType Sigmoid(ElemType z)
    {
#if 1   // BUGBUG: Numerically bad. But if I don't use this, results change.
        ElemType negElem = -z;
        ElemType e = exp_(negElem);

        return 1 / (e + 1);
#else
#if 1   // Efficient implementation that avoids to divergent CUDA code paths that both compute exp() [jdroppo]. This version compiles to PTX without branches.
        ElemType q = exp_(-fabs_(z));
        ElemType numer;
        if (z > 0)                      // q = exp(-z)
            numer = 1;
        else                            // q = exp(z)
            numer = q;
        return numer / (1 + q);
#else   // Reference code:
        if (z > 0)
            return 1 / (1 + exp_(-z));
        else
        {
            ElemType v = exp_(z);
            return v / (1 + v);
        }
#endif
#endif
    }

    template<class ElemType>
    DECL ElemType SigmoidDerivative(ElemType z)
    {
        ElemType v = Sigmoid(z);
        return v * (1 - v);
    }

    template<class ElemType>
    DECL ElemType LinearRectifierDerivative(ElemType z)
    {
        return z > 0 ? (ElemType)1 : 0;
    }

    template<class ElemType>
    DECL ElemType Sqrt(ElemType z)
    {
        // BUGBUG: Why clip to 0? An invalid sqrt() should show up as a NaN in the result, instead of hiding it.
        return sqrt_(z > 0 ? z : 0);
    }

    template<class ElemType>
    DECL ElemType ClippedLog(ElemType z)
    {
        return z < EPS_IN_LOG ? LOG_OF_EPS_IN_LOG : log_(z);
    }

    template<class ElemType>
    DECL ElemType ClippedQuotient(ElemType a, ElemType b)
    {
        if (fabs(b) < EPS_IN_INVERSE)   // clip the denominator
        {
            if (b > 0)
                b = EPS_IN_INVERSE;
            else
                b = -EPS_IN_INVERSE;
        }
        return a / b;
    }

    template<typename ElemType>
    DECL ElemType LogAdd(ElemType x, ElemType y)
    {
        if (x < y)
        {
            ElemType temp = x; x = y; y = temp;
        }
        ElemType diff = y - x;
        if (diff < (ElemType)MINLOGEXP)
        {
            return (x < (ElemType)LSMALL) ? (ElemType)LZERO : x;
        }
        else
        {
            ElemType z = exp_(diff);
            return x + log_((ElemType)1.0 + z);
        }
    }

    template<class ElemType> DECL ElemType Sqr(ElemType z) { return z * z; }

    // -----------------------------------------------------------------------
    // ElementWiseOperator implementations
    //
    // Define a static function for every ElementWiseOperator (CommonMatrix.h).
    // -----------------------------------------------------------------------

#pragma push_macro("DefNullaryOp")
    #define DefNullaryOp(op, expr) template<class ElemType> DECL ElemType Op ## op() { return expr; }

    DefNullaryOp(ConstOne, 1);
#pragma pop_macro("DefNullaryOp")

#pragma push_macro("DefUnaryOp")
    #define DefUnaryOp(op, expr) template<class ElemType> DECL ElemType Op ## op(ElemType a) { return expr; }

    DefUnaryOp(Copy, a);
    DefUnaryOp(Negate, -a); DefUnaryOp(Not, !a);
    DefUnaryOp(Abs, fabs_(a));
    DefUnaryOp(Sigmoid, Sigmoid(a)); DefUnaryOp(Tanh, tanh_(a)); DefUnaryOp(Sqrt, Sqrt(a)); DefUnaryOp(Exp, exp_(a)); DefUnaryOp(Log, ClippedLog(a)); DefUnaryOp(LinearRectifier, a > 0 ? a : 0); DefUnaryOp(Cosine, cos_(a));
#pragma pop_macro("DefUnaryOp")

#pragma push_macro("DefBinaryOp")
    #define DefBinaryOp(op, expr) template<class ElemType> DECL ElemType Op ## op(ElemType a, ElemType b) { return expr; }

    DefBinaryOp(Sum, a + b); DefBinaryOp(Difference, a - b); DefBinaryOp(ElementwiseProduct, a * b); DefBinaryOp(ElementwiseQuotient, ClippedQuotient(a, b));
    DefBinaryOp(LogSum, LogAdd(a, b)); DefBinaryOp(Max, a > b ? a : b); DefBinaryOp(Min, a < b ? a : b);
    DefBinaryOp(EQ, a == b); DefBinaryOp(NE, a != b); DefBinaryOp(GT, a > b); DefBinaryOp(LT, a < b); DefBinaryOp(GE, a >= b); DefBinaryOp(LE, a <= b);
    DefBinaryOp(And, (float)((!!a) && (!!b))); DefBinaryOp(Or, (float)((!!a) || (!!b))); DefBinaryOp(Xor, (float)((!!a) ^ (!!b)));
    DefBinaryOp(MaskNegative, b >= 0 ? a : 0);
    DefBinaryOp(ElementwiseProductWithSigmoidDerivative, a * SigmoidDerivative(b));
    DefBinaryOp(ElementwiseProductWithTanhDerivative, a * (1 - Sqr(tanh_(b))));
    DefBinaryOp(ElementwiseProductWithExp, a * exp_(b));
    DefBinaryOp(ElementwiseProductWithLinearRectifierDerivative, b > 0 ? a : 0);
    DefBinaryOp(ElementwiseProductWithCosDerivative, a * -sin_(b));

#pragma pop_macro("DefBinaryOp")

#pragma push_macro("DefTernaryOp")
    #define DefTernaryOp(op, expr) template<class ElemType> DECL ElemType Op ## op(ElemType a, ElemType b, ElemType c) { return expr; }

    DefTernaryOp(Cond, a ? b : c); DefTernaryOp(Clip, a < b ? b : (a > c ? c : a));
#pragma pop_macro("DefTernaryOp")

}}}
#pragma pop_macro("DECL")
#pragma pop_macro("TENSOR_OPS_DECL")