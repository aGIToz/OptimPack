/*
 * opky.c --
 *
 * Yorick interface for OptimPack library.
 *
 *-----------------------------------------------------------------------------
 *
 * This file is part of OptimPack (https://github.com/emmt/OptimPack).
 *
 * Copyright (c) 2014, 2015 Éric Thiébaut
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 *-----------------------------------------------------------------------------
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

#include <pstdlib.h>
#include <yapi.h>

#include "optimpack-private.h"

#define TRUE    OPK_TRUE
#define FALSE   OPK_FALSE

/* Define some macros to get rid of some GNU extensions when not compiling
   with GCC. */
#if ! (defined(__GNUC__) && __GNUC__ > 1)
#   define __attribute__(x)
#   define __inline__
#   define __FUNCTION__        ""
#   define __PRETTY_FUNCTION__ ""
#endif

PLUG_API void y_error(const char *) __attribute__ ((noreturn));

/*---------------------------------------------------------------------------*/
/* PRIVATE DATA AND DEFINTIONS */

/* Indices of keywords. */
static long description_index = -1L;
static long dims_index = -1L;
static long evaluations_index = -1L;
static long flags_index = -1L;
static long iterations_index = -1L;
static long lower_index = -1L;
static long mem_index = -1L;
static long name_index = -1L;
static long projections_index = -1L;
static long reason_index = -1L;
static long restarts_index = -1L;
static long single_index = -1L;
static long size_index = -1L;
static long status_index = -1L;
static long task_index = -1L;
static long upper_index = -1L;

static char descr[OPK_MAX(OPK_NLCG_DESCRIPTION_MAX_SIZE,
                          OPK_VMLMB_DESCRIPTION_MAX_SIZE)];

static void push_string(const char *value);
static void copy_dims(long dst[], const long src[]);
static int same_dims(const long adims[], const long bdims[]);
static long get_dims(int iarg, long dims[]);

/*---------------------------------------------------------------------------*/
/* OPTIMIZER OBJECT */

typedef struct _yopt_instance yopt_instance_t;
typedef struct _yopt_operations yopt_operations_t;

struct _yopt_operations {
  opk_task_t   (*start)(yopt_instance_t* opt);
  opk_task_t   (*iterate)(yopt_instance_t* opt, double fx);
  opk_task_t   (*get_task)(yopt_instance_t* opt);
  opk_status_t (*get_status)(yopt_instance_t* opt);
  unsigned int (*get_flags)(yopt_instance_t* opt);
  long         (*get_iterations)(yopt_instance_t* opt);
  long         (*get_evaluations)(yopt_instance_t* opt);
  long         (*get_restarts)(yopt_instance_t* opt);
  long         (*get_projections)(yopt_instance_t* opt);
  const char*  (*get_name)(yopt_instance_t* opt);
  const char*  (*get_description)(yopt_instance_t* opt);
};

#define START(opt)             ((opt)->ops->start(opt))
#define ITERATE(opt, fx)       ((opt)->ops->iterate(opt, fx))
#define GET_TASK(opt)          ((opt)->ops->get_task(opt))
#define GET_FLAGS(opt)         ((opt)->ops->get_flags(opt))
#define GET_STATUS(opt)        ((opt)->ops->get_status(opt))
#define GET_ITERATIONS(opt)    ((opt)->ops->get_iterations(opt))
#define GET_EVALUATIONS(opt)   ((opt)->ops->get_evaluations(opt))
#define GET_RESTARTS(opt)      ((opt)->ops->get_restarts(opt))
#define GET_PROJECTIONS(opt)   ((opt)->ops->get_projections(opt))
#define GET_NAME(opt)          ((opt)->ops->get_name(opt))
#define GET_DESCRIPTION(opt)   ((opt)->ops->get_description(opt))

struct _yopt_instance {
  long dims[Y_DIMSIZE];
  yopt_operations_t* ops;
  opk_object_t* optimizer;
  opk_vspace_t* vspace;
  opk_vector_t* x;
  opk_vector_t* gx;
  opk_bound_t* xl;
  opk_bound_t* xu;
  int single;
};

static void yopt_free(void *);
static void yopt_print(void *);
/*static void yopt_eval(void *, int);*/
static void yopt_extract(void *, char *);

