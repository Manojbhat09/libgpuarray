#define _CRT_SECURE_NO_WARNINGS

#include "compyte/compat.h"
#include "compyte/buffer.h"
#include "compyte/util.h"
#include "compyte/error.h"

#ifdef __APPLE__

#include <OpenCL/opencl.h>

#else

#include <CL/opencl.h>

#endif

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#ifdef _MSC_VER
#define strdup _strdup
#endif

#define SSIZE_MIN (-(SSIZE_MAX-1))

static cl_int err;

#define FAIL(v, e) { if (ret) *ret = e; return v; }
#define CHKFAIL(v) if (err != CL_SUCCESS) FAIL(v, GA_IMPL_ERROR)

struct _gpudata {
  cl_mem buf;
  cl_event ev;
};

gpudata *cl_make_buf(cl_mem buf) {
  gpudata *res;

  res = malloc(sizeof(*res));
  if (res == NULL) return NULL;

  res->buf = buf;
  res->ev = NULL;
  err = clRetainMemObject(buf);
  if (err != CL_SUCCESS) {
    free(res);
    return NULL;
  }

  return res;
}

cl_mem cl_get_buf(gpudata *g) { return g->buf; }

struct _gpukernel {
  cl_kernel k;
  gpudata **bs;
};

#define PRAGMA "#pragma OPENCL EXTENSION "
#define ENABLE " : enable\n"
#define CL_SMALL "cl_khr_byte_addressable_store"
#define CL_DOUBLE "cl_khr_fp64"
#define CL_HALF "cl_khr_fp16"

static gpukernel *cl_newkernel(void *ctx, unsigned int count,
			       const char **strings, const size_t *lengths,
			       const char *fname, int flags, int *ret);
static void cl_freekernel(gpukernel *k);
static int cl_setkernelarg(gpukernel *k, unsigned int index,
			   int typecode, const void *val);
static int cl_setkernelargbuf(gpukernel *k, unsigned int index,
			      gpudata *b);
static int cl_callkernel(gpukernel *k, size_t);

static const char CL_PREAMBLE[] =
  "#define local_barrier() barrier(CLK_LOCAL_MEM_FENCE)\n"
  "#define WHITHIN_KERNEL /* empty */\n"
  "#define KERNEL __kernel\n"
  "#define GLOBAL_MEM __global\n"
  "#define LOCAL_MEM __local\n"
  "#define LOCAL_MEM_ARG __local\n"
  "#define REQD_WG_SIZE(x, y, z) __attribute__((reqd_work_group_size(x, y, z)))\n"
  "#define LID_0 get_local_id(0)\n"
  "#define LID_1 get_local_id(1)\n"
  "#define LID_2 get_local_id(2)\n"
  "#define LDIM_0 get_local_size(0)\n"
  "#define LDIM_1 get_local_size(1)\n"
  "#define LDIM_2 get_local_size(2)\n"
  "#define GID_0 get_group_id(0)\n"
  "#define GID_1 get_group_id(1)\n"
  "#define GID_2 get_group_id(2)\n"
  "#define GDIM_0 get_num_groups(0)\n"
  "#define GDIM_1 get_num_groups(1)\n"
  "#define GDIM_2 get_num_groups(2)\n"
  "#define ga_bool uchar\n"
  "#define ga_byte char\n"
  "#define ga_ubyte uchar\n"
  "#define ga_short short\n"
  "#define ga_ushort ushort\n"
  "#define ga_int int\n"
  "#define ga_uint uint\n"
  "#define ga_long long\n"
  "#define ga_ulong ulong\n"
  "#define ga_float float\n"
  "#define ga_double double\n"
  "#define ga_half half\n";
/* XXX: add complex types, quad types, and longlong */
/* XXX: add vector types */

