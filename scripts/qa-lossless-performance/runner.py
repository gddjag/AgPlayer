"""Sequential, load-gated scanner comparison. Controls only its own child processes."""
import argparse
import ctypes as c
import datetime
import hashlib
import json
import math
import os
from pathlib import Path
import random
import statistics
import subprocess
import sys
import threading
import time

import env_probe as native

ROOT = Path(__file__).resolve().parent
QA = ROOT.parents[1] / 'build/qa'
IGNORED_FIELDS = {'algorithmVersion', 'parameterVersion', 'elapsedMs'}
NORMAL_PRIORITY_CLASS = 0x20
THRESHOLDS = {'hostBusyPercent': 20.0, 'siblingBusyPercent': 15.0,
              'mcpCpuPercentOneCore': 5.0, 'dispersionPercent': 5.0}
k32 = native.k32
k32.GetCurrentProcess.restype = native.w.HANDLE
k32.SetProcessAffinityMask.argtypes = [native.w.HANDLE, c.c_size_t]
k32.SetProcessAffinityMask.restype = native.w.BOOL

def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def save(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding='utf-8')

def affinity(handle):
    allowed, system = c.c_size_t(), c.c_size_t()
    if not k32.GetProcessAffinityMask(handle, c.byref(allowed), c.byref(system)):
        raise c.WinError(c.get_last_error())
    return allowed.value

def observe(count, seconds, stop=None):
    cpu0 = native.cpu_snapshot(count)
    proc0 = native.process_snapshot()
    start = time.perf_counter()
    if stop is None:
        time.sleep(seconds)
    else:
        stop.wait(seconds)
    cpu1 = native.cpu_snapshot(count)
    proc1 = native.process_snapshot()
    elapsed = time.perf_counter() - start
    busy = native.load_delta(cpu0, cpu1)
    rows = []
    for pid, row in proc1.items():
        old = proc0.get(pid)
        matched = old is not None and old['created'] == row['created']
        row['cpuPercentOneCore'] = 100.0 * max(0.0, row['cpuSeconds'] - (old['cpuSeconds'] if matched else 0.0)) / elapsed
        rows.append(row)
    mcp = [r for r in rows if r['name'].lower() == 'codebase-memory-mcp.exe']
    mcp_ids = {r['pid'] for r in mcp}
    exits = [r['pid'] for pid, r in proc0.items()
             if pid not in proc1 and r['name'].lower() == 'codebase-memory-mcp.exe']
    return {'atUtc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
            'elapsedSeconds': elapsed, 'hostBusyPercent': sum(busy) / count,
            'logicalBusyPercent': busy, 'mcpCpuPercentOneCoreLowerBound': sum(r['cpuPercentOneCore'] for r in mcp),
            'observedMcpWorkers': [r['pid'] for r in mcp if r['parentPid'] in mcp_ids],
            'observedMcpExits': exits, 'mcp': mcp,
            'topProcesses': sorted(rows, key=lambda r: r['cpuPercentOneCore'], reverse=True)[:8]}

def environmental_checks(sample, siblings, logical_cpu, running=False):
    other = [value for index, value in enumerate(sample['logicalBusyPercent']) if index != logical_cpu]
    checked_siblings = [cpu for cpu in siblings if not running or cpu != logical_cpu]
    return {
        'backgroundBusy': (sum(other) / len(other) if running else sample['hostBusyPercent']) <= THRESHOLDS['hostBusyPercent'],
        'smtSiblingBusy': all(sample['logicalBusyPercent'][cpu] <= THRESHOLDS['siblingBusyPercent'] for cpu in checked_siblings),
        'mcpCpu': sample['mcpCpuPercentOneCoreLowerBound'] <= THRESHOLDS['mcpCpuPercentOneCore'],
        'noMcpWorkerOrExit': not sample['observedMcpWorkers'] and not sample['observedMcpExits'],
    }

