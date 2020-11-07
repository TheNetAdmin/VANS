import click
import os
import json
from pathlib import Path
from contextlib import contextmanager
from random import randint


@contextmanager
def work_dir(path, mkdir=False):
    old_dir = os.getcwd()
    if not path.exists() and mkdir is True:
        path.mkdir(exist_ok=True, parents=True)
    os.chdir(path)
    try:
        yield
    finally:
        os.chdir(old_dir)


@click.command()
@click.argument('out_path')
@click.option('--desc', help='Add description to trace')
@click.option('--start_addr', required=True, type=int)
@click.option('--end_addr', required=True, type=int)
@click.option('--addr_type', type=click.Choice(['seq', 'rand', 'ptr-chasing']))
@click.option('--access_type', type=click.Choice(['r', 'w', 'rw', 'wr', 'mix']))
@click.option('--step', type=int, default=1)
@click.option('--critical_load', is_flag=True)
def trace_gen(out_path, desc, start_addr, end_addr, addr_type, access_type, step, critical_load):
    traces = trace_list(start_addr, end_addr, addr_type, access_type, step, critical_load)
    path = Path(out_path)
    with work_dir(path, True):
        with open('trace.tmp', 'w') as f:
            f.writelines(traces)
        if desc is not None:
            info = dict()
            info['desc'] = desc
            with open('info.json', 'w') as f:
                json.dump(info, f, indent=4, sort_keys=True)


def trace_list(start_addr, end_addr, addr_type, access_type, step=1, critical_load=False, repeat_round=1, idle_clk=0):
    tl = list()
    test_size = end_addr - start_addr
    for curr_round in range(0, repeat_round):
        if addr_type == 'seq':
            for addr in range(start_addr, end_addr, step):
                tl.append(trace(addr, access_type, critical_load, idle_clk))
        elif addr_type == 'ptr-chasing':
            arr_size = (end_addr - start_addr) // step
            carr = [0 for _ in range(0, arr_size)]
            curr_pos = 0
            next_pos = 0
            for i in range(0, arr_size - 1):
                while carr[next_pos] != 0 or curr_pos == next_pos:
                    next_pos = randint(0, arr_size - 1)
                carr[curr_pos] = next_pos
                curr_pos = next_pos
            for i in range(0, len(carr)):
                carr[i] = start_addr + carr[i] * step
            for addr in carr:
                tl.append(trace(addr, access_type, critical_load, idle_clk))

        start_addr += test_size + step
        end_addr += test_size + step

    return tl


def overwrite_trace(addr, iter, line_size = 256):
    t = []
    for i in range(0, iter):
        a = addr + (i * 64) % line_size
        t.append(trace(a, "w"))
    return t


def trace(addr, access_type, critical_load=True, idle_clk=0):
    if access_type not in ['r', 'R', 'w', 'W']:
        raise ValueError(f'Wrong access_type: {access_type}')
    acc = access_type.upper()
    if critical_load and acc == 'R':
        acc = 'C'

    if idle_clk != 0:
        trace_str = f'{addr:#010x} {acc}:{idle_clk}\n'
    else:
        trace_str = f'{addr:#010x} {acc}\n'

    return trace_str


if __name__ == '__main__':
    trace_gen()