static const char *get_error_string(cl_int err) {
  /* OpenCL 1.0 error codes */
  switch (err) {
  case CL_SUCCESS:                        return "Success!";
  case CL_DEVICE_NOT_FOUND:               return "Device not found.";
  case CL_DEVICE_NOT_AVAILABLE:           return "Device not available";
  case CL_COMPILER_NOT_AVAILABLE:         return "Compiler not available";
  case CL_MEM_OBJECT_ALLOCATION_FAILURE:  return "Memory object allocation failure";
  case CL_OUT_OF_RESOURCES:               return "Out of resources";
  case CL_OUT_OF_HOST_MEMORY:             return "Out of host memory";
  case CL_PROFILING_INFO_NOT_AVAILABLE:   return "Profiling information not available";
  case CL_MEM_COPY_OVERLAP:               return "Memory copy overlap";
  case CL_IMAGE_FORMAT_MISMATCH:          return "Image format mismatch";
  case CL_IMAGE_FORMAT_NOT_SUPPORTED:     return "Image format not supported";
  case CL_BUILD_PROGRAM_FAILURE:          return "Program build failure";
  case CL_MAP_FAILURE:                    return "Map failure";
#ifdef CL_VERSION_1_1
  case CL_MISALIGNED_SUB_BUFFER_OFFSET:   return "Buffer offset improperly aligned";
  case CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST: return "Event in wait list has an error status";
#endif
  case CL_INVALID_VALUE:                  return "Invalid value";
  case CL_INVALID_DEVICE_TYPE:            return "Invalid device type";
  case CL_INVALID_PLATFORM:               return "Invalid platform";
  case CL_INVALID_DEVICE:                 return "Invalid device";
  case CL_INVALID_CONTEXT:                return "Invalid context";
  case CL_INVALID_QUEUE_PROPERTIES:       return "Invalid queue properties";
  case CL_INVALID_COMMAND_QUEUE:          return "Invalid command queue";
  case CL_INVALID_HOST_PTR:               return "Invalid host pointer";
  case CL_INVALID_MEM_OBJECT:             return "Invalid memory object";
  case CL_INVALID_IMAGE_FORMAT_DESCRIPTOR:return "Invalid image format descriptor";
  case CL_INVALID_IMAGE_SIZE:             return "Invalid image size";
  case CL_INVALID_SAMPLER:                return "Invalid sampler";
  case CL_INVALID_BINARY:                 return "Invalid binary";
  case CL_INVALID_BUILD_OPTIONS:          return "Invalid build options";
  case CL_INVALID_PROGRAM:                return "Invalid program";
  case CL_INVALID_PROGRAM_EXECUTABLE:     return "Invalid program executable";
  case CL_INVALID_KERNEL_NAME:            return "Invalid kernel name";
  case CL_INVALID_KERNEL_DEFINITION:      return "Invalid kernel definition";
  case CL_INVALID_KERNEL:                 return "Invalid kernel";
  case CL_INVALID_ARG_INDEX:              return "Invalid argument index";
  case CL_INVALID_ARG_VALUE:              return "Invalid argument value";
  case CL_INVALID_ARG_SIZE:               return "Invalid argument size";
  case CL_INVALID_KERNEL_ARGS:            return "Invalid kernel arguments";
  case CL_INVALID_WORK_DIMENSION:         return "Invalid work dimension";
  case CL_INVALID_WORK_GROUP_SIZE:        return "Invalid work group size";
  case CL_INVALID_WORK_ITEM_SIZE:         return "Invalid work item size";
  case CL_INVALID_GLOBAL_OFFSET:          return "Invalid global offset";
  case CL_INVALID_EVENT_WAIT_LIST:        return "Invalid event wait list";
  case CL_INVALID_EVENT:                  return "Invalid event";
  case CL_INVALID_OPERATION:              return "Invalid operation";
  case CL_INVALID_GL_OBJECT:              return "Invalid OpenGL object";
  case CL_INVALID_BUFFER_SIZE:            return "Invalid buffer size";
  case CL_INVALID_MIP_LEVEL:              return "Invalid mip-map level";
  case CL_INVALID_GLOBAL_WORK_SIZE:       return "Invalid global work size";
#ifdef CL_VERSION_1_1
  case CL_INVALID_PROPERTY:               return "Invalid property";
#endif
  default: return "Unknown error";
  }
}

static cl_device_id get_dev(cl_context ctx, int *ret) {
  size_t sz;
  cl_device_id res;
  cl_device_id *ids;

  err = clGetContextInfo(ctx, CL_CONTEXT_DEVICES, 0, NULL, &sz);
  CHKFAIL(NULL);

  ids = malloc(sz);
  if (ids == NULL) FAIL(NULL, GA_MEMORY_ERROR);

  err = clGetContextInfo(ctx, CL_CONTEXT_DEVICES, sz, ids, NULL);
  res = ids[0];
  free(ids);
  if (err != CL_SUCCESS) FAIL(NULL, GA_IMPL_ERROR);
  return res;
}