def gate(count, siblings, args):
    start = time.perf_counter()
    streak = 0
    rows = []
    while time.perf_counter() - start < args.gate_timeout:
        sample = observe(count, min(args.poll, max(0.05, args.gate_timeout - (time.perf_counter() - start))))
        sample['checks'] = environmental_checks(sample, siblings, args.logical_cpu)
        passed = all(sample['checks'].values())
        streak = streak + 1 if passed else 0
        rows.append(sample)
        if streak >= args.quiet_samples:
            return {'ready': True, 'samples': rows, 'elapsedSeconds': time.perf_counter() - start}
    return {'ready': False, 'reason': 'no-quiet-window-before-timeout',
            'samples': rows, 'elapsedSeconds': time.perf_counter() - start}

def run_child(command, args, count, siblings, env):
    current = k32.GetCurrentProcess()
    original_mask = affinity(current)
    target_mask = 1 << args.logical_cpu
    if not original_mask & target_mask:
        raise RuntimeError('Requested logical CPU is unavailable to this runner')
    started = time.perf_counter()
    # The fresh child inherits this mask from its first instruction. Restore only this runner immediately.
    if not k32.SetProcessAffinityMask(current, target_mask):
        raise c.WinError(c.get_last_error())
    child = None
    try:
        child = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                 env=env, creationflags=NORMAL_PRIORITY_CLASS)
    finally:
        if not k32.SetProcessAffinityMask(current, original_mask):
            if child is not None:
                child.kill()
                child.communicate()
            raise c.WinError(c.get_last_error())
    row = {'pid': child.pid, 'command': command, 'runnerOriginalAffinityMask': original_mask,
           'requestedAffinityMask': target_mask, 'environment': []}
    stop = threading.Event()
    def monitor():
        while not stop.is_set():
            try:
                sample = observe(count, args.poll, stop)
                sample['checks'] = environmental_checks(sample, siblings, args.logical_cpu, running=True)
                row['environment'].append(sample)
            except Exception as error:
                row['environment'].append({'observationError': str(error)})
                break
    watcher = None
    try:
        row['affinityMask'] = affinity(int(child._handle))
        row['priorityClass'] = k32.GetPriorityClass(int(child._handle))
        if row['affinityMask'] != target_mask or row['priorityClass'] != NORMAL_PRIORITY_CLASS:
            raise RuntimeError('Fresh scanner affinity/priority verification failed')
        watcher = threading.Thread(target=monitor, name='qa-native-load-observer', daemon=True)
        watcher.start()
        try:
            out, err = child.communicate(timeout=args.scan_timeout)
            row['timedOut'] = False
        except subprocess.TimeoutExpired:
            # Only the child created by this runner is terminated; never another process or process tree.
            child.kill()
            out, err = child.communicate()
            row['timedOut'] = True
        row['wallSeconds'] = time.perf_counter() - started
        created, exited, kernel, user = [c.c_uint64() for _ in range(4)]
        if not k32.GetProcessTimes(int(child._handle), c.byref(created), c.byref(exited), c.byref(kernel), c.byref(user)):
            raise c.WinError(c.get_last_error())
        row.update(createdFileTime=created.value, cpuSeconds=(kernel.value + user.value) / 1e7,
                   returnCode=child.returncode, stdout=out.decode('utf-8', errors='replace'),
                   stderr=err.decode('utf-8', errors='replace'))
        return row
    finally:
        stop.set()
        # Reap the owned child before an observer cleanup error can propagate.
        if child.poll() is None:
            child.kill()
            child.communicate()
        if watcher is not None:
            watcher.join(timeout=args.poll + 5)
            if watcher.is_alive():
                raise RuntimeError('Native observer did not terminate')