static y_userobj_t yopt_type = {
  "OPKY optimizer",
  yopt_free,
  yopt_print,
  /*yopt_eval*/NULL,
  yopt_extract,
  NULL
};

static void
yopt_free(void* ptr)
{
  yopt_instance_t* opt = (yopt_instance_t*)ptr;
  OPK_DROP(opt->optimizer);
  OPK_DROP(opt->vspace);
  OPK_DROP(opt->x);
  OPK_DROP(opt->gx);
  OPK_DROP(opt->xl);
  OPK_DROP(opt->xu);
}

static void
yopt_print(void* ptr)
{
  yopt_instance_t* opt = (yopt_instance_t*)ptr;
  char buffer[100];
  y_print(GET_NAME(opt), FALSE);
  y_print(" implementing ", FALSE);
  y_print(GET_DESCRIPTION(opt), FALSE);
  sprintf(buffer, " (size=%ld, type=%s)",
          (long)opt->vspace->size,
          (opt->single ? "float" : "double"));
  y_print(buffer, TRUE);
}

/*static void yopt_eval(void *, int);*/

static void
yopt_extract(void* ptr, char* member)
{
  yopt_instance_t* opt = (yopt_instance_t*)ptr;
  long index = yget_global(member, 0);
  if (index == flags_index) {
    ypush_long(GET_FLAGS(opt));
  } else if (index == task_index) {
    ypush_int(GET_TASK(opt));
  } else if (index == status_index) {
    ypush_int(GET_STATUS(opt));
  } else if (index == reason_index) {
    push_string(opk_get_reason(GET_STATUS(opt)));
  } else if (index == description_index) {
    push_string(GET_DESCRIPTION(opt));
  } else if (index == name_index) {
    push_string(GET_NAME(opt));
  } else if (index == size_index) {
    ypush_long(opt->vspace->size);
  } else if (index == dims_index) {
    long ndims, dims[2];
    ndims = opt->dims[0];
    dims[0] = 1;
    dims[1] = ndims + 1;
    copy_dims(ypush_l(dims), opt->dims);
  } else if (index == iterations_index) {
    ypush_long(GET_ITERATIONS(opt));
  } else if (index == evaluations_index) {
    ypush_long(GET_EVALUATIONS(opt));
  } else if (index == restarts_index) {
    ypush_long(GET_RESTARTS(opt));
  } else if (index == projections_index) {
    ypush_long(GET_PROJECTIONS(opt));
  } else if (index == single_index) {
    ypush_int(opt->single);
  } else {
    ypush_nil();
  }
}


/*---------------------------------------------------------------------------*/
/* NON-LINEAR CONJUGATE GRADIENT (NLCG) METHOD */

#define NLCG(obj) ((opk_nlcg_t*)(obj))

static opk_task_t
nlcg_start(yopt_instance_t* opt)
{
  return opk_start_nlcg(NLCG(opt->optimizer), opt->x);
}

static opk_task_t
nlcg_iterate(yopt_instance_t* opt, double fx)
{
  return opk_iterate_nlcg(NLCG(opt->optimizer), opt->x, fx, opt->gx);
}

static opk_task_t
nlcg_get_task(yopt_instance_t* opt)
{
  return opk_get_nlcg_task(NLCG(opt->optimizer));
}

static opk_status_t
nlcg_get_status(yopt_instance_t* opt)
{
  return opk_get_nlcg_status(NLCG(opt->optimizer));
}

static unsigned int
nlcg_get_flags(yopt_instance_t* opt)
{
  return opk_get_nlcg_flags(NLCG(opt->optimizer));
}

static long
nlcg_get_iterations(yopt_instance_t* opt)
{
  return opk_get_nlcg_iterations(NLCG(opt->optimizer));
}

static long
nlcg_get_evaluations(yopt_instance_t* opt)
{
  return opk_get_nlcg_evaluations(NLCG(opt->optimizer));
}

static long
nlcg_get_restarts(yopt_instance_t* opt)
{
  return opk_get_nlcg_restarts(NLCG(opt->optimizer));
}

static long
nlcg_get_projections(yopt_instance_t* opt)
{
  return 0L;
}

static const char*
nlcg_get_name(yopt_instance_t* opt)
{
  return "NLCG";
}