static cl_command_queue get_a_q(cl_context ctx, int *ret) {
  static cl_context last_context = NULL;
  static cl_command_queue last_q;
  cl_command_queue_properties qprop;
  cl_device_id id;
  cl_command_queue res;

  /* Might get in problems with multi-thread */
  if (ctx == last_context) {
    clRetainCommandQueue(last_q);
    return last_q;
  }

  id = get_dev(ctx, ret);
  if (id == NULL) return NULL;

  err = clGetDeviceInfo(id, CL_DEVICE_QUEUE_PROPERTIES, sizeof(qprop),
                        &qprop, NULL);
  CHKFAIL(NULL);

  res = clCreateCommandQueue(ctx, id,
                             qprop&CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE,
                             &err);
  if (res == NULL)
    FAIL(NULL, GA_IMPL_ERROR);

  if (last_context != NULL) {
    clReleaseContext(last_context);
    clReleaseCommandQueue(last_q);
  }
  last_context = ctx;
  last_q = res;
  clRetainContext(last_context);
  clRetainCommandQueue(last_q);
  return res;
}

static int check_ext(cl_context ctx, const char *name) {
  static cl_context last_context = NULL;
  static char *exts = NULL;
  cl_device_id dev;
  size_t sz;
  int res = 0;

  if (ctx != last_context) {
    last_context = NULL;
    free(exts);

    dev = get_dev(ctx, &res);
    if (dev == NULL) return res;

    err = clGetDeviceInfo(dev, CL_DEVICE_EXTENSIONS, 0, NULL, &sz);
    if (err != CL_SUCCESS) return GA_IMPL_ERROR;

    exts = malloc(sz);
    if (exts == NULL) return GA_MEMORY_ERROR;

    err = clGetDeviceInfo(dev, CL_DEVICE_EXTENSIONS, sz, exts, NULL);
    if (err != CL_SUCCESS) {
      free(exts);
      exts = NULL;
      return GA_IMPL_ERROR;
    }
    last_context = ctx;
  }
  return (strstr(exts, name) == NULL) ? -1 : 0;
}

static void
#ifdef _MSC_VER
__stdcall
#endif
errcb(const char *errinfo, const void *pi, size_t cb, void *u) {
  fprintf(stderr, "%s\n", errinfo);
}

static void *cl_init(int devno, int *ret) {
  int platno;
  cl_device_id *ds;
  cl_device_id d;
  cl_platform_id *ps;
  cl_platform_id p;
  cl_uint nump, numd;
  cl_context_properties props[3] = {
    CL_CONTEXT_PLATFORM, 0,
    0,
  };
  cl_context ctx;

  platno = devno >> 16;
  devno &= 0xFFFF;

  err = clGetPlatformIDs(0, NULL, &nump);
  CHKFAIL(NULL);

  if ((unsigned int)platno >= nump || platno < 0) FAIL(NULL, GA_VALUE_ERROR);

  ps = calloc(sizeof(*ps), nump);
  if (ps == NULL) FAIL(NULL, GA_MEMORY_ERROR);
  err = clGetPlatformIDs(nump, ps, NULL);
  /* We may get garbage on failure here but it won't matter as we will
     not use it */
  p = ps[platno];
  free(ps);
  CHKFAIL(NULL);

  err = clGetDeviceIDs(p, CL_DEVICE_TYPE_ALL, 0, NULL, &numd);
  CHKFAIL(NULL);

  if ((unsigned int)devno >= numd || devno < 0) FAIL(NULL, GA_VALUE_ERROR);

  ds = calloc(sizeof(*ds), numd);
  if (ds == NULL) FAIL(NULL, GA_MEMORY_ERROR);
  err = clGetDeviceIDs(p, CL_DEVICE_TYPE_ALL, numd, ds, NULL);
  d = ds[devno];
  free(ds);
  CHKFAIL(NULL);

  props[1] = (cl_context_properties)p;
  ctx = clCreateContext(props, 1, &d, errcb, NULL, &err);
  CHKFAIL(NULL);

  return ctx;
}