def compare(a, b, path=''):
    differences = []
    maximum = {'absolute': 0.0, 'relative': 0.0, 'path': None}
    def walk(x, y, key):
        if isinstance(x, dict) and isinstance(y, dict):
            if set(x) != set(y):
                differences.append({'path': key, 'reason': 'different-keys'})
                return
            for name in x:
                if name not in IGNORED_FIELDS:
                    walk(x[name], y[name], key + '.' + name)
        elif isinstance(x, list) and isinstance(y, list):
            if len(x) != len(y):
                differences.append({'path': key, 'reason': 'different-length'})
            else:
                for index, (left, right) in enumerate(zip(x, y)):
                    walk(left, right, f'{key}[{index}]')
        elif type(x) is int and type(y) is int:
            if x != y:
                differences.append({'path': key, 'before': x, 'after': y})
        elif type(x) in (float, int) and type(y) in (float, int):
            delta = abs(x - y)
            relative = delta / max(abs(x), abs(y), 1e-12)
            if delta > maximum['absolute']:
                maximum.update(absolute=delta, relative=relative, path=key)
            if not math.isfinite(x) or not math.isfinite(y) or not math.isclose(x, y, rel_tol=1e-8, abs_tol=1e-8):
                differences.append({'path': key, 'before': x, 'after': y})
        elif type(x) is not type(y) or x != y:
            differences.append({'path': key, 'before': x, 'after': y})
    walk(a, b, path)
    return {'differences': differences, 'maximumNumericDifference': maximum}

def parse_analysis(row):
    if row['timedOut'] or row['returnCode'] != 0:
        return {'valid': False, 'reason': 'scanner-timeout' if row['timedOut'] else 'scanner-failed'}
    try:
        report = json.loads(row['stdout'])
        if len(report['results']) != 1 or report['results'][0].get('error') or not report['qaMemory'].get('available'):
            raise ValueError('Expected one successful result and available qaMemory')
        row['analysis'] = report['results'][0]
        row['qaMemory'] = report['qaMemory']
        row['peakPrivateBytes'] = report['qaMemory']['afterPeakPagefileUsageBytes']
        return {'valid': True}
    except (ValueError, KeyError, TypeError) as error:
        return {'valid': False, 'reason': 'invalid-scanner-output', 'detail': str(error)}

def summarize(blocks, planned, files, run_valid):
    summary = {'runIntegrityValid': run_valid, 'allScheduledBlocksCompleted': len(blocks) == len(planned), 'files': []}
    for key in files:
        selected = [block for block in blocks if block['file'] == key and block['kind'] == 'measured']
        rows = [row for block in selected for row in block['runs'] if row.get('validation', {}).get('valid')]
        item = {'file': key, 'completedBlocks': len(selected),
                'allBlocksValid': bool(selected) and all(block['valid'] for block in selected),
                'statisticsScope': 'All completed measured rows, including contaminated or reverse results; no performance outlier removal.'}
        for version in ['baseline', 'candidate']:
            values = [row for row in rows if row['version'] == version]
            if values:
                item[version] = {metric: {'median': statistics.median(row[metric] for row in values),
                                         'minimum': min(row[metric] for row in values),
                                         'maximum': max(row[metric] for row in values)}
                                 for metric in ['cpuSeconds', 'wallSeconds', 'peakPrivateBytes']}
                cpus = [row['cpuSeconds'] for row in values]
                median = statistics.median(cpus)
                item[version]['cpuMadPercent'] = 100 * statistics.median(abs(value - median) for value in cpus) / median if median else None
        ratios = []
        for block in selected:
            runs = block['runs']
            if len(runs) == 4 and all(row.get('validation', {}).get('valid') for row in runs):
                for left, right in [(runs[0], runs[1]), (runs[2], runs[3])]:
                    a, b = (left, right) if left['version'] == 'baseline' else (right, left)
                    if a['cpuSeconds'] > 0:
                        ratios.append(b['cpuSeconds'] / a['cpuSeconds'])
        item['pairedCpuRatiosCandidateOverBaseline'] = ratios
        if ratios:
            median = statistics.median(ratios)
            item['medianPairedCpuRatio'] = median
            item['pairedRatioMadPercent'] = 100 * statistics.median(abs(value - median) for value in ratios) / median
        item['stableAcceptanceEligible'] = (run_valid and summary['allScheduledBlocksCompleted'] and item['allBlocksValid']
            and len(ratios) >= 8 and item.get('pairedRatioMadPercent', float('inf')) <= THRESHOLDS['dispersionPercent']
            and all(item.get(version, {}).get('cpuMadPercent', float('inf')) <= THRESHOLDS['dispersionPercent'] for version in ['baseline', 'candidate']))
        summary['files'].append(item)
    return summary

