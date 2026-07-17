#include "probe.h"
#include <cstdio>
#include <cstdlib>

static void Usage() {
    wprintf(
        L"WnfStorm\n\n"
        L"  self-unsub  <states> <ms>   self-unsubscribe from within callback\n"
        L"  stamp-race  <threads> <ms>  concurrent stamp update race\n"
        L"  destroy     <ms>            delete WNF state name during delivery\n\n"
        L"Run under Application Verifier (PageHeap + Handles) to catch UAFs.\n"
    );
}

int wmain(int argc, wchar_t** argv) {
    WnfChainApi api;
    if (!api.Init()) {
        wprintf(L"[-] ntdll WNF exports not found\n");
        return 1;
    }

    if (argc < 2) { Usage(); return 0; }

    if (_wcsicmp(argv[1], L"self-unsub") == 0 && argc >= 4) {
        ULONG states = (ULONG)_wtoi(argv[2]);
        ULONG ms     = (ULONG)_wtoi(argv[3]);
        wnfchain::RunSelfUnsubUAF(api, states ? states : 64, ms ? ms : 5000);
    } else if (_wcsicmp(argv[1], L"stamp-race") == 0 && argc >= 4) {
        ULONG threads = (ULONG)_wtoi(argv[2]);
        ULONG ms      = (ULONG)_wtoi(argv[3]);
        wnfchain::RunStampRace(api, threads ? threads : 8, ms ? ms : 5000);
    } else if (_wcsicmp(argv[1], L"destroy") == 0 && argc >= 3) {
        ULONG ms = (ULONG)_wtoi(argv[2]);
        wnfchain::RunStateDestroyRace(api, ms ? ms : 5000);
    } else {
        Usage();
    }
    return 0;
}