static gpudata *cl_alloc(void *ctx, size_t size, int *ret) {
  gpudata *res;

  res = malloc(sizeof(*res));
  if (res == NULL) FAIL(NULL, GA_SYS_ERROR);

  if (size == 0) {
    /* OpenCL doesn't like a zero-sized buffer */
    size = 1;
  }

  res->buf = clCreateBuffer((cl_context)ctx, CL_MEM_READ_WRITE, size, NULL,
                            &err);
  res->ev = NULL;
  if (err != CL_SUCCESS) {
    free(res);
    FAIL(NULL, GA_IMPL_ERROR);
  }

  return res;
}

static void cl_free(gpudata *b) {
  clReleaseMemObject(b->buf);
  if (b->ev != NULL)
    clReleaseEvent(b->ev);
  free(b);
}

static int cl_share(gpudata *a, gpudata *b, int *ret) {
#ifdef CL_VERSION_1_1
  cl_mem aa, bb;
#endif
  if (a->buf == b->buf) return 1;
#ifdef CL_VERSION_1_1
  err = clGetMemObjectInfo(a->buf, CL_MEM_ASSOCIATED_MEMOBJECT, sizeof(aa), &aa, NULL);
  CHKFAIL(-1);
  err = clGetMemObjectInfo(b->buf, CL_MEM_ASSOCIATED_MEMOBJECT, sizeof(bb), &bb, NULL);
  CHKFAIL(-1);
  if (aa == NULL) aa = a->buf;
  if (bb == NULL) bb = b->buf;
  if (aa == bb) return 1;
#endif
  return 0;
}

static int cl_move(gpudata *dst, size_t dstoff, gpudata *src, size_t srcoff,
                   size_t sz) {
  cl_event ev;
  cl_event evw[2];
  cl_event *evl = NULL;
  cl_context ctx;
  cl_command_queue q;
  int res;
  cl_uint num_ev = 0;

  if (sz == 0) return GA_NO_ERROR;

  err = clGetMemObjectInfo(dst->buf, CL_MEM_CONTEXT, sizeof(ctx), &ctx, NULL);
  if (err != CL_SUCCESS) return GA_IMPL_ERROR;

  if (src->ev != NULL)
    evw[num_ev++] = src->ev;
  if (dst->ev != NULL)
    evw[num_ev++] = dst->ev;

  if (num_ev > 0)
    evl = evw;

  q = get_a_q(ctx, &res);
  if (q == NULL) return res;

  err = clEnqueueCopyBuffer(q, src->buf, dst->buf, srcoff, dstoff, sz, num_ev,
                            evl, &ev);
  clReleaseCommandQueue(q);
  if (err != CL_SUCCESS) {
    return GA_IMPL_ERROR;
  }
  if (src->ev != NULL)
    clReleaseEvent(src->ev);
  if (dst->ev != NULL)
    clReleaseEvent(dst->ev);

  src->ev = ev;
  dst->ev = ev;
  clRetainEvent(ev);

  return GA_NO_ERROR;
}

static int cl_read(void *dst, gpudata *src, size_t srcoff, size_t sz) {
  cl_context ctx;
  cl_command_queue q;
  cl_event ev[1];
  cl_event *evl = NULL;
  cl_uint num_ev = 0;
  int res;

  if (sz == 0) return GA_NO_ERROR;

  err = clGetMemObjectInfo(src->buf, CL_MEM_CONTEXT, sizeof(ctx), &ctx, NULL);
  if (err != CL_SUCCESS) return GA_IMPL_ERROR;

  q = get_a_q(ctx, &res);
  if (q == NULL) return res;

  if (src->ev != NULL) {
    ev[0] = src->ev;
    evl = ev;
    num_ev = 1;
  }

  err = clEnqueueReadBuffer(q, src->buf, CL_TRUE, srcoff, sz, dst, num_ev, evl,
                            NULL);
  src->ev = NULL;
  if (num_ev == 1) clReleaseEvent(ev[0]);
  clReleaseCommandQueue(q);
  if (err != CL_SUCCESS) {
    return GA_IMPL_ERROR;
  }

  return GA_NO_ERROR;
}

