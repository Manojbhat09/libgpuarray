import sys
import os
import versioneer

have_cython = False

try:
    import Cython
    if Cython.__version__ < '0.25':
        raise Exception('cython is too old or not installed '
                        '(at least 0.25 required)')
    from Cython.Build import cythonize
    have_cython = True
except Exception:
    # for devel version
    raise

    def cythonize(args):
        for arg in args:
            arg.sources = [(s[:-3] + 'c' if s.endswith('.pyx') else s) for s in arg.sources]

# clang gives an error if passed -mno-fused-madd
# (and I don't even understand why it's passed in the first place)
if sys.platform == 'darwin':
    from distutils import sysconfig, ccompiler

    sysconfig_customize_compiler = sysconfig.customize_compiler

    def customize_compiler(compiler):
        sysconfig_customize_compiler(compiler)
        if sys.platform == 'darwin':
            while '-mno-fused-madd' in compiler.compiler:
                compiler.compiler.remove('-mno-fused-madd')
            while '-mno-fused-madd' in compiler.compiler_so:
                compiler.compiler_so.remove('-mno-fused-madd')
            while '-mno-fused-madd' in compiler.linker_so:
                compiler.linker_so.remove('-mno-fused-madd')
    sysconfig.customize_compiler = customize_compiler
    ccompiler.customize_compiler = customize_compiler

try:
    from setuptools import setup, Extension as _Extension

    # setuptools is stupid and rewrites "sources" to change '.pyx' to '.c'
    # if it can't find Pyrex (and in recent versions, Cython).
    #
    # This is a really stupid thing to do behind the users's back (since
    # it breaks development builds) especially with no way of disabling it
    # short of the hack below.
    class Extension(_Extension):
        def __init__(self, *args, **kwargs):
            save_sources = kwargs.get('sources', None)
            _Extension.__init__(self, *args, **kwargs)
            self.sources = save_sources
except ImportError:
    from distutils.core import setup, Extension

import numpy as np

to_del = []

for i, a in enumerate(sys.argv):
    if a == '--disable-cython':
        to_del.append(i)
        have_cython = False

for i in reversed(to_del):
    del sys.argv[i]

del to_del

include_dirs = [np.get_include()]
library_dirs = []
if sys.platform == 'win32' and not os.getenv('CONDA_BUILD'):
    # This is a hack so users don't need to do many steps for windows install
    # Just use the default location.
    current_dir = os.path.abspath(os.path.dirname(__file__))
    include_dirs += [os.path.join(current_dir, 'src')]

    default_bin_dir = os.path.join(current_dir, 'lib')
    if not os.path.isdir(default_bin_dir):
        raise RuntimeError('default binary dir {} does not exist, you may need to build the C library in release mode'.format(default_bin_dir))
    library_dirs += [default_bin_dir]

ea = []
if sys.platform in ('darwin', 'linux'):
    # Silence unused stuff warnings
    ea = ["-Wno-unused-variable", "-Wno-unused-function"]

exts = [Extension('pygpu.gpuarray',
                  sources=['pygpu/gpuarray.pyx'],
                  include_dirs=include_dirs,
                  libraries=['gpuarray'],
                  library_dirs=library_dirs,
                  extra_compile_args=ea,
                  define_macros=[('GPUARRAY_SHARED', None)]
                  ),
        Extension('pygpu.blas',
                  sources=['pygpu/blas.pyx'],
                  include_dirs=include_dirs,
                  libraries=['gpuarray'],
                  library_dirs=library_dirs,
                  extra_compile_args=ea,
                  define_macros=[('GPUARRAY_SHARED', None)]
                  ),
        Extension('pygpu._elemwise',
                  sources=['pygpu/_elemwise.pyx'],
                  include_dirs=include_dirs,
                  libraries=['gpuarray'],
                  library_dirs=library_dirs,
                  extra_compile_args=ea,
                  define_macros=[('GPUARRAY_SHARED', None)]
                  ),
        Extension('pygpu.collectives',
                  sources=['pygpu/collectives.pyx'],
                  include_dirs=include_dirs,
                  libraries=['gpuarray'],
                  library_dirs=library_dirs,
                  extra_compile_args=ea,
                  define_macros=[('GPUARRAY_SHARED', None)]
                  )]

setup(name='pygpu',
      version=versioneer.get_version(),
      cmdclass=versioneer.get_cmdclass(),
      description='numpy-like wrapper on libgpuarray for GPU computations',
      packages=['pygpu', 'pygpu/tests'],
      include_package_data=True,
      package_data={'pygpu': ['gpuarray.h', 'gpuarray_api.h',
                              'blas_api.h', 'numpy_compat.h',
                              'collectives.h', 'collectives_api.h']},
      ext_modules=cythonize(exts),
      install_requires=[
          'git+https://github.com/Manojbhat09/mako@changed#egg=Mako-1.1.2-py2.7.egg', 'six'],
      )
