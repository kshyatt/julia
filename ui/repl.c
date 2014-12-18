/*
  repl.c
  system startup, main(), and console interaction
*/

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <setjmp.h>
#include <signal.h>
#include <assert.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include <errno.h>
#include <math.h>
#include <ctype.h>

#ifndef _MSC_VER
#include <unistd.h>
#include <getopt.h>
#else
#include "getopt.h"
#endif

#include "uv.h"
#define WHOLE_ARCHIVE
#include "../src/julia.h"

#ifndef JL_SYSTEM_IMAGE_PATH
#error "JL_SYSTEM_IMAGE_PATH not defined!"
#endif

#ifdef __cplusplus
extern "C" {
#endif

static int lisp_prompt = 0;
static int codecov  = JL_LOG_NONE;
static int malloclog= JL_LOG_NONE;
static char *program = NULL;
static int imagepathspecified = 0;

static const char *usage = "julia [options] [program] [args...]\n";
static const char *opts =
    " -v, --version             Display version information\n"
    " -h, --help                Print this message\n"
    " -q, --quiet               Quiet startup without banner\n"
    " -H, --home <dir>          Set location of julia executable\n\n"

    " -e, --eval <expr>         Evaluate <expr>\n"
    " -E, --print <expr>        Evaluate and show <expr>\n"
    " -P, --post-boot <expr>    Evaluate <expr>, but don't disable interactive mode\n"
    " -L, --load <file>         Load <file> immediately on all processors\n"
    " -J, --sysimage <file>     Start up with the given system image file\n"
    " -C, --cpu-target <target> Limit usage of cpu features up to <target>\n\n"

    " -p, --procs <n>           Run n local processes\n"
    " --machinefile <file>      Run processes on hosts listed in <file>\n\n"

    " -i                        Force isinteractive() to be true\n"
    " --color={yes|no}          Enable or disable color text\n\n"
    " --history-file={yes|no}   Load or save history\n"
    " --no-history-file         Don't load history file (deprecated, use --history-file=no)\n"
    " --startup-file={yes|no}   Load ~/.juliarc.jl\n"
    " -f, --no-startup          Don't load ~/.juliarc (deprecated, use --startup-file=no)\n"
    " -F                        Load ~/.juliarc (deprecated, use --startup-file=yes)\n\n"

    " --compile={yes|no|all}    Enable or disable compiler, or request exhaustive compilation\n\n"

    " --code-coverage={none|user|all}, --code-coverage\n"
    "                           Count executions of source lines (omitting setting is equivalent to 'user')\n\n"

    " --track-allocation={none|user|all}, --track-allocation\n"
    "                           Count bytes allocated by each source line\n\n"
    " -O, --optimize\n"
    "                           Run time-intensive code optimizations\n\n"

    " --check-bounds={yes|no}   Emit bounds checks always or never (ignoring declarations)\n"
    " --int-literals={32|64}    Select integer literal size independent of platform\n"
    " --dump-bitcode={yes|no}   Dump bitcode for the system image (used with --build)\n"
    " --depwarn={yes|no}        Enable or disable syntax and method deprecation warnings\n";

void parse_opts(int *argcp, char ***argvp)
{
    enum { machinefile = 300,
	   color,
	   history_file,
	   no_history_file,
	   startup_file,
	   compile,
	   code_coverage,
	   track_allocation,
	   check_bounds,
	   int_literals,
	   dump_bitcode,
	   depwarn,
	   worker,
	   bind_to
    };
    static char* shortopts = "+vhqFfH:e:E:P:L:J:C:ip:Ob:";
    static struct option longopts[] = {
        // exposed command line options
        { "version",         no_argument,       0, 'v' },
        { "help",            no_argument,       0, 'h' },
        { "quiet",           no_argument,       0, 'q' },
        { "home",            required_argument, 0, 'H' },
        { "eval",            required_argument, 0, 'e' },
        { "print",           required_argument, 0, 'E' },
        { "post-boot",       required_argument, 0, 'P' },
        { "load",            required_argument, 0, 'L' },
        { "sysimage",        required_argument, 0, 'J' },
        { "cpu-target",      required_argument, 0, 'C' },
        { "procs",           required_argument, 0, 'p' },
        { "machinefile",     required_argument, 0, machinefile },
        { "color",           required_argument, 0, color },
        { "history-file",    required_argument, 0, history_file },
        { "no-history-file", no_argument,       0, no_history_file }, // deprecated
        { "startup-file",    required_argument, 0, startup_file },
        { "no-startup",      no_argument,       0, 'f' }, // deprecated
        { "compile",         required_argument, 0, compile },
        { "code-coverage",   optional_argument, 0, code_coverage },
        { "track-allocation",optional_argument, 0, track_allocation },
        { "optimize",        no_argument,       0, 'O' },
        { "check-bounds",    required_argument, 0, check_bounds },
        { "int-literals",    required_argument, 0, int_literals },
        { "dump-bitcode",    required_argument, 0, dump_bitcode },
        { "depwarn",         required_argument, 0, depwarn },
        // hidden command line options
        { "build",           required_argument, 0, 'b' },
        { "worker",          no_argument,       0, worker },
        { "bind-to",         required_argument, 0, bind_to },
        { "lisp",            no_argument,       &lisp_prompt, 1 },
        { 0, 0, 0, 0 }
    };
    int c;
    char *endptr;
    opterr = 0;
    int skip = 0;
    int lastind = optind;
    while ((c = getopt_long(*argcp,*argvp,shortopts,longopts,0)) != -1) {
        switch (c) {
        case 0:
            break;
        case '?':
	    if (optind != lastind) skip++;
	    lastind = optind;
	    break;
        case 'v': // version
            jl_options.version = 1;
            break;
        case 'h': // help
            ios_printf(ios_stdout, "%s%s", usage, opts);
            exit(0);
        case 'q': // quiet
            jl_options.quiet = 1;
            break;
        case 'H': // home
            jl_compileropts.julia_home = strdup(optarg);
            break;
        case 'e': // eval
            jl_options.eval = strdup(optarg);
            break;
        case 'E': // print
            jl_options.print = strdup(optarg);
            break;
        case 'P': // post-boot
            jl_options.postboot = strdup(optarg);
            break;
        case 'L': // load
            jl_options.load = strdup(optarg);
            break;
        case 'J': // sysimage
            jl_compileropts.image_file = strdup(optarg);
            imagepathspecified = 1;
            break;
        case 'C': // cpu-target
            jl_compileropts.cpu_target = strdup(optarg);
            break;
        case 'p': // procs
            errno = 0;
            jl_options.nprocs = strtol(optarg, &endptr, 10);
            if (errno != 0 || optarg == endptr || *endptr != 0 || jl_options.nprocs < 1) {
                ios_printf(ios_stderr, "julia: -p,--procs=<n> must be an integer >= 1\n");
                exit(1);
            }
            break;
        case machinefile:
            jl_options.machinefile = strdup(optarg);
            break;
        case color:
            if (!strcmp(optarg,"yes"))
                jl_options.color = JL_OPTIONS_COLOR_ON;
            else if (!strcmp(optarg,"no"))
                jl_options.color = JL_OPTIONS_COLOR_OFF;
            else {
                ios_printf(ios_stderr, "julia: invalid argument to --color={yes|no} (%s)\n", optarg);
                exit(1);
            }
            break;
        case history_file:
            if (!strcmp(optarg,"yes"))
                jl_options.historyfile = JL_OPTIONS_HISTORYFILE_ON;
            else if (!strcmp(optarg,"no"))
                jl_options.historyfile = JL_OPTIONS_HISTORYFILE_OFF;
            else {
                ios_printf(ios_stderr, "julia: invalid argument to --history-file={yes|no} (%s)\n", optarg);
                exit(1);
            }
            break;
	case no_history_file:
	    jl_options.historyfile = JL_OPTIONS_HISTORYFILE_OFF;
	    break;
        case startup_file:
            if (!strcmp(optarg,"yes"))
                jl_options.startupfile = JL_OPTIONS_STARTUPFILE_ON;
            else if (!strcmp(optarg,"no"))
                jl_options.startupfile = JL_OPTIONS_STARTUPFILE_OFF;
            else {
                ios_printf(ios_stderr, "julia: invalid argument to --startup-file={yes|no} (%s)\n", optarg);
                exit(1);
            }
            break;
	case 'f':
	    jl_options.startupfile = JL_OPTIONS_STARTUPFILE_OFF;
	    break;
	case 'F':
	    jl_options.startupfile = JL_OPTIONS_STARTUPFILE_ON;
	    break;
        case compile:
            if (!strcmp(optarg,"yes"))
                jl_compileropts.compile_enabled = JL_COMPILEROPT_COMPILE_ON;
            else if (!strcmp(optarg,"no"))
                jl_compileropts.compile_enabled = JL_COMPILEROPT_COMPILE_OFF;
            else if (!strcmp(optarg,"all"))
                jl_compileropts.compile_enabled = JL_COMPILEROPT_COMPILE_ALL;
            else {
                ios_printf(ios_stderr, "julia: invalid argument to --compile (%s)\n", optarg);
                exit(1);
            }
            break;
        case code_coverage:
            if (optarg != NULL) {
                if (!strcmp(optarg,"user"))
                    codecov = JL_LOG_USER;
                else if (!strcmp(optarg,"all"))
                    codecov = JL_LOG_ALL;
                else if (!strcmp(optarg,"none"))
                    codecov = JL_LOG_NONE;
                break;
            }
            else {
                codecov = JL_LOG_USER;
            }
            break;
        case track_allocation:
            if (optarg != NULL) {
                if (!strcmp(optarg,"user"))
                    malloclog = JL_LOG_USER;
                else if (!strcmp(optarg,"all"))
                    malloclog = JL_LOG_ALL;
                else if (!strcmp(optarg,"none"))
                    malloclog = JL_LOG_NONE;
                break;
            }
            else {
                malloclog = JL_LOG_USER;
            }
            break;
        case 'O': // optimize
            jl_compileropts.opt_level = 1;
            break;
        case 'i': // isinteractive
            jl_options.isinteractive = 1;
            break;
        case check_bounds:
            if (!strcmp(optarg,"yes"))
                jl_compileropts.check_bounds = JL_COMPILEROPT_CHECK_BOUNDS_ON;
            else if (!strcmp(optarg,"no"))
                jl_compileropts.check_bounds = JL_COMPILEROPT_CHECK_BOUNDS_OFF;
	    else {
                ios_printf(ios_stderr, "julia: invalid argument to --check-bounds={yes|no} (%s)\n", optarg);
		exit(1);
	    }
            break;
        case int_literals:
            if (!strcmp(optarg,"32"))
                jl_compileropts.int_literals = 32;
            else if (!strcmp(optarg,"64"))
                jl_compileropts.int_literals = 64;
            else {
                ios_printf(ios_stderr, "julia: invalid argument to --int-literals={32|64} (%s)\n", optarg);
                exit(1);
            }
            break;
        case dump_bitcode:
            if (!strcmp(optarg,"yes"))
                jl_compileropts.dumpbitcode = JL_COMPILEROPT_DUMPBITCODE_ON;
            else if (!strcmp(optarg,"no"))
                jl_compileropts.dumpbitcode = JL_COMPILEROPT_DUMPBITCODE_OFF;
            break;
        case depwarn:
            if (!strcmp(optarg,"yes"))
                jl_compileropts.depwarn = 1;
            else if (!strcmp(optarg,"no"))
                jl_compileropts.depwarn = 0;
            else {
                ios_printf(ios_stderr, "julia: invalid argument to --depwarn={yes|no} (%s)\n", optarg);
                exit(1);
            }
            break;
        case 'b': // build
            jl_compileropts.build_path = strdup(optarg);
            if (!imagepathspecified)
                jl_compileropts.image_file = NULL;
            break;
        case worker:
            jl_options.worker = 1;
            break;
        case bind_to:
            jl_options.bindto = strdup(optarg);
            break;
        default:
            ios_printf(ios_stderr, "julia: unhandled option -- %c\n",  c);
            ios_printf(ios_stderr, "This is a bug, please report it.\n");
            exit(1);
        }
    }
    jl_compileropts.code_coverage = codecov;
    jl_compileropts.malloc_log = malloclog;
    optind -= skip;
    *argvp += optind;
    *argcp -= optind;
    if (jl_compileropts.image_file==NULL && *argcp > 0) {
        if (strcmp((*argvp)[0], "-")) {
            program = (*argvp)[0];
        }
    }
}

static int exec_program(void)
{
    int err = 0;
 again: ;
    JL_TRY {
        if (err) {
            jl_value_t *errs = jl_stderr_obj();
            jl_value_t *e = jl_exception_in_transit;
            if (errs != NULL) {
                jl_show(jl_stderr_obj(), e);
            }
            else {
                jl_printf(JL_STDERR, "error during bootstrap: ");
                jl_static_show(JL_STDERR, e);
            }
            jl_printf(JL_STDERR, "\n");
            JL_EH_POP();
            return 1;
        }
        jl_load(program);
    }
    JL_CATCH {
        err = 1;
        goto again;
    }
    return 0;
}

void jl_lisp_prompt();

#ifndef _WIN32
int jl_repl_raise_sigtstp(void)
{
    return raise(SIGTSTP);
}
#endif

#ifdef JL_GF_PROFILE
static void print_profile(void)
{
    size_t i;
    void **table = jl_base_module->bindings.table;
    for(i=1; i < jl_base_module->bindings.size; i+=2) {
        if (table[i] != HT_NOTFOUND) {
            jl_binding_t *b = (jl_binding_t*)table[i];
            if (b->value != NULL && jl_is_function(b->value) &&
                jl_is_gf(b->value)) {
                ios_printf(ios_stdout, "%d\t%s\n",
                           jl_gf_mtable(b->value)->ncalls,
                           jl_gf_name(b->value)->name);
            }
        }
    }
}
#endif

static int true_main(int argc, char *argv[])
{
    if (jl_base_module != NULL) {
        jl_array_t *args = (jl_array_t*)jl_get_global(jl_base_module, jl_symbol("ARGS"));
        if (args == NULL) {
            args = jl_alloc_cell_1d(0);
            jl_set_const(jl_base_module, jl_symbol("ARGS"), (jl_value_t*)args);
        }
        assert(jl_array_len(args) == 0);
        jl_array_grow_end(args, argc);
        int i;
        for (i=0; i < argc; i++) {
            jl_value_t *s = (jl_value_t*)jl_cstr_to_string(argv[i]);
            s->type = (jl_value_t*)jl_utf8_string_type;
            jl_arrayset(args, s, i);
        }
    }

    // run program if specified, otherwise enter REPL
    if (program) {
        int ret = exec_program();
        uv_tty_reset_mode();
        return ret;
    }

    jl_function_t *start_client =
        (jl_function_t*)jl_get_global(jl_base_module, jl_symbol("_start"));

    if (start_client) {
        jl_apply(start_client, NULL, 0);
        return 0;
    }

    int iserr = 0;

 again:
    ;
    JL_TRY {
        if (iserr) {
            //jl_show(jl_exception_in_transit);# What if the error was in show?
            jl_printf(JL_STDERR, "\n\n");
            iserr = 0;
        }
        uv_run(jl_global_event_loop(),UV_RUN_DEFAULT);
    }
    JL_CATCH {
        iserr = 1;
        JL_PUTS("error during run:\n",JL_STDERR);
        jl_show(jl_stderr_obj(),jl_exception_in_transit);
        JL_PUTS("\n",JL_STDOUT);
        goto again;
    }
    return iserr;
}

DLLEXPORT extern void julia_save();

#ifndef _OS_WINDOWS_
int main(int argc, char *argv[])
{
#else
int wmain(int argc, wchar_t *argv[], wchar_t *envp[])
{
    int i;
    for (i=0; i<argc; i++) { // write the command line to UTF8
        wchar_t *warg = argv[i];
        size_t wlen = wcslen(warg)+1;
        size_t len = WideCharToMultiByte(CP_UTF8, 0, warg, wlen, NULL, 0, NULL, NULL);
        if (!len) return 1;
        char *arg = (char*)alloca(len);
        if (!WideCharToMultiByte(CP_UTF8, 0, warg, wlen, arg, len, NULL, NULL)) return 1;
        argv[i] = (wchar_t*)arg;
    }
#endif
    libsupport_init();
    parse_opts(&argc, (char***)&argv);
    if (lisp_prompt) {
        jl_lisp_prompt();
        return 0;
    }
    julia_init(imagepathspecified ? JL_IMAGE_CWD : JL_IMAGE_JULIA_HOME);
    int ret = true_main(argc, (char**)argv);
    jl_atexit_hook();
    julia_save();
    return ret;
}

#ifdef __cplusplus
}
#endif
