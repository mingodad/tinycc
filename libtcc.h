#ifndef LIBTCC_H
#define LIBTCC_H

#ifndef LIBTCCAPI
# define LIBTCCAPI
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct TCCState;

typedef struct TCCState TCCState;

#ifndef _OFF_T_DEFINED
#define _OFF_T_DEFINED
#ifndef _OFF_T_
#define _OFF_T_
  typedef long _off_t;
#ifndef _SIZE_T_
#define _SIZE_T_
  typedef unsigned long size_t;
#endif
#if !defined(NO_OLDNAMES) || defined(_POSIX)
  typedef long off_t;
#endif
#endif
#endif

struct vio_module_t;

typedef struct vio_fd {
    int fd;
    void *vio_udata;
    struct vio_module_t *vio_module;
} vio_fd;

#define CALL_VIO_OPEN_FIRST 0x01
#define CALL_VIO_OPEN_LAST 0x02

typedef struct vio_module_t {
    void *user_data;
    struct TCCState *tcc_state;
    int call_vio_open_flags; /*CALL_VIO_OPEN_FIRST, CALL_VIO_OPEN_LAST, one or both */
    int (*vio_open)(vio_fd *fd, const char *fn, int oflag) ;
    off_t (*vio_lseek)(vio_fd fd, off_t offset, int whence);
    size_t (*vio_read)(vio_fd fd, void *buf, size_t bytes);
    int (*vio_close)(vio_fd *fd);
} vio_module_t;


/* create a new TCC compilation context */
LIBTCCAPI TCCState *tcc_new(void);

/* free a TCC compilation context */
LIBTCCAPI void tcc_delete(TCCState *s);

/* set CONFIG_TCCDIR at runtime */
LIBTCCAPI void tcc_set_lib_path(TCCState *s, const char *path);

/* set error/warning display callback */
LIBTCCAPI void tcc_set_error_func(TCCState *s, void *error_opaque,
    void (*error_func)(void *opaque, const char *msg));

/* set options as from command line (multiple supported) */
LIBTCCAPI int tcc_set_options(TCCState *s, const char *str);

/* set virtual io module */
LIBTCCAPI void tcc_set_vio_module(TCCState *s, vio_module_t *vio_module);

/*****************************/
/* preprocessor */

/* add include path */
LIBTCCAPI int tcc_add_include_path(TCCState *s, const char *pathname);

/* add in system include path */
LIBTCCAPI int tcc_add_sysinclude_path(TCCState *s, const char *pathname);

/* define preprocessor symbol 'sym'. Can put optional value */
LIBTCCAPI void tcc_define_symbol(TCCState *s, const char *sym, const char *value);

/* undefine preprocess symbol 'sym' */
LIBTCCAPI void tcc_undefine_symbol(TCCState *s, const char *sym);

/*****************************/
/* compiling */

/* add a file (C file, dll, object, library, ld script). Return -1 if error. */
LIBTCCAPI int tcc_add_file(TCCState *s, const char *filename);

/* compile a string containing a C source. Return -1 if error. */
LIBTCCAPI int tcc_compile_string(TCCState *s, const char *buf);

/* compile a string containing a C source. Return non zero if
   error. Can associate a name with string for errors. */
int tcc_compile_named_string(TCCState *s, const char *buf, const char *strname);

/*****************************/
/* linking commands */

/* set output type. MUST BE CALLED before any compilation */
LIBTCCAPI int tcc_set_output_type(TCCState *s, int output_type);
#define TCC_OUTPUT_MEMORY   0 /* output will be run in memory (default) */
#define TCC_OUTPUT_EXE      1 /* executable file */
#define TCC_OUTPUT_DLL      2 /* dynamic library */
#define TCC_OUTPUT_OBJ      3 /* object file */
#define TCC_OUTPUT_PREPROCESS 4 /* only preprocess (used internally) */

/* equivalent to -Lpath option */
LIBTCCAPI int tcc_add_library_path(TCCState *s, const char *pathname);

/* the library name is the same as the argument of the '-l' option */
LIBTCCAPI int tcc_add_library(TCCState *s, const char *libraryname);

/* add a symbol to the compiled program */
LIBTCCAPI int tcc_add_symbol(TCCState *s, const char *name, const void *val);

/* output an executable, library or object file. DO NOT call
   tcc_relocate() before. */
LIBTCCAPI int tcc_output_file(TCCState *s, const char *filename);

/* link and run main() function and return its value. DO NOT call
   tcc_relocate() before. */
LIBTCCAPI int tcc_run(TCCState *s, int argc, char **argv);

/* do all relocations (needed before using tcc_get_symbol()) */
LIBTCCAPI int tcc_relocate(TCCState *s1, void *ptr);
/* possible values for 'ptr':
   - TCC_RELOCATE_AUTO : Allocate and manage memory internally
   - NULL              : return required memory size for the step below
   - memory address    : copy code to memory passed by the caller
   returns -1 if error. */
#define TCC_RELOCATE_AUTO (void*)1

/* return symbol value or NULL if not found */
LIBTCCAPI void *tcc_get_symbol(TCCState *s, const char *name);

#ifdef __cplusplus
}
#endif

#endif
