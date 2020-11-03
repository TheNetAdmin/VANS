from template import testcase, base_test, overwrite_trace
from pathlib import Path
import bitmath
import re
import csv

class test(base_test):
    def generate_trace(self, iter: int, testcase_out_path, cache_trace: bool) -> Path:
        generate_trace = False

        if cache_trace:
            trace_file_path = self.basedata['out_path'].parent / \
                'trace' / 'overwrite' / f'{iter}.trace.txt'
            if not trace_file_path.exists():
                trace_file_path.resolve().parent.mkdir(parents=True, exist_ok=True)
                generate_trace = True
        else:
            trace_file_path = testcase_out_path / 'trace.txt'
            generate_trace = True

        if generate_trace:
            tl = overwrite_trace(0, iter)
            with trace_file_path.open('w') as f:
                f.writelines(tl)

        return trace_file_path

    def generate_testcases(self):
        out_path = self.basedata['out_path'] / self.metadata['out_path']

        testcase_out_path = out_path
        testcase_out_path.mkdir(parents=True, exist_ok=True)

        dump_path = testcase_out_path / 'vans_dump'
        dump_path.mkdir(parents=True)

        run_script_lines = ['#!/bin/bash']

        # generate trace file
        self.metadata['input']['iter'] = int(self.metadata['input']['iter'])
        trace_file_path = self.generate_trace(
            self.metadata['input']['iter'], testcase_out_path, self.metadata['input']['cache_trace'])

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
        sz = 64 * self.metadata['input']['iter']
        size_str = str(bitmath.Byte(bytes=sz).best_prefix())
        info = {}
        info['name'] = f"{self.metadata['name']}\t(\tIter: {str(self.metadata['input']['iter']):>14})"
        info['job_id'] = f"overwrite"
        info['access_size'] = sz
        info['path'] = testcase_out_path
        info['run_script'] = 'run.sh'
        self.testcases.append(testcase(info))

    def form_subsection(self):
        rl = []
        rl.append(f'### Overwrite')

        cl = []
        cl.append(f'result_df <- read_data("{self.metadata["result_csv_path"].resolve()}")')
        cl.append(f'result_df <- result_df[result_df$iter <= {self.metadata["input"]["iter"]},]')
        cl.append(f'result_df <- result_df[result_df$tail <= 1000 | result_df$tail >= 1800,]')
        cl.append(f'\n')
        cl.append(f'ref_df    <- read_data("{self.metadata["reference_csv_path"].resolve()}")')
        cl.append(f'ref_df <- ref_df[ref_df$rep <= {self.metadata["input"]["iter"]/4},]')
        cl.append(f'\n')

        cl.append(f'naplot() +')
        cl.append(f'  geom_line(data = result_df, aes(x = iter / 4, y = tail / 1000 + 0.25, color = "result"), size=line_default_size) +')
        cl.append(f'  geom_line(data = ref_df, aes(x = rep, y = (cycle * (1 / 2.2)) / 1000, color = "reference"), size=line_default_size) +')
        cl.append(f'  scale_y_continuous(name = "Tail latency (us)", trans = "log10") +')
        cl.append(f'  scale_x_continuous(name = "Overwrite iteration", labels = format_si())')

        rl += self.form_code_region(cl)
        rl.append(f'\n')

        return rl

    def form_report(self):
        rl = self.form_report_base()
        rl += self.form_subsection()
        return rl

    def collect_results(self):
        res = []
        src_filename = self.basedata['out_path'] / self.metadata['out_path'] / 'vans_dump' / 'stdout'
        with open(src_filename, 'r') as f:
            for row in f:
                mat = re.search(r'(?:\[)(\d+)(?:\]:)(\d+)', row)
                if mat is not None:
                    ent = dict()
                    ent['iter'] = int(mat.group(1))
                    ent['clk'] = int(mat.group(2))
                    res.append(ent)
        for i in range(len(res)):
            if i == 0:
                res[i]['tail'] = float(res[i]['clk']) * 0.75
            else:
                res[i]['tail'] = float(res[i]['clk'] - res[i-1]['clk']) * 0.75
        wres = []
        for i, e in enumerate(res):
            wres.append(e)
        out_filename = self.basedata['out_path'] / 'overwrite_result.csv'
        with open(out_filename, 'w') as f:
            writer = csv.DictWriter(f, fieldnames=wres[0].keys())
            writer.writeheader()
            writer.writerows(wres)
