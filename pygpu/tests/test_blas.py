﻿from itertools import product
import numpy
from nose.plugins.skip import SkipTest

from .support import (guard_devsup, gen_gpuarray, context)

try:
    import scipy.linalg.blas
    try:
        fblas = scipy.linalg.blas.fblas
    except AttributeError:
        fblas = scipy.linalg.blas
except ImportError as e:
    raise SkipTest("no scipy blas to compare against")

import pygpu.blas as gblas
from pygpu.gpuarray import (GpuArrayException, UnsupportedException)

def guard_devsup_blasdouble(func):
    def f(*args, **kwargs):
        try:
            func(*args, **kwargs)
        except UnsupportedException as e:
            raise SkipTest("operation not supported")
        except GpuArrayException as e:
            if 'float64' in args and "does not support double precision" in str(e):
                raise SkipTest("double precision not supported")
            raise
    return f


def test_dot():
    bools = [True, False]
    for N, dtype, offseted_i, sliced in product(
            [1, 256, 1337], ['float32', 'float64'], bools, bools):
        yield dot, N, dtype, offseted_i, sliced, True, False
    for overwrite, init_z in product(bools, bools):
        yield dot, 666, 'float32', False, False, overwrite, init_z


@guard_devsup_blasdouble
def dot(N, dtype, offseted_i, sliced, overwrite, init_z):
    cX, gX = gen_gpuarray((N,), dtype, offseted_inner=offseted_i,
                          sliced=sliced, ctx=context)
    cY, gY = gen_gpuarray((N,), dtype, offseted_inner=offseted_i,
                          sliced=sliced, ctx=context)
    if init_z:
        gZ = gen_gpuarray((), dtype, offseted_inner=offseted_i,
                          sliced=sliced, ctx=context)[1]
    else:
        gZ = None

    # Always check against double precision: scipy's single precision
    # has enough error that this sometimes fails when we're closer
    cr = fblas.ddot(cX, cY)
    gr = gblas.dot(gX, gY, gZ, overwrite_z=overwrite)
    numpy.testing.assert_allclose(cr, numpy.asarray(gr), rtol=1e-6)


def test_gemv():
    bools = [False, True]
    for shape, order, trans, offseted_i, sliced in product(
            [(100, 128), (128, 50)], 'fc', bools, bools, [1, 2, -1, -2]):
        yield (gemv, shape, 'float32', order, trans,
               offseted_i, sliced, True, False)
    for overwrite, init_y in product(bools, bools):
        yield (gemv, (4, 3), 'float32', 'f', False, False, 1,
               overwrite, init_y)
    yield gemv, (32, 32), 'float64', 'f', False, False, 1, True, False
    for alpha, beta, overwrite in product(
            [0, 1, -1, 0.6], [0, 1, -1, 0.6], bools):
        yield (gemv, (32, 32), 'float32', 'f', False, False, 1,
               overwrite, True, alpha, beta)


@guard_devsup_blasdouble
def gemv(shp, dtype, order, trans, offseted_i, sliced,
         overwrite, init_y, alpha=1.0, beta=0.0):
    cA, gA = gen_gpuarray(shp, dtype, order=order, offseted_inner=offseted_i,
                          sliced=sliced, ctx=context)
    if trans:
        shpX = (shp[0],)
        shpY = (shp[1],)
    else:
        shpX = (shp[1],)
        shpY = (shp[0],)
    cX, gX = gen_gpuarray(shpX, dtype, offseted_inner=offseted_i,
                          sliced=sliced, ctx=context)
    if init_y:
        cY, gY = gen_gpuarray(shpY, dtype, ctx=context)
    else:
        cY, gY = None, None

    if dtype == 'float32':
        cr = fblas.sgemv(alpha, cA, cX, beta, cY, trans=trans,
                         overwrite_y=overwrite)
    else:
        cr = fblas.dgemv(alpha, cA, cX, beta, cY, trans=trans,
                         overwrite_y=overwrite)
    gr = gblas.gemv(alpha, gA, gX, beta, gY, trans_a=trans,
                    overwrite_y=overwrite)

    numpy.testing.assert_allclose(cr, numpy.asarray(gr), rtol=1e-6)


def test_gemm():
    bools = [False, True]
    for (m, n, k), order, trans, offseted_o in product(
        [(48, 15, 32), (15, 32, 48)], list(product(*['fc']*3)),
            list(product(bools, bools)), bools):
        yield (gemm, m, n, k, 'float32', order, trans,
               offseted_o, 1, False, False)
    for sliced, overwrite, init_res in product([1, 2, -1, -2], bools, bools):
        yield (gemm, 4, 3, 2, 'float32', ('f', 'f', 'f'),
               (False, False), False, sliced, overwrite, init_res)
    yield (gemm, 32, 32, 32, 'float64', ('f', 'f', 'f'), (False, False),
           False, 1, False, False)
    for alpha, beta, overwrite in product(
            [0, 1, -1, 0.6], [0, 1, -1, 0.6], bools):
        yield (gemm, 32, 23, 32, 'float32', ('f', 'f', 'f'),
               (False, False), False, 1, overwrite, True, alpha, beta)


