from template import testcase, base_test, trace_list
from pathlib import Path
import bitmath
import csv

class test(base_test):
    def generate_trace(self, pattern: str, sz: int, testcase_out_path, repeat, cache_trace: bool) -> Path:
        generate_trace = False

        if cache_trace:
            trace_file_path = self.basedata['out_path'].parent / \
                'trace' / 'bandwidth' / pattern / f'{sz}.trace.txt'
            if not trace_file_path.exists():
                trace_file_path.resolve().parent.mkdir(parents=True, exist_ok=True)
                generate_trace = True
        else:
            trace_file_path = testcase_out_path / 'trace.txt'
            generate_trace = True

        if generate_trace:
            if self.metadata['input']['idle_clk']:
                clk = self.metadata['input']['idle_clk']
            else:
                clk = 0
            trace_access_type = 'r' if pattern == 'read' else 'w'
            tl = trace_list(0, sz, 'seq', trace_access_type, step=64,
                            critical_load=False,
                            repeat_round=repeat,
                            idle_clk=clk)
            with trace_file_path.open('w') as f:
                f.writelines(tl)

        return trace_file_path

    def generate_testcases(self):
        for pattern in self.metadata['input']['pattern']:
            out_path = self.metadata['out_path'] + '_' + pattern
            out_path = self.basedata['out_path'] / out_path

            for sz in self.metadata['input']['access_size']:
                testcase_out_path = out_path / str(sz)
                testcase_out_path.mkdir(parents=True)

                dump_path = testcase_out_path / 'vans_dump'
                dump_path.mkdir(parents=True)

                run_script_lines = ['#!/bin/bash']
                repeat = 1

                # generate trace file
                run_script_lines.append(
                    f"# trace_list: trace_list(0, {sz}, 'seq', {pattern}, step=64, critical_load=False, repeat_round={repeat})")
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
                info['job_id'] = f"bandwidth_{pattern}"
                info['access_size'] = sz
                info['path'] = testcase_out_path
                info['repeat_cnt'] = repeat
                info['run_script'] = 'run.sh'
                self.testcases.append(testcase(info))

    def form_subsection(self, pattern):
        rl = []
        rl.append(f'### Bandwidth {pattern}')

        rl.append('```')
        rl.append(f'test:')
        rl.append(f'  pattern: {pattern}')
        rl.append('```')

        cl = []
        cl.append(f'result_df <- read_data("{self.metadata["result_csv_path"].resolve()}")')
        cl.append(f'result_df <- result_df[result_df$job_id == "bandwidth_{pattern}",]')
        cl.append(f'\n')

        if pattern == 'read':
            ref_bw = 7 * 1024 * 1024 * 1024
        elif pattern == 'write':
            ref_bw = 1.5 * 1024 * 1024 * 1024
        else:
            assert(False)

        cl.append(f'naplot() +')
        cl.append(f'  geom_line(data = result_df, aes(x = access_size, y = access_size / total_ns * 1e9, color = "result"), size=line_default_size) +')
        cl.append(f'  hline({ref_bw}) +')
        cl.append(f'  scale_y_continuous(name = "Bandwidth (Byte/Sec)", trans = "log2", labels = byte_scale) +')
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
            cl.append(f'result_df <- result_df[result_df$job_id == "bandwidth_{pattern}",]')
            cl.append(f'\n')
            for cnt in counters:
                cl.append(f'naplot() +')
                cl.append(f'  geom_line(data = result_df, aes(x = access_size, y = ({cnt} / repeat_cnt) / access_size * 64, color = "{cnt}"), size=line_default_size) +')
                cl.append(f'  scale_y_continuous(name = "Counter per Access") +')
                cl.append(f'  scale_x_continuous(name = "Access region size (Byte)", trans = "log2", labels = byte_scale) +')
                cl.append(f'  ggtitle("{cnt}") +')
                cl.append(f'  theme(text=element_text(size=20))')

            rl += self.form_code_region(cl, 'fig.hold="hold", out.width="30%"')

        return rl

    def form_report(self):
        rl = self.form_report_base()
        for pattern in self.metadata['input']['pattern']:
            rl += self.form_subsection(pattern)
        return rl
