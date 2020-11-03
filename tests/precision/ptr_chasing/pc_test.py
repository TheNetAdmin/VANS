from template import testcase, base_test, trace_list
from pathlib import Path
import bitmath
import csv

class test(base_test):
    def generate_trace(self, pattern: str, sz: int, testcase_out_path, repeat, cache_trace: bool) -> Path:
        generate_trace = False

        if cache_trace:
            trace_file_path = self.basedata['out_path'].parent / \
                'trace' / 'ptr_chasing' / pattern / f'{sz}.trace.txt'
            if not trace_file_path.exists():
                trace_file_path.resolve().parent.mkdir(parents=True, exist_ok=True)
                generate_trace = True
        else:
            trace_file_path = testcase_out_path / 'trace.txt'
            generate_trace = True

        if generate_trace:
            trace_access_type = 'r' if pattern == 'read' else 'w'
            tl = trace_list(0, sz, 'ptr-chasing', trace_access_type, step=64,
                            critical_load=True,
                            repeat_round=repeat)
            with trace_file_path.open('w') as f:
                f.writelines(tl)

        return trace_file_path

    def generate_testcases(self):
        for pattern in self.metadata['input']['pattern']:
            out_path = self.metadata['out_path'] + '_' + pattern
            out_path = self.basedata['out_path'] / out_path

            for sz in self.metadata['input']['access_size']:
                testcase_out_path = out_path / str(sz)
                dump_path = testcase_out_path / 'vans_dump'
                # repeat = self.metadata['input']['repeat']
                repeat = self.metadata['input']['repeat_max_size'] // sz
                if sz >= self.metadata['input']['repeat_max_size']:
                    repeat = 1

                if not self.readonly:
                    testcase_out_path.mkdir(parents=True)
                    dump_path.mkdir(parents=True)

                    run_script_lines = ['#!/bin/bash']

                    # generate trace file
                    run_script_lines.append(
                        f"# trace_list: trace_list(0, {sz}, 'ptr-chasing', {pattern}, step=64, critical_load=True, repeat_round={repeat})")
                    trace_file_path = self.generate_trace(
                        pattern, sz, testcase_out_path, repeat, self.metadata['input']['cache_trace'])

                    # generate run script file
                    run_script_lines.append(f"cd $(dirname $0)")
                    run_script_lines.append(
                        f"{self.basedata['vans']['dramtrace_bin'].resolve()} \\")
                    run_script_lines.append(
                        f"\t -c {self.metadata['local_cfg_path'].resolve()} \\")
                    run_script_lines.append(
                        f"\t -t {trace_file_path.resolve()} \\")
                    run_script_lines.append(f"\t 2>&1 \\")
                    run_script_lines.append(f"\t > vans_dump/stdout")
                    run_script_file = testcase_out_path / 'run.sh'
                    with run_script_file.open('w') as f:
                        f.write('\n'.join(run_script_lines))

                # generate testcase instances
                size_str = str(bitmath.Byte(bytes=sz).best_prefix())
                info = {}
                info['name'] = f"{self.metadata['name']}\t(Pattern: {pattern:>6} |\tSize: {size_str:>14} |\tRepeat: {repeat})"
                info['job_id'] = f"ptr_chasing_{pattern}"
                info['access_size'] = sz
                info['path'] = testcase_out_path
                info['repeat_cnt'] = repeat
                info['run_script'] = 'run.sh'
                self.testcases.append(testcase(info))

    def form_subsection(self, pattern):
        rl = []
        rl.append(f'### Pointer chasing {pattern}')

        rl.append('```')
        rl.append(f'test:')
        rl.append(f'  pattern: {pattern}')
        rl.append('```')

        rl.append(f'#### Latency compared to host machine')
        cl = []
        cl.append(f'result_df <- read_data("{self.metadata["result_csv_path"].resolve()}")')
        cl.append(f'result_df <- result_df[result_df$job_id == "ptr_chasing_{pattern}",]')
        cl.append(f'\n')
        cl.append(f'ref_df    <- read_data("{self.metadata["reference_csv_path"].resolve()}")')
        cl.append(f'ref_df    <- ref_df[ref_df$raw_type == "raw-chasing-separate-64",]')
        cl.append(f'ref_df    <- ref_df[ref_df$access_size <= max(result_df$access_size, na.rm = TRUE),]')
        cl.append(f'\n')

        if pattern == 'read':
            ref_line_formula = 'average_float * (cycle_read_end - cycle_write_end) / (cycle_read_end - cycle_start) / access_size * 64'
        elif pattern == 'write':
            ref_line_formula = 'average_float * (cycle_write_end - cycle_start) / (cycle_read_end - cycle_start) / access_size * 64'
        else:
            assert(False)

        cl.append(f'naplot() +')
        cl.append(f'  geom_line(data = result_df, aes(x = access_size, y = last_ns / (access_size * repeat_cnt) * 64, color = "result"), size=line_default_size) +')
        cl.append(f'  geom_line(data = ref_df, aes(x = access_size, y = {ref_line_formula}, color = "reference"), size=line_default_size) +')
        cl.append(f'  scale_y_continuous(name = "Latency per CL (ns)") +')
        cl.append(f'  scale_x_continuous(name = "Access region size (Byte)", trans = "log2", labels = byte_scale)')

        rl += self.form_code_region(cl)
        rl.append(f'\n')

        if self.metadata['input']['draw_counters']:
            rl.append(f'#### Counters')
            counters = []
            with self.metadata['result_csv_path'].open('r') as f:
                reader = csv.DictReader(f)
                for row in reader:
                    counters = [x for x in row.keys() if x.startswith('cnt.')]
                    break

            cl = []
            cl.append(f'result_df <- read_data("{self.metadata["result_csv_path"].resolve()}")')
            cl.append(f'result_df <- result_df[result_df$job_id == "ptr_chasing_{pattern}",]')
            cl.append(f'\n')
            for cnt in counters:
                cl.append(f'naplot() +')
                if self.metadata['input']['counter_per_round'] and cnt in self.metadata['input']['counter_per_round']:
                    cl.append(f'  geom_line(data = result_df, aes(x = access_size, y = ({cnt} / repeat_cnt), color = "{cnt}"), size=line_default_size) +')
                    cl.append(f'  scale_y_continuous(name = "Counter per Round") +')
                else:
                    cl.append(f'  geom_line(data = result_df, aes(x = access_size, y = ({cnt} / repeat_cnt) / access_size * 64, color = "{cnt}"), size=line_default_size) +')
                    cl.append(f'  scale_y_continuous(name = "Counter per Cacheline") +')
                cl.append(f'  scale_x_continuous(name = "Access region size (Byte)", trans = "log2", labels = byte_scale) +')
                cl.append(f'  ggtitle("{cnt}") +')
                cl.append(f'  theme(text=element_text(size=20))')

            rl += self.form_code_region(cl, 'fig.hold="hold", out.width="30%"')

        if self.metadata['input']['draw_simulation_performance']:
            rl.append(f'#### Simulation Performance')

            cl = []
            cl.append(f'result_df <- read_data("{self.metadata["result_csv_path"].resolve()}")')
            cl.append(f'result_df <- result_df[result_df$job_id == "ptr_chasing_{pattern}",]')
            cl.append(f'\n')
            cl.append(f'naplot() +')
            cl.append(f'  geom_line(data = result_df, aes(x = access_size, y = (sim.time_sec / sim.total_clock), color = "Sim time per cycle (sec/cycle)"), size=line_default_size) +')
            cl.append(f'  scale_y_continuous(name = "Counter Value") +')
            cl.append(f'  scale_x_continuous(name = "Access region size (Byte)", trans = "log2", labels = byte_scale) +')
            cl.append(f'  ggtitle("Sim time per cycle (sec/cycle)") +')
            cl.append(f'  theme(text=element_text(size=20))')

            rl += self.form_code_region(cl, 'fig.hold="hold", out.width="30%"')

        return rl

    def form_report(self):
        rl = self.form_report_base()
        for pattern in self.metadata['input']['pattern']:
            rl += self.form_subsection(pattern)
        return rl
