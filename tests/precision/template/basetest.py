from multiprocessing import Pool
from time import sleep, time
from random import random
from subprocess import check_call
from ruamel.yaml import YAML
from pathlib import Path
import re
import csv
import shutil


class testcase:
    def __init__(self, info: dict):
        # info['name']       # test case name
        # info['path']       # test case root path
        # info['job_id']     # short test case name for csv output
        # info['run_script'] # test run script filename
        # info['repeat']     # repeat iter
        self.info = info.copy()

    # Run all 'run_script' under test dir
    def run(self):
        print(f"[START] {self.info['name']}")
        script_path = (self.info['path'] / self.info['run_script']).resolve()
        start = time()
        check_call(['chmod', '+x', script_path])
        check_call(script_path)
        end = time()
        print(f"[ END ] {self.info['name']} {end - start:.3f} sec")

    def collect_result(self):
        # Collect stats
        path = self.info['path'] / 'vans_dump'
        assert path.is_dir()
        result = {}
        yaml = YAML(typ='safe')
        for file in path.iterdir():
            if 'stats_' != file.name[0:6]:
                continue
            with file.open('r') as f:
                content = f.read()
                content.replace('\t', '')
            stat = yaml.load(content)
            for k, v in stat.items():
                if k not in result.keys():
                    result[k] = 0
                result[k] += v
        # Collect clocks
        with (path / 'stdout').open() as f:
            content = f.read()
        result['total_clock'] = int(re.search(r'(?:Total clock\:\s)(\d+)', content).group(1))
        result['sim.total_clock'] = result['total_clock']
        result['total_ns'] = float(re.search(r'(?:Total ns\:\s)([\d\.]+)', content).group(1))
        result['last_clock'] = int(re.search(r'(?:Last command clock\:\s)(\d+)', content).group(1))
        result['last_ns'] = float(re.search(r'(?:Last command ns\:\s)([\d\.]+)', content).group(1))
        result['sim.time_sec'] = float(re.search(r'(?:Simulation time\:\s)([\d\.]+)', content).group(1))
        # Return
        result = {**result, **self.info}
        for k in ['name', 'path', 'run_script']:
            del result[k]
        return result


# Useful resources: Human readable sizes:
# https://stackoverflow.com/questions/14996453/python-libraries-to-calculate-human-readable-filesize-from-bytes
class base_test:
    def __init__(self, basedata, metadata, readonly=False):
        self.basedata = basedata.copy()
        self.metadata = metadata.copy()
        self.testcases = []
        self.parse_input()
        self.parse_metadata()
        self.readonly = readonly

        if not self.readonly:
            (self.basedata['out_path'] / self.metadata['out_path']).mkdir(parents=True)
            self.copy_config_file()

    def parse_metadata(self):
        self.metadata['src_path'] = Path('./tests/precision') / self.metadata['src_path']
        if 'result_file' in self.metadata.keys():
            self.metadata['result_csv_path'] = (self.basedata['out_path'] / self.metadata['result_file'])
        if 'reference_file' in self.metadata.keys():
            self.metadata['reference_csv_path'] = (self.metadata['src_path'] / self.metadata['reference_file'])

    def parse_arg_entry(self, arg_entry):
        t = list(arg_entry.keys())[0]
        v = list(arg_entry.values())[0]
        return (t, v)

    def eval_argument(self, arg):
        (arg_type, arg_value) = self.parse_arg_entry(arg)
        if arg_type == 'value':
            return eval(str(arg_value))
        elif arg_type == 'string':
            return str(arg_value)
        elif arg_type == 'list':
            return arg_value
        elif arg_type == 'array':
            arr = []
            for arr_expr in arg_value:
                arr += eval(arr_expr)
            return arr

    def parse_input(self):
        for arg in self.metadata['input']:
            res = self.eval_argument(self.metadata['input'][arg])
            self.metadata['input'][arg] = res

    def copy_config_file(self):
        src_cfg = self.basedata['vans']['config_file']
        dst_cfg = self.basedata['out_path'] / self.metadata['out_path'] / self.basedata['vans']['config_file'].name
        shutil.copy(src_cfg, dst_cfg)
        self.metadata['local_cfg_path'] = dst_cfg
        if 'override_config' in self.metadata.keys():
            with self.metadata['local_cfg_path'].open('a') as f:
                for d, kv in self.metadata['override_config'].items():
                    f.write(f'[{d}]\n')
                    for k, v in kv.items():
                        f.write(f'{k} = {v}\n')

    # generate_testcases:
    #   1. mkdir 'testcase_name/{0..repeat_max}/'
    #   2. generate 'testcase_name/{0..repeat_max}/run.sh'
    #   3. generate 'testcase_name/{0..repeat_max}/trace.txt'
    #   4. generate testcase object and add to self.testcases
    def generate_testcases(self):
        raise NotImplementedError()

    def testcase_executor(self, index: int):
        self.testcases[index].run()

    def testcase_collector(self, index: int):
        return self.testcases[index].collect_result()

    def run_testcases(self):
        thread_max = self.basedata['thread']
        with Pool(thread_max) as p:
            p.map(self.testcase_executor, range(len(self.testcases)))

    def collect_results(self):
        thread_max = self.basedata['thread']
        with Pool(thread_max) as p:
            result = p.map(self.testcase_collector, range(len(self.testcases)))
        print(self.testcases[0].collect_result())
        print(result)
        with (self.basedata['out_path'] / self.metadata['result_file']).open('w') as f:
            writer = csv.DictWriter(f, fieldnames=result[0].keys())
            writer.writeheader()
            writer.writerows(result)

    def form_report_base(self):
        rl = []
        rl.append(f"## {self.metadata['name']}\n")
        rl.append(f"{self.metadata['description']}\n")

        if 'override_config' in self.metadata.keys():
            rl.append('Config override:')
            rl.append('```')
            for d, kv in self.metadata['override_config'].items():
                rl.append(f'  {d}')
                for k, v in kv.items():
                    rl.append(f'  - {k}: {v}')
            rl.append('```')

        return rl

    def form_report(self):
        # rl: report_lines
        rl = self.form_report_base()

        report_code_path = (src_path / self.metadata['report_code_file'])

        # cl: code_lines
        cl = []
        cl.append(f"result_csv <- read_data({self.metadata['result_csv_path']})")
        cl.append(f"reference_csv <- read_data({self.metadata['reference_csv_path']})")
        cl += get_code_file(report_code_path)

        # Append cl as code region
        rl += form_code_region(cl)
        return rl

    # R helper functions
    def get_code_file(self, file: Path):
        result = file.open().readlines()
        result = [x.rstrip() for x in result]
        return result

    def form_code_region(self, code_content: list, region_config=''):
        result = []
        result.append("```{r " + region_config + "}")
        result += code_content
        result.append("```")
        result.append("\n")
        return result
