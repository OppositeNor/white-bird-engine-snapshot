"""Capture one deferred-test configuration with scripted input and shut down only the owned process."""
from argparse import ArgumentParser
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from typing import Any

# Output names for the F1 captures that follow the initial frame capture, in capture_sequence() order.
# R switches lighting to the summed-area table first; V enables the voxel overlay before the voxel captures.
CAPTURE_LABELS: list[str] = ['summed_area_table', 'summed_area_table_later', 'voxels', 'voxels_later']
LIGHTING_SWITCH_MESSAGE: str = 'Deferred shadow filtering: summed-area table.'


def tap(p_key: str, p_delay: float) -> dict[str, Any]:
    """One-poll key press after p_delay seconds, in WBE_EXEC_TEST_INPUT_SEQUENCE format."""
    return {'key': p_key, 'delay': p_delay, 'count': 1}


def capture_sequence(p_settle: float) -> list[dict[str, Any]]:
    """Scripted input for capture mode. Delays chain from the previous entry and start at the first logic tick."""
    return [
        tap('KEYCODE_R', p_settle),
        tap('KEYCODE_F1', 1.0),
        tap('KEYCODE_F1', 3.0),
        tap('KEYCODE_V', 0.0),
        tap('KEYCODE_F1', 1.0),
        tap('KEYCODE_F1', 4.0),
    ]


