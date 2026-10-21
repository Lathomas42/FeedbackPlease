// On an unhandled exception, writes a symbolized stack trace to stderr and to
// feedbackplease_crash.txt so lab crashes can be diagnosed without a debugger.

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>

#include <cstdio>

namespace fp {

namespace {

void writeTrace(std::FILE* f, EXCEPTION_POINTERS* ep) {
    HANDLE proc = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(proc, nullptr, TRUE);

    std::fprintf(f, "Unhandled exception 0x%08lX at %p\n", ep->ExceptionRecord->ExceptionCode,
                 ep->ExceptionRecord->ExceptionAddress);
    CONTEXT ctx = *ep->ContextRecord;
    STACKFRAME64 sf{};
    sf.AddrPC.Offset = ctx.Rip;
    sf.AddrPC.Mode = AddrModeFlat;
    sf.AddrFrame.Offset = ctx.Rbp;
    sf.AddrFrame.Mode = AddrModeFlat;
    sf.AddrStack.Offset = ctx.Rsp;
    sf.AddrStack.Mode = AddrModeFlat;

    alignas(SYMBOL_INFO) char symBuf[sizeof(SYMBOL_INFO) + 512];
    for (int i = 0; i < 40; ++i) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, thread, &sf, &ctx, nullptr, SymFunctionTableAccess64,
                         SymGetModuleBase64, nullptr) ||
            sf.AddrPC.Offset == 0)
            break;
        auto* sym = reinterpret_cast<SYMBOL_INFO*>(symBuf);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 511;
        DWORD64 disp = 0;
        const char* name = SymFromAddr(proc, sf.AddrPC.Offset, &disp, sym) ? sym->Name : "?";
        IMAGEHLP_LINE64 line{};
        line.SizeOfStruct = sizeof line;
        DWORD ldisp = 0;
        if (SymGetLineFromAddr64(proc, sf.AddrPC.Offset, &ldisp, &line))
            std::fprintf(f, "  #%02d %s  %s:%lu\n", i, name, line.FileName, line.LineNumber);
        else
            std::fprintf(f, "  #%02d %s\n", i, name);
    }
}

LONG WINAPI onCrash(EXCEPTION_POINTERS* ep) {
    writeTrace(stderr, ep);
    if (std::FILE* f = std::fopen("feedbackplease_crash.txt", "w")) {
        writeTrace(f, ep);
        std::fclose(f);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

}  // namespace

void installCrashHandler() { SetUnhandledExceptionFilter(onCrash); }

}  // namespace fp

#else
namespace fp {
void installCrashHandler() {}
}  // namespace fp
#endif
