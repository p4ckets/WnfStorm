#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <cstdint>
#include <atomic>

typedef ULONG     WNF_CHANGE_STAMP;
typedef ULONGLONG WNF_STATE_NAME;
typedef GUID      WNF_TYPE_ID, *PWNF_TYPE_ID;

enum WNF_STATE_NAME_LIFETIME : ULONG {
    WnfWellKnownStateName = 0,
    WnfPermanentStateName = 1,
    WnfVolatileStateName  = 2,
    WnfTemporaryStateName = 3,
};

enum WNF_STATE_NAME_DATA_SCOPE : ULONG {
    WnfDataScopeSystem  = 0,
    WnfDataScopeSession = 1,
    WnfDataScopeUser    = 2,
    WnfDataScopeProcess = 3,
    WnfDataScopeHost    = 4,
};

typedef LONG(*WNF_USER_CALLBACK)(WNF_STATE_NAME, WNF_CHANGE_STAMP, PWNF_TYPE_ID, PVOID, PVOID, ULONG);

typedef LONG(*RtlSubscribeWnfStateChangeNotification_t)(
    PVOID*, WNF_STATE_NAME, WNF_CHANGE_STAMP, WNF_USER_CALLBACK,
    PVOID, PWNF_TYPE_ID, ULONG, PVOID);
typedef LONG(*RtlUnsubscribeWnfStateChangeNotification_t)(PVOID);
typedef LONG(*NtCreateWnfStateName_t)(
    PULONGLONG, WNF_STATE_NAME_LIFETIME, WNF_STATE_NAME_DATA_SCOPE,
    BOOLEAN, PWNF_TYPE_ID, ULONG, PSECURITY_DESCRIPTOR);
typedef LONG(*NtDeleteWnfStateName_t)(PULONGLONG);
typedef LONG(*NtUpdateWnfStateData_t)(PULONGLONG, PVOID, ULONG, PWNF_TYPE_ID, PVOID, ULONG, ULONG);

struct WnfChainApi {
    RtlSubscribeWnfStateChangeNotification_t   Subscribe;
    RtlUnsubscribeWnfStateChangeNotification_t Unsubscribe;
    NtCreateWnfStateName_t                     Create;
    NtDeleteWnfStateName_t                     Delete;
    NtUpdateWnfStateData_t                     Update;
    bool Init();
};

namespace wnfchain {

void RunSelfUnsubUAF(const WnfChainApi& api, ULONG stateCount, ULONG durationMs);




void RunStateDestroyRace(const WnfChainApi& api, ULONG durationMs);

} 
