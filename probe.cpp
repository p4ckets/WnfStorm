#include "probe.h"
#include <cstdio>
#include <vector>
#include <atomic>

bool WnfChainApi::Init() {
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    if (!nt) return false;
    Subscribe   = (RtlSubscribeWnfStateChangeNotification_t)  GetProcAddress(nt, "RtlSubscribeWnfStateChangeNotification");
    Unsubscribe = (RtlUnsubscribeWnfStateChangeNotification_t)GetProcAddress(nt, "RtlUnsubscribeWnfStateChangeNotification");
    Create      = (NtCreateWnfStateName_t)                    GetProcAddress(nt, "NtCreateWnfStateName");
    Delete      = (NtDeleteWnfStateName_t)                    GetProcAddress(nt, "NtDeleteWnfStateName");
    Update      = (NtUpdateWnfStateData_t)                    GetProcAddress(nt, "NtUpdateWnfStateData");
    return Subscribe && Unsubscribe && Create && Delete && Update;
}


struct SelfUnsubCtx {
    const WnfChainApi* api;
    PVOID              subHandle;   // set before callback fires
    std::atomic<bool>  resubscribe; // re-arm for next delivery
    WNF_STATE_NAME     stateName;
    std::atomic<ULONG> cbFired;
    std::atomic<ULONG> cbUAF;       // incremented if post-callback write detected
};

static LONG CALLBACK SelfUnsubCb(WNF_STATE_NAME name, WNF_CHANGE_STAMP stamp,
                                  PWNF_TYPE_ID, PVOID ctx, PVOID, ULONG) {
    auto* c = (SelfUnsubCtx*)ctx;
    c->cbFired.fetch_add(1);

    PVOID handle = c->subHandle;
    if (handle) {
        // Drop the subscription from within the callback.
        // If delivery path touches sub after this free → UAF.
        c->subHandle = nullptr;
        c->api->Unsubscribe(handle);
        c->cbUAF.fetch_add(1);
    }

    if (c->resubscribe.load()) {
        // Re-arm: subscribe again on the same state so the storm continues.
        PVOID newHandle = nullptr;
        if (c->api->Subscribe(&newHandle, name, 0, SelfUnsubCb, ctx, nullptr, 0, nullptr) >= 0)
            c->subHandle = newHandle;
    }
    return 0;
}

struct UpdateThreadArg {
    const WnfChainApi* api;
    WNF_STATE_NAME     stateName;
    std::atomic<bool>* stop;
    ULONG              threadIdx;
};

static DWORD WINAPI UpdateThread(PVOID p) {
    auto* a = (UpdateThreadArg*)p;
    BYTE  payload[8]{};
    while (!a->stop->load()) {
        a->api->Update(&a->stateName, payload, sizeof(payload), nullptr, nullptr, 0, 0);
        _mm_pause();
    }
    return 0;
}

void wnfchain::RunSelfUnsubUAF(const WnfChainApi& api, ULONG stateCount, ULONG durationMs) {
    wprintf(L"[WnfChainUAF] self-unsubscribe UAF: %u states, %u ms\n", stateCount, durationMs);

    std::vector<SelfUnsubCtx>    ctxs(stateCount);
    std::vector<UpdateThreadArg> args(stateCount);
    std::vector<HANDLE>          threads(stateCount);
    std::atomic<bool>            stop{ false };

    for (ULONG i = 0; i < stateCount; i++) {
        WNF_STATE_NAME sn = 0;
        if (api.Create(&sn, WnfTemporaryStateName, WnfDataScopeProcess,
                        FALSE, nullptr, 0x100, nullptr) < 0) {
            wprintf(L"[-] Create failed at idx %u\n", i);
            stateCount = i;
            break;
        }
        ctxs[i].api         = &api;
        ctxs[i].subHandle   = nullptr;
        ctxs[i].resubscribe = true;
        ctxs[i].stateName   = sn;
        ctxs[i].cbFired     = 0;
        ctxs[i].cbUAF       = 0;

        PVOID handle = nullptr;
        if (api.Subscribe(&handle, sn, 0, SelfUnsubCb, &ctxs[i], nullptr, 0, nullptr) < 0) {
            wprintf(L"[-] Subscribe failed at idx %u\n", i);
            api.Delete(&sn);
            stateCount = i;
            break;
        }
        ctxs[i].subHandle = handle;

        args[i] = { &api, sn, &stop, i };
        threads[i] = CreateThread(nullptr, 0, UpdateThread, &args[i], 0, nullptr);
    }

    wprintf(L"[*] Running for %u ms (attach verifier to detect UAF)...\n", durationMs);
    Sleep(durationMs);
    stop.store(true);

    ULONG totalFired = 0, totalUAF = 0;
    for (ULONG i = 0; i < stateCount; i++) {
        WaitForSingleObject(threads[i], 1000);
        CloseHandle(threads[i]);
        totalFired += ctxs[i].cbFired.load();
        totalUAF   += ctxs[i].cbUAF.load();

        // Clean up remaining subscriptions and state names
        ctxs[i].resubscribe.store(false);
        if (ctxs[i].subHandle) {
            api.Unsubscribe(ctxs[i].subHandle);
            ctxs[i].subHandle = nullptr;
        }
        api.Delete(&ctxs[i].stateName);
    }
    wprintf(L"[+] callbacks fired: %u  self-unsub attempts: %u\n", totalFired, totalUAF);
}