static int cl_write(gpudata *dst, size_t dstoff, const void *src, size_t sz) {
  cl_context ctx;
  cl_command_queue q;
  cl_event ev[1];
  cl_event *evl = NULL;
  cl_uint num_ev = 0;
  int res;

  if (sz == 0) return GA_NO_ERROR;

  err = clGetMemObjectInfo(dst->buf, CL_MEM_CONTEXT, sizeof(ctx), &ctx, NULL);
  if (err != CL_SUCCESS) return GA_IMPL_ERROR;

  q = get_a_q(ctx, &res);
  if (q == NULL) return res;

  if (dst->ev != NULL) {
    ev[0] = dst->ev;
    evl = ev;
    num_ev = 1;
  }

  err = clEnqueueWriteBuffer(q, dst->buf, CL_TRUE, dstoff, sz, src, num_ev,
                             evl, NULL);
  dst->ev = NULL;
  if (num_ev == 1) clReleaseEvent(ev[0]);
  clReleaseCommandQueue(q);
  if (err != CL_SUCCESS) {
    return GA_IMPL_ERROR;
  }

  return GA_NO_ERROR;
}

static int cl_memset(gpudata *dst, size_t offset, int data) {
  char local_kern[256];
  const char *rlk[1];
  size_t sz, bytes, n;
  gpukernel *m;
  int r, res = GA_IMPL_ERROR;

  cl_context ctx;

  unsigned char val = (unsigned)data;
  cl_uint pattern = (cl_uint)val & (cl_uint)val >> 8 & \
    (cl_uint)val >> 16 & (cl_uint)val >> 24;

  if ((err = clGetMemObjectInfo(dst->buf, CL_MEM_SIZE, sizeof(bytes), &bytes,
				NULL)) != CL_SUCCESS) {
    return GA_IMPL_ERROR;
  }

  bytes -= offset;

  if (bytes == 0) return GA_NO_ERROR;

  if ((err = clGetMemObjectInfo(dst->buf, CL_MEM_CONTEXT, sizeof(ctx),
                                &ctx, NULL)) != CL_SUCCESS)
    return GA_IMPL_ERROR;

  if ((bytes % 16) == 0) {
    r = snprintf(local_kern, sizeof(local_kern),
                 "__kernel void kmemset(unsigned int n, __global uint4 *mem) {"
                 "unsigned int i; __global char *tmp = (__global char *)mem;"
                 "tmp += %" SPREFIX "u; mem = (__global uint4 *)tmp;"
                 "for (i = get_global_id(0); i < n; i += get_global_size(0)) {"
                 "mem[i] = (uint4)(%u,%u,%u,%u); }}",
                 offset, pattern, pattern, pattern, pattern);
    n = bytes/16;
  } else if ((bytes % 8) == 0) {
    r = snprintf(local_kern, sizeof(local_kern),
                 "__kernel void kmemset(unsigned int n, __global uint2 *mem) {"
                 "unsigned int i; __global char *tmp = (__global char *)mem;"
                 "tmp += %" SPREFIX "u; mem = (__global uint2 *)tmp;"
                 "for (i = get_global_id(0); i < n; i += get_global_size(0)) {"
                 "mem[i] = (uint2)(%u,%u); }}",
                 offset, pattern, pattern);
    n = bytes/8;
  } else if ((bytes % 4) == 0) {
    r = snprintf(local_kern, sizeof(local_kern),
                 "__kernel void kmemset(unsigned int n,"
                 "__global unsigned int *mem) {"
                 "unsigned int i; __global char *tmp = (__global char *)mem;"
                 "tmp += %" SPREFIX "u; mem = (__global unsigned int *)tmp;"
                 "for (i = get_global_id(0); i < n; i += get_global_size(0)) {"
                 "mem[i] = %u; }}",
                 offset, pattern);
    n = bytes/4;
  } else {
    if (check_ext(ctx, CL_SMALL))
      return GA_DEVSUP_ERROR;
    r = snprintf(local_kern, sizeof(local_kern),
                 "__kernel void kmemset(unsigned int n,"
                 "__global unsigned char *mem) {"
                 "unsigned int i; mem += %" SPREFIX "u;"
                 "for (i = get_global_id(0); i < n; i += get_global_size(0)) {"
                 "mem[i] = %u; }}",
                 offset, val);
    n = bytes;
  }
  /* If this assert fires, increase the size of local_kern above. */
  assert(r <= sizeof(local_kern));

  sz = strlen(local_kern);
  rlk[0] = local_kern;

  m = cl_newkernel(ctx, 1, rlk, &sz, "kmemset", 0, &res);
  if (m == NULL) return res;
  res = cl_setkernelarg(m, 0, GA_UINT, &n);
  res = cl_setkernelargbuf(m, 1, dst);
  if (res != GA_NO_ERROR) goto fail;

  res = cl_callkernel(m, n);

 fail:
  cl_freekernel(m);
  return res;
}

