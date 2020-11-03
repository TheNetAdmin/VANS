import json
import click
import pathlib
from pydoc import locate
from ruamel.yaml import YAML
from datetime import datetime
from subprocess import check_call
from slack_webhook import Slack

yaml = YAML(typ='safe')


def load_config(basedata_yml):
    config = yaml.load(open(basedata_yml, 'r').read())
    config['job_id'] = datetime.now().strftime("%Y%m%d-%H%M%S")
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
        tests[test_name] = test(config, metadata)
    return tests


@click.command()
@click.argument('test_path', type=str)
def precision_test(test_path):
    test_src_root = pathlib.Path(test_path)
    basedata_yml = test_src_root / 'basedata.yml'

    print(f'Read {basedata_yml}')
    config = load_config(basedata_yml)
    config['test_src_root'] = test_src_root
    print(config)

    tests = load_tests(config)
    print(tests)

    if config['report']['slack']:
        slack = Slack(url=config['report']['slack_url'])
        slack.post(text="[Start] VANS Precision Test")

    print('Generating testcases, this may take a few minutes if you have not generated traces before...')

    for (_, test) in tests.items():
        test.generate_testcases()

    for (_, test) in tests.items():
        test.run_testcases()

    for (_, test) in tests.items():
        test.collect_results()

    test_reports = []
    for (_, test) in tests.items():
        test_reports += test.form_report()

    # Generate report
    report = []
    with open(f'{config["report"]["template_path"]}', 'r') as f:
        report = f.readlines()
        report = [x.rstrip() for x in report]

    report.append(f'```')
    report.append(f'Job ID: {config["job_id"]}')
    report.append(f'```')

    if config['report']['print_config']:
        report.append(f'## Config file {config["vans"]["config_file"]}')
        report.append('```')
        cfg_file = config["vans"]["config_file"].open('r').readlines()
        report += [x.strip() for x in cfg_file]
        report.append('```')

    report += test_reports

    report_out_path = config['out_path'] / config['report']['out_file']
    with open(report_out_path.resolve(), 'w') as f:
        f.write('\n'.join(report))

    if config['report']['generate']:
        print('Generate report')
        report_generate_script = report_out_path.parent / 'report.sh'
        with open(report_generate_script.resolve(), 'w') as f:
            fl = []
            fl.append('#! /bin/bash')
            fl.append('curr_dir=$(dirname $(realpath $0))')
            fl.append('pushd $curr_dir')
            fl.append("Rscript -e 'rmarkdown::render(\"report.Rmd\")'")
            fl.append('popd')
            f.write('\n'.join(fl))
        check_call(['chmod', '+x', report_generate_script.resolve()])
        check_call(report_generate_script.resolve())
        if config['report']['open']:
            check_call([config['report']['open_tool'],
                        (report_out_path.parent / ('report.' + config['report']['file_type'])).resolve()])

    if config['report']['slack']:
        slack.post(text="[ End ] VANS Precision Test")


if __name__ == "__main__":
    precision_test()