struct StampRaceCtx {
    WNF_STATE_NAME     stateName;
    const WnfChainApi* api;
    std::atomic<ULONG> deliveries;
};

static LONG CALLBACK StampRaceCb(WNF_STATE_NAME, WNF_CHANGE_STAMP,
                                  PWNF_TYPE_ID, PVOID ctx, PVOID, ULONG) {
    ((StampRaceCtx*)ctx)->deliveries.fetch_add(1);
    return 0;
}

void wnfchain::RunStampRace(const WnfChainApi& api, ULONG threadCount, ULONG durationMs) {
    wprintf(L"[WnfChainUAF] stamp race: %u updater threads, %u ms\n", threadCount, durationMs);

    StampRaceCtx ctx{ 0, &api, 0 };
    if (api.Create(&ctx.stateName, WnfTemporaryStateName, WnfDataScopeProcess,
                    FALSE, nullptr, 0x100, nullptr) < 0) {
        wprintf(L"[-] Create failed\n"); return;
    }

    PVOID sub = nullptr;
    if (api.Subscribe(&sub, ctx.stateName, 0, StampRaceCb, &ctx, nullptr, 0, nullptr) < 0) {
        wprintf(L"[-] Subscribe failed\n"); api.Delete(&ctx.stateName); return;
    }

    std::atomic<bool> stop{ false };
    std::vector<UpdateThreadArg> args(threadCount);
    std::vector<HANDLE>          ths(threadCount);
    for (ULONG i = 0; i < threadCount; i++) {
        args[i] = { &api, ctx.stateName, &stop, i };
        ths[i]  = CreateThread(nullptr, 0, UpdateThread, &args[i], 0, nullptr);
    }

    Sleep(durationMs);
    stop.store(true);
    for (ULONG i = 0; i < threadCount; i++) {
        WaitForSingleObject(ths[i], 1000);
        CloseHandle(ths[i]);
    }
    api.Unsubscribe(sub);
    api.Delete(&ctx.stateName);
    wprintf(L"[+] deliveries observed: %u\n", ctx.deliveries.load());
}



struct DestroyRaceCtx {
    const WnfChainApi* api;
    std::atomic<bool>  stop;
    std::atomic<ULONG> cbCount;
};

static LONG CALLBACK DestroyRaceCb(WNF_STATE_NAME, WNF_CHANGE_STAMP,
                                    PWNF_TYPE_ID, PVOID ctx, PVOID, ULONG) {
    ((DestroyRaceCtx*)ctx)->cbCount.fetch_add(1);
    // Stall in callback: keep delivery path inside the subscription while
    // the racer thread deletes the underlying state name.
    Sleep(1);
    return 0;
}

static DWORD WINAPI RacerThread(PVOID p) {
    auto* ctx = (DestroyRaceCtx*)p;
    const WnfChainApi& api = *ctx->api;
    while (!ctx->stop.load()) {
        WNF_STATE_NAME sn = 0;
        if (api.Create(&sn, WnfTemporaryStateName, WnfDataScopeProcess,
                        FALSE, nullptr, 0x100, nullptr) < 0) continue;
        PVOID sub = nullptr;
        if (api.Subscribe(&sub, sn, 0, DestroyRaceCb, ctx, nullptr, 0, nullptr) < 0) {
            api.Delete(&sn); continue;
        }
        BYTE payload[4]{};
        api.Update(&sn, payload, sizeof(payload), nullptr, nullptr, 0, 0);
        // Delete name while callback may be running (stalling on Sleep(1))
        api.Delete(&sn);
        api.Unsubscribe(sub);
    }
    return 0;
}

void wnfchain::RunStateDestroyRace(const WnfChainApi& api, ULONG durationMs) {
    wprintf(L"[WnfChainUAF] state-destroy race: %u ms\n", durationMs);
    DestroyRaceCtx ctx{ &api, false, 0 };

    const ULONG kThreads = 4;
    HANDLE ths[kThreads];
    for (ULONG i = 0; i < kThreads; i++)
        ths[i] = CreateThread(nullptr, 0, RacerThread, &ctx, 0, nullptr);

    Sleep(durationMs);
    ctx.stop.store(true);
    WaitForMultipleObjects(kThreads, ths, TRUE, 5000);
    for (auto h : ths) CloseHandle(h);
    wprintf(L"[+] total callbacks fired: %u\n", ctx.cbCount.load());
}
