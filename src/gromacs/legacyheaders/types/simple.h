/*
 *
 *                This source code is part of
 *
 *                 G   R   O   M   A   C   S
 *
 *          GROningen MAchine for Chemical Simulations
 *
 *                        VERSION 3.2.0
 * Written by David van der Spoel, Erik Lindahl, Berk Hess, and others.
 * Copyright (c) 1991-2000, University of Groningen, The Netherlands.
 * Copyright (c) 2001-2004, The GROMACS development team,
 * check out http://www.gromacs.org for more information.

 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * If you want to redistribute modifications, please consider that
 * scientific software is very special. Version control is crucial -
 * bugs must be traceable. We will be happy to consider code for
 * inclusion in the official distribution, but derived work must not
 * be called official GROMACS. Details are found in the README & COPYING
 * files - if they are missing, get the official version at www.gromacs.org.
 *
 * To help us fund GROMACS development, we humbly ask that you cite
 * the papers on the package - you can find them in the top README file.
 *
 * For more info, check our website at http://www.gromacs.org
 *
 * And Hey:
 * GRoups of Organic Molecules in ACtion for Science
 */

#ifndef _simple_h
#define _simple_h

/* Dont remove this instance of HAVE_CONFIG_H!!!
 *
 * We dont _require_ config.h here, but IF one is
 * available it might contain valuable information about simple types
 * that helps us automate things better and avoid bailing out.
 *
 * Note that this does not have to be the gromacs config.h - several
 * package setups define these simple types.
 */
#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/* Information about integer data type sizes */
#include <limits.h>
#define __STDC_LIMIT_MACROS
#include <stdint.h>
#ifndef _MSC_VER
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif
#if 0
}
#endif


#define XX      0           /* Defines for indexing in	*/
#define YY      1           /* vectors			*/
#define ZZ      2
#define DIM     3           /* Dimension of vectors		*/
#define XXXX    0           /* defines to index matrices */
#define XXYY    1
#define XXZZ    2
#define YYXX    3
#define YYYY    4
#define YYZZ    5
#define ZZXX    6
#define ZZYY    7
#define ZZZZ    8

/* There is no standard size for 'bool' in C++, so when
 * we previously defined it to int for C code the data types
 * (and structs) would have different size depending on your compiler,
 * both at gromacs build time and when you use the library.
 * The only way around this is to NOT assume anything about the C++ type,
 * so we cannot use the name 'bool' in our C code anymore.
 */

typedef int gmx_bool;

#ifndef FALSE
#  define FALSE   0
#endif
#ifndef TRUE
#  define TRUE    1
#endif
#define BOOL_NR 2


typedef int         atom_id;      /* To indicate an atoms id         */
#define NO_ATID     (atom_id)(~0) /* Use this to indicate invalid atid */

/*! \brief Double precision accuracy */
#define GMX_DOUBLE_EPS   1.11022302E-16

/*! \brief Maximum double precision value - reduced 1 unit in last digit for MSVC */
#define GMX_DOUBLE_MAX   1.79769312E+308

/*! \brief Minimum double precision value */
#define GMX_DOUBLE_MIN   2.22507386E-308

/*! \brief Single precision accuracy */
#define GMX_FLOAT_EPS    5.96046448E-08

/*! \brief Maximum single precision value - reduced 1 unit in last digit for MSVC */
#define GMX_FLOAT_MAX    3.40282346E+38

/*! \brief Minimum single precision value */
#define GMX_FLOAT_MIN    1.17549435E-38


/* Check whether we already have a real type! */
#ifdef GMX_DOUBLE

#ifndef HAVE_REAL
typedef double      real;
#define HAVE_REAL
#endif

#define GMX_MPI_REAL    MPI_DOUBLE
#define GMX_REAL_EPS    GMX_DOUBLE_EPS
#define GMX_REAL_MIN    GMX_DOUBLE_MIN
#define GMX_REAL_MAX    GMX_DOUBLE_MAX
#define gmx_real_fullprecision_pfmt "%21.14e"
#else

#ifndef HAVE_REAL
typedef float           real;
#define HAVE_REAL
#endif

#define GMX_MPI_REAL    MPI_FLOAT
#define GMX_REAL_EPS    GMX_FLOAT_EPS
#define GMX_REAL_MIN    GMX_FLOAT_MIN
#define GMX_REAL_MAX    GMX_FLOAT_MAX
#define gmx_real_fullprecision_pfmt "%14.7e"
#endif

typedef real            rvec[DIM];

typedef double          dvec[DIM];

typedef real            matrix[DIM][DIM];

typedef real            tensor[DIM][DIM];

typedef int             ivec[DIM];