def main() -> int:
    parser = ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, default=Path.cwd())
    parser.add_argument('--target', default='debug')
    parser.add_argument('--svo', choices=('1', '2', 'octree'), default='2')
    parser.add_argument('--mode', choices=('capture', 'stability'), default='capture')
    parser.add_argument('--full-scene-reconstruct', action='store_true',
                        help='Rebuild the static voxel structure every frame (WBE_REAL_TIME_FULL_SCENE_RECONSTRUCT=1)')
    parser.add_argument('--duration', type=float, default=25.0, help='Capture-free stability duration in seconds')
    parser.add_argument('--settle', type=float, default=5.0,
                        help='Seconds after the first logic tick before scripted input starts (capture mode)')
    parser.add_argument('--startup-timeout', type=float, default=90.0)
    parser.add_argument('--shutdown-timeout', type=float, default=25.0)
    parser.add_argument('--cpus', type=int, default=4, help='Maximum CPUs selected from the allowed affinity mask')
    parser.add_argument('--output-dir', type=Path, help='Parent directory for a fresh run directory')
    args = parser.parse_args()
    if args.cpus < 1 or min(args.duration, args.settle, args.startup_timeout, args.shutdown_timeout) <= 0:
        parser.error('CPU count and durations must be positive')
    if not hasattr(signal, 'SIGTERM') or os.name != 'posix':
        parser.error('This helper requires a POSIX platform: clean shutdown relies on SDL turning SIGTERM into SDL_QUIT')
    root = args.repo.resolve()
    binary = root / 'build' / args.target / 'bin/wbe_render_test_vulkan_draw_sponza_deferred_test'
    if not binary.is_file():
        parser.error(f'Build the executable first: {binary}')
    parent = (args.output_dir or root / 'build/render_checks').resolve()
    parent.mkdir(parents=True, exist_ok=True)
    reconstruct_tag = '_reconstruct' if args.full_scene_reconstruct else ''
    output = Path(tempfile.mkdtemp(prefix=f'svo{args.svo}{reconstruct_tag}_{args.mode}_', dir=parent))
    capture = output / 'mipmap.png'
    log_path = output / 'application.log'
    env = dict(os.environ, WBE_WAIT_PROCESS_INJECT='0', WBE_SVO_IMPLEMENTATION=args.svo,
               WBE_REAL_TIME_FULL_SCENE_RECONSTRUCT='1' if args.full_scene_reconstruct else '0')
    for name in ('WBE_CAPTURE_PNG', 'WBE_CAPTURE_FRAME', 'WBE_EXEC_TEST_INPUT_SEQUENCE'):
        env.pop(name, None)
    sequence: list[dict[str, Any]] = []
    if args.mode == 'capture':
        sequence = capture_sequence(args.settle)
        env.update(WBE_CAPTURE_PNG=str(capture), WBE_CAPTURE_FRAME='15',
                   WBE_EXEC_TEST_INPUT_SEQUENCE=json.dumps(sequence))
    cpus: list[int] = sorted(os.sched_getaffinity(0))[:args.cpus] if hasattr(os, 'sched_getaffinity') else []
    command: list[str] = [str(binary)]
    if shutil.which('stdbuf') is not None:
        command = ['stdbuf', '-oL', '-eL', *command]
    result: dict[str, object] = {'svo': args.svo, 'mode': args.mode,
                                 'full_scene_reconstruct': args.full_scene_reconstruct, 'cpus': cpus,
                                 'input_sequence': sequence, 'clean_exit': False, 'captures': []}
    started = time.monotonic()
    with log_path.open('w') as log:
        process = subprocess.Popen(command, cwd=root, env=env, stdout=log, stderr=subprocess.STDOUT,
                                   preexec_fn=(lambda: os.sched_setaffinity(0, cpus)) if cpus else None)
        print(f'RenderCheck: PID {process.pid}; artifacts: {output}', flush=True)

        def log_text() -> str:
            return log_path.read_text(errors='replace')

        def require_running() -> None:
            if process.poll() is not None:
                raise RuntimeError(f'Application exited before shutdown: {process.returncode}; inspect {log_path}')

        def pause(p_seconds: float) -> None:
            deadline = time.monotonic() + p_seconds
            while time.monotonic() < deadline:
                require_running()
                time.sleep(min(0.1, max(0.0, deadline - time.monotonic())))

        def captured() -> list[tuple[str, str]]:
            return re.findall(r'Captured frame (\d+) to (.+)', log_text())

        try:
            deadline = time.monotonic() + args.startup_timeout
            while 'Voxel construction:' not in log_text():
                if time.monotonic() >= deadline:
                    raise RuntimeError('Startup timed out')
                pause(0.2)
            if args.mode == 'stability':
                pause(args.duration)
            else:
                # The scripted input runs on its own clock; wait for the initial capture plus one per F1 press.
                expected = 1 + len(CAPTURE_LABELS)
                scripted_seconds = sum(float(entry['delay']) for entry in sequence)
                deadline = time.monotonic() + args.startup_timeout + scripted_seconds
                while len(captured()) < expected:
                    if time.monotonic() >= deadline:
                        raise RuntimeError(f'Timed out with {len(captured())} of {expected} captures')
                    pause(0.2)
                frames = captured()
                text = log_text()
                switch = text.find(LIGHTING_SWITCH_MESSAGE)
                if switch < 0:
                    raise RuntimeError('Lighting graph did not switch to summed-area table')
                if text.find(f'Captured frame {frames[0][0]} ') > switch:
                    raise RuntimeError('Initial capture happened after the lighting switch; increase --settle')
                captures: list[dict[str, object]] = [{'frame': int(frames[0][0]), 'path': str(capture)}]
                for label, (frame, source) in zip(CAPTURE_LABELS, frames[1:]):
                    destination = output / f'{label}.png'
                    shutil.copy2(source.strip(), destination)
                    captures.append({'frame': int(frame), 'path': str(destination)})
                result['captures'] = captures
            # SDL converts SIGTERM into SDL_QUIT, the same clean shutdown path as closing the window.
            process.send_signal(signal.SIGTERM)
            code = process.wait(timeout=args.shutdown_timeout)
            result['exit_code'] = code
            if code != 0:
                raise RuntimeError(f'Application exited with {code}')
            result['clean_exit'] = True
        except (RuntimeError, subprocess.SubprocessError, OSError, KeyboardInterrupt) as error:
            result['error'] = str(error) or type(error).__name__
            print(f'RenderCheck: FAILED: {result["error"]}', flush=True)
        finally:
            if process.poll() is None:
                # Forced cleanup is not a successful shutdown. Never leave an owned GPU process behind.
                process.kill()
                process.wait()
            result['exit_code'] = process.returncode
            result['elapsed_seconds'] = round(time.monotonic() - started, 3)
            (output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(f'RenderCheck: clean_exit={result["clean_exit"]}; result: {output / "result.json"}', flush=True)
    return 0 if result['clean_exit'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
