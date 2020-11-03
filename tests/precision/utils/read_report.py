import click
import pathlib
import subprocess

@click.command()
@click.argument('report_id', type=int)
def read_report(report_id):
    test_out_path = pathlib.Path('./out/precision_test/')
    tests = [x for x in test_out_path.iterdir() if x.name.startswith('20')]
    report_file = tests[report_id] / 'report.html'
    subprocess.check_call(['xdg-open', report_file.resolve()])


if __name__ == '__main__':
    read_report()