typedef int             imatrix[DIM][DIM];

#ifdef _MSC_VER
typedef __int32 gmx_int32_t;
#define GMX_PRId32 "I32d"
#define GMX_SCNd32 "I32d"

typedef __int64 gmx_int64_t;
#define GMX_PRId64 "I64d"
#define GMX_SCNd64 "I64d"

typedef unsigned __int32 gmx_uint32_t;
#define GMX_PRIu32 "U32d"
#define GMX_SCNu32 "U32d"

typedef unsigned __int64 gmx_uint64_t;
#define GMX_PRIu64 "U64d"
#define GMX_SCNu64 "U64d"
#else
typedef int32_t gmx_int32_t;
#define GMX_PRId32 PRId32
#define GMX_SCNd32 SCNd32

typedef int64_t gmx_int64_t;
#define GMX_PRId64 PRId64
#define GMX_SCNd64 SCNd64

typedef uint32_t gmx_uint32_t;
#define GMX_PRIu32 PRIu32
#define GMX_SCNu32 SCNu32

typedef uint64_t gmx_uint64_t;
#define GMX_PRIu64 PRIu64
#define GMX_SCNu64 SCNu64
#endif

#define GMX_INT32_MAX INT32_MAX
#define GMX_INT32_MIN INT32_MIN

#define GMX_INT64_MAX INT64_MAX
#define GMX_INT64_MIN INT64_MIN

#define GMX_UINT32_MAX UINT32_MAX
#define GMX_UINT32_MIN UINT32_MIN

#define GMX_UINT64_MAX UINT64_MAX
#define GMX_UINT64_MIN UINT64_MIN

#ifndef gmx_inline
/* config.h tests for inline definitions and should work on a much wider range
 * of compilers, but does not work with installed headers. These compiler checks
 * still enable a real inline keyword for the most common compilers.
 */

/* Try to define suitable inline keyword for gmx_inline.
 * Set it to empty if we cannot find one (and dont complain to the user)
 */
#ifndef __cplusplus

#ifdef __GNUC__
/* GCC */
#  define gmx_inline   __inline__
#elif (defined(__INTEL_COMPILER) || defined(__ECC)) && defined(__ia64__)
/* ICC */
#  define gmx_inline __inline__
#elif defined(__PATHSCALE__)
/* Pathscale */
#  define gmx_inline __inline__
#elif defined(__PGIC__)
/* Portland */
#  define gmx_inline __inline
#elif defined _MSC_VER
/* MSVC */
#  define gmx_inline __inline
#elif defined(__xlC__)
/* IBM */
#  define gmx_inline __inline
#else
#  define gmx_inline
#endif

#else
/* C++ */
#  define gmx_inline inline
#endif

#endif /* ifndef gmx_inline */


/* Restrict keywords. Note that this has to be done for C++ too, unless
 * it was set from the more general checks if we had config.h (gmx internal)
 */
#ifndef gmx_restrict

#ifdef __GNUC__
/* GCC */
#  define gmx_restrict   __restrict__
#elif (defined(__INTEL_COMPILER) || defined(__ECC)) && defined(__ia64__)
/* ICC */
#  define gmx_restrict __restrict__
#elif defined(__PATHSCALE__)
/* Pathscale */
#  define gmx_restrict __restrict
#elif defined(__PGIC__)
/* Portland */
#  define gmx_restrict __restrict
#elif defined _MSC_VER
/* MSVC */
#  define gmx_restrict __restrict
#elif defined(__xlC__)
/* IBM */
#  define gmx_restrict __restrict
#else
#  define gmx_restrict
#endif

#endif

/*
 * These attributes suppress compiler warnings about unused function arguments
 * by marking them as possibly unused. Some arguments are unused but
 * have to be retained to preserve a function signature
 * that must match that of another function.
 * Some arguments are only used in *some* code paths (e.g. MPI)
 */

#ifndef gmx_unused
#ifdef __GNUC__
/* GCC, clang, and some ICC pretending to be GCC */
#  define gmx_unused __attribute__ ((unused))
#elif (defined(__INTEL_COMPILER) || defined(__ECC)) && !defined(_MSC_VER)
/* ICC on *nix */
#  define gmx_unused __attribute__ ((unused))
#elif defined _MSC_VER
/* MSVC */
#  define gmx_unused /*@unused@*/
#elif defined(__xlC__)
/* IBM */
#  define gmx_unused __attribute__ ((unused))
#else
#  define gmx_unused
#endif
#endif

/* Standard sizes for char* string buffers */
#define STRLEN 4096
#define BIG_STRLEN 1048576


#ifdef __cplusplus
}
#endif

#endif