static int cl_check_extensions(const char **preamble, unsigned int *count,
                               int flags, cl_context ctx) {
  if (flags & GA_USE_CLUDA) {
    preamble[*count] = CL_PREAMBLE;
    (*count)++;
  }
  if (flags & GA_USE_SMALL) {
    if (check_ext(ctx, CL_SMALL)) return GA_DEVSUP_ERROR;
    preamble[*count] = PRAGMA CL_SMALL ENABLE;
    (*count)++;
  }
  if (flags & GA_USE_DOUBLE) {
    if (check_ext(ctx, CL_DOUBLE)) return GA_DEVSUP_ERROR;
    preamble[*count] = PRAGMA CL_DOUBLE ENABLE;
    (*count)++;
  }
  if (flags & GA_USE_COMPLEX) {
    return GA_DEVSUP_ERROR; // for now
  }
  if (flags & GA_USE_HALF) {
    if (check_ext(ctx, CL_HALF)) return GA_DEVSUP_ERROR;
    preamble[*count] = PRAGMA CL_HALF ENABLE;
    (*count)++;
  }
  if (flags & GA_USE_PTX) {
    return GA_DEVSUP_ERROR;
  }
  return GA_NO_ERROR;
}

static gpukernel *cl_newkernel(void *ctx, unsigned int count, 
			       const char **strings, const size_t *lengths,
			       const char *fname, int flags, int *ret) {
  gpukernel *res;
  cl_device_id dev;
  cl_program p;
  // Sync this table size with the number of flags that can add stuff
  // at the beginning
  const char *preamble[4];
  size_t *newl;
  const char **news;
  unsigned int n = 0;
  cl_uint num_args;
  int error;

  if (count == 0) FAIL(NULL, GA_VALUE_ERROR);

  dev = get_dev((cl_context)ctx, ret);
  if (dev == NULL) return NULL;

  error = cl_check_extensions(preamble, &n, flags, (cl_context)ctx);
  if (error != GA_NO_ERROR) FAIL(NULL, error);

  res = malloc(sizeof(*res));
  if (res == NULL) FAIL(NULL, GA_MEMORY_ERROR);
  res->k = NULL;

  if (n != 0) {
    news = calloc(count+n, sizeof(const char *));
    if (news == NULL) {
      free(res);
      FAIL(NULL, GA_SYS_ERROR);
    }
    memcpy(news, preamble, n*sizeof(const char *));
    memcpy(news+n, strings, count*sizeof(const char *));
    if (lengths == NULL) {
      newl = NULL;
    } else {
      newl = calloc(count+n, sizeof(size_t));
      if (newl == NULL) {
        free(news);
        free(res);
        FAIL(NULL, GA_MEMORY_ERROR);
      }
      memcpy(newl+n, lengths, count*sizeof(size_t));
    }
  } else {
    news = strings;
    newl = (size_t *)lengths;
  }

  p = clCreateProgramWithSource((cl_context)ctx, count+n, news, newl, &err);
  if (n != 0) {
    free(news);
    free(newl);
  }
  if (err != CL_SUCCESS) {
    free(res);
    FAIL(NULL, GA_IMPL_ERROR);
  }

  err = clBuildProgram(p, 1, &dev, "-w", NULL, NULL);
  if (err != CL_SUCCESS) {
    free(res);
    clReleaseProgram(p);
    FAIL(NULL, GA_IMPL_ERROR);
  }  

  res->bs = NULL;
  res->k = clCreateKernel(p, fname, &err);
  clReleaseProgram(p);
  if (err != CL_SUCCESS) {
    cl_freekernel(res);
    FAIL(NULL, GA_IMPL_ERROR);
  }

  err = clGetKernelInfo(res->k, CL_KERNEL_NUM_ARGS, sizeof(num_args),
                        &num_args, NULL);
  if (err != CL_SUCCESS) {
    cl_freekernel(res);
    FAIL(NULL, GA_IMPL_ERROR);
  }

  res->bs = calloc(sizeof(gpudata *), num_args);
  if (res->bs == NULL) {
    cl_freekernel(res);
    FAIL(NULL, GA_MEMORY_ERROR);
  }

  return res;
}