static const char*
nlcg_get_description(yopt_instance_t* opt)
{
  opk_status_t status;
  status = opk_get_nlcg_description(NLCG(opt->optimizer), descr);
  if (status != OPK_SUCCESS) {
    y_error(opk_get_reason(status));
  }
  return descr;
}

static yopt_operations_t nlcg_ops = {
  nlcg_start,
  nlcg_iterate,
  nlcg_get_task,
  nlcg_get_status,
  nlcg_get_flags,
  nlcg_get_iterations,
  nlcg_get_evaluations,
  nlcg_get_restarts,
  nlcg_get_projections,
  nlcg_get_name,
  nlcg_get_description
};

/*---------------------------------------------------------------------------*/
/* LIMITED MEMORY VARIABLE METRIC METHOD WITH BOUNDS */

#define VMLMB(obj) ((opk_vmlmb_t*)(obj))

static opk_task_t
vmlmb_start(yopt_instance_t* opt)
{
  return opk_start_vmlmb(VMLMB(opt->optimizer), opt->x);
}

static opk_task_t
vmlmb_iterate(yopt_instance_t* opt, double fx)
{
  return opk_iterate_vmlmb(VMLMB(opt->optimizer), opt->x, fx, opt->gx);
}

static opk_task_t
vmlmb_get_task(yopt_instance_t* opt)
{
  return opk_get_vmlmb_task(VMLMB(opt->optimizer));
}

static opk_status_t
vmlmb_get_status(yopt_instance_t* opt)
{
  return opk_get_vmlmb_status(VMLMB(opt->optimizer));
}

static unsigned int
vmlmb_get_flags(yopt_instance_t* opt)
{
  return opk_get_vmlmb_flags(VMLMB(opt->optimizer));
}

static long
vmlmb_get_iterations(yopt_instance_t* opt)
{
  return opk_get_vmlmb_iterations(VMLMB(opt->optimizer));
}

static long
vmlmb_get_evaluations(yopt_instance_t* opt)
{
  return opk_get_vmlmb_evaluations(VMLMB(opt->optimizer));
}

static long
vmlmb_get_restarts(yopt_instance_t* opt)
{
  return opk_get_vmlmb_restarts(VMLMB(opt->optimizer));
}

static long
vmlmb_get_projections(yopt_instance_t* opt)
{
  return opk_get_vmlmb_evaluations(VMLMB(opt->optimizer));
}

static const char*
vmlmb_get_name(yopt_instance_t* opt)
{
  return opk_get_vmlmb_method_name(VMLMB(opt->optimizer));
}

static const char*
vmlmb_get_description(yopt_instance_t* opt)
{
  opk_status_t status;
  status = opk_get_vmlmb_description(VMLMB(opt->optimizer), descr);
  if (status != OPK_SUCCESS) {
    y_error(opk_get_reason(status));
  }
  return descr;
}

static yopt_operations_t vmlmb_ops = {
  vmlmb_start,
  vmlmb_iterate,
  vmlmb_get_task,
  vmlmb_get_status,
  vmlmb_get_flags,
  vmlmb_get_iterations,
  vmlmb_get_evaluations,
  vmlmb_get_restarts,
  vmlmb_get_projections,
  vmlmb_get_name,
  vmlmb_get_description
};

/*---------------------------------------------------------------------------*/
/* PRIVATE FUNCTIONS */

static void push_string(const char *value)
{
  ypush_q((long *)NULL)[0] = (value ? p_strcpy((char *)value) : NULL);
}

static void
error_handler(const char* message)
{
  y_error(message);
}

static int same_dims(const long adims[], const long bdims[])
{
  long i, ndims = adims[0];
  for (i = 0; i <= ndims; ++i) {
    if (bdims[i] != adims[i]) {
      return OPK_FALSE;
    }
  }
  return OPK_TRUE;
}

static void copy_dims(long dst[], const long src[])
{
  long i, ndims = src[0];
  for (i = 0; i <= ndims; ++i) {
    dst[i] = src[i];
  }
}

