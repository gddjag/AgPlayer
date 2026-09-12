"""Read-only Win32 load/topology probe. Does not start/control other processes."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import datetime
import json
from pathlib import Path
import platform
import time

k32 = c.WinDLL('kernel32', use_last_error=True)
ntdll = c.WinDLL('ntdll')
PTR = c.c_size_t

class ProcessorTimes(c.Structure):
    _fields_ = [('idle', c.c_int64), ('kernel', c.c_int64), ('user', c.c_int64),
                ('dpc', c.c_int64), ('interrupt', c.c_int64), ('interruptCount', w.ULONG)]

class ProcessEntry(c.Structure):
    _fields_ = [('size', w.DWORD), ('usage', w.DWORD), ('pid', w.DWORD),
                ('heap', PTR), ('module', w.DWORD), ('threads', w.DWORD),
                ('parentPid', w.DWORD), ('basePriority', w.LONG), ('flags', w.DWORD),
                ('name', w.WCHAR * 260)]

k32.GetLogicalProcessorInformationEx.argtypes = [c.c_int, c.c_void_p, c.POINTER(w.DWORD)]
k32.GetLogicalProcessorInformationEx.restype = w.BOOL
k32.CreateToolhelp32Snapshot.argtypes = [w.DWORD, w.DWORD]
k32.CreateToolhelp32Snapshot.restype = w.HANDLE
k32.Process32FirstW.argtypes = [w.HANDLE, c.POINTER(ProcessEntry)]
k32.Process32NextW.argtypes = [w.HANDLE, c.POINTER(ProcessEntry)]
k32.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
k32.OpenProcess.restype = w.HANDLE
k32.CloseHandle.argtypes = [w.HANDLE]
k32.GetProcessTimes.argtypes = [w.HANDLE] + [c.POINTER(c.c_uint64)] * 4
k32.GetProcessTimes.restype = w.BOOL
k32.GetProcessAffinityMask.argtypes = [w.HANDLE, c.POINTER(PTR), c.POINTER(PTR)]
k32.GetPriorityClass.argtypes = [w.HANDLE]
k32.GetPriorityClass.restype = w.DWORD
ntdll.NtQuerySystemInformation.argtypes = [w.ULONG, c.c_void_p, w.ULONG, c.POINTER(w.ULONG)]
ntdll.NtQuerySystemInformation.restype = w.LONG

def topology():
    length = w.DWORD()
    k32.GetLogicalProcessorInformationEx(0, None, c.byref(length))
    if not length.value:
        raise c.WinError(c.get_last_error())
    buf = c.create_string_buffer(length.value)
    if not k32.GetLogicalProcessorInformationEx(0, buf, c.byref(length)):
        raise c.WinError(c.get_last_error())
    raw = buf.raw
    offset = 0
    cores = []
    while offset < length.value:
        relation = int.from_bytes(raw[offset:offset + 4], 'little')
        size = int.from_bytes(raw[offset + 4:offset + 8], 'little')
        if relation != 0 or size < 48 or offset + size > length.value:
            raise RuntimeError('Unexpected processor topology record')
        groups = int.from_bytes(raw[offset + 30:offset + 32], 'little')
        masks = []
        for index in range(groups):
            start = offset + 32 + 16 * index
            if start + 16 > offset + size:
                raise RuntimeError('Processor topology bounds failure')
            mask = int.from_bytes(raw[start:start + 8], 'little')
            group = int.from_bytes(raw[start + 8:start + 10], 'little')
            masks.append({'group': group, 'mask': mask,
                          'logicalCpus': [bit for bit in range(64) if mask & (1 << bit)]})
        cores.append({'core': len(cores), 'smt': bool(raw[offset + 8] & 1),
                      'efficiencyClass': raw[offset + 9], 'groups': masks})
        offset += size
    if any(g['group'] != 0 for core in cores for g in core['groups']):
        raise RuntimeError('This bounded QA probe supports one processor group only')
    return cores

def cpu_snapshot(count):
    values = (ProcessorTimes * count)()
    returned = w.ULONG()
    status = ntdll.NtQuerySystemInformation(8, values, c.sizeof(values), c.byref(returned))
    if status != 0 or returned.value != c.sizeof(values):
        raise RuntimeError(f'Processor counter read failed: {status:#x}, bytes={returned.value}')
    return [(v.idle, v.kernel, v.user) for v in values]

def process_snapshot():
    snapshot = k32.CreateToolhelp32Snapshot(2, 0)
    if snapshot == c.c_void_p(-1).value:
        raise c.WinError(c.get_last_error())
    rows = {}
    entry = ProcessEntry()
    entry.size = c.sizeof(entry)
    try:
        ok = k32.Process32FirstW(snapshot, c.byref(entry))
        while ok:
            handle = k32.OpenProcess(0x1000, False, entry.pid)
            if handle:
                try:
                    created, exited, kernel, user = [c.c_uint64() for _ in range(4)]
                    if k32.GetProcessTimes(handle, c.byref(created), c.byref(exited), c.byref(kernel), c.byref(user)):
                        row = {'pid': entry.pid, 'parentPid': entry.parentPid, 'name': entry.name,
                               'created': created.value, 'cpuSeconds': (kernel.value + user.value) / 1e7}
                        if entry.name.lower() == 'codebase-memory-mcp.exe':
                            allowed, system = PTR(), PTR()
                            if k32.GetProcessAffinityMask(handle, c.byref(allowed), c.byref(system)):
                                row['affinityMask'] = allowed.value
                            row['priorityClass'] = k32.GetPriorityClass(handle)
                        rows[entry.pid] = row
                finally:
                    k32.CloseHandle(handle)
            ok = k32.Process32NextW(snapshot, c.byref(entry))
    finally:
        k32.CloseHandle(snapshot)
    return rows

def load_delta(previous, current):
    values = []
    for (i0, k0, u0), (i1, k1, u1) in zip(previous, current):
        total = (k1 - k0) + (u1 - u0)
        if total <= 0:
            raise RuntimeError('Processor counters did not advance')
        values.append(100.0 * max(0, total - (i1 - i0)) / total)
    return values

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--samples', type=int, default=10)
    parser.add_argument('--interval', type=float, default=1.0)
    args = parser.parse_args()
    if not 1 <= args.samples <= 60 or not 0.5 <= args.interval <= 5:
        raise ValueError('Bounded probe: samples 1..60, interval 0.5..5 seconds')
    cores = topology()
    count = max(cpu for core in cores for group in core['groups'] for cpu in group['logicalCpus']) + 1
    before_cpu = cpu_snapshot(count)
    before_processes = process_snapshot()
    before_time = time.perf_counter()
    start = datetime.datetime.now(datetime.timezone.utc).isoformat()
    samples = []
    for index in range(args.samples):
        time.sleep(args.interval)
        after_cpu = cpu_snapshot(count)
        after_processes = process_snapshot()
        after_time = time.perf_counter()
        elapsed = after_time - before_time
        per_cpu = load_delta(before_cpu, after_cpu)
        process_rows = []
        for pid, row in after_processes.items():
            old = before_processes.get(pid)
            if old and old['created'] == row['created']:
                row['cpuPercentOneCore'] = 100.0 * (row['cpuSeconds'] - old['cpuSeconds']) / elapsed
                row['deltaScope'] = 'same-process-between-snapshots'
            else:
                row['cpuPercentOneCore'] = 100.0 * row['cpuSeconds'] / elapsed
                row['deltaScope'] = 'new-process-cpu-since-creation'
            process_rows.append(row)
        mcp = [r for r in process_rows if r['name'].lower() == 'codebase-memory-mcp.exe']
        busy = sorted((r for r in process_rows if r['pid'] != 0),
                      key=lambda r: r['cpuPercentOneCore'], reverse=True)[:10]
        samples.append({'sample': index, 'elapsedSeconds': elapsed,
                        'hostBusyPercent': sum(per_cpu) / count, 'logicalBusyPercent': per_cpu,
                        'mcp': mcp, 'mcpCpuPercentOneCoreLowerBound': sum(r['cpuPercentOneCore'] for r in mcp),
                        'topProcesses': busy,
                        'exitedSincePreviousSnapshot': [r for pid, r in before_processes.items() if pid not in after_processes]})
        before_cpu, before_processes, before_time = after_cpu, after_processes, after_time
    rankings = []
    for core in cores:
        cpus = [cpu for group in core['groups'] for cpu in group['logicalCpus']]
        logical_means = {cpu: sum(s['logicalBusyPercent'][cpu] for s in samples) / len(samples) for cpu in cpus}
        rankings.append({'core': core['core'], 'efficiencyClass': core['efficiencyClass'], 'logicalCpus': cpus,
                         'meanBusiestSiblingPercent': sum(max(s['logicalBusyPercent'][cpu] for cpu in cpus) for s in samples) / len(samples),
                         'maxSiblingPercent': max(s['logicalBusyPercent'][cpu] for s in samples for cpu in cpus),
                         'logicalMeanPercent': logical_means})
    rankings.sort(key=lambda r: (-r['efficiencyClass'], r['meanBusiestSiblingPercent'], r['core']))
    doc = {'startedUtc': start, 'platform': platform.platform(), 'logicalCount': count,
           'topology': cores, 'samples': samples, 'coreRanking': rankings,
           'readOnly': True, 'scope': 'Native Win32 snapshots only; no process launch/control/configuration changes.',
           'limitations': 'A process that exits between snapshots can be missed; MCP process CPU is a lower bound. Aggregate logical CPU counters still include its CPU work. No cache/frequency isolation is established.'}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(doc, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({'output': str(args.out), 'samples': len(samples), 'logicalCount': count,
                      'hostBusyPercentRange': [min(s['hostBusyPercent'] for s in samples), max(s['hostBusyPercent'] for s in samples)],
                      'rankings': rankings}, ensure_ascii=False))

if __name__ == '__main__':
    main()