def self_test(args, count, siblings):
    # Explicit plumbing test: no audio, no scanner output or qaMemory is fabricated.
    env = os.environ.copy()
    command = [sys.executable, '-c', "import json,time; end=time.perf_counter()+.15\nwhile time.perf_counter()<end: pass\nprint(json.dumps({'runnerSelfTest':True}))"]
    normal = run_child(command, args, count, siblings, env)
    assert normal['returnCode'] == 0 and not normal['timedOut']
    assert json.loads(normal['stdout']) == {'runnerSelfTest': True}
    assert normal['cpuSeconds'] > 0 and normal['wallSeconds'] > 0
    old_timeout = args.scan_timeout
    args.scan_timeout = 0.05
    timed = run_child([sys.executable, '-c', 'import time; time.sleep(2)'], args, count, siblings, env)
    args.scan_timeout = old_timeout
    assert timed['timedOut'] and timed['returnCode'] != 0
    old_gate, old_quiet = args.gate_timeout, args.quiet_samples
    args.gate_timeout, args.quiet_samples = 0.5, 5
    blocked = gate(count, siblings, args)
    args.gate_timeout, args.quiet_samples = old_gate, old_quiet
    assert not blocked['ready']
    assert compare({'n': 1, 'flag': True}, {'n': 2, 'flag': True})['differences']
    assert compare({'n': 1, 'flag': True}, {'n': 1, 'flag': 1})['differences']
    assert not compare({'x': 1.0}, {'x': 1.0 + 1e-12})['differences']
    # Synthetic summary fixture: a final integrity failure must override otherwise eligible rows.
    fixture_runs = [{'version': version, 'cpuSeconds': 1.0, 'wallSeconds': 1.0,
                     'peakPrivateBytes': 1, 'validation': {'valid': True}}
                    for version in ['baseline', 'candidate', 'candidate', 'baseline']]
    fixture_blocks = [{'kind': 'measured', 'file': 'fixture', 'valid': True, 'runs': fixture_runs} for _ in range(4)]
    assert summarize(fixture_blocks, fixture_blocks, ['fixture'], True)['files'][0]['stableAcceptanceEligible']
    assert not summarize(fixture_blocks, fixture_blocks, ['fixture'], False)['files'][0]['stableAcceptanceEligible']
    assert Path(native.__file__).resolve() == ROOT / 'env_probe.py'
    report = {'kind': 'runner-plumbing-self-test-no-audio', 'passed': True,
              'normalChild': normal, 'timeoutChild': timed, 'boundedGate': blocked,
              'scriptSha256': sha(__file__), 'nativeProbeSha256': sha(ROOT / 'env_probe.py'),
              'nativeProbePath': str(Path(native.__file__).resolve()), 'qaRoot': str(QA.resolve()),
              'runnerAffinityRestored': affinity(k32.GetCurrentProcess()) == normal['runnerOriginalAffinityMask']}
    assert report['runnerAffinityRestored']
    save(args.out / 'self-test.json', report)
    print(json.dumps({'selfTestPassed': True, 'audioScans': 0, 'report': str(args.out / 'self-test.json')}), flush=True)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--baseline', type=Path, default=QA / 'round7-local-validation/candidate-1.11.exe')
    parser.add_argument('--candidate', type=Path, default=QA / 'round8-performance/candidate-1.13.exe')
    parser.add_argument('--workloads', type=Path, default=QA / 'round8-performance/workloads.json')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--logical-cpu', type=int, default=6)
    parser.add_argument('--blocks', type=int, default=4)
    parser.add_argument('--order-seed', type=int, default=20260907)
    parser.add_argument('--quiet-samples', type=int, default=5)
    parser.add_argument('--poll', type=float, default=1.0)
    parser.add_argument('--gate-timeout', type=float, default=60)
    parser.add_argument('--scan-timeout', type=float, default=300)
    parser.add_argument('--self-test', action='store_true')
    parser.add_argument('--preflight-only', action='store_true')
    args = parser.parse_args()
    if args.out.exists() and any(args.out.iterdir()):
        raise RuntimeError('Output directory must be new or empty; do not overwrite a prior run')
    args.out.mkdir(parents=True, exist_ok=True)
    if not 1 <= args.blocks <= 8 or not 1 <= args.quiet_samples <= 10 or not 0.5 <= args.poll <= 2 or not 0.5 <= args.gate_timeout <= 60 or args.scan_timeout <= 0:
        raise ValueError('Invalid bounded benchmark settings')
    cores = native.topology()
    count = max(cpu for core in cores for group in core['groups'] for cpu in group['logicalCpus']) + 1
    selected = [core for core in cores if any(args.logical_cpu in group['logicalCpus'] for group in core['groups'])]
    if len(selected) != 1:
        raise ValueError('Logical CPU not present')
    siblings = [cpu for group in selected[0]['groups'] for cpu in group['logicalCpus']]
    if args.self_test:
        self_test(args, count, siblings)
        return
    workload_document = json.loads(args.workloads.read_text(encoding='utf-8'))
    files = workload_document['files']
    scanners = {'baseline': args.baseline.resolve(), 'candidate': args.candidate.resolve()}
    hashes = {'scanners': {name: sha(path) for name, path in scanners.items()},
              'inputs': {name: sha(row['path']) for name, row in files.items()},
              'runner': sha(__file__), 'probe': sha(ROOT / 'env_probe.py')}
    for name, row in files.items():
        if hashes['inputs'][name].lower() != row['sha256'].lower():
            raise RuntimeError(f'Input SHA mismatch: {name}')
    order = list(files)
    random.Random(args.order_seed).shuffle(order)
    schedule = [{'kind': 'warmup', 'file': key, 'versions': ['baseline', 'candidate']} for key in order]
    for block in range(args.blocks):
        rotated = order[block % len(order):] + order[:block % len(order)]
        schedule.extend({'kind': 'measured', 'block': block, 'file': key,
                         'versions': ['baseline', 'candidate', 'candidate', 'baseline']} for key in rotated)
    method = {'createdUtc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'arguments': {key: str(value) if isinstance(value, Path) else value for key, value in vars(args).items()},
              'thresholds': THRESHOLDS, 'topology': cores, 'selectedCoreSiblings': siblings,
              'schedule': schedule, 'hashesBefore': hashes, 'files': files,
              'scope': 'Fresh sequential full-input scanner processes; only runner and own child affinity controlled; no other process control.',
              'selectionRule': 'All results retained; a no-quiet-window timeout aborts explicitly. Environmental contamination invalidates entire block, including both directions. No reverse-performance result is dropped.',
              'numericComparison': {'excludedFields': sorted(IGNORED_FIELDS), 'floatAbsoluteTolerance': 1e-8, 'floatRelativeTolerance': 1e-8, 'integersAndBooleans': 'exact'}}
    dll_dir = Path('D:/ai/AgPlayer/build/release/vcpkg_installed/x64-windows/bin')
    method['dllHashes'] = {str(path): sha(path) for path in dll_dir.glob('*.dll') if path.name.lower().startswith(('avcodec', 'avformat', 'avutil', 'swresample', 'swscale', 'soundtouch'))}
    qt_core = Path('D:/Qt/6.7.0/msvc2019_64/bin/Qt6Core.dll')
    method['dllHashes'][str(qt_core)] = sha(qt_core)
    save(args.out / 'method.json', method)
    env = os.environ.copy()
    env['PATH'] = 'D:/Qt/6.7.0/msvc2019_64/bin;' + str(dll_dir) + ';' + env['PATH']
    if args.preflight_only:
        observed = gate(count, siblings, args)
        result = {'kind': 'environment-preflight-no-audio', 'audioScans': 0,
                  'status': 'ready' if observed['ready'] else 'invalid',
                  'gate': observed, 'hashesBefore': hashes,
                  'scope': 'No benchmark or scanner process is launched, even if the load gate passes.'}
        save(args.out / 'preflight.json', result)
        print(json.dumps({'status': result['status'], 'audioScans': 0,
                          'output': str(args.out / 'preflight.json')}, ensure_ascii=False), flush=True)
        if not observed['ready']:
            raise SystemExit(2)
        return
    completed = []
    abort = None
    try:
        for sequence, step in enumerate(schedule):
            print(f"gate {sequence + 1}/{len(schedule)} {step['kind']} {step['file']}", flush=True)
            before = gate(count, siblings, args)
            block = {**step, 'sequence': sequence, 'preGate': before, 'runs': [], 'valid': False}
            completed.append(block)
            if not before['ready']:
                abort = before['reason']
                break
            for version in step['versions']:
                scanner = scanners[version]
                row = run_child([str(scanner), '--qa-memory', files[step['file']]['path']], args, count, siblings, env)
                row['version'] = version
                row['scannerSha256'] = hashes['scanners'][version]
                row['validation'] = parse_analysis(row)
                block['runs'].append(row)
                print(f"{step['kind']} {step['file']} {version} cpu={row['cpuSeconds']:.3f} wall={row['wallSeconds']:.3f} status={row['validation']}", flush=True)
                save(args.out / 'results.json', {'status': 'running', 'blocks': completed})
                if not row['validation']['valid']:
                    abort = row['validation']['reason']
                    break
            if abort:
                break
            after = gate(count, siblings, args)
            block['postGate'] = after
            comparisons = []
            base = next(row['analysis'] for row in block['runs'] if row['version'] == 'baseline')
            for row in block['runs']:
                comparisons.append(compare(base, row['analysis'], step['file']))
            block['semanticComparisons'] = comparisons
            environment_valid = all(row['environment'] and all('observationError' not in sample and all(sample['checks'].values()) for sample in row['environment']) for row in block['runs'])
            block['valid'] = after['ready'] and environment_valid and all(not item['differences'] for item in comparisons)
            if not block['valid']:
                abort = 'post-gate-timeout' if not after['ready'] else ('environment-contaminated-block' if not environment_valid else 'semantic-difference')
                break
    except KeyboardInterrupt:
        abort = 'interrupted'
    except Exception as error:
        abort = f'{type(error).__name__}: {error}'
    finally:
        inputs_after = {name: sha(row['path']) for name, row in files.items()}
        scanners_after = {name: sha(path) for name, path in scanners.items()}
        unchanged = inputs_after == hashes['inputs'] and scanners_after == hashes['scanners']
        if not unchanged:
            abort = 'input-or-scanner-changed'
        result = {'status': 'invalid' if abort else 'complete', 'invalidReason': abort,
                  'blocks': completed, 'summary': summarize(completed, schedule, files, abort is None and unchanged),
                  'hashesUnchanged': unchanged, 'hashesAfter': {'inputs': inputs_after, 'scanners': scanners_after},
                  'ownedScannerCleanup': 'Every created child was reaped; only owned child termination is used on timeout/failure.'}
        save(args.out / 'results.json', result)
        print(json.dumps({'status': result['status'], 'invalidReason': abort, 'completedSteps': len(completed), 'output': str(args.out)}, ensure_ascii=False), flush=True)
    if abort:
        raise SystemExit(2)

if __name__ == '__main__':
    main()