static long get_dims(int iarg, long dims[])
{
  long dim;
  int rank, type;

  type = yarg_typeid(iarg);
  if (type == Y_VOID) {
    dims[0] = 0;
    return 1L;
  }
  if (type > Y_LONG) {
    y_error("invalid type for dimension list");
  }
  rank = yarg_rank(iarg);
  if (rank == 0) {
    /* Got a scalar. */
    dim = ygets_l(iarg);
    if (dim < 1) y_error("invalid dimension length");
    dims[0] = 1;
    dims[1] = dim;
    return dim;
  }
  if (rank == 1) {
    /* Got a vector. */
    long ntot, ndims, i, number = 1L;
    long* vals = ygeta_l(iarg, &ntot, NULL);
    if (ntot > Y_DIMSIZE) {
      y_error("too many dimensions");
    }
    ndims = ntot - 1;
    if (vals[0] == ndims) {
      dims[0] = ndims;
      for (i = 1; i <= ndims; ++i) {
        dim = vals[i];
        if (dim < 1) y_error("invalid dimension length");
        dims[i] = dim;
        number *= dim;
      }
      return number;
    }
  }

  y_error("invalid dimension list");
  return -1L;
}

static opk_vector_t*
create_wrapper(opk_vspace_t* vspace, int single)
{
  if (single) {
    float dummy = 0.0f;
    return opk_wrap_simple_float_vector(vspace, &dummy, NULL, NULL);
  } else {
    double dummy = 0.0;
    return opk_wrap_simple_double_vector(vspace, &dummy, NULL, NULL);
  }
}

static void
rewrap(int iarg, yopt_instance_t* opt, opk_vector_t* vect)
{
  long dims[Y_DIMSIZE];
  if (opt->single) {
    float* data = ygeta_f(iarg, NULL, dims);
    if (! same_dims(opt->dims, dims)) {
      y_error("bad dimensions");
    }
    if (opk_rewrap_simple_float_vector(vect, data, NULL, NULL) != OPK_SUCCESS) {
      y_error("failed to wrap vector");
    }
  } else {
    double* data = ygeta_d(iarg, NULL, dims);
    if (! same_dims(opt->dims, dims)) {
      y_error("bad dimensions");
    }
    if (opk_rewrap_simple_double_vector(vect, data, NULL, NULL) != OPK_SUCCESS) {
      y_error("failed to wrap vector");
    }
  }
}

static opk_bound_t*
get_bound(int iarg, yopt_instance_t* opt)
{
  opk_bound_t* bnd = NULL;
  int rank;

  rank = yarg_rank(iarg);
  if (rank == 0) {
    /* Got a scalar. */
    double value = ygets_d(iarg);
    bnd = opk_new_bound(opt->vspace, OPK_BOUND_SCALAR, &value);
  } else if (rank > 0) {
    /* Got an array.  FIXME: for now we do a copy. */
    long ntot, dims[Y_DIMSIZE];
    void* src;
    opk_vector_t* vector;
    if (opt->single) {
      src = ygeta_f(iarg, &ntot, dims);
    } else {
      src = ygeta_d(iarg, &ntot, dims);
    }
    if (! same_dims(opt->dims, dims)) {
      y_error("bad bound dimensions");
    }
    if (ntot != opt->vspace->size) {
      y_error("bad number of elements for the bound");
    }
    vector = opk_vcreate(opt->vspace);
    if (vector == NULL) {
      y_error("failed to create a \"vector\" for the bound");
    }
    bnd = opk_new_bound(opt->vspace, OPK_BOUND_VECTOR, vector);
    OPK_DROP(vector); /* must be done before error checking */
    if (bnd == NULL) {
      y_error("failed to create the bound");
    }
    if (opt->single) {
      float* dst = opk_get_simple_float_vector_data(vector);
      memcpy(dst, src, ntot*sizeof(float));
    } else {
      double* dst = opk_get_simple_double_vector_data(vector);
      memcpy(dst, src, ntot*sizeof(double));
    }
  } else if (! yarg_nil(iarg)) {
    y_error("invalid bound");
  }
  return bnd;
}

static void set_global_int(const char* name, int value)
{
  ypush_int(value);
  yput_global(yget_global(name, 0), 0);
  yarg_drop(1);
}

static unsigned int
get_optional_uint(int iarg, unsigned int def)
{
  int type = yarg_typeid(iarg);
  if (type == Y_VOID) {
    return def;
  }
  if (type > Y_LONG || yarg_rank(iarg) != 0) {
    y_error("expecting nothing or an integer scalar");
  }
  return (unsigned int)ygets_l(iarg);
}

