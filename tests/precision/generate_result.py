import json
import click
import pathlib
from pydoc import locate
from ruamel.yaml import YAML
from datetime import datetime
from subprocess import check_call
from slack_webhook import Slack

yaml = YAML(typ='safe')

def load_config(basedata_yml, job_id):
    config = yaml.load(open(basedata_yml, 'r').read())
    config['job_id'] = job_id
    config['out_path'] = pathlib.Path(config['out_path']) / config['job_id']
    for p in config['vans']:
        config['vans'][p] = pathlib.Path(config['vans'][p])
    return config


def load_tests(config):
    tests = {}
    for test_name in config['tests']:
        print(f'Read metadata for test [{test_name}]')
        test_path = config['test_src_root'] / test_name
        metadata_filepath = test_path / 'metadata.yml'
        metadata = yaml.load(open(metadata_filepath, 'r').read())

        test_class_path = f".{test_name}.{metadata['test_class']}"
        print(f'Load test class [{test_class_path}]')

        # Load a Python class from file
        test = locate(test_class_path)
        assert test is not None
        tests[test_name] = test(config, metadata, readonly=True)
    return tests

@click.command()
@click.argument('test_path', type=str)
@click.argument('job_id', type=str)
def generate_result(test_path, job_id):
    test_src_root = pathlib.Path(test_path)
    basedata_yml = test_src_root / 'basedata.yml'

    print(f'Read {basedata_yml}')
    config = load_config(basedata_yml, job_id)
    config['test_src_root'] = test_src_root
    print(config)

    tests = load_tests(config)
    print(tests)

    for (_, test) in tests.items():
        test.generate_testcases()

    for (_, test) in tests.items():
        test.collect_results()

    report_out_path = config['out_path'] / config['report']['out_file']

    if config['report']['generate']:
        print('Generate report')
        report_generate_script = report_out_path.parent / 'report.sh'
        check_call(report_generate_script.resolve())

if __name__ == "__main__":
    generate_result()