static void cl_freekernel(gpukernel *k) {
  if (k->k) clReleaseKernel(k->k);
  free(k->bs);
  free(k);
}

static int cl_setkernelarg(gpukernel *k, unsigned int index, int typecode,
			   const void *val) {
  size_t sz;
  if (typecode == GA_DELIM)
    sz = sizeof(cl_mem);
  else
    sz = compyte_get_elsize(typecode);
  err = clSetKernelArg(k->k, index, sz, val);
  if (err != CL_SUCCESS) {
    return GA_IMPL_ERROR;
  }
  return GA_NO_ERROR;
}

static int cl_setkernelargbuf(gpukernel *k, unsigned int index, gpudata *b) {
  k->bs[index] = b;
  return cl_setkernelarg(k, index, GA_DELIM, &b->buf);
}

static int cl_callkernel(gpukernel *k, size_t n) {
  cl_event ev;
  cl_event *evw;
  cl_context ctx;
  cl_command_queue q;
  cl_device_id dev;
  size_t n_max;
  cl_uint num_ev;
  cl_uint num_args;
  cl_uint i;
  int res;

  err = clGetKernelInfo(k->k, CL_KERNEL_CONTEXT, sizeof(ctx), &ctx, NULL);
  if (err != CL_SUCCESS) return GA_IMPL_ERROR;

  dev = get_dev(ctx, &res);
  if (dev == NULL) return res;

  err = clGetKernelWorkGroupInfo(k->k, dev, CL_KERNEL_WORK_GROUP_SIZE,
                                 sizeof(n_max), &n_max, NULL);
  if (err != CL_SUCCESS) return GA_IMPL_ERROR;

  if (n > n_max) n = n_max;

  err = clGetKernelInfo(k->k, CL_KERNEL_NUM_ARGS, sizeof(num_args), &num_args,
                        NULL);
  if (err != CL_SUCCESS) return GA_IMPL_ERROR;

  q = get_a_q(ctx, &res);
  if (q == NULL) return res;

  num_ev = 0;
  evw = calloc(sizeof(cl_event), num_args);
  if (evw == NULL) {
    clReleaseCommandQueue(q);
    return GA_MEMORY_ERROR;
  }
  
  for (i = 0; i < num_args; i++) {
    if (k->bs[i] != NULL && k->bs[i]->ev != NULL) {
      evw[num_ev++] = k->bs[i]->ev;
      k->bs[i]->ev = NULL;
    }
  }

  if (num_ev == 0) {
    free(evw);
    evw = NULL;
  }

  err = clEnqueueNDRangeKernel(q, k->k, 1, NULL, &n, NULL, num_ev, evw, &ev);
  clReleaseCommandQueue(q);
  for (i = 0; i < num_ev; i++)
    clReleaseEvent(evw[i]);
  free(evw);
  if (err != CL_SUCCESS) return GA_IMPL_ERROR;

  for (i = 0; i < num_args; i++) {
    if (k->bs[i] != NULL && k->bs[i]->ev == NULL) {
      k->bs[i]->ev = ev;
      clRetainEvent(ev);
    }
  }
  clReleaseEvent(ev);

  return GA_NO_ERROR;
}