static long
get_optional_long(int iarg, int def)
{
  int type = yarg_typeid(iarg);
  if (type == Y_VOID) {
    return def;
  }
  if (type > Y_LONG || yarg_rank(iarg) != 0) {
    y_error("expecting nothing or an integer scalar");
  }
  return ygets_l(iarg);
}

/*---------------------------------------------------------------------------*/
/* BUILTIN FUNCTIONS */

void Y_opk_init(int argc)
{
  opk_set_error_handler(error_handler);

#define GET_GLOBAL(a) a##_index = yget_global(#a, 0)
  GET_GLOBAL(description);
  GET_GLOBAL(dims);
  GET_GLOBAL(evaluations);
  GET_GLOBAL(flags);
  GET_GLOBAL(iterations);
  GET_GLOBAL(mem);
  GET_GLOBAL(name);
  GET_GLOBAL(reason);
  GET_GLOBAL(restarts);
  GET_GLOBAL(projections);
  GET_GLOBAL(single);
  GET_GLOBAL(size);
  GET_GLOBAL(status);
  GET_GLOBAL(task);
  GET_GLOBAL(upper);
  GET_GLOBAL(lower);
#undef GET_GLOBAL

#define SET_GLOBAL_INT(a) set_global_int(#a, a)
  SET_GLOBAL_INT(OPK_TASK_ERROR);
  SET_GLOBAL_INT(OPK_TASK_COMPUTE_FG);
  SET_GLOBAL_INT(OPK_TASK_NEW_X);
  SET_GLOBAL_INT(OPK_TASK_FINAL_X);
  SET_GLOBAL_INT(OPK_TASK_WARNING);

  SET_GLOBAL_INT(OPK_NLCG_FLETCHER_REEVES);
  SET_GLOBAL_INT(OPK_NLCG_HESTENES_STIEFEL);
  SET_GLOBAL_INT(OPK_NLCG_POLAK_RIBIERE_POLYAK);
  SET_GLOBAL_INT(OPK_NLCG_FLETCHER);
  SET_GLOBAL_INT(OPK_NLCG_LIU_STOREY);
  SET_GLOBAL_INT(OPK_NLCG_DAI_YUAN);
  SET_GLOBAL_INT(OPK_NLCG_PERRY_SHANNO);
  SET_GLOBAL_INT(OPK_NLCG_HAGER_ZHANG);
  SET_GLOBAL_INT(OPK_NLCG_POWELL);
  SET_GLOBAL_INT(OPK_NLCG_SHANNO_PHUA);
  SET_GLOBAL_INT(OPK_NLCG_DEFAULT);

  SET_GLOBAL_INT(OPK_EMULATE_BLMVM);
#undef SET_GLOBAL_INT

  ypush_nil();
}

void Y_opk_nlcg(int argc)
{
  yopt_instance_t* opt;
  long n = -1;
  long dims[Y_DIMSIZE];
  int iarg, single = FALSE;
  unsigned int flags = OPK_NLCG_DEFAULT;

  for (iarg = argc - 1; iarg >= 0; --iarg) {
    long index = yarg_key(iarg);
    if (index < 0L) {
      /* Non-keyword argument. */
      if (n != -1) {
        y_error("too many arguments");
      }
      n = get_dims(iarg, dims);
    } else {
      /* Keyword argument. */
      --iarg;
      if (index == single_index) {
        single = yarg_true(iarg);
      } else if (index == flags_index) {
        flags = get_optional_uint(iarg, flags);
      } else {
        y_error("unknown keyword");
      }
    }
  }
  if (n == -1) {
    y_error("not enough arguments");
  }
  opt = (yopt_instance_t*)ypush_obj(&yopt_type, sizeof(yopt_instance_t));
  copy_dims(opt->dims, dims);
  opt->ops = &nlcg_ops;
  opt->single = single;
  if (single) {
    opt->vspace = opk_new_simple_float_vector_space(n);
  } else {
    opt->vspace = opk_new_simple_double_vector_space(n);
  }
  if (opt->vspace == NULL) {
    y_error("failed to create vector space");
  }
  opt->x = create_wrapper(opt->vspace, single);
  opt->gx = create_wrapper(opt->vspace, single);
  if (opt->x == NULL || opt->gx == NULL) {
    y_error("failed to create working vectors");
  }
  opt->optimizer = (opk_object_t*)opk_new_nlcg_optimizer(opt->vspace, flags, NULL);
  if (opt->optimizer == NULL) {
    y_error(opk_get_reason(opk_guess_status(errno)));
  }
}

