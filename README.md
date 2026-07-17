#  WnfStorm

```
  ／ヽ
 ( ˘ω˘ ) ♡  found a race condition
  づ⚡づ
```

> *hammering wnf until something breaks uwu*

---

## what is it

race condition harness for Windows Notification Facility (WNF) internals ~

WNF sits between usermode notification logic and kernel-backed state tracking. it's heavily concurrent by design. this just hits those edges hard until something cracks ♡

---

## what it does

three targets:

** self-unsub race**
subscription removes itself from inside its own callback while delivery is still running. stresses refcount and teardown ordering.

** stamp update race**
multiple threads slam `NtUpdateWnfStateData` while subscriptions are active. forces inconsistent delivery timing under contention.

** destroy race**
create state → update → deliver → delete, all in tight loops while callbacks are still mid-execution. looking for lifetime bugs where delivery touches freed memory.

---

## build

- MSVC, C++17+
- Windows SDK
- x64

```bash
cl /std:c++17 *.cpp /O2
```

---

## usage

```bash
WnfStorm.exe self-unsub <states> <ms>
WnfStorm.exe stamp-race <threads> <ms>
WnfStorm.exe destroy <ms>
```

examples:

```bash
WnfStorm.exe self-unsub 64 5000
WnfStorm.exe stamp-race 8 5000
WnfStorm.exe destroy 5000
```

---

## internals

no static imports. everything resolved at runtime from ntdll:

- `RtlSubscribeWnfStateChangeNotification`
- `RtlUnsubscribeWnfStateChangeNotification`
- `NtCreateWnfStateName`
- `NtDeleteWnfStateName`
- `NtUpdateWnfStateData`

---

## what to expect

run under Application Verifier (PageHeap + Handles) or WinDbg ♡

you'll see:
- random crashes
- inconsistent delivery counts
- silent corruption outside verifier

thats the point

---

## ⚠️ dont run this on anything you care about

research and debugging only ~

```
  ∧＿∧
(｡･ω･｡)つ━⚡ race to the crash ♡
```