static const char ELEM_HEADER[] = "#define DTYPEA %s\n"
  "#define DTYPEB %s\n"
  "__kernel void elemk(__global const DTYPEA *a_data,"
  "                    __global DTYPEB *b_data){"
  "const int idx = get_global_id(0);"
  "const int numThreads = get_global_size(0);"
  "__global char *tmp; tmp = (__global char *)a_data; tmp += %" SPREFIX "u;"
  "a_data = (__global const DTYPEA *)tmp; tmp = (__global char *)b_data;"
  "tmp += %" SPREFIX "u; b_data = (__global DTYPEB *)tmp;"
  "for (int i = idx; i < %" SPREFIX "u; i+= numThreads) {"
  "__global const char *a_p = (__global const char *)a_data;"
  "__global char *b_p = (__global char *)b_data;";

static const char ELEM_FOOTER[] =
  "__global const DTYPEA *a = (__global const DTYPEA *)a_p;"
  "__global DTYPEB *b = (__global DTYPEB *)b_p;"
  "b[0] = a[0];}}\n";

static int cl_extcopy(gpudata *input, size_t ioff, gpudata *output,
                      size_t ooff, int intype, int outtype, unsigned int a_nd,
                      const size_t *a_dims, const ssize_t *a_str,
                      unsigned int b_nd, const size_t *b_dims,
                      const ssize_t *b_str) {
  char *strs[64];
  size_t nEls;
  cl_context ctx;
  gpukernel *k;
  unsigned int count = 0;
  int res = GA_SYS_ERROR;
  unsigned int i;
  int flags = GA_USE_CLUDA;

  nEls = 1;
  for (i = 0; i < a_nd; i++) {
    nEls *= a_dims[i];
  }

  if (nEls == 0) return GA_NO_ERROR;

  if ((err = clGetMemObjectInfo(input->buf, CL_MEM_CONTEXT, sizeof(ctx),
                                &ctx, NULL)) != CL_SUCCESS)
    return GA_IMPL_ERROR;

  if (outtype == GA_DOUBLE || intype == GA_DOUBLE ||
      outtype == GA_CDOUBLE || intype == GA_CDOUBLE) {
    flags |= GA_USE_DOUBLE;
  }

  if (outtype == GA_HALF || intype == GA_HALF) {
    flags |= GA_USE_HALF;
  }

  if (compyte_get_elsize(outtype) < 4 || compyte_get_elsize(intype) < 4) {
    /* Should check for non-mod4 strides too */
    flags |= GA_USE_SMALL;
  }

  if (outtype == GA_CFLOAT || intype == GA_CFLOAT ||
      outtype == GA_CDOUBLE || intype == GA_CDOUBLE) {
    flags |= GA_USE_COMPLEX;
  }

  if (asprintf(&strs[count], ELEM_HEADER,
	       compyte_get_type(intype)->cluda_name,
	       compyte_get_type(outtype)->cluda_name,
               ioff, ooff, nEls) == -1)
    goto fail;
  count++;
  
  if (compyte_elem_perdim(strs, &count, a_nd, a_dims, a_str, "a_p") == -1)
    goto fail;
  if (compyte_elem_perdim(strs, &count, b_nd, b_dims, b_str, "b_p") == -1)
    goto fail;

  strs[count] = strdup(ELEM_FOOTER);
  if (strs[count] == NULL) 
    goto fail;
  count++;

  assert(count < (sizeof(strs)/sizeof(strs[0])));

  k = cl_newkernel(ctx, count, (const char **)strs, NULL, "elemk",
                   flags, &res);
  if (k == NULL) goto fail;
  res = cl_setkernelargbuf(k, 0, input);
  if (res != GA_NO_ERROR) goto kfail;
  res = cl_setkernelargbuf(k, 1, output);
  if (res != GA_NO_ERROR) goto kfail;

  assert(nEls < UINT_MAX);
  res = cl_callkernel(k, nEls);

 kfail:
  cl_freekernel(k);
 fail:
  for (i = 0; i< count; i++) {
    free(strs[i]);
  }
  return res;
}

static const char *cl_error(void) {
  return get_error_string(err);
}

compyte_buffer_ops opencl_ops = {cl_init,
                                 cl_alloc,
                                 cl_free,
                                 cl_share,
                                 cl_move,
                                 cl_read,
                                 cl_write,
                                 cl_memset,
                                 cl_newkernel,
                                 cl_freekernel,
                                 cl_setkernelarg,
                                 cl_setkernelargbuf,
                                 cl_callkernel,
                                 cl_extcopy,
                                 cl_error};