void Y_opk_vmlmb(int argc)
{
  yopt_instance_t* opt;
  long n = -1, mem = 5;
  long dims[Y_DIMSIZE];
  int iarg, single = FALSE;
  int lower = -1, upper = -1;
  unsigned int flags = 0;

  for (iarg = argc - 1; iarg >= 0; --iarg) {
    long index = yarg_key(iarg);
    if (index < 0L) {
      /* Non-keyword argument. */
      if (n != -1) {
        y_error("too many arguments");
      }
      n = get_dims(iarg, dims);
    } else {
      /* Keyword argument. */
      --iarg;
      if (index == mem_index) {
        mem = get_optional_long(iarg, mem);
        if (mem <= 0) y_error("invalid value for MEM keyword");
      } else if (index == flags_index) {
        flags = get_optional_uint(iarg, flags);
      } else if (index == single_index) {
        single = yarg_true(iarg);
      } else if (index == lower_index) {
        lower = iarg;
      } else if (index == upper_index) {
        upper = iarg;
      } else {
        y_error("unknown keyword");
      }
    }
  }
  if (n == -1) {
    y_error("not enough arguments");
  }
  opt = (yopt_instance_t*)ypush_obj(&yopt_type, sizeof(yopt_instance_t));
  copy_dims(opt->dims, dims);
  opt->ops = &vmlmb_ops;
  opt->single = single;
  if (single) {
    opt->vspace = opk_new_simple_float_vector_space(n);
  } else {
    opt->vspace = opk_new_simple_double_vector_space(n);
  }
  if (opt->vspace == NULL) {
    y_error("failed to create vector space");
  }
  opt->x = create_wrapper(opt->vspace, single);
  opt->gx = create_wrapper(opt->vspace, single);
  if (opt->x == NULL || opt->gx == NULL) {
    y_error("failed to create working vectors");
  }
  opt->xl = (lower >= 0 ? get_bound(lower + 1, opt) : NULL);
  opt->xu = (upper >= 0 ? get_bound(upper + 1, opt) : NULL);
  opt->optimizer = (opk_object_t*)opk_new_vmlmb_optimizer(opt->vspace, mem, flags,
                                                          opt->xl, opt->xu, NULL);
  if (opt->optimizer == NULL) {
    y_error(opk_get_reason(opk_guess_status(errno)));
  }
}

void Y_opk_task(int argc)
{
  yopt_instance_t* opt;

  if (argc != 1) {
    y_error("expecting exactly 1 argument");
  }
  opt = yget_obj(0, &yopt_type);
  opt->ops->get_task(opt);
}

void Y_opk_start(int argc)
{
  yopt_instance_t* opt;
  opk_task_t task;

  if (argc != 2) {
    y_error("expecting exactly 2 arguments");
  }
  opt = yget_obj(1, &yopt_type);
  rewrap(0, opt, opt->x);
  task = START(opt);
  ypush_int(task);
}

void Y_opk_iterate(int argc)
{
  yopt_instance_t* opt;
  double fx;
  opk_task_t task;

  if (argc != 4) {
    y_error("expecting 4 arguments");
  }
  opt = yget_obj(3, &yopt_type);
  rewrap(2, opt, opt->x);
  fx = ygets_d(1);
  rewrap(0, opt, opt->gx);
  task = ITERATE(opt, fx);
  ypush_int(task);
}

void Y_opk_get_status(int argc)
{
  yopt_instance_t* opt;
  opk_status_t status;

  if (argc != 1) y_error("expecting exactly one argument");
  opt = yget_obj(0, &yopt_type);
  status = GET_STATUS(opt);
  ypush_int(status);
}

void Y_opk_get_task(int argc)
{
  yopt_instance_t* opt;
  opk_task_t task;

  if (argc != 1) y_error("expecting exactly one argument");
  opt = yget_obj(0, &yopt_type);
  task = GET_TASK(opt);
  ypush_int(task);
}

void Y_opk_get_reason(int argc)
{
  if (argc != 1) y_error("expecting exactly one argument");
  push_string(opk_get_reason(ygets_l(0)));
}