@guard_devsup_blasdouble
def gemm(m, n, k, dtype, order, trans, offseted_o, sliced, overwrite,
         init_res, alpha=1.0, beta=0.0):
    if trans[0]:
        shpA = (k, m)
    else:
        shpA = (m, k)
    if trans[1]:
        shpB = (n, k)
    else:
        shpB = (k, n)

    cA, gA = gen_gpuarray(shpA, dtype, order=order[0],
                          offseted_outer=offseted_o,
                          sliced=sliced, ctx=context)
    cB, gB = gen_gpuarray(shpB, dtype, order=order[1],
                          offseted_outer=offseted_o,
                          sliced=sliced, ctx=context)
    if init_res:
        cC, gC = gen_gpuarray((m, n), dtype, order=order[2], ctx=context)
    else:
        cC, gC = None, None

    if dtype == 'float32':
        cr = fblas.sgemm(alpha, cA, cB, beta, cC, trans_a=trans[0],
                         trans_b=trans[1], overwrite_c=overwrite)
    else:
        cr = fblas.dgemm(alpha, cA, cB, beta, cC, trans_a=trans[0],
                         trans_b=trans[1], overwrite_c=overwrite)
    gr = gblas.gemm(alpha, gA, gB, beta, gC, trans_a=trans[0],
                    trans_b=trans[1], overwrite_c=overwrite)

    numpy.testing.assert_allclose(cr, numpy.asarray(gr), rtol=1e-6)


def test_ger():
    bools = [False, True]
    for (m, n), order, sliced_x, sliced_y in product(
            [(4, 5)], 'fc', [1, 2, -2, -1], [1, 2, -2, -1]):
        yield ger, m, n, 'float32', order, sliced_x, sliced_y, False
    yield ger, 4, 5, 'float64', 'f', 1, 1, False
    for init_res, overwrite in product(bools, bools):
        yield ger, 4, 5, 'float32', 'f', 1, 1, init_res, overwrite

@guard_devsup_blasdouble
def ger(m, n, dtype, order, sliced_x, sliced_y, init_res, overwrite=False):
    cX, gX = gen_gpuarray((m,), dtype, order, sliced=sliced_x, ctx=context)
    cY, gY = gen_gpuarray((n,), dtype, order, sliced=sliced_y, ctx=context)

    if init_res:
        cA, gA = gen_gpuarray((m, n), dtype, order, ctx=context)
    else:
        cA, gA = None, None

    if dtype == 'float32':
        cr = fblas.sger(1.0, cX, cY, a=cA, overwrite_a=overwrite)
    else:
        cr = fblas.dger(1.0, cX, cY, a=cA, overwrite_a=overwrite)

    gr = gblas.ger(1.0, gX, gY, gA, overwrite_a=overwrite)

    numpy.testing.assert_allclose(cr, numpy.asarray(gr), rtol=1e-6)


def test_rgemmBatch_3d():
    bools = [False, True]
    for b, (m, n, k), order, trans, offseted_o in product(
        [1, 17, 31], [(24, 7, 16), (7, 16, 24)],
        list(product('fc', 'fc', 'c')),
            list(product(bools, bools)), bools):
        yield (rgemmBatch_3d, b, m, n, k, 'float32', order, trans,
               offseted_o, 1, False, False)
    for sliced, overwrite, init_res in product([1, 2, -1, -2], bools, bools):
        yield (rgemmBatch_3d, 5, 4, 3, 2, 'float32', ('f', 'f', 'c'),
               (False, False), False, sliced, overwrite, init_res)
    yield (rgemmBatch_3d, 16, 16, 16, 16, 'float64', ('f', 'f', 'c'),
           (False, False), False, 1, False, False)
    for alpha, beta, overwrite in product(
            [0, 1, -1, 0.6], [0, 1, -1, 0.6], bools):
        yield (rgemmBatch_3d, 16, 16, 9, 16, 'float32', ('f', 'f', 'c'),
               (False, False), False, 1, overwrite, True, alpha, beta)


@guard_devsup_blasdouble
def rgemmBatch_3d(b, m, n, k, dtype, order, trans, offseted_o, sliced,
                  overwrite, init_res, alpha=1.0, beta=0.0):
    if trans[0]:
        shpA = (b, k, m)
    else:
        shpA = (b, m, k)
    if trans[1]:
        shpB = (b, n, k)
    else:
        shpB = (b, k, n)

    cA, gA = gen_gpuarray(shpA, dtype, order=order[0],
                          offseted_outer=offseted_o,
                          sliced=sliced, ctx=context)
    cB, gB = gen_gpuarray(shpB, dtype, order=order[1],
                          offseted_outer=offseted_o,
                          sliced=sliced, ctx=context)
    if init_res:
        cC, gC = gen_gpuarray((b, m, n), dtype, order=order[2], ctx=context)
    else:
        cC, gC = None, None

    cr = numpy.empty((b, m, n), dtype=dtype)
    if dtype == 'float32':
        fn_gemm_c = fblas.sgemm
    else:
        fn_gemm_c = fblas.dgemm
    for i in range(b):
        cCi = cC if cC is None else cC[i]
        cr[i] = fn_gemm_c(alpha, cA[i], cB[i], beta, cCi, trans_a=trans[0],
                          trans_b=trans[1], overwrite_c=overwrite)

    gr = gblas.gemmBatch_3d(alpha, gA, gB, beta, gC, trans_a=trans[0],
                            trans_b=trans[1], overwrite_c=overwrite)

    numpy.testing.assert_allclose(cr, numpy.asarray(gr), rtol=1e-5)